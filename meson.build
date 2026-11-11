project('lava-lock', 'c',
  version: '0.1.0',
  meson_version: '>=1.0.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'buildtype=release',
  ])

cc = meson.get_compiler('c')

sdl3_dep = dependency('sdl3', version: '>=3.2.0')
x11_dep = dependency('x11')
xext_dep = dependency('xext')
pam_dep = dependency('pam')
threads_dep = dependency('threads')
m_dep = cc.find_library('m', required: false)

hardening = [
  '-D_GNU_SOURCE',
  '-fstack-protector-strong',
  '-Wformat',
  '-Wformat-security',
]
if get_option('buildtype') != 'debug'
  hardening += ['-D_FORTIFY_SOURCE=2']
endif
add_project_arguments(hardening, language: 'c')
add_project_link_arguments(['-Wl,-z,relro,-z,now'], language: 'c')

sources = files(
  'src/main.c',
  'src/options.c',
  'src/util.c',
  'src/sims.c',
  'src/font.c',
  'src/field.c',
  'src/render.c',
  'src/pam_auth.c',
  'src/lock.c',
)

executable('lava-lock', sources,
  dependencies: [sdl3_dep, x11_dep, xext_dep, pam_dep, threads_dep, m_dep],
  install: true,
)

smoke = find_program('tests/smoke.sh')
test('smoke', smoke,
  args: [meson.current_build_dir() / 'lava-lock'],
  timeout: 120,
)

if get_option('xfce4_screensaver')
  # xfce4-screensaver refuses theme commands outside its own engine
  # directories, so a symlink to the binary is installed there and the theme
  # files run that.
  saver_dir = get_option('xfce4_screensaver_dir')
  # Not install_symlink(): that silently skips the link (and every other
  # symlink) when the directory is not writable, e.g. for a ~/.local prefix.
  meson.add_install_script('data/install-saver-link.sh', saver_dir,
    get_option('prefix') / get_option('bindir') / 'lava-lock')
  saver_conf = configuration_data()
  saver_conf.set('saverdir', saver_dir)
  foreach kind : ['lava', 'fire']
    configure_file(
      input: 'data/lava-lock-@0@.desktop.in'.format(kind),
      output: 'lava-lock-@0@.desktop'.format(kind),
      configuration: saver_conf,
      install: true,
      install_dir: get_option('datadir') / 'applications' / 'screensavers',
    )
  endforeach
endif
