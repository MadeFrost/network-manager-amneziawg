/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * What the service reports to NetworkManager for a connection: the generic,
 * IPv4 and IPv6 config dictionaries. No side effects, so it is unit-tested.
 */
#include "ipconfig.h"

#include "awg-common.h"

#include <arpa/inet.h>
#include <string.h>

static void
mask_cidr (AwgCidr *c)
{
	int len = c->family == AF_INET ? 4 : 16;
	int i;

	for (i = 0; i < len; i++) {
		int bits = c->prefix - i * 8;
		if (bits <= 0)
			c->addr[i] = 0;
		else if (bits < 8)
			c->addr[i] &= (guint8) (0xFF << (8 - bits));
	}
}

static guint32
addr4 (const guint8 *a)
{
	guint32 v;
	memcpy (&v, a, 4); /* network byte order, as NM expects */
	return v;
}

static GVariant *
addr6 (const guint8 *a)
{
	return g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, a, 16, 1);
}

/* Builds the three dictionaries NetworkManager wants. */
gboolean
awg_ip_config_build (NMSettingVpn *s_vpn, const char *ifname, const struct sockaddr_storage *gw, guint32 mtu,
                     GVariant **out_config, GVariant **out_ip4, GVariant **out_ip6, GError **error)
{
	g_auto (GStrv) addrs = awg_split_list (nm_setting_vpn_get_data_item (s_vpn, AWG_KEY_ADDRESS));
	g_auto (GStrv) dns = awg_split_list (nm_setting_vpn_get_data_item (s_vpn, AWG_KEY_DNS));
	g_auto (GStrv) allowed = awg_split_list (nm_setting_vpn_get_data_item (s_vpn, AWG_KEY_ALLOWED_IPS));
	GVariantBuilder cfg, ip4, ip6, dns4, dns6, routes4, routes6, domains;
	gboolean has4 = FALSE, has6 = FALSE, send6, def4 = FALSE, def6 = FALSE;
	gboolean any_dns4 = FALSE, any_dns6 = FALSE, any_r4 = FALSE, any_r6 = FALSE, any_dom = FALSE;
	guint8 zero[16] = { 0 };
	AwgCidr c;
	guint i;

	*out_config = *out_ip4 = *out_ip6 = NULL;

	g_variant_builder_init (&ip4, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_init (&ip6, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_init (&dns4, G_VARIANT_TYPE ("au"));
	g_variant_builder_init (&dns6, G_VARIANT_TYPE ("aay"));
	g_variant_builder_init (&routes4, G_VARIANT_TYPE ("aau"));
	g_variant_builder_init (&routes6, G_VARIANT_TYPE ("a(ayuayu)"));
	g_variant_builder_init (&domains, G_VARIANT_TYPE ("as"));

	for (i = 0; addrs[i]; i++) {
		if (!awg_parse_cidr (addrs[i], &c)) {
			g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
			             "invalid Address '%s'", addrs[i]);
			goto fail;
		}
		if (c.family == AF_INET && !has4) {
			has4 = TRUE;
			g_variant_builder_add (&ip4, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_ADDRESS,
			                       g_variant_new_uint32 (addr4 (c.addr)));
			g_variant_builder_add (&ip4, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_PREFIX,
			                       g_variant_new_uint32 (c.prefix));
		} else if (c.family == AF_INET6 && !has6) {
			has6 = TRUE;
			g_variant_builder_add (&ip6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_ADDRESS, addr6 (c.addr));
			g_variant_builder_add (&ip6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_PREFIX,
			                       g_variant_new_uint32 (c.prefix));
		} else
			g_message ("ignoring extra address %s (only one per family is supported)", addrs[i]);
	}
	if (!has4 && !has6) {
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
		             "Address is required");
		goto fail;
	}

	for (i = 0; dns[i]; i++) {
		guint8 a[16];
		if (inet_pton (AF_INET, dns[i], a) == 1) {
			g_variant_builder_add (&dns4, "u", addr4 (a));
			any_dns4 = TRUE;
		} else if (inet_pton (AF_INET6, dns[i], a) == 1) {
			g_variant_builder_add (&dns6, "@ay", addr6 (a));
			any_dns6 = TRUE;
		} else {
			/* like wg-quick: non-IP DNS entries are search domains */
			g_variant_builder_add (&domains, "s", dns[i]);
			any_dom = TRUE;
		}
	}

	for (i = 0; allowed[i]; i++) {
		if (!awg_parse_cidr (allowed[i], &c)) {
			g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
			             "invalid AllowedIPs entry '%s'", allowed[i]);
			goto fail;
		}
		mask_cidr (&c);
		if (c.family == AF_INET) {
			if (c.prefix == 0) {
				def4 = TRUE;
				continue;
			}
			{
				GVariantBuilder r;
				g_variant_builder_init (&r, G_VARIANT_TYPE ("au"));
				g_variant_builder_add (&r, "u", addr4 (c.addr));
				g_variant_builder_add (&r, "u", (guint32) c.prefix);
				g_variant_builder_add (&r, "u", (guint32) 0);
				g_variant_builder_add (&r, "u", (guint32) 0);
				g_variant_builder_add (&routes4, "au", &r);
			}
			any_r4 = TRUE;
		} else {
			if (c.prefix == 0) {
				def6 = TRUE;
				continue;
			}
			g_variant_builder_add (&routes6, "(@ayu@ayu)", addr6 (c.addr), (guint32) c.prefix,
			                       addr6 (zero), (guint32) 0);
			any_r6 = TRUE;
		}
	}
	/* Fail closed: without an IPv4 address NM gets no IPv4 config, and IPv4
	 * traffic for AllowedIPs would bypass the tunnel. */
	if (!has4 && (def4 || any_r4)) {
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_BAD_ARGUMENTS,
		             "AllowedIPs has IPv4 networks, but Address has no IPv4 address");
		goto fail;
	}
	/* IPv6 routes but no IPv6 address (a typical Amnezia export: AllowedIPs
	 * "0.0.0.0/0, ::/0", Address IPv4 only). Like awg-quick and the Amnezia
	 * app, still route that IPv6 into the tunnel: the server has no IPv6 for
	 * us and drops it, so it is blocked instead of leaking past the VPN.
	 * NetworkManager rejects an IPv6 config without an address, so the
	 * interface gets a placeholder ULA /128. A ULA source also makes the
	 * system prefer IPv4 (RFC 6724), so connections do not stall on IPv6. */
	send6 = has6 || def6 || any_r6;
	if (send6 && !has6) {
		guint8 ula[16] = { 0xfd, 0x61, 0x77, 0x67, [15] = 1 }; /* fd61:7767::1 */
		g_variant_builder_add (&ip6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_ADDRESS, addr6 (ula));
		g_variant_builder_add (&ip6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_PREFIX, g_variant_new_uint32 (128));
		g_message ("no IPv6 Address configured; IPv6 traffic for AllowedIPs is routed into "
		           "the tunnel and blocked there instead of bypassing the VPN");
	}

	/* generic config */
	g_variant_builder_init (&cfg, G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add (&cfg, "{sv}", NM_VPN_PLUGIN_CONFIG_TUNDEV, g_variant_new_string (ifname));
	g_variant_builder_add (&cfg, "{sv}", NM_VPN_PLUGIN_CONFIG_MTU, g_variant_new_uint32 (mtu));
	g_variant_builder_add (&cfg, "{sv}", NM_VPN_PLUGIN_CONFIG_HAS_IP4, g_variant_new_boolean (has4));
	g_variant_builder_add (&cfg, "{sv}", NM_VPN_PLUGIN_CONFIG_HAS_IP6, g_variant_new_boolean (send6));
	if (gw->ss_family == AF_INET)
		g_variant_builder_add (&cfg, "{sv}", NM_VPN_PLUGIN_CONFIG_EXT_GATEWAY,
		                       g_variant_new_uint32 (((struct sockaddr_in *) gw)->sin_addr.s_addr));
	else
		g_variant_builder_add (&cfg, "{sv}", NM_VPN_PLUGIN_CONFIG_EXT_GATEWAY,
		                       addr6 ((const guint8 *) &((struct sockaddr_in6 *) gw)->sin6_addr));
	*out_config = g_variant_ref_sink (g_variant_builder_end (&cfg));

	if (has4) {
		if (any_dns4)
			g_variant_builder_add (&ip4, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_DNS, g_variant_builder_end (&dns4));
		if (any_dom)
			g_variant_builder_add (&ip4, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_DOMAINS, g_variant_builder_end (&domains));
		if (any_r4)
			g_variant_builder_add (&ip4, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_ROUTES, g_variant_builder_end (&routes4));
		g_variant_builder_add (&ip4, "{sv}", NM_VPN_PLUGIN_IP4_CONFIG_NEVER_DEFAULT, g_variant_new_boolean (!def4));
		*out_ip4 = g_variant_ref_sink (g_variant_builder_end (&ip4));
	} else
		g_variant_builder_clear (&ip4);

	if (send6) {
		if (any_dns6)
			g_variant_builder_add (&ip6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_DNS, g_variant_builder_end (&dns6));
		if (any_dom && !has4)
			g_variant_builder_add (&ip6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_DOMAINS, g_variant_builder_end (&domains));
		if (any_r6)
			g_variant_builder_add (&ip6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_ROUTES, g_variant_builder_end (&routes6));
		g_variant_builder_add (&ip6, "{sv}", NM_VPN_PLUGIN_IP6_CONFIG_NEVER_DEFAULT, g_variant_new_boolean (!def6));
		*out_ip6 = g_variant_ref_sink (g_variant_builder_end (&ip6));
	} else
		g_variant_builder_clear (&ip6);

	/* builders that were not consumed */
	if (!(has4 && any_dns4))
		g_variant_builder_clear (&dns4);
	if (!(send6 && any_dns6))
		g_variant_builder_clear (&dns6);
	if (!(has4 && any_r4))
		g_variant_builder_clear (&routes4);
	if (!(send6 && any_r6))
		g_variant_builder_clear (&routes6);
	if (!(any_dom && (has4 || send6)))
		g_variant_builder_clear (&domains);
	return TRUE;

fail:
	g_variant_builder_clear (&ip4);
	g_variant_builder_clear (&ip6);
	g_variant_builder_clear (&dns4);
	g_variant_builder_clear (&dns6);
	g_variant_builder_clear (&routes4);
	g_variant_builder_clear (&routes6);
	g_variant_builder_clear (&domains);
	return FALSE;
}
