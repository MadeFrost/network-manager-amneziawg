project('network-manager-amneziawg', 'c',
  version: '0.4.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 0.63',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',
    'buildtype=debugoptimized',
    'b_lundef=false', # the plugins use symbols of the host (NM, GTK)
  ],
)

gettext_package = meson.project_name()
prefix = get_option('prefix')
libexecdir = prefix / get_option('libexecdir')
localedir = prefix / get_option('localedir')
nm_plugindir = prefix / get_option('libdir') / 'NetworkManager'
# Not libdir: NetworkManager looks in <prefix>/lib/NetworkManager/VPN on every arch.
nm_vpndir = prefix / 'lib' / 'NetworkManager' / 'VPN'
dbus_confdir = prefix / get_option('datadir') / 'dbus-1' / 'system.d'

cc = meson.get_compiler('c')
add_project_arguments(
  '-D_GNU_SOURCE',
  cc.get_supported_arguments('-Wno-unused-parameter', '-Wno-missing-field-initializers'),
  language: 'c',
)
add_project_link_arguments(cc.get_supported_link_arguments('-Wl,-z,relro', '-Wl,-z,now'), language: 'c')

conf = configuration_data()
conf.set_quoted('GETTEXT_PACKAGE', gettext_package)
conf.set_quoted('LOCALEDIR', localedir)
conf.set_quoted('VERSION', meson.project_version())
configure_file(output: 'config.h', configuration: conf)
config_inc = include_directories('.')

glib = dependency('glib-2.0', version: '>= 2.74')
libnm = dependency('libnm', version: '>= 1.40')
dl = cc.find_library('dl', required: false)
gtk4 = dependency('gtk4', version: '>= 4.6', required: get_option('gtk4'))
gtk3 = dependency('gtk+-3.0', version: '>= 3.22', required: get_option('gtk3'))

subdir('src')
subdir('data')
subdir('po')
subdir('tests')

summary({
  'prefix': prefix,
  'GTK4 editor (GNOME Settings)': gtk4.found(),
  'GTK3 editor (nm-connection-editor)': gtk3.found(),
}, bool_yn: true)
