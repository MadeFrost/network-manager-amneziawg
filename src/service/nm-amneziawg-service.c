/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * NetworkManager VPN service for AmneziaWG.
 *
 * NetworkManager starts this program (as root) when an AmneziaWG VPN
 * connection is activated. It creates an `amneziawg` kernel interface,
 * configures it with `awg setconf` and reports addresses, routes and DNS
 * back to NetworkManager, which applies them to the interface.
 */
#include <NetworkManager.h>
#include <glib-unix.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>

#include "awg-common.h"
#include "iface.h"
#include "ipconfig.h"

#define DEFAULT_MTU 1420

typedef struct {
	NMVpnServicePlugin parent;
	char *ifname;
	GVariant *config, *ip4, *ip6;
	guint idle_id;
} NMAwgPlugin;

typedef struct {
	NMVpnServicePluginClass parent;
} NMAwgPluginClass;

GType nm_awg_plugin_get_type (void);
G_DEFINE_TYPE (NMAwgPlugin, nm_awg_plugin, NM_TYPE_VPN_SERVICE_PLUGIN)

/*****************************************************************************/

static void
reset_state (NMAwgPlugin *self)
{
	g_clear_handle_id (&self->idle_id, g_source_remove);
	g_clear_pointer (&self->config, g_variant_unref);
	g_clear_pointer (&self->ip4, g_variant_unref);
	g_clear_pointer (&self->ip6, g_variant_unref);
	if (self->ifname)
		awg_iface_delete (self->ifname);
	g_clear_pointer (&self->ifname, g_free);
}

/*****************************************************************************/

static gboolean
resolve_endpoint (const char *endpoint, struct sockaddr_storage *out_sa, char **out_str, GError **error)
{
	g_autofree char *host = NULL, *port = NULL;
	struct addrinfo hints = { .ai_socktype = SOCK_DGRAM, .ai_flags = AI_ADDRCONFIG };
	struct addrinfo *res = NULL, *ai, *pick = NULL;
	char buf[INET6_ADDRSTRLEN];
	int r;

	if (!awg_parse_endpoint (endpoint, &host, &port)) {
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
		             "invalid endpoint '%s' (expected host:port)", endpoint);
		return FALSE;
	}

	r = getaddrinfo (host, port, &hints, &res);
	if (r != 0) {
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
		             "cannot resolve endpoint '%s': %s", host, gai_strerror (r));
		return FALSE;
	}
	for (ai = res; ai; ai = ai->ai_next) {
		if (ai->ai_family == AF_INET) {
			pick = ai;
			break;
		}
		if (ai->ai_family == AF_INET6 && !pick)
			pick = ai;
	}
	if (!pick) {
		freeaddrinfo (res);
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
		             "endpoint '%s' has no usable address", host);
		return FALSE;
	}
	memset (out_sa, 0, sizeof *out_sa);
	memcpy (out_sa, pick->ai_addr, pick->ai_addrlen);
	freeaddrinfo (res);

	if (out_sa->ss_family == AF_INET) {
		inet_ntop (AF_INET, &((struct sockaddr_in *) out_sa)->sin_addr, buf, sizeof buf);
		*out_str = g_strdup_printf ("%s:%s", buf, port);
	} else {
		inet_ntop (AF_INET6, &((struct sockaddr_in6 *) out_sa)->sin6_addr, buf, sizeof buf);
		*out_str = g_strdup_printf ("[%s]:%s", buf, port);
	}
	return TRUE;
}

static gboolean
send_config_idle (gpointer user_data)
{
	NMAwgPlugin *self = user_data;
	NMVpnServicePlugin *base = NM_VPN_SERVICE_PLUGIN (self);

	self->idle_id = 0;
	nm_vpn_service_plugin_set_config (base, self->config);
	if (self->ip4)
		nm_vpn_service_plugin_set_ip4_config (base, self->ip4);
	if (self->ip6)
		nm_vpn_service_plugin_set_ip6_config (base, self->ip6);
	g_message ("%s is up", self->ifname);
	return G_SOURCE_REMOVE;
}

/*****************************************************************************/

static gboolean
real_connect (NMVpnServicePlugin *base, NMConnection *connection, GError **error)
{
	NMAwgPlugin *self = (NMAwgPlugin *) base;
	NMSettingVpn *s_vpn = nm_connection_get_setting_vpn (connection);
	const char *endpoint, *mtu_str;
	g_autofree char *endpoint_ip = NULL, *setconf = NULL, *ifname = NULL;
	struct sockaddr_storage gw;
	guint32 mtu = DEFAULT_MTU;

	reset_state (self);

	if (!s_vpn) {
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_INVALID_CONNECTION,
		             "no VPN setting");
		return FALSE;
	}
	/* The same checks as the editor's, so a bad value is named here instead
	 * of failing obscurely in `awg setconf`. */
	{
		g_autoptr (GError) bad = NULL;

		if (!awg_setting_check (s_vpn, &bad)) {
			g_set_error_literal (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS, bad->message);
			return FALSE;
		}
	}
	endpoint = nm_setting_vpn_get_data_item (s_vpn, AWG_KEY_ENDPOINT);
	mtu_str = nm_setting_vpn_get_data_item (s_vpn, AWG_KEY_MTU);
	if (mtu_str)
		mtu = (guint32) g_ascii_strtoull (mtu_str, NULL, 10); /* range checked above */

	if (!resolve_endpoint (endpoint, &gw, &endpoint_ip, error))
		return FALSE;

	awg_iface_remove_stale ();
	ifname = awg_iface_pick_name ();
	if (!ifname) {
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             "no free interface name");
		return FALSE;
	}
	if (!awg_ip_config_build (s_vpn, ifname, &gw, mtu, &self->config, &self->ip4, &self->ip6, error)
	    || !awg_iface_create (ifname, error)) {
		reset_state (self); /* the interface does not exist: nothing to delete */
		return FALSE;
	}
	self->ifname = g_steal_pointer (&ifname);

	setconf = awg_conf_to_setconf (s_vpn, endpoint_ip);
	if (!awg_iface_configure (self->ifname, setconf, mtu, error)) {
		reset_state (self);
		return FALSE;
	}

	g_message ("created %s, endpoint %s", self->ifname, endpoint_ip);
	/* Report config after the Connect() D-Bus call has returned. */
	self->idle_id = g_idle_add (send_config_idle, self);
	return TRUE;
}

static gboolean
real_need_secrets (NMVpnServicePlugin *base, NMConnection *connection,
                   const char **setting_name, GError **error)
{
	/* Keys are stored in the system connection; nothing to ask the user.
	 * A missing key is reported by connect() with a clear error. */
	return FALSE;
}

static gboolean
real_disconnect (NMVpnServicePlugin *base, GError **error)
{
	NMAwgPlugin *self = (NMAwgPlugin *) base;

	if (self->ifname)
		g_message ("removing %s", self->ifname);
	reset_state (self);
	return TRUE;
}

static void
nm_awg_plugin_init (NMAwgPlugin *self)
{
}

static void
dispose (GObject *object)
{
	reset_state ((NMAwgPlugin *) object);
	G_OBJECT_CLASS (nm_awg_plugin_parent_class)->dispose (object);
}

static void
nm_awg_plugin_class_init (NMAwgPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMVpnServicePluginClass *parent_class = NM_VPN_SERVICE_PLUGIN_CLASS (klass);

	object_class->dispose = dispose;
	parent_class->connect = real_connect;
	parent_class->need_secrets = real_need_secrets;
	parent_class->disconnect = real_disconnect;
}

/*****************************************************************************/

static gboolean
signal_quit (gpointer loop)
{
	g_main_loop_quit (loop);
	return G_SOURCE_REMOVE;
}

static void
plugin_quit (NMVpnServicePlugin *plugin, gpointer loop)
{
	g_main_loop_quit (loop);
}

int
main (int argc, char *argv[])
{
	gboolean persist = FALSE, debug = FALSE;
	g_autofree char *bus_name = NULL;
	g_autoptr (GOptionContext) ctx = NULL;
	g_autoptr (GError) error = NULL;
	g_autoptr (GMainLoop) loop = NULL;
	NMAwgPlugin *plugin;
	GOptionEntry entries[] = {
		{ "persist", 0, 0, G_OPTION_ARG_NONE, &persist, "Don't quit when VPN connection terminates", NULL },
		{ "debug", 0, 0, G_OPTION_ARG_NONE, &debug, "Enable verbose debug logging", NULL },
		{ "bus-name", 0, 0, G_OPTION_ARG_STRING, &bus_name, "D-Bus name to use for this instance", NULL },
		{ NULL },
	};

	ctx = g_option_context_new (NULL);
	g_option_context_set_summary (ctx, "nm-amneziawg-service provides AmneziaWG VPN support to NetworkManager.");
	g_option_context_add_main_entries (ctx, entries, NULL);
	if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
		g_printerr ("Error parsing the command line options: %s\n", error->message);
		return 1;
	}

	if (!bus_name)
		bus_name = g_strdup (AWG_DBUS_SERVICE);

	plugin = g_initable_new (nm_awg_plugin_get_type (), NULL, &error,
	                         NM_VPN_SERVICE_PLUGIN_DBUS_SERVICE_NAME, bus_name,
	                         NM_VPN_SERVICE_PLUGIN_DBUS_WATCH_PEER, !debug,
	                         NULL);
	if (!plugin) {
		g_printerr ("Failed to initialize the plugin: %s\n", error->message);
		return 1;
	}

	loop = g_main_loop_new (NULL, FALSE);
	if (!persist)
		g_signal_connect (plugin, "quit", G_CALLBACK (plugin_quit), loop);
	g_unix_signal_add (SIGTERM, signal_quit, loop);
	g_unix_signal_add (SIGINT, signal_quit, loop);

	g_main_loop_run (loop);

	g_object_unref (plugin); /* removes the interface */
	return 0;
}
