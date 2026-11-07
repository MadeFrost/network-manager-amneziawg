/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "config.h"

#include <NetworkManager.h>

#include <glib/gi18n-lib.h>

#include "awg-conf.h"
#include "awg-keys.h"
#include "awg-validate.h"

#define AWG_EXPORT __attribute__((visibility ("default")))

#define AWG_DBUS_SERVICE "org.freedesktop.NetworkManager.amneziawg"

/* vpn.data / vpn.secrets keys.
 * [Interface] keys are stored as-is, [Peer] keys get the "Peer-" prefix.
 * Not a dot: Ubuntu stores NM connections through netplan, which splits
 * "vpn.Peer.PublicKey" into a bogus [vpn.Peer] group and drops the key. */
#define AWG_PEER_PREFIX "Peer-"

#define AWG_KEY_PRIVATE_KEY   "PrivateKey"
#define AWG_KEY_ADDRESS       "Address"
#define AWG_KEY_DNS           "DNS"
#define AWG_KEY_MTU           "MTU"
#define AWG_KEY_PUBLIC_KEY    "Peer-PublicKey"
#define AWG_KEY_PRESHARED_KEY "Peer-PresharedKey"
#define AWG_KEY_HEADER_PROTECTION_KEY "HeaderProtectionKey"
#define AWG_KEY_ENDPOINT      "Peer-Endpoint"
#define AWG_KEY_ALLOWED_IPS   "Peer-AllowedIPs"

/* Splits "a, b,c" into a NULL-terminated, stripped, non-empty list. */
char **awg_split_list (const char *value);

/* Binds the translation domain to LOCALEDIR (once). Call from every
 * library entry point; the language itself follows the system locale. */
void awg_i18n_init (void);
