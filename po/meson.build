i18n = import('i18n')
# Targets: network-manager-amneziawg-pot (regenerate the template) and
# network-manager-amneziawg-update-po (merge it into the .po files).
i18n.gettext(gettext_package,
  preset: 'glib',
  args: ['--add-comments=TRANSLATORS:', '--package-version=' + meson.project_version()],
)
