/* Imports a .conf with the plugin and writes the keyfile NM would store. */
#include <NetworkManager.h>
#include <dlfcn.h>
#include <stdio.h>
int main (int c, char **v) {
	GError *e = NULL;
	void *h = dlopen (v[1], RTLD_NOW);
	NMVpnEditorPlugin *(*f) (GError **) = dlsym (h, "nm_vpn_editor_plugin_factory");
	NMConnection *con = nm_vpn_editor_plugin_import (f (&e), v[2], &e);
	if (!con) { printf ("import: %s\n", e->message); return 1; }
	GKeyFile *kf = nm_keyfile_write (con, NM_KEYFILE_HANDLER_FLAGS_NONE, NULL, NULL, &e);
	if (!kf) { printf ("write: %s\n", e->message); return 1; }
	return g_key_file_save_to_file (kf, v[3], &e) ? 0 : 1;
}
