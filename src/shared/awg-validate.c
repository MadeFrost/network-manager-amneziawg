/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "awg-validate.h"

#include "awg-common.h"

#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>

gboolean
awg_parse_cidr (const char *str, AwgCidr *out)
{
	g_autofree char *s = g_strdup (str);
	char *slash = strchr (s, '/');
	int max;

	memset (out, 0, sizeof *out);
	if (slash)
		*slash = '\0';
	if (inet_pton (AF_INET, s, out->addr) == 1)
		out->family = AF_INET;
	else if (inet_pton (AF_INET6, s, out->addr) == 1)
		out->family = AF_INET6;
	else
		return FALSE;

	max = out->family == AF_INET ? 32 : 128;
	if (slash) {
		char *end;
		gint64 p = g_ascii_strtoll (slash + 1, &end, 10);
		if (*end || end == slash + 1 || p < 0 || p > max)
			return FALSE;
		out->prefix = (int) p;
	} else
		out->prefix = max;
	return TRUE;
}

gboolean
awg_parse_endpoint (const char *endpoint, char **out_host, char **out_port)
{
	g_autofree char *host = NULL;
	const char *port, *p;
	char *end;
	gint64 n;

	if (!endpoint)
		return FALSE;
	if (endpoint[0] == '[') {
		const char *close = strchr (endpoint, ']');
		if (!close || close[1] != ':')
			return FALSE;
		host = g_strndup (endpoint + 1, close - endpoint - 1);
		port = close + 2;
	} else {
		p = strrchr (endpoint, ':');
		if (!p)
			return FALSE;
		host = g_strndup (endpoint, p - endpoint);
		port = p + 1;
		if (strchr (host, ':')) /* bare IPv6 must be bracketed */
			return FALSE;
	}
	if (!*host || strpbrk (host, " \t"))
		return FALSE;
	n = g_ascii_strtoll (port, &end, 10);
	if (*end || end == port || n < 1 || n > 65535)
		return FALSE;

	if (out_host)
		*out_host = g_steal_pointer (&host);
	if (out_port)
		*out_port = g_strdup (port);
	return TRUE;
}

/* Unsigned decimal as kstrtoint() reads it: optional '+', fits in an int.
 * (The kernel also takes negative numbers, which break the packet: refuse.) */
static gboolean
is_count (const char *s)
{
	guint64 n = 0;

	if (*s == '+')
		s++;
	if (!*s)
		return FALSE;
	for (; *s; s++) {
		if (!g_ascii_isdigit (*s))
			return FALSE;
		n = n * 10 + (*s - '0');
		if (n > G_MAXINT)
			return FALSE;
	}
	return TRUE;
}

gboolean
awg_check_ipacket (const char *value, char **reason)
{
	const char *p = value ? value : "";
	char *msg = NULL;

	/* The kernel skips text outside <…>, and an unclosed last tag runs to the
	 * end of the string; the tag name ends at the first space. */
	while (!msg && (p = strchr (p, '<'))) {
		const char *end = strchr (++p, '>');
		g_autofree char *tag = end ? g_strndup (p, end - p) : g_strdup (p);
		char *val = strchr (tag, ' ');

		if (val)
			*val++ = '\0';
		if (g_str_equal (tag, "b")) {
			gsize n = val && g_str_has_prefix (val, "0x") ? strlen (val + 2) : 0;

			if (!n || strspn (val + 2, "0123456789abcdefABCDEF") != n)
				msg = g_strdup (_("<b> needs bytes in hex after “0x”, for example <b 0x1a2b>"));
			else if (n % 2)
				msg = g_strdup (_("<b> needs an even number of hex digits, two per byte"));
		} else if (g_str_equal (tag, "c") || g_str_equal (tag, "t")) {
			if (val)
				msg = g_strdup_printf (_("<%s> takes no value, write just <%s>"), tag, tag);
		} else if (g_str_equal (tag, "r") || g_str_equal (tag, "rc") || g_str_equal (tag, "rd")) {
			if (!val || !is_count (val))
				msg = g_strdup_printf (_("<%s> needs a length in bytes, for example <%s 16>"), tag, tag);
		} else
			msg = g_strdup_printf (_("unknown tag <%s>; supported: <b 0x…>, <r N>, <rc N>, <rd N>, <c>, <t>"),
			                       tag);
		if (!end)
			break;
		p = end + 1;
	}
	if (reason)
		*reason = msg;
	else
		g_free (msg);
	return msg == NULL;
}

/* Decimal digits only (no sign, no spaces), within min..max. */
static gboolean
is_number_in (const char *s, guint min, guint max)
{
	guint64 n = 0;

	if (!*s)
		return FALSE;
	for (; *s; s++) {
		if (!g_ascii_isdigit (*s))
			return FALSE;
		n = n * 10 + (*s - '0');
		if (n > max)
			return FALSE;
	}
	return n >= min;
}

gboolean
awg_key_check (const AwgKey *k, const char *value, char **reason)
{
	g_auto (GStrv) items = NULL;
	char *msg = NULL;
	guint i;

	if (reason)
		*reason = NULL;
	if (!value || !*value)
		return !(k->flags & AWG_KF_REQUIRED);

	switch (k->check) {
	case AWG_CHECK_KEY:
		if (!nm_utils_base64secret_decode (value, 32, NULL))
			msg = g_strdup (_("must be a 32-byte key in base64, as made by “awg genkey”"));
		break;
	case AWG_CHECK_CIDRS:
		items = awg_split_list (value);
		if (!items[0])
			return !(k->flags & AWG_KF_REQUIRED);
		for (i = 0; items[i] && !msg; i++) {
			AwgCidr c;
			if (!awg_parse_cidr (items[i], &c))
				msg = g_strdup_printf (_("“%s” is not an IP address with an optional prefix, such as 10.8.1.2/32"),
				                       items[i]);
		}
		break;
	case AWG_CHECK_ENDPOINT:
		if (!awg_parse_endpoint (value, NULL, NULL))
			msg = g_strdup (_("must be host:port, such as vpn.example.com:51820 or [2001:db8::1]:51820"));
		break;
	case AWG_CHECK_IPACKET:
		return awg_check_ipacket (value, reason);
	case AWG_CHECK_NUMBER:
		if (!((k->flags & AWG_KF_OFF) && g_ascii_strcasecmp (value, "off") == 0)
		    && !is_number_in (value, k->min, k->max))
			msg = g_strdup_printf (_("must be a whole number from %u to %u"), k->min, k->max);
		break;
	case AWG_CHECK_NONE:
		break;
	}
	if (reason)
		*reason = msg;
	else
		g_free (msg);
	return msg == NULL;
}

static gboolean
has_family (const char *list, int family)
{
	g_auto (GStrv) items = awg_split_list (list);
	AwgCidr c;
	guint i;

	for (i = 0; items[i]; i++) {
		if (awg_parse_cidr (items[i], &c) && c.family == family)
			return TRUE;
	}
	return FALSE;
}

gboolean
awg_check_families (const char *address, const char *allowed_ips, char **reason)
{
	if (!has_family (allowed_ips, AF_INET) || has_family (address, AF_INET))
		return TRUE;
	if (reason)
		*reason = g_strdup (_("an IPv4 address is needed: AllowedIPs has IPv4 networks"));
	return FALSE;
}

gboolean
awg_setting_check (NMSettingVpn *s_vpn, GError **error)
{
	const AwgKey *k;

	for (k = awg_keys; k->name; k++) {
		const char *v = (k->flags & AWG_KF_DROP) ? NULL : awg_setting_get (s_vpn, k->name);
		g_autofree char *reason = NULL;

		if (awg_key_check (k, v, &reason))
			continue;
		if (reason)
			g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("invalid %s: %s"), k->conf_name, reason);
		else
			g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_MISSING_PROPERTY,
			             _("%s is missing"), k->conf_name);
		return FALSE;
	}
	{
		g_autofree char *reason = NULL;

		if (!awg_check_families (awg_setting_get (s_vpn, AWG_KEY_ADDRESS),
		                         awg_setting_get (s_vpn, AWG_KEY_ALLOWED_IPS), &reason)) {
			g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("invalid %s: %s"), "Address", reason);
			return FALSE;
		}
	}
	return TRUE;
}
