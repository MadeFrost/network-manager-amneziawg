/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <glib.h>

/* Interfaces the service creates are named AWG_IFNAME_PREFIX<N>. */
#define AWG_IFNAME_PREFIX "awg-nm"

/* Runs argv; stdin_data (may contain secrets) is fed via a pipe, never argv,
 * and text awg quotes in its errors is hidden (see awg_redact_quoted). */
gboolean awg_run (const char *const *argv, const char *stdin_data, GError **error);

/* Replaces everything between ` and ' with "(hidden)": awg quotes the
 * offending config text, which can be a key. */
char *awg_redact_quoted (const char *text);

/* Removes awg-nmN interfaces left behind by a service that did not exit
 * cleanly. Call before awg_iface_pick_name(). */
void awg_iface_remove_stale (void);

/* The first free awg-nmN, or NULL. */
char *awg_iface_pick_name (void);

/* Creates the interface and labels it with this process as its owner. */
gboolean awg_iface_create (const char *ifname, GError **error);

/* `awg setconf` with the given text, then MTU and up. */
gboolean awg_iface_configure (const char *ifname, const char *setconf, guint32 mtu, GError **error);

void awg_iface_delete (const char *ifname);
