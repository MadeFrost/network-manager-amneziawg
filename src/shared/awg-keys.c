/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "awg-keys.h"

#include "awg-common.h"

#define IFACE(name, flags, check, ...) { name, name, AWG_SECTION_INTERFACE, flags, check, __VA_ARGS__ }
/* [Peer] keys are stored with the "Peer-" prefix (see AWG_PEER_PREFIX). */
#define PEER(name, flags, check, ...) { AWG_PEER_PREFIX name, name, AWG_SECTION_PEER, flags, check, __VA_ARGS__ }
#define U16 0, 65535
#define OBFS_NUM(name) IFACE (name, AWG_KF_OBFS, AWG_CHECK_NUMBER, U16)

const AwgKey awg_keys[] = {
	/* [Interface] */
	IFACE ("PrivateKey", AWG_KF_SECRET | AWG_KF_REQUIRED, AWG_CHECK_KEY),
	IFACE ("ListenPort", 0, AWG_CHECK_NUMBER, U16),
	IFACE ("FwMark", 0, AWG_CHECK_NONE),
	IFACE ("Address", AWG_KF_REQUIRED | AWG_KF_QUICK_ONLY | AWG_KF_LIST, AWG_CHECK_CIDRS),
	IFACE ("DNS", AWG_KF_QUICK_ONLY | AWG_KF_LIST, AWG_CHECK_NONE),
	IFACE ("MTU", AWG_KF_QUICK_ONLY, AWG_CHECK_NUMBER, 576, 65535),
	OBFS_NUM ("Jc"),
	OBFS_NUM ("Jmin"),
	OBFS_NUM ("Jmax"),
	OBFS_NUM ("S1"),
	OBFS_NUM ("S2"),
	OBFS_NUM ("S3"),
	OBFS_NUM ("S4"),
	/* a number or a range "a-b" */
	IFACE ("H1", AWG_KF_OBFS, AWG_CHECK_NONE),
	IFACE ("H2", AWG_KF_OBFS, AWG_CHECK_NONE),
	IFACE ("H3", AWG_KF_OBFS, AWG_CHECK_NONE),
	IFACE ("H4", AWG_KF_OBFS, AWG_CHECK_NONE),
	IFACE ("I1", AWG_KF_OBFS, AWG_CHECK_IPACKET),
	IFACE ("I2", AWG_KF_OBFS, AWG_CHECK_IPACKET),
	IFACE ("I3", AWG_KF_OBFS, AWG_CHECK_IPACKET),
	IFACE ("I4", AWG_KF_OBFS, AWG_CHECK_IPACKET),
	IFACE ("I5", AWG_KF_OBFS, AWG_CHECK_IPACKET),
	IFACE ("HeaderProtectionKey", AWG_KF_SECRET, AWG_CHECK_KEY),
	/* Scripts must never reach a root-run service. */
	IFACE ("Table", AWG_KF_DROP, AWG_CHECK_NONE),
	IFACE ("PreUp", AWG_KF_DROP, AWG_CHECK_NONE),
	IFACE ("PostUp", AWG_KF_DROP, AWG_CHECK_NONE),
	IFACE ("PreDown", AWG_KF_DROP, AWG_CHECK_NONE),
	IFACE ("PostDown", AWG_KF_DROP, AWG_CHECK_NONE),
	IFACE ("SaveConfig", AWG_KF_DROP, AWG_CHECK_NONE),

	/* [Peer] */
	PEER ("PublicKey", AWG_KF_REQUIRED, AWG_CHECK_KEY),
	PEER ("PresharedKey", AWG_KF_SECRET, AWG_CHECK_KEY),
	PEER ("Endpoint", AWG_KF_REQUIRED, AWG_CHECK_ENDPOINT),
	PEER ("AllowedIPs", AWG_KF_LIST, AWG_CHECK_CIDRS),
	PEER ("PersistentKeepalive", AWG_KF_OFF, AWG_CHECK_NUMBER, U16),
	{ NULL },
};

const AwgKey *
awg_key_find (const char *name)
{
	const AwgKey *k;

	for (k = awg_keys; name && k->name; k++) {
		if (g_str_equal (k->name, name))
			return k;
	}
	return NULL;
}

const AwgKey *
awg_key_find_conf (AwgSection section, const char *conf_name)
{
	const AwgKey *k;

	for (k = awg_keys; k->name; k++) {
		if (k->section == section && g_ascii_strcasecmp (k->conf_name, conf_name) == 0)
			return k;
	}
	return NULL;
}

/* Values that let someone decrypt or impersonate the tunnel. They go to
 * vpn.secrets (root only), not to vpn.data, which any local user can read. */
gboolean
awg_key_is_secret (const char *name)
{
	const AwgKey *k = awg_key_find (name);

	return k && (k->flags & AWG_KF_SECRET);
}
