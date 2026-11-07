/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Value checks shared by the editor (before saving) and the service (before
 * connecting), so both refuse the same values with the same explanation.
 */
#pragma once

#include <NetworkManager.h>

#include "awg-keys.h"

typedef struct {
	int family; /* AF_INET or AF_INET6 */
	guint8 addr[16];
	int prefix;
} AwgCidr;

/* "10.0.0.1/24", "fd00::1" (prefix defaults to the full length). */
gboolean awg_parse_cidr (const char *str, AwgCidr *out);

/* "host:port" or "[v6]:port"; port must be 1..65535. */
gboolean awg_parse_endpoint (const char *endpoint, char **out_host, char **out_port);

/* Checks an I1..I5 value ("<b 0x…><r 16><t>…") exactly as the amneziawg kernel
 * module parses it (jp_parse_tags() in junk.c), so a bad value is reported
 * here instead of as a bare EINVAL from `awg setconf`. On failure returns
 * FALSE and, if reason is non-NULL, a translated explanation (g_free it). */
gboolean awg_check_ipacket (const char *value, char **reason);

/* Checks a value of key k as its registry entry says. NULL or "" means "not
 * set", which only a required key refuses; then reason stays NULL. Otherwise
 * a failure sets reason (if non-NULL) to a translated explanation. It never
 * contains the value of a secret. */
gboolean awg_key_check (const AwgKey *k, const char *value, char **reason);

/* Checks every registry key of s_vpn. On failure sets error to
 * "<Key> is missing" or "invalid <Key>: <reason>". */
gboolean awg_setting_check (NMSettingVpn *s_vpn, GError **error);

/* FALSE (with a reason) if AllowedIPs has IPv4 networks but Address has no IPv4
 * address: NM would get no IPv4 routes, and that traffic would bypass the tunnel. */
gboolean awg_check_families (const char *address, const char *allowed_ips, char **reason);
