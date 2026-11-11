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
  dependencies: [sdl3_dep, x11_dep, pam_dep, threads_dep, m_dep],
  install: true,
)

smoke = find_program('tests/smoke.sh')
test('smoke', smoke,
  args: [meson.current_build_dir() / 'lava-lock'],
  timeout: 120,
)
