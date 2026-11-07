# meson test                       everything (integration tests skip
#                                  themselves when the module is missing)
# meson test --suite unit          unit tests only, seconds
# meson test --suite integration   the integration tests
# See tests/README.md.

cases = files('data/ipacket-cases.txt')

test_conf = executable('test-conf', 'unit/test-conf.c', dependencies: [awg_shared_dep, dl])
test('conf', test_conf, args: plugin, suite: 'unit')

test_service = executable('test-service', 'unit/test-service.c', dependencies: service_core_dep)
test('service', test_service, suite: 'unit')

test_ipacket = executable('test-ipacket', 'unit/test-ipacket.c', dependencies: awg_shared_dep)
test('ipacket', test_ipacket, args: cases, suite: 'unit')

if gtk4.found()
  test_editor = executable('test-editor', 'unit/test-editor.c', dependencies: [awg_shared_dep, gtk4, dl])
  # The plugin loads the editor from its own directory: build both first.
  test('editor', test_editor, args: plugin, depends: editors, suite: 'unit',
    env: {'LANGUAGE': 'C'})
endif

# Helpers for the integration tests.
fake_nm = executable('fake-nm', 'integration/helpers/fake-nm.c', dependencies: [libnm, dl])
kfwrite = executable('kfwrite', 'integration/helpers/kfwrite.c', dependencies: [libnm, dl])

integration_env = {
  'BUILD': meson.project_build_root() / 'src',
  'HELPERS': meson.current_build_dir(),
}
run_integration = find_program('run-integration.sh')
foreach t : ['e2e ok', 'e2e badobfs', 'e2e hpk', 'e2e badhpk', 'e2e badi1', 'e2e badkey',
             'e2e badawg',
             'stale', 'ipacket-kernel', 'nm-real']
  test(t, run_integration,
    args: t,
    env: integration_env,
    depends: [service, auth_dialog, plugin, fake_nm, kfwrite, test_ipacket] + editors,
    suite: 'integration',
    timeout: 180,
  )
endforeach
