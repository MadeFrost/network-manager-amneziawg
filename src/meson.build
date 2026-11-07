# All components are built into one directory: the libnm plugin loads the
# editor from its own directory, and the tests find everything here.

shared_inc = include_directories('shared')

# Code shared by the service, the plugin and the editors (built with -fPIC,
# since it ends up in shared modules too).
awg_shared = static_library('awg-shared',
  'shared/awg-common.c',
  'shared/awg-conf.c',
  'shared/awg-keys.c',
  'shared/awg-validate.c',
  include_directories: config_inc,
  dependencies: libnm,
  pic: true,
  gnu_symbol_visibility: 'hidden',
)
awg_shared_dep = declare_dependency(
  link_with: awg_shared,
  include_directories: [config_inc, shared_inc],
  dependencies: libnm,
)

# The service minus its D-Bus object and main(): unit-tested on its own.
service_core = static_library('awg-service-core',
  'service/iface.c',
  'service/ipconfig.c',
  dependencies: awg_shared_dep,
)
service_core_dep = declare_dependency(
  link_with: service_core,
  include_directories: include_directories('service'),
  dependencies: awg_shared_dep,
)

service = executable('nm-amneziawg-service',
  'service/nm-amneziawg-service.c',
  dependencies: service_core_dep,
  install: true,
  install_dir: libexecdir,
)

auth_dialog = executable('nm-amneziawg-auth-dialog',
  'auth-dialog/nm-amneziawg-auth-dialog.c',
  include_directories: config_inc,
  dependencies: libnm,
  install: true,
  install_dir: libexecdir,
)

plugin = shared_module('nm-vpn-plugin-amneziawg',
  'plugin/nm-amneziawg-plugin.c',
  dependencies: [awg_shared_dep, dl],
  gnu_symbol_visibility: 'hidden',
  install: true,
  install_dir: nm_plugindir,
)

editor_sources = files(
  'editor/editor-fields.c',
  'editor/editor-peers.c',
  'editor/nm-amneziawg-editor.c',
)
editors = []
if gtk4.found()
  editors += shared_module('nm-gtk4-vpn-plugin-amneziawg-editor',
    editor_sources,
    dependencies: [awg_shared_dep, gtk4],
    gnu_symbol_visibility: 'hidden',
    install: true,
    install_dir: nm_plugindir,
  )
endif
if gtk3.found()
  editors += shared_module('nm-vpn-plugin-amneziawg-editor',
    editor_sources,
    dependencies: [awg_shared_dep, gtk3],
    gnu_symbol_visibility: 'hidden',
    install: true,
    install_dir: nm_plugindir,
  )
endif
