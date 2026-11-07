/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wg-quick style .conf files <-> NMSettingVpn, and the text for `awg setconf`.
 */
#include "awg-conf.h"

#include "awg-common.h"

#include <string.h>

const char *
awg_setting_get (NMSettingVpn *s_vpn, const char *key)
{
	const char *v;

	if (!awg_key_is_secret (key))
		return nm_setting_vpn_get_data_item (s_vpn, key);
	v = nm_setting_vpn_get_secret (s_vpn, key);
	/* Up to 0.3.3 HeaderProtectionKey was kept in vpn.data: still honour it. */
	return v ? v : nm_setting_vpn_get_data_item (s_vpn, key);
}

/* k: the registry entry of key, NULL for an unknown key. */
static void
store_value (NMSettingVpn *s_vpn, const char *key, const AwgKey *k, const char *value)
{
	const char *old = awg_setting_get (s_vpn, key);
	g_autofree char *joined = NULL;

	if (old && *old && k && (k->flags & AWG_KF_LIST))
		value = joined = g_strdup_printf ("%s, %s", old, value);

	if (awg_key_is_secret (key)) {
		nm_setting_vpn_add_secret (s_vpn, key, value);
		nm_setting_set_secret_flags (NM_SETTING (s_vpn), key, NM_SETTING_SECRET_FLAG_NONE, NULL);
	} else
		nm_setting_vpn_add_data_item (s_vpn, key, value);
}

gboolean
awg_conf_parse (const char *text, NMSettingVpn *s_vpn, GError **error)
{
	enum { SEC_NONE, SEC_IFACE, SEC_PEER } section = SEC_NONE;
	g_auto (GStrv) lines = NULL;
	guint n_iface = 0, n_peer = 0;
	guint i;

	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i]; i++) {
		char *line = lines[i];
		char *p, *key, *value;
		const AwgKey *k;
		g_autofree char *full_key = NULL;

		if ((p = strchr (line, '#')))
			*p = '\0';
		g_strstrip (line);
		if (!*line)
			continue;

		if (line[0] == '[') {
			if (g_ascii_strcasecmp (line, "[Interface]") == 0) {
				section = SEC_IFACE;
				n_iface++;
			} else if (g_ascii_strcasecmp (line, "[Peer]") == 0) {
				section = SEC_PEER;
				n_peer++;
			} else {
				g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_FAILED,
				             _("line %u: unknown section %s"), i + 1, line);
				return FALSE;
			}
			continue;
		}

		p = strchr (line, '=');
		if (!p || section == SEC_NONE) {
			g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_FAILED,
			             _("line %u: expected \"Key = Value\" inside a section"), i + 1);
			return FALSE;
		}
		*p = '\0';
		key = g_strstrip (line);
		value = g_strstrip (p + 1);
		if (!*key) {
			g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_FAILED,
			             _("line %u: empty key"), i + 1);
			return FALSE;
		}

		k = awg_key_find_conf (section == SEC_IFACE ? AWG_SECTION_INTERFACE : AWG_SECTION_PEER, key);
		if (k && (k->flags & AWG_KF_DROP))
			continue;
		if (k)
			full_key = g_strdup (k->name);
		else if (section == SEC_IFACE)
			full_key = g_strdup (key); /* unknown: kept for `awg setconf` */
		else
			full_key = g_strconcat (AWG_PEER_PREFIX, key, NULL);
		store_value (s_vpn, full_key, k, value);
	}

	if (n_iface != 1) {
		g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_FAILED,
		             _("config must contain exactly one [Interface] section"));
		return FALSE;
	}
	if (n_peer != 1) {
		g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_FAILED,
		             _("config must contain exactly one [Peer] section (found %u)"), n_peer);
		return FALSE;
	}
	if (!awg_setting_get (s_vpn, AWG_KEY_PUBLIC_KEY) || !awg_setting_get (s_vpn, AWG_KEY_ENDPOINT)) {
		g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_FAILED,
		             _("[Peer] must have PublicKey and Endpoint"));
		return FALSE;
	}
	return TRUE;
}

gboolean
awg_conf_is_amnezia (NMSettingVpn *s_vpn)
{
	const AwgKey *k;

	for (k = awg_keys; k->name; k++) {
		if ((k->flags & AWG_KF_OBFS) && nm_setting_vpn_get_data_item (s_vpn, k->name))
			return TRUE;
	}
	return FALSE;
}

static void
append_kv (GString *s, const char *key, const char *value)
{
	/* NM keeps secret flags as "<key>-flags" data items; never emit those. */
	if (g_str_has_suffix (key, "-flags"))
		return;
	if (value && *value)
		g_string_append_printf (s, "%s = %s\n", key, value);
}

/* Known keys of a section in registry order, then the unknown ones. */
static void
append_section (GString *s, NMSettingVpn *s_vpn, AwgSection section, gboolean quick, const char *endpoint_override)
{
	g_autofree const char **keys = NULL;
	gsize plen = section == AWG_SECTION_PEER ? strlen (AWG_PEER_PREFIX) : 0;
	const AwgKey *k;
	guint n_keys = 0, i;

	for (k = awg_keys; k->name; k++) {
		const char *v;

		if (k->section != section || (k->flags & AWG_KF_DROP) || (!quick && (k->flags & AWG_KF_QUICK_ONLY)))
			continue;
		v = awg_setting_get (s_vpn, k->name);
		if (endpoint_override && g_str_equal (k->name, AWG_KEY_ENDPOINT))
			v = endpoint_override;
		append_kv (s, k->conf_name, v);
	}
	keys = nm_setting_vpn_get_data_keys (s_vpn, &n_keys);
	for (i = 0; i < n_keys; i++) {
		if (g_str_has_prefix (keys[i], AWG_PEER_PREFIX) != (section == AWG_SECTION_PEER) || awg_key_find (keys[i]))
			continue;
		append_kv (s, keys[i] + plen, nm_setting_vpn_get_data_item (s_vpn, keys[i]));
	}
}

static char *
build_conf (NMSettingVpn *s_vpn, gboolean quick, const char *endpoint_override)
{
	GString *s = g_string_new ("[Interface]\n");

	append_section (s, s_vpn, AWG_SECTION_INTERFACE, quick, endpoint_override);
	g_string_append (s, "\n[Peer]\n");
	append_section (s, s_vpn, AWG_SECTION_PEER, quick, endpoint_override);
	return g_string_free (s, FALSE);
}

char *
awg_conf_to_quick (NMSettingVpn *s_vpn)
{
	return build_conf (s_vpn, TRUE, NULL);
}

char *
awg_conf_to_setconf (NMSettingVpn *s_vpn, const char *endpoint_override)
{
	return build_conf (s_vpn, FALSE, endpoint_override);
}
