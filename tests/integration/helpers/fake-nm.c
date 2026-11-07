/* Plays NetworkManager's role towards nm-amneziawg-service:
 * imports a .conf through the libnm plugin, calls Connect over D-Bus,
 * waits for Config/Ip4Config/Ip6Config and prints what NM would apply.
 * usage: fake-nm plugin.so file.conf bus-name   (connect)
 *        fake-nm - - bus-name                   (disconnect) */
#include <NetworkManager.h>
#include <dlfcn.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>

#define PATH  "/org/freedesktop/NetworkManager/VPN/Plugin"
#define IFACE "org.freedesktop.NetworkManager.VPN.Plugin"

static GMainLoop *loop;
static gboolean got_cfg, got_ip4, want_ip6, got_ip6;
static int rc = 1;

static void
ip4s (guint32 a, char *buf)
{
	inet_ntop (AF_INET, &a, buf, INET_ADDRSTRLEN);
}

static void
on_signal (GDBusConnection *c, const char *sender, const char *path, const char *iface,
           const char *name, GVariant *params, gpointer data)
{
	g_autofree char *dump = g_variant_print (params, TRUE);
	char b[64];

	fprintf (stderr, "signal %s %s\n", name, dump);
	if (g_str_equal (name, "Failure")) {
		printf ("FAILURE\n");
		g_main_loop_quit (loop);
		return;
	}
	if (g_str_equal (name, "Config")) {
		g_autoptr (GVariant) d = g_variant_get_child_value (params, 0);
		const char *dev;
		gboolean has6 = FALSE;
		guint32 mtu, gw;
		if (g_variant_lookup (d, "tundev", "&s", &dev))
			printf ("TUNDEV=%s\n", dev);
		if (g_variant_lookup (d, "mtu", "u", &mtu))
			printf ("MTU=%u\n", mtu);
		if (g_variant_lookup (d, "gateway", "u", &gw)) {
			ip4s (gw, b);
			printf ("EXT_GATEWAY=%s\n", b);
		}
		g_variant_lookup (d, "has-ip6", "b", &has6);
		want_ip6 = has6;
		got_cfg = TRUE;
	}
	if (g_str_equal (name, "Ip4Config")) {
		g_autoptr (GVariant) d = g_variant_get_child_value (params, 0);
		g_autoptr (GVariant) routes = NULL, dns = NULL;
		guint32 addr, prefix;
		gboolean nd = FALSE;
		if (g_variant_lookup (d, "address", "u", &addr) && g_variant_lookup (d, "prefix", "u", &prefix)) {
			ip4s (addr, b);
			printf ("ADDR4=%s/%u\n", b, prefix);
		}
		if ((routes = g_variant_lookup_value (d, "routes", G_VARIANT_TYPE ("aau")))) {
			for (gsize i = 0; i < g_variant_n_children (routes); i++) {
				g_autoptr (GVariant) r = g_variant_get_child_value (routes, i);
				gsize n;
				const guint32 *v = g_variant_get_fixed_array (r, &n, sizeof (guint32));
				ip4s (v[0], b);
				printf ("ROUTE4=%s/%u\n", b, v[1]);
			}
		}
		if ((dns = g_variant_lookup_value (d, "dns", G_VARIANT_TYPE ("au")))) {
			for (gsize i = 0; i < g_variant_n_children (dns); i++) {
				guint32 a;
				g_variant_get_child (dns, i, "u", &a);
				ip4s (a, b);
				printf ("DNS4=%s\n", b);
			}
		}
		g_variant_lookup (d, "never-default", "b", &nd);
		printf ("NEVER_DEFAULT4=%d\n", nd);
		got_ip4 = TRUE;
	}
	if (g_str_equal (name, "Ip6Config"))
		got_ip6 = TRUE;
	if (got_cfg && got_ip4 && (!want_ip6 || got_ip6)) {
		rc = 0;
		g_main_loop_quit (loop);
	}
}

static gboolean
timeout (gpointer d)
{
	printf ("TIMEOUT\n");
	g_main_loop_quit (loop);
	return G_SOURCE_REMOVE;
}

int
main (int argc, char **argv)
{
	g_autoptr (GError) error = NULL;
	g_autoptr (GDBusConnection) bus = NULL;
	g_autoptr (GVariant) ret = NULL;
	const char *busname = argv[3];

	bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (!bus) {
		fprintf (stderr, "bus: %s\n", error->message);
		return 1;
	}

	if (g_str_equal (argv[1], "-")) {
		ret = g_dbus_connection_call_sync (bus, busname, PATH, IFACE, "Disconnect", NULL, NULL,
		                                   G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &error);
		printf (ret ? "DISCONNECTED\n" : "DISCONNECT_ERROR %s\n", ret ? "" : error->message);
		return ret ? 0 : 1;
	}

	void *h = dlopen (argv[1], RTLD_NOW);
	NMVpnEditorPlugin *(*factory) (GError **) = dlsym (h, "nm_vpn_editor_plugin_factory");
	NMVpnEditorPlugin *plugin = factory (&error);
	NMConnection *con;
	if (g_str_has_suffix (argv[2], ".nmconnection")) {
		/* Load the keyfile NetworkManager would read back from disk. */
		g_autoptr (GKeyFile) kf = g_key_file_new ();
		if (!g_key_file_load_from_file (kf, argv[2], G_KEY_FILE_NONE, &error)) {
			fprintf (stderr, "keyfile: %s\n", error->message);
			return 1;
		}
		con = nm_keyfile_read (kf, "/", NM_KEYFILE_HANDLER_FLAGS_NONE, NULL, NULL, &error);
	} else
		con = nm_vpn_editor_plugin_import (plugin, argv[2], &error);
	if (!con) {
		fprintf (stderr, "import: %s\n", error->message);
		return 1;
	}

	loop = g_main_loop_new (NULL, FALSE);
	g_dbus_connection_signal_subscribe (bus, busname, IFACE, NULL, PATH, NULL,
	                                    G_DBUS_SIGNAL_FLAGS_NONE, on_signal, NULL, NULL);

	ret = g_dbus_connection_call_sync (bus, busname, PATH, IFACE, "NeedSecrets",
	                                   g_variant_new ("(@a{sa{sv}})", nm_connection_to_dbus (con, NM_CONNECTION_SERIALIZE_ALL)),
	                                   NULL, G_DBUS_CALL_FLAGS_NONE, 10000, NULL, &error);
	if (!ret) {
		printf ("NEEDSECRETS_ERROR %s\n", error->message);
		return 1;
	}
	{
		const char *s;
		g_variant_get (ret, "(&s)", &s);
		printf ("NEED_SECRETS='%s'\n", s);
	}
	g_clear_pointer (&ret, g_variant_unref);

	ret = g_dbus_connection_call_sync (bus, busname, PATH, IFACE, "Connect",
	                                   g_variant_new ("(@a{sa{sv}})", nm_connection_to_dbus (con, NM_CONNECTION_SERIALIZE_ALL)),
	                                   NULL, G_DBUS_CALL_FLAGS_NONE, 20000, NULL, &error);
	if (!ret) {
		printf ("CONNECT_ERROR %s\n", error->message);
		return 1;
	}
	g_timeout_add_seconds (10, timeout, NULL);
	g_main_loop_run (loop);
	return rc;
}
