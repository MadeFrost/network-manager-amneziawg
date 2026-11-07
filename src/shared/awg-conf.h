/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <NetworkManager.h>

/* A key's value: from vpn.secrets for secret keys (falling back to vpn.data,
 * where 0.3.3 and older kept HeaderProtectionKey), else from vpn.data. */
const char *awg_setting_get (NMSettingVpn *s_vpn, const char *key);

/* Parses a wg-quick / awg-quick style config into s_vpn (data + secrets). */
gboolean awg_conf_parse (const char *text, NMSettingVpn *s_vpn, GError **error);

/* TRUE if any AmneziaWG obfuscation parameter (Jc, S1, H1, I1, ...) is set. */
gboolean awg_conf_is_amnezia (NMSettingVpn *s_vpn);

/* awg-quick format (with Address/DNS/MTU), for export. */
char *awg_conf_to_quick (NMSettingVpn *s_vpn);

/* `awg setconf` format. endpoint_override replaces Peer-Endpoint if non-NULL. */
char *awg_conf_to_setconf (NMSettingVpn *s_vpn, const char *endpoint_override);
