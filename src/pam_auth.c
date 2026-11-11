#include "pam_auth.h"

#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef struct {
  PamAuth *auth;
  char *password;
} AuthJob;

static int conversation(int num_msg, const struct pam_message **msg,
                        struct pam_response **resp, void *appdata) {
  AuthJob *job = appdata;
  if (num_msg <= 0 || msg == NULL || resp == NULL) {
    return PAM_CONV_ERR;
  }

  struct pam_response *responses =
      calloc((size_t)num_msg, sizeof(struct pam_response));
  if (responses == NULL) {
    return PAM_BUF_ERR;
  }

  for (int i = 0; i < num_msg; i++) {
    responses[i].resp = NULL;
    responses[i].resp_retcode = 0;
    switch (msg[i]->msg_style) {
      case PAM_PROMPT_ECHO_OFF:
        responses[i].resp = strdup(job->password);
        break;
      case PAM_PROMPT_ECHO_ON:
        responses[i].resp = strdup(job->auth->username);
        break;
      case PAM_ERROR_MSG:
      case PAM_TEXT_INFO:
        break;
      default:
        for (int j = 0; j < i; j++) {
          free(responses[j].resp);
        }
        free(responses);
        return PAM_CONV_ERR;
    }
    if ((msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
         msg[i]->msg_style == PAM_PROMPT_ECHO_ON) &&
        responses[i].resp == NULL) {
      for (int j = 0; j < i; j++) {
        free(responses[j].resp);
      }
      free(responses);
      return PAM_BUF_ERR;
    }
  }

  *resp = responses;
  return PAM_SUCCESS;
}

static void *auth_worker(void *arg) {
  AuthJob *job = arg;
  PamAuth *auth = job->auth;
  struct pam_conv conv = {
    .conv = conversation,
    .appdata_ptr = job,
  };

  pam_handle_t *pamh = NULL;
  int ret = pam_start(auth->service, auth->username, &conv, &pamh);
  if (ret == PAM_SUCCESS) {
    ret = pam_authenticate(pamh, 0);
  }

  const char *reason = pam_strerror(pamh, ret);
  bool success = (ret == PAM_SUCCESS);

  pthread_mutex_lock(&auth->mu);
  auth->success = success;
  snprintf(auth->reason, sizeof(auth->reason), "%s",
           reason != NULL ? reason : "authentication failed");
  auth->busy = false;
  auth->done = true;
  pthread_mutex_unlock(&auth->mu);

  if (pamh != NULL) {
    pam_end(pamh, ret);
  }
  util_secure_zero(job->password, strlen(job->password));
  free(job->password);
  free(job);
  return NULL;
}

void pam_auth_init(PamAuth *auth, const char *username, const char *service) {
  memset(auth, 0, sizeof(*auth));
  pthread_mutex_init(&auth->mu, NULL);
  snprintf(auth->username, sizeof(auth->username), "%s", username);
  snprintf(auth->service, sizeof(auth->service), "%s", service);
}

bool pam_auth_start(PamAuth *auth, const char *password, size_t len) {
  pthread_mutex_lock(&auth->mu);
  bool busy = auth->busy;
  bool join_old = auth->thread_started;
  pthread_mutex_unlock(&auth->mu);
  if (busy) {
    return false;
  }

  if (join_old) {
    pthread_join(auth->thread, NULL);
    auth->thread_started = false;
  }

  AuthJob *job = calloc(1, sizeof(*job));
  if (job == NULL) {
    return false;
  }
  job->auth = auth;
  job->password = malloc(len + 1);
  if (job->password == NULL) {
    free(job);
    return false;
  }
  memcpy(job->password, password, len);
  job->password[len] = '\0';

  pthread_mutex_lock(&auth->mu);
  auth->busy = true;
  auth->done = false;
  auth->success = false;
  auth->reason[0] = '\0';
  pthread_mutex_unlock(&auth->mu);

  if (pthread_create(&auth->thread, NULL, auth_worker, job) != 0) {
    util_secure_zero(job->password, len);
    free(job->password);
    free(job);
    pthread_mutex_lock(&auth->mu);
    auth->busy = false;
    pthread_mutex_unlock(&auth->mu);
    return false;
  }
  auth->thread_started = true;
  return true;
}

bool pam_auth_busy(PamAuth *auth) {
  pthread_mutex_lock(&auth->mu);
  bool busy = auth->busy;
  pthread_mutex_unlock(&auth->mu);
  return busy;
}

bool pam_auth_poll(PamAuth *auth, bool *success, char *reason,
                   size_t reason_len) {
  pthread_mutex_lock(&auth->mu);
  if (!auth->done) {
    pthread_mutex_unlock(&auth->mu);
    return false;
  }
  *success = auth->success;
  if (reason != NULL && reason_len > 0) {
    snprintf(reason, reason_len, "%s", auth->reason);
  }
  auth->done = false;
  pthread_mutex_unlock(&auth->mu);
  return true;
}

void pam_auth_destroy(PamAuth *auth) {
  if (auth->thread_started) {
    pthread_join(auth->thread, NULL);
    auth->thread_started = false;
  }
  pthread_mutex_destroy(&auth->mu);
}
