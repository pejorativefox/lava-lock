#ifndef LAVA_PAM_AUTH_H
#define LAVA_PAM_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

typedef struct {
  pthread_mutex_t mu;
  pthread_t thread;
  bool thread_started;
  bool busy;
  bool done;
  bool success;
  char reason[256];
  char username[256];
  char service[128];
} PamAuth;

void pam_auth_init(PamAuth *auth, const char *username, const char *service);

/* Start authenticating with a copy of `password`.  Returns false if an
 * authentication is already in flight or the thread could not be created. */
bool pam_auth_start(PamAuth *auth, const char *password, size_t len);

bool pam_auth_busy(PamAuth *auth);

/* If a result is ready, copy it and return true.  The caller must call this
 * repeatedly from the main thread. */
bool pam_auth_poll(PamAuth *auth, bool *success, char *reason,
                   size_t reason_len);

void pam_auth_destroy(PamAuth *auth);

#endif /* LAVA_PAM_AUTH_H */
