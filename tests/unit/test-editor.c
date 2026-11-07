/* The GTK4 editor (the form in GNOME Settings) saving a connection, without
 * showing a window. Needs a display: skipped (77) when GTK cannot start.
 * usage: test-editor PLUGIN.so   (the editor .so is loaded from next to it) */
#include <NetworkManager.h>
#include <gtk/gtk.h>

#include <dlfcn.h>
#include <string.h>

#include "awg-common.h"
#include "check.h"

#define KEY_A "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
#define KEY_B "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBA="
#define KEY_C "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCA="
#define HPK   "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHA="
#define K_HPK AWG_KEY_HEADER_PROTECTION_KEY

static NMVpnEditorPlugin *plugin;

static NMSettingVpn *
vpn (NMConnection *c)
{
	return nm_connection_get_setting_vpn (c);
}

static void
add_secret (NMSettingVpn *s, const char *key, const char *value)
{
	nm_setting_vpn_add_secret (s, key, value);
	nm_setting_set_secret_flags (NM_SETTING (s), key, NM_SETTING_SECRET_FLAG_NONE, NULL);
}

/* What the importer produces for a typical Amnezia config. */
static NMConnection *
make_connection (void)
{
	NMConnection *c = nm_simple_connection_new ();
	NMSettingVpn *s = NM_SETTING_VPN (nm_setting_vpn_new ());
	NMSetting *s_con = nm_setting_connection_new ();
	g_autofree char *uuid = nm_utils_uuid_generate ();

	g_object_set (s_con, NM_SETTING_CONNECTION_ID, "office", NM_SETTING_CONNECTION_UUID, uuid,
	              NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME, NULL);
	nm_connection_add_setting (c, s_con);
	g_object_set (s, NM_SETTING_VPN_SERVICE_TYPE, AWG_DBUS_SERVICE, NULL);
	nm_setting_vpn_add_data_item (s, AWG_KEY_ADDRESS, "10.8.1.2/32");
	nm_setting_vpn_add_data_item (s, "Jc", "4");
	nm_setting_vpn_add_data_item (s, "H1", "1157836183");
	nm_setting_vpn_add_data_item (s, AWG_KEY_PUBLIC_KEY, KEY_B);
	nm_setting_vpn_add_data_item (s, AWG_KEY_ENDPOINT, "vpn.example.com:51820");
	nm_setting_vpn_add_data_item (s, AWG_KEY_ALLOWED_IPS, "0.0.0.0/0");
	nm_setting_vpn_add_data_item (s, "FutureKey", "7");
	add_secret (s, AWG_KEY_PRIVATE_KEY, KEY_A);
	add_secret (s, AWG_KEY_PRESHARED_KEY, KEY_C);
	add_secret (s, K_HPK, HPK);
	nm_connection_add_setting (c, NM_SETTING (s));
	return c;
}

/* Opens the editor on `in` and saves into a copy, as GNOME Settings does. */
static NMConnection *
save_via_editor (NMConnection *in, GError **error)
{
	g_autoptr (NMVpnEditor) ed = nm_vpn_editor_plugin_get_editor (plugin, in, error);
	g_autoptr (NMConnection) out = NULL;

	if (!ed)
		return NULL;
	out = nm_simple_connection_new_clone (in);
	nm_connection_remove_setting (out, NM_TYPE_SETTING_VPN);
	if (!nm_vpn_editor_update_connection (ed, out, error))
		return NULL;
	return g_steal_pointer (&out);
}

static gboolean
flags_none (NMSettingVpn *s, const char *key)
{
	NMSettingSecretFlags fl = 99;

	nm_setting_get_secret_flags (NM_SETTING (s), key, &fl, NULL);
	return fl == NM_SETTING_SECRET_FLAG_NONE;
}

static void
test_keeps_everything (void)
{
	g_autoptr (GError) error = NULL;
	g_autoptr (NMConnection) in = make_connection ();
	g_autoptr (NMConnection) out = save_via_editor (in, &error);

	printf ("save keeps what the form does not show:\n");
	CHECK ("save succeeds", out != NULL);
	if (!out) {
		printf ("        %s\n", error ? error->message : "");
		return;
	}
	CHECK ("PrivateKey kept as a secret", !g_strcmp0 (nm_setting_vpn_get_secret (vpn (out), AWG_KEY_PRIVATE_KEY), KEY_A));
	CHECK ("PresharedKey kept as a secret", !g_strcmp0 (nm_setting_vpn_get_secret (vpn (out), AWG_KEY_PRESHARED_KEY), KEY_C));
	CHECK ("HeaderProtectionKey (no field) kept as a secret", !g_strcmp0 (nm_setting_vpn_get_secret (vpn (out), K_HPK), HPK));
	CHECK ("HeaderProtectionKey not in vpn.data", !nm_setting_vpn_get_data_item (vpn (out), K_HPK));
	CHECK ("unknown data key kept", !g_strcmp0 (nm_setting_vpn_get_data_item (vpn (out), "FutureKey"), "7"));
	CHECK ("obfuscation kept", !g_strcmp0 (nm_setting_vpn_get_data_item (vpn (out), "H1"), "1157836183"));
	CHECK ("connection verifies", nm_connection_verify (out, NULL));
}

static void
test_migrates_legacy_hpk (void)
{
	g_autoptr (GError) error = NULL;
	g_autoptr (NMConnection) in = make_connection (), out = NULL;

	/* Saved by 0.3.3 and older: HeaderProtectionKey in vpn.data. */
	nm_setting_vpn_remove_secret (vpn (in), K_HPK);
	nm_setting_vpn_add_data_item (vpn (in), K_HPK, "OLD");
	out = save_via_editor (in, &error);

	printf ("old HeaderProtectionKey in vpn.data:\n");
	CHECK ("save succeeds", out != NULL);
	if (!out)
		return;
	CHECK ("moved to vpn.secrets", !g_strcmp0 (nm_setting_vpn_get_secret (vpn (out), K_HPK), "OLD"));
	CHECK ("removed from vpn.data", !nm_setting_vpn_get_data_item (vpn (out), K_HPK));
	CHECK ("secret flags NONE", flags_none (vpn (out), K_HPK));
}

static void
test_ipacket_refusals (void)
{
	struct {
		const char *value, *reason; /* reason NULL: must save */
	} cases[] = {
		{ "<b 0x1a2b><r 16><t>", NULL },
		{ "<b 0x1a2>", "even number of hex digits" },
		{ "<r>", "<r> needs a length" },
		{ "<c 1>", "<c> takes no value" },
		{ "<x 5>", "unknown tag <x>" },
	};

	printf ("I1 is checked on save:\n");
	for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
		g_autoptr (GError) error = NULL;
		g_autoptr (NMConnection) in = make_connection (), out = NULL;
		g_autofree char *name = NULL;

		nm_setting_vpn_add_data_item (vpn (in), "I1", cases[i].value);
		out = save_via_editor (in, &error);
		if (!cases[i].reason) {
			name = g_strdup_printf ("%-22s saved", cases[i].value);
			CHECK (name, out != NULL);
		} else {
			name = g_strdup_printf ("%-22s refused: %s", cases[i].value, error ? error->message : "(saved!)");
			CHECK (name, !out && error && strstr (error->message, cases[i].reason));
		}
	}
}

static void
test_shared_checks (void)
{
	struct {
		const char *key, *value, *reason;
	} cases[] = {
		{ AWG_KEY_MTU, "500", "from 576 to 65535" },
		{ AWG_KEY_ADDRESS, "10.8.1.300/32", "“10.8.1.300/32” is not an IP address" },
		{ AWG_KEY_ADDRESS, "fd00::2/128", "an IPv4 address is needed" },
	};

	printf ("same checks as the service:\n");
	for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
		g_autoptr (GError) error = NULL;
		g_autoptr (NMConnection) in = make_connection (), out = NULL;
		g_autofree char *name = NULL;

		nm_setting_vpn_add_data_item (vpn (in), cases[i].key, cases[i].value);
		out = save_via_editor (in, &error);
		name = g_strdup_printf ("%s = %s refused: %s", cases[i].key, cases[i].value,
		                        error ? error->message : "(saved!)");
		CHECK (name, !out && error && strstr (error->message, cases[i].reason));
	}
}

static void
test_required (void)
{
	g_autoptr (GError) error = NULL;
	g_autoptr (NMConnection) in = make_connection (), out = NULL;

	printf ("required fields:\n");
	nm_setting_vpn_remove_secret (vpn (in), AWG_KEY_PRIVATE_KEY);
	out = save_via_editor (in, &error);
	CHECK ("no PrivateKey: refused", !out && error);
}

int
main (int argc, char **argv)
{
	g_autoptr (GError) error = NULL;
	NMVpnEditorPlugin *(*factory) (GError **);
	void *h;

	if (argc != 2) {
		fprintf (stderr, "usage: %s PLUGIN.so\n", argv[0]);
		return 2;
	}
	if (!gtk_init_check ()) {
		printf ("SKIP: GTK cannot open a display\n");
		return TEST_SKIP;
	}
	g_setenv ("LANGUAGE", "C", TRUE); /* error messages are checked in English */
	h = dlopen (argv[1], RTLD_NOW);
	factory = h ? dlsym (h, "nm_vpn_editor_plugin_factory") : NULL;
	plugin = factory ? factory (&error) : NULL;
	if (!plugin) {
		fprintf (stderr, "cannot load %s\n", argv[1]);
		return 2;
	}

	test_keeps_everything ();
	test_migrates_legacy_hpk ();
	test_ipacket_refusals ();
	test_shared_checks ();
	test_required ();
	return test_finish ();
}
