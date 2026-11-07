/* What the service reports to NetworkManager (ipconfig.c) and the hiding of
 * values in awg errors (iface.c). No root, no kernel module. */
#include "awg-common.h"
#include "check.h"
#include "iface.h"
#include "ipconfig.h"

#include <arpa/inet.h>
#include <string.h>

typedef struct {
	GVariant *config, *ip4, *ip6;
	GError *error;
	gboolean ok;
} Result;

static void
result_clear (Result *r)
{
	g_clear_pointer (&r->config, g_variant_unref);
	g_clear_pointer (&r->ip4, g_variant_unref);
	g_clear_pointer (&r->ip6, g_variant_unref);
	g_clear_error (&r->error);
}

static Result
build (const char *address, const char *dns, const char *allowed)
{
	g_autoptr (NMSettingVpn) s = NM_SETTING_VPN (nm_setting_vpn_new ());
	struct sockaddr_storage gw = { 0 };
	struct sockaddr_in *sin = (struct sockaddr_in *) &gw;
	Result r = { 0 };

	if (address)
		nm_setting_vpn_add_data_item (s, AWG_KEY_ADDRESS, address);
	if (dns)
		nm_setting_vpn_add_data_item (s, AWG_KEY_DNS, dns);
	if (allowed)
		nm_setting_vpn_add_data_item (s, AWG_KEY_ALLOWED_IPS, allowed);
	sin->sin_family = AF_INET;
	inet_pton (AF_INET, "203.0.113.7", &sin->sin_addr);
	r.ok = awg_ip_config_build (s, "awg-nm0", &gw, 1420, &r.config, &r.ip4, &r.ip6, &r.error);
	return r;
}

static gboolean
get_bool (GVariant *dict, const char *key)
{
	gboolean v = FALSE;

	return dict && g_variant_lookup (dict, key, "b", &v) && v;
}

static gboolean
has (GVariant *dict, const char *key)
{
	g_autoptr (GVariant) v = dict ? g_variant_lookup_value (dict, key, NULL) : NULL;

	return v != NULL;
}

static char *
ip6_address (GVariant *ip6)
{
	g_autoptr (GVariant) v = ip6 ? g_variant_lookup_value (ip6, NM_VPN_PLUGIN_IP6_CONFIG_ADDRESS, NULL) : NULL;
	char buf[INET6_ADDRSTRLEN];
	gsize n = 0;
	const guint8 *a = v ? g_variant_get_fixed_array (v, &n, 1) : NULL;
	guint32 prefix = 0;

	if (n != 16)
		return NULL;
	g_variant_lookup (ip6, NM_VPN_PLUGIN_IP6_CONFIG_PREFIX, "u", &prefix);
	inet_ntop (AF_INET6, a, buf, sizeof buf);
	return g_strdup_printf ("%s/%u", buf, prefix);
}

static void
test_split_tunnel (void)
{
	Result r = build ("10.8.1.2/32", "1.1.1.1, corp.example", "10.8.1.0/24, 172.16.0.0/12");
	g_autoptr (GVariant) routes = NULL;
	guint32 prefix = 0;

	printf ("IPv4 split tunnel:\n");
	CHECK ("builds", r.ok);
	CHECK ("tun device", r.config && g_variant_lookup (r.config, NM_VPN_PLUGIN_CONFIG_TUNDEV, "&s", NULL));
	CHECK ("has IPv4, no IPv6", get_bool (r.config, NM_VPN_PLUGIN_CONFIG_HAS_IP4)
	                                && !get_bool (r.config, NM_VPN_PLUGIN_CONFIG_HAS_IP6) && !r.ip6);
	CHECK ("external gateway", has (r.config, NM_VPN_PLUGIN_CONFIG_EXT_GATEWAY));
	CHECK ("prefix", r.ip4 && g_variant_lookup (r.ip4, NM_VPN_PLUGIN_IP4_CONFIG_PREFIX, "u", &prefix)
	                     && prefix == 32);
	routes = r.ip4 ? g_variant_lookup_value (r.ip4, NM_VPN_PLUGIN_IP4_CONFIG_ROUTES, NULL) : NULL;
	CHECK ("two routes", routes && g_variant_n_children (routes) == 2);
	CHECK ("never default", get_bool (r.ip4, NM_VPN_PLUGIN_IP4_CONFIG_NEVER_DEFAULT));
	CHECK ("DNS server and search domain", has (r.ip4, NM_VPN_PLUGIN_IP4_CONFIG_DNS)
	                                           && has (r.ip4, NM_VPN_PLUGIN_IP4_CONFIG_DOMAINS));
	result_clear (&r);
}

static void
test_ipv6_blocked (void)
{
	Result r = build ("10.8.1.2/32", NULL, "0.0.0.0/0, ::/0");
	g_autofree char *a6 = ip6_address (r.ip6);

	printf ("::/0 without an IPv6 address (typical Amnezia export):\n");
	CHECK ("builds", r.ok);
	CHECK ("IPv4 is the default route", r.ip4 && !get_bool (r.ip4, NM_VPN_PLUGIN_IP4_CONFIG_NEVER_DEFAULT));
	CHECK ("IPv6 is sent to NM", get_bool (r.config, NM_VPN_PLUGIN_CONFIG_HAS_IP6) && r.ip6);
	CHECK ("placeholder address fd61:7767::1/128", !g_strcmp0 (a6, "fd61:7767::1/128"));
	CHECK ("IPv6 is the default route too (blocked in the tunnel)",
	       r.ip6 && !get_bool (r.ip6, NM_VPN_PLUGIN_IP6_CONFIG_NEVER_DEFAULT));
	result_clear (&r);
}

static void
test_ipv6_address (void)
{
	Result r = build ("10.8.1.2/32, fd00::2/128", NULL, "0.0.0.0/0, ::/0");
	g_autofree char *a6 = ip6_address (r.ip6);

	printf ("real IPv6 address:\n");
	CHECK ("own address, no placeholder", !g_strcmp0 (a6, "fd00::2/128"));
	result_clear (&r);
}

static void
test_errors (void)
{
	Result r = build (NULL, NULL, "10.0.0.0/8");

	printf ("errors:\n");
	CHECK ("no Address", !r.ok && r.error && strstr (r.error->message, "Address"));
	CHECK ("no dictionaries on failure", !r.config && !r.ip4 && !r.ip6);
	result_clear (&r);
	r = build ("10.8.1.2/32", NULL, "10.0.0.0/8, nonsense");
	CHECK ("bad AllowedIPs entry named", !r.ok && r.error && strstr (r.error->message, "nonsense"));
	result_clear (&r);
	r = build ("fd00::2/128", NULL, "0.0.0.0/0, ::/0");
	CHECK ("IPv4 default route without an IPv4 address refused (no leak)",
	       !r.ok && r.error && strstr (r.error->message, "no IPv4 address") && !r.config);
	result_clear (&r);
	r = build ("fd00::2/128", NULL, "10.0.0.0/8, ::/0");
	CHECK ("IPv4 routes without an IPv4 address refused", !r.ok && r.error);
	result_clear (&r);
	r = build ("fd00::2/128", NULL, "::/0");
	CHECK ("IPv6 only: builds", r.ok && !get_bool (r.config, NM_VPN_PLUGIN_CONFIG_HAS_IP4) && r.ip6);
	result_clear (&r);
}

static void
test_redact (void)
{
	g_autofree char *a = awg_redact_quoted ("Key is not the correct length or format: `SECRET'\nmore");
	g_autofree char *b = awg_redact_quoted ("`one' and `two'");
	g_autofree char *c = awg_redact_quoted ("unbalanced `quote");

	printf ("hiding quoted values in awg errors:\n");
	CHECK ("quoted value hidden", !g_strcmp0 (a, "Key is not the correct length or format: `(hidden)'\nmore"));
	CHECK ("every quote", !g_strcmp0 (b, "`(hidden)' and `(hidden)'"));
	CHECK ("unbalanced quote left as is", !g_strcmp0 (c, "unbalanced `quote"));
}

int
main (void)
{
	test_split_tunnel ();
	test_ipv6_blocked ();
	test_ipv6_address ();
	test_errors ();
	test_redact ();
	return test_finish ();
}
