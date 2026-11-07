/* .conf import/export through the libnm plugin, secret storage and the text
 * handed to `awg setconf`. All keys are fake.
 * usage: test-conf PLUGIN.so */
#include "awg-common.h"
#include "check.h"

#include <dlfcn.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

#define KEY_A "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
#define KEY_B "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBA="
#define KEY_C "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCA="
#define HPK   "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHA="

/* As exported by the Amnezia app, plus things the importer must handle. */
static const char amnezia_conf[] =
	"[Interface]\n"
	"PrivateKey = " KEY_A "\n"
	"Address = 10.8.1.2/32\n"
	"Address = fd00::2/128   # second line is joined to the first\n"
	"DNS = 1.1.1.1, 1.0.0.1\n"
	"Jc = 4\nJmin = 10\nJmax = 50\nS1 = 117\nS2 = 21\nS3 = 13\nS4 = 14\n"
	"H1 = 1157836183\nH2 = 1342456823\nH3 = 993474852\nH4 = 1781947022\n"
	"I1 = <b 0x1a2b><r 16>\n"
	"headerprotectionkey = " HPK "\n" /* keys are case-insensitive */
	"FutureKey = 7\n"                   /* unknown: kept for awg setconf */
	"PostUp = touch /tmp/pwned\n"       /* scripts are never imported */
	"Table = off\n"
	"\n"
	"[Peer]\n"
	"PublicKey = " KEY_B "\n"
	"PresharedKey = " KEY_C "\n"
	"Endpoint = vpn.example.com:51820\n"
	"AllowedIPs = 0.0.0.0/0, ::/0\n"
	"PersistentKeepalive = 25\n";

static NMVpnEditorPlugin *plugin;
static char *tmpdir;

static NMSettingVpn *
vpn (NMConnection *c)
{
	return nm_connection_get_setting_vpn (c);
}

static char *
write_tmp (const char *name, const char *text)
{
	char *path = g_build_filename (tmpdir, name, NULL);

	g_file_set_contents (path, text, -1, NULL);
	return path;
}

static NMConnection *
import_text (const char *name, const char *text, GError **error)
{
	g_autofree char *path = write_tmp (name, text);

	return nm_vpn_editor_plugin_import (plugin, path, error);
}

static gboolean
is_secret_none (NMSettingVpn *s, const char *key)
{
	NMSettingSecretFlags fl = 99;

	nm_setting_get_secret_flags (NM_SETTING (s), key, &fl, NULL);
	return fl == NM_SETTING_SECRET_FLAG_NONE;
}

static gboolean
once (const char *haystack, const char *needle)
{
	const char *p = strstr (haystack, needle);

	return p && !strstr (p + 1, needle);
}

static gboolean
no_dots (NMSettingVpn *s)
{
	g_autofree const char **keys = nm_setting_vpn_get_data_keys (s, NULL);

	for (guint i = 0; keys && keys[i]; i++) {
		if (strchr (keys[i], '.'))
			return FALSE;
	}
	return TRUE;
}

static void
remove_tmpdir (void)
{
	g_autoptr (GDir) dir = g_dir_open (tmpdir, 0, NULL);
	const char *name;

	while (dir && (name = g_dir_read_name (dir))) {
		g_autofree char *path = g_build_filename (tmpdir, name, NULL);
		g_unlink (path);
	}
	g_rmdir (tmpdir);
}

static void
test_import (void)
{
	g_autoptr (GError) error = NULL;
	g_autoptr (NMConnection) c = import_text ("office.conf", amnezia_conf, &error);
	NMSettingVpn *s;

	printf ("import:\n");
	CHECK ("Amnezia config is imported", c != NULL);
	if (!c)
		return;
	s = vpn (c);
	CHECK ("connection is named after the file", !g_strcmp0 (nm_connection_get_id (c), "office"));
	CHECK ("service type", !g_strcmp0 (nm_setting_vpn_get_service_type (s), AWG_DBUS_SERVICE));
	CHECK ("Address lines are joined",
	       !g_strcmp0 (nm_setting_vpn_get_data_item (s, AWG_KEY_ADDRESS), "10.8.1.2/32, fd00::2/128"));
	CHECK ("obfuscation in vpn.data", !g_strcmp0 (nm_setting_vpn_get_data_item (s, "H1"), "1157836183"));
	CHECK ("I1 kept verbatim", !g_strcmp0 (nm_setting_vpn_get_data_item (s, "I1"), "<b 0x1a2b><r 16>"));
	CHECK ("unknown key kept", !g_strcmp0 (nm_setting_vpn_get_data_item (s, "FutureKey"), "7"));
	CHECK ("PostUp dropped", !nm_setting_vpn_get_data_item (s, "PostUp"));
	CHECK ("Table dropped", !nm_setting_vpn_get_data_item (s, "Table"));
	CHECK ("peer keys get the Peer- prefix",
	       !g_strcmp0 (nm_setting_vpn_get_data_item (s, AWG_KEY_ENDPOINT), "vpn.example.com:51820"));
	CHECK ("no key contains a dot (netplan splits on dots)", no_dots (s));

	/* Secrets: in vpn.secrets (root-only), never in vpn.data. */
	const char *secrets[] = { AWG_KEY_PRIVATE_KEY, AWG_KEY_PRESHARED_KEY, AWG_KEY_HEADER_PROTECTION_KEY };
	const char *values[] = { KEY_A, KEY_C, HPK };
	for (guint i = 0; i < G_N_ELEMENTS (secrets); i++) {
		g_autofree char *n1 = g_strdup_printf ("%s in vpn.secrets", secrets[i]);
		g_autofree char *n2 = g_strdup_printf ("%s not in vpn.data", secrets[i]);
		g_autofree char *n3 = g_strdup_printf ("%s flags NONE (system-owned)", secrets[i]);
		CHECK (n1, !g_strcmp0 (nm_setting_vpn_get_secret (s, secrets[i]), values[i]));
		CHECK (n2, !nm_setting_vpn_get_data_item (s, secrets[i]));
		CHECK (n3, is_secret_none (s, secrets[i]));
	}
	CHECK ("connection verifies", nm_connection_verify (c, NULL));
}

static void
test_refusals (void)
{
	struct {
		const char *name, *text, *expect;
	} cases[] = {
		{ "plain WireGuard config is left to NM's WireGuard support",
		  "[Interface]\nPrivateKey = " KEY_A "\nAddress = 10.0.0.2/32\n\n[Peer]\nPublicKey = " KEY_B
		  "\nEndpoint = 1.2.3.4:51820\nAllowedIPs = 0.0.0.0/0\n",
		  "WireGuard" },
		{ "two [Peer] sections",
		  "[Interface]\nPrivateKey = " KEY_A "\nJc = 4\n\n[Peer]\nPublicKey = " KEY_B
		  "\nEndpoint = 1.2.3.4:1\n\n[Peer]\nPublicKey = " KEY_C "\nEndpoint = 1.2.3.4:2\n",
		  "[Peer]" },
		{ "peer without Endpoint",
		  "[Interface]\nPrivateKey = " KEY_A "\nJc = 4\n\n[Peer]\nPublicKey = " KEY_B "\n", "Endpoint" },
		{ "unknown section", "[Interface]\nJc = 4\n[Foo]\n", "Foo" },
		{ "line outside a section", "Jc = 4\n", "section" },
	};

	printf ("refusals:\n");
	for (guint i = 0; i < G_N_ELEMENTS (cases); i++) {
		g_autoptr (GError) error = NULL;
		g_autoptr (NMConnection) c = import_text ("bad.conf", cases[i].text, &error);
		CHECK (cases[i].name, !c && error && strstr (error->message, cases[i].expect));
	}
}

static void
test_export (void)
{
	g_autoptr (GError) error = NULL;
	g_autoptr (NMConnection) c = import_text ("office.conf", amnezia_conf, NULL), c2 = NULL;
	g_autofree char *path = g_build_filename (tmpdir, "exported.conf", NULL), *text = NULL;
	struct stat st;

	printf ("export:\n");
	CHECK ("export succeeds", c && nm_vpn_editor_plugin_export (plugin, path, c, &error));
	g_file_get_contents (path, &text, NULL, NULL);
	CHECK ("file is private (0600)", stat (path, &st) == 0 && (st.st_mode & 0777) == 0600);
	CHECK ("has [Interface] and [Peer]", text && strstr (text, "[Interface]\n") && strstr (text, "\n[Peer]\n"));
	CHECK ("PrivateKey exported once", text && once (text, "PrivateKey = " KEY_A "\n"));
	CHECK ("HeaderProtectionKey exported once", text && once (text, "HeaderProtectionKey = " HPK "\n"));
	CHECK ("PresharedKey under [Peer] without prefix",
	       text && strstr (text, "\nPresharedKey = " KEY_C "\n") && !strstr (text, "Peer-"));
	CHECK ("Address and DNS kept (awg-quick format)", text && strstr (text, "Address = ") && strstr (text, "DNS = "));
	CHECK ("no -flags helper keys", text && !strstr (text, "-flags"));
	CHECK ("PostUp not exported", text && !strstr (text, "PostUp"));

	/* Round trip: the exported file imports to the same settings. */
	c2 = nm_vpn_editor_plugin_import (plugin, path, NULL);
	CHECK ("exported file imports again", c2 != NULL);
	if (c2) {
		NMSettingVpn *a = vpn (c), *b = vpn (c2);
		g_autofree const char **keys = nm_setting_vpn_get_data_keys (a, NULL);
		gboolean same = nm_setting_vpn_get_num_data_items (a) == nm_setting_vpn_get_num_data_items (b);
		for (guint i = 0; keys && keys[i]; i++)
			same &= !g_strcmp0 (nm_setting_vpn_get_data_item (a, keys[i]), nm_setting_vpn_get_data_item (b, keys[i]));
		CHECK ("round trip: same vpn.data", same);
		CHECK ("round trip: same secrets",
		       !g_strcmp0 (nm_setting_vpn_get_secret (b, AWG_KEY_HEADER_PROTECTION_KEY), HPK)
		           && !g_strcmp0 (nm_setting_vpn_get_secret (b, AWG_KEY_PRIVATE_KEY), KEY_A));
	}
}

static void
test_setconf (void)
{
	g_autoptr (NMConnection) c = import_text ("office.conf", amnezia_conf, NULL);
	g_autoptr (NMConnection) legacy = NULL;
	g_autofree char *text = NULL, *old = NULL;

	printf ("awg setconf text:\n");
	if (!c) {
		CHECK ("import for setconf", FALSE);
		return;
	}
	text = awg_conf_to_setconf (vpn (c), "203.0.113.7:51820");
	CHECK ("no Address/DNS/MTU (awg-quick only)",
	       !strstr (text, "Address") && !strstr (text, "DNS") && !strstr (text, "MTU"));
	CHECK ("Endpoint replaced by the resolved address", strstr (text, "Endpoint = 203.0.113.7:51820\n") != NULL);
	CHECK ("PrivateKey taken from secrets", once (text, "PrivateKey = " KEY_A "\n"));
	CHECK ("HeaderProtectionKey taken from secrets", once (text, "HeaderProtectionKey = " HPK "\n"));
	CHECK ("PresharedKey taken from secrets", once (text, "PresharedKey = " KEY_C "\n"));
	CHECK ("unknown key passed through", strstr (text, "FutureKey = 7\n") != NULL);
	CHECK ("no -flags helper keys", !strstr (text, "-flags"));
	CHECK ("exact text, keys in registry order",
	       !g_strcmp0 (text, "[Interface]\n"
	                         "PrivateKey = " KEY_A "\n"
	                         "Jc = 4\nJmin = 10\nJmax = 50\nS1 = 117\nS2 = 21\nS3 = 13\nS4 = 14\n"
	                         "H1 = 1157836183\nH2 = 1342456823\nH3 = 993474852\nH4 = 1781947022\n"
	                         "I1 = <b 0x1a2b><r 16>\n"
	                         "HeaderProtectionKey = " HPK "\n"
	                         "FutureKey = 7\n"
	                         "\n[Peer]\n"
	                         "PublicKey = " KEY_B "\n"
	                         "PresharedKey = " KEY_C "\n"
	                         "Endpoint = 203.0.113.7:51820\n"
	                         "AllowedIPs = 0.0.0.0/0, ::/0\n"
	                         "PersistentKeepalive = 25\n"));

	/* Connections saved by 0.3.3 and older keep HeaderProtectionKey in vpn.data. */
	legacy = nm_simple_connection_new_clone (c);
	nm_setting_vpn_remove_secret (vpn (legacy), AWG_KEY_HEADER_PROTECTION_KEY);
	nm_setting_vpn_add_data_item (vpn (legacy), AWG_KEY_HEADER_PROTECTION_KEY, "OLD");
	old = awg_conf_to_setconf (vpn (legacy), NULL);
	CHECK ("legacy HeaderProtectionKey from vpn.data, once", once (old, "HeaderProtectionKey = OLD\n"));
}

static void
test_registry (void)
{
	gboolean prefix_ok = TRUE, secrets_ok = TRUE, no_dots = TRUE, ranges_ok = TRUE, unique = TRUE;
	const AwgKey *k;

	printf ("key registry:\n");
	for (k = awg_keys; k->name; k++) {
		g_autofree char *expect = k->section == AWG_SECTION_PEER ? g_strconcat (AWG_PEER_PREFIX, k->conf_name, NULL)
		                                                          : g_strdup (k->conf_name);
		prefix_ok &= g_str_equal (k->name, expect);
		secrets_ok &= !(k->flags & AWG_KF_SECRET) || k->check == AWG_CHECK_KEY;
		no_dots &= !strchr (k->name, '.');
		ranges_ok &= k->check != AWG_CHECK_NUMBER || k->min <= k->max;
		unique &= awg_key_find (k->name) == k;
	}
	CHECK ("stored name = Peer- prefix + conf name for [Peer], conf name otherwise", prefix_ok);
	CHECK ("every secret is a base64 key", secrets_ok);
	CHECK ("no dots in names (netplan)", no_dots);
	CHECK ("number ranges", ranges_ok);
	CHECK ("names are unique", unique);
	CHECK ("conf lookup ignores case", awg_key_find_conf (AWG_SECTION_PEER, "allowedips")
	                                       == awg_key_find (AWG_KEY_ALLOWED_IPS));
	CHECK ("conf lookup respects the section", !awg_key_find_conf (AWG_SECTION_INTERFACE, "Endpoint"));
	CHECK ("the AWG_KEY_* names are in the registry",
	       awg_key_find (AWG_KEY_PRIVATE_KEY) && awg_key_find (AWG_KEY_ADDRESS) && awg_key_find (AWG_KEY_DNS)
	           && awg_key_find (AWG_KEY_MTU) && awg_key_find (AWG_KEY_PUBLIC_KEY)
	           && awg_key_find (AWG_KEY_PRESHARED_KEY) && awg_key_find (AWG_KEY_HEADER_PROTECTION_KEY)
	           && awg_key_find (AWG_KEY_ENDPOINT) && awg_key_find (AWG_KEY_ALLOWED_IPS));
}

static void
test_checks (void)
{
	g_autoptr (NMConnection) c = import_text ("office.conf", amnezia_conf, NULL);
	g_autoptr (GError) error = NULL;
	g_autofree char *reason = NULL;
	const AwgKey *mtu = awg_key_find (AWG_KEY_MTU), *ka = awg_key_find ("Peer-PersistentKeepalive");
	const AwgKey *psk = awg_key_find (AWG_KEY_PRESHARED_KEY), *addr = awg_key_find (AWG_KEY_ADDRESS);

	printf ("value checks:\n");
	CHECK ("imported config passes", c && awg_setting_check (vpn (c), NULL));
	CHECK ("MTU 1420", awg_key_check (mtu, "1420", NULL));
	CHECK ("MTU 500 refused", !awg_key_check (mtu, "500", NULL));
	CHECK ("MTU 1e3 refused", !awg_key_check (mtu, "1e3", NULL));
	CHECK ("keepalive off", awg_key_check (ka, "off", NULL));
	CHECK ("MTU off refused", !awg_key_check (mtu, "off", NULL));
	CHECK ("optional key may be unset", awg_key_check (psk, NULL, NULL));
	CHECK ("required key unset: refused, no reason", !awg_key_check (addr, "", &reason) && !reason);
	CHECK ("bad address named in the reason",
	       !awg_key_check (addr, "10.8.1.2/32, 10.8.1.300", &reason) && reason && strstr (reason, "10.8.1.300"));
	g_clear_pointer (&reason, g_free);
	CHECK ("bad key: reason without the value",
	       !awg_key_check (psk, "NOT-A-KEY", &reason) && reason && !strstr (reason, "NOT-A-KEY"));
	if (c) {
		nm_setting_vpn_add_data_item (vpn (c), "I2", "<x>");
		CHECK ("setting check names the key",
		       !awg_setting_check (vpn (c), &error) && strstr (error->message, "invalid I2: "));
		g_clear_error (&error);
		nm_setting_vpn_remove_data_item (vpn (c), "I2");
		nm_setting_vpn_add_data_item (vpn (c), AWG_KEY_ADDRESS, "fd00::2/128");
		CHECK ("IPv4 AllowedIPs without an IPv4 address refused",
		       !awg_setting_check (vpn (c), &error) && strstr (error->message, "invalid Address: an IPv4 address"));
		g_clear_error (&error);
		CHECK ("family check: IPv6-only routes need no IPv4 address", awg_check_families ("fd00::2/128", "::/0", NULL));
		CHECK ("family check: IPv4 address and IPv4 routes", awg_check_families ("10.8.1.2/32", "0.0.0.0/0", NULL));
		nm_setting_vpn_add_data_item (vpn (c), AWG_KEY_ADDRESS, "10.8.1.2/32");
		nm_setting_vpn_remove_secret (vpn (c), AWG_KEY_PRIVATE_KEY);
		CHECK ("setting check reports a missing key",
		       !awg_setting_check (vpn (c), &error) && !g_strcmp0 (error->message, "PrivateKey is missing"));
	}
}

static void
test_helpers (void)
{
	AwgCidr cidr;
	g_autofree char *host = NULL, *port = NULL;
	g_auto (GStrv) parts = awg_split_list (" 1.1.1.1,1.0.0.1 ,, example.com ");

	printf ("helpers:\n");
	CHECK ("CIDR v4", awg_parse_cidr ("10.8.1.2/24", &cidr) && cidr.family == AF_INET && cidr.prefix == 24);
	CHECK ("CIDR v6 without prefix is /128", awg_parse_cidr ("fd00::1", &cidr) && cidr.prefix == 128);
	CHECK ("CIDR bad prefix", !awg_parse_cidr ("10.0.0.1/33", &cidr));
	CHECK ("CIDR garbage", !awg_parse_cidr ("10.0.0/8", &cidr));
	CHECK ("endpoint host:port", awg_parse_endpoint ("vpn.example.com:51820", &host, &port)
	                                 && !g_strcmp0 (host, "vpn.example.com") && !g_strcmp0 (port, "51820"));
	CHECK ("endpoint [v6]:port", awg_parse_endpoint ("[2001:db8::1]:443", NULL, NULL));
	CHECK ("endpoint bare v6 refused", !awg_parse_endpoint ("2001:db8::1:443", NULL, NULL));
	CHECK ("endpoint port 0 refused", !awg_parse_endpoint ("host:0", NULL, NULL));
	CHECK ("endpoint http:// refused", !awg_parse_endpoint ("http://1.2.3.4/:51820", NULL, NULL));
	CHECK ("split list", g_strv_length (parts) == 3 && !g_strcmp0 (parts[2], "example.com"));
	CHECK ("secret keys", awg_key_is_secret (AWG_KEY_PRIVATE_KEY) && awg_key_is_secret (AWG_KEY_PRESHARED_KEY)
	                          && awg_key_is_secret (AWG_KEY_HEADER_PROTECTION_KEY) && !awg_key_is_secret ("Jc")
	                          && !awg_key_is_secret (AWG_KEY_PUBLIC_KEY));
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
	h = dlopen (argv[1], RTLD_NOW);
	factory = h ? dlsym (h, "nm_vpn_editor_plugin_factory") : NULL;
	plugin = factory ? factory (&error) : NULL;
	if (!plugin) {
		fprintf (stderr, "cannot load %s: %s\n", argv[1], h ? (error ? error->message : "no factory") : dlerror ());
		return 2;
	}
	tmpdir = g_dir_make_tmp ("awg-test-XXXXXX", NULL);

	test_import ();
	test_refusals ();
	test_export ();
	test_setconf ();
	test_registry ();
	test_checks ();
	test_helpers ();

	remove_tmpdir ();
	return test_finish ();
}
