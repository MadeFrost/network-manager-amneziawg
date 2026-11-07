/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <NetworkManager.h>

#include <sys/socket.h>

/* Builds the dictionaries for nm_vpn_service_plugin_set_config(),
 * set_ip4_config() and set_ip6_config() from Address, DNS and AllowedIPs.
 * gw is the resolved endpoint. out_ip4 / out_ip6 are NULL when that family
 * is not used. The results are full references (g_variant_unref them). */
gboolean awg_ip_config_build (NMSettingVpn *s_vpn, const char *ifname, const struct sockaddr_storage *gw,
                              guint32 mtu, GVariant **out_config, GVariant **out_ip4, GVariant **out_ip6,
                              GError **error);
