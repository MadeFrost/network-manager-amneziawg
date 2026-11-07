/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The registry of known config keys: one place that says where a key lives,
 * how it is stored and checked. Import, export, `awg setconf`, the service
 * and the editor all read it. A new AmneziaWG key needs one line here (and a
 * field in the editor, if it should be editable).
 */
#pragma once

#include <glib.h>

typedef enum {
	AWG_SECTION_INTERFACE,
	AWG_SECTION_PEER,
} AwgSection;

typedef enum {
	AWG_KF_SECRET = 1 << 0,     /* vpn.secrets (root only), not vpn.data */
	AWG_KF_REQUIRED = 1 << 1,   /* a connection cannot work without it */
	AWG_KF_QUICK_ONLY = 1 << 2, /* awg-quick only: not passed to `awg setconf` */
	AWG_KF_LIST = 1 << 3,       /* repeated lines are joined with ", " */
	AWG_KF_OBFS = 1 << 4,       /* AmneziaWG obfuscation parameter */
	AWG_KF_DROP = 1 << 5,       /* wg-quick scripts and tables: dropped on import */
	AWG_KF_OFF = 1 << 6,        /* AWG_CHECK_NUMBER that also takes "off" */
} AwgKeyFlags;

typedef enum {
	AWG_CHECK_NONE,
	AWG_CHECK_KEY,      /* 32 bytes in base64 */
	AWG_CHECK_CIDRS,    /* list of addresses with optional prefix */
	AWG_CHECK_ENDPOINT, /* host:port or [v6]:port */
	AWG_CHECK_IPACKET,  /* I1..I5 tag syntax */
	AWG_CHECK_NUMBER,   /* decimal in min..max */
} AwgCheck;

typedef struct {
	const char *name;      /* in vpn.data / vpn.secrets, e.g. "Peer-Endpoint" */
	const char *conf_name; /* in the .conf, e.g. "Endpoint" */
	AwgSection section;
	AwgKeyFlags flags;
	AwgCheck check;
	guint min, max; /* AWG_CHECK_NUMBER */
} AwgKey;

/* All known keys in .conf order, terminated by an entry with name NULL. */
extern const AwgKey awg_keys[];

/* By stored name (exact): "PrivateKey", "Peer-Endpoint". */
const AwgKey *awg_key_find (const char *name);

/* By .conf name within a section, ignoring case as wg-quick does. */
const AwgKey *awg_key_find_conf (AwgSection section, const char *conf_name);

/* TRUE for keys stored in vpn.secrets. */
gboolean awg_key_is_secret (const char *name);
