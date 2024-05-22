project('mono-dynamic-host', 'c',
  version : '0.1',
  default_options : ['warning_level=3', 'c_std=c99'])

cc = meson.get_compiler('c')

# https://github.com/brad0/libplacebo/commit/6cce976d478306fbcb360e6c3f33418e6b18932a
if host_machine.system() == 'windows' or host_machine.system().endswith('bsd') or \
	host_machine.system() == 'dragonfly'
    dl_dep = declare_dependency()
else
    dl_dep = cc.find_library('dl', required: true)
endif

exe = executable('mdh', 'mono_dynamic_host.c',
  dependencies : [dl_dep],
  install : true)
