name_conf = configuration_data()
name_conf.set('LIBEXECDIR', libexecdir)
configure_file(
  input: 'nm-amneziawg-service.name.in',
  output: 'nm-amneziawg-service.name',
  configuration: name_conf,
  install_dir: nm_vpndir,
)

install_data('nm-amneziawg-service.conf', install_dir: dbus_confdir)
