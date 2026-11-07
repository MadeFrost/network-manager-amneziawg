/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * libnm VPN editor plugin (GTK-free): import/export of .conf files and
 * loading of the GTK3 or GTK4 editor, whichever toolkit the host uses.
 */
#include <NetworkManager.h>

#include <dlfcn.h>
#include <string.h>

#include "awg-common.h"

#define AWG_PLUGIN_NAME "AmneziaWG"
#define AWG_PLUGIN_DESC N_("WireGuard with AmneziaWG traffic obfuscation")

typedef NMVpnEditor *(*AwgEditorFactory) (NMVpnEditorPlugin *, NMConnection *, GError **);

enum { PROP_0, PROP_NAME, PROP_DESC, PROP_SERVICE };

typedef struct {
	GObject parent;
} AwgEditorPlugin;

typedef struct {
	GObjectClass parent;
} AwgEditorPluginClass;

static void awg_editor_plugin_interface_init (NMVpnEditorPluginInterface *iface);

GType awg_editor_plugin_get_type (void);
G_DEFINE_TYPE_WITH_CODE (AwgEditorPlugin, awg_editor_plugin, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (NM_TYPE_VPN_EDITOR_PLUGIN, awg_editor_plugin_interface_init))

/*****************************************************************************/

static NMConnection *
import_from_file (NMVpnEditorPlugin *iface, const char *path, GError **error)
{
	g_autofree char *contents = NULL, *base = NULL, *uuid = NULL;
	g_autoptr (NMSettingVpn) s_vpn = NULL;
	NMConnection *connection;
	NMSetting *s_con, *s_ip4, *s_ip6;
	char *dot;

	if (!g_file_get_contents (path, &contents, NULL, error))
		return NULL;

	s_vpn = NM_SETTING_VPN (nm_setting_vpn_new ());
	g_object_set (s_vpn, NM_SETTING_VPN_SERVICE_TYPE, AWG_DBUS_SERVICE, NULL);

	if (!awg_conf_parse (contents, s_vpn, error))
		return NULL;

	/* Plain WireGuard configs are better served by NetworkManager's native
	 * WireGuard support, so leave those to it. */
	if (!awg_conf_is_amnezia (s_vpn)) {
		g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_FAILED,
		             _("This is a plain WireGuard config without AmneziaWG parameters (Jc, S1, H1…). Import it as a WireGuard connection instead."));
		return NULL;
	}

	base = g_path_get_basename (path);
	if ((dot = strrchr (base, '.')) && dot != base)
		*dot = '\0';
	uuid = nm_utils_uuid_generate ();

	connection = nm_simple_connection_new ();

	s_con = nm_setting_connection_new ();
	g_object_set (s_con,
	              NM_SETTING_CONNECTION_ID, base,
	              NM_SETTING_CONNECTION_UUID, uuid,
	              NM_SETTING_CONNECTION_TYPE, NM_SETTING_VPN_SETTING_NAME,
	              NULL);
	nm_connection_add_setting (connection, s_con);
	nm_connection_add_setting (connection, NM_SETTING (g_steal_pointer (&s_vpn)));

	s_ip4 = nm_setting_ip4_config_new ();
	g_object_set (s_ip4, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP4_CONFIG_METHOD_AUTO, NULL);
	nm_connection_add_setting (connection, s_ip4);

	s_ip6 = nm_setting_ip6_config_new ();
	g_object_set (s_ip6, NM_SETTING_IP_CONFIG_METHOD, NM_SETTING_IP6_CONFIG_METHOD_AUTO, NULL);
	nm_connection_add_setting (connection, s_ip6);

	return connection;
}

static gboolean
export_to_file (NMVpnEditorPlugin *iface, const char *path, NMConnection *connection, GError **error)
{
	NMSettingVpn *s_vpn = nm_connection_get_setting_vpn (connection);
	g_autofree char *text = NULL;

	if (!s_vpn) {
		g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_MISSING_SETTING,
		             _("The connection has no VPN settings"));
		return FALSE;
	}
	text = awg_conf_to_quick (s_vpn);
	return g_file_set_contents_full (path, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error);
}

static char *
get_suggested_filename (NMVpnEditorPlugin *iface, NMConnection *connection)
{
	const char *id = nm_connection_get_id (connection);
	g_autofree char *safe = g_strdup (id && *id ? id : "amneziawg");

	g_strdelimit (safe, "/\\", '_');
	return g_strdup_printf ("%s.conf", safe);
}

static NMVpnEditorPluginCapability
get_capabilities (NMVpnEditorPlugin *iface)
{
	return NM_VPN_EDITOR_PLUGIN_CAPABILITY_IMPORT | NM_VPN_EDITOR_PLUGIN_CAPABILITY_EXPORT;
}

static NMVpnEditor *
get_editor (NMVpnEditorPlugin *iface, NMConnection *connection, GError **error)
{
	static void *handles[2];
	static const char *const libs[2] = {
		"libnm-gtk4-vpn-plugin-amneziawg-editor.so",
		"libnm-vpn-plugin-amneziawg-editor.so",
	};
	/* gtk_container_add() exists in GTK3 only. */
	int gtk3 = dlsym (RTLD_DEFAULT, "gtk_container_add") != NULL;
	AwgEditorFactory factory;

	if (!handles[gtk3]) {
		Dl_info info;
		g_autofree char *dir = NULL, *lib = NULL;

		if (!dladdr ((void *) get_editor, &info) || !info.dli_fname) {
			g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_FAILED,
			             _("Cannot locate the plugin directory"));
			return NULL;
		}
		dir = g_path_get_dirname (info.dli_fname);
		lib = g_build_filename (dir, libs[gtk3], NULL);
		handles[gtk3] = dlopen (lib, RTLD_LAZY | RTLD_LOCAL);
		if (!handles[gtk3]) {
			g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_FAILED,
			             _("Cannot load the editor %s: %s"), lib, dlerror ());
			return NULL;
		}
	}
	factory = (AwgEditorFactory) dlsym (handles[gtk3], "nm_vpn_editor_factory_amneziawg");
	if (!factory) {
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_FAILED,
		             _("The editor library is invalid"));
		return NULL;
	}
	return factory (iface, connection, error);
}

/*****************************************************************************/

static void
get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, AWG_PLUGIN_NAME);
		break;
	case PROP_DESC:
		g_value_set_string (value, _(AWG_PLUGIN_DESC));
		break;
	case PROP_SERVICE:
		g_value_set_string (value, AWG_DBUS_SERVICE);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
awg_editor_plugin_init (AwgEditorPlugin *self)
{
}

static void
awg_editor_plugin_class_init (AwgEditorPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = get_property;
	g_object_class_override_property (object_class, PROP_NAME, NM_VPN_EDITOR_PLUGIN_NAME);
	g_object_class_override_property (object_class, PROP_DESC, NM_VPN_EDITOR_PLUGIN_DESCRIPTION);
	g_object_class_override_property (object_class, PROP_SERVICE, NM_VPN_EDITOR_PLUGIN_SERVICE);
}

static void
awg_editor_plugin_interface_init (NMVpnEditorPluginInterface *iface)
{
	iface->get_editor = get_editor;
	iface->get_capabilities = get_capabilities;
	iface->import_from_file = import_from_file;
	iface->export_to_file = export_to_file;
	iface->get_suggested_filename = get_suggested_filename;
}

AWG_EXPORT NMVpnEditorPlugin *
nm_vpn_editor_plugin_factory (GError **error)
{
	awg_i18n_init ();
	return g_object_new (awg_editor_plugin_get_type (), NULL);
}
