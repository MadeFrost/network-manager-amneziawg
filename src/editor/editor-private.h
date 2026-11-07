/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Internals of the editor shared by its files:
 *   editor-fields.c        the field table and its input widgets
 *   editor-peers.c         the "Peers" list and the peer popover
 *   nm-amneziawg-editor.c  the NMVpnEditor object: load, check and save
 */
#pragma once

#include <NetworkManager.h>

#include "awg-common.h"
#include "gtk-compat.h"

typedef enum { SEC_IFACE, SEC_PEER, SEC_OBFS } Section;
typedef enum { W_ENTRY, W_SECRET, W_SPIN } Kind;

/* How a key is shown. What it is (required, secret, how it is checked, the
 * range of a number) comes from the key registry (awg-keys.c). */
typedef struct {
	const char *key;
	const char *label;     /* translatable, with mnemonic */
	const char *tooltip;   /* translatable */
	const char *placeholder;
	Section section;
	Kind kind;
	const char *zero_text; /* W_SPIN: shown for 0, translatable */
	const char *unit;      /* W_SPIN: translatable */
} Field;

extern const Field fields[];
#define N_FIELDS 26 /* checked against the table in editor-fields.c */

typedef struct {
	GObject parent;
	GtkWidget *widget;
	GtkWidget *inputs[N_FIELDS];
	GtkSizeGroup *labels;
	NMSettingVpn *orig; /* keeps keys the UI does not know about */

	/* The peer, as applied from the popover (the popover edits a copy). */
	gboolean has_peer;
	char *peer_vals[N_FIELDS];

	GtkWidget *add_peer_btn;
	GtkWidget *empty_row;
	GtkWidget *peer_row;
	GtkWidget *peer_label;
	GtkWidget *peer_popover;
} AwgEditor;

/* editor-fields.c */
const AwgKey *field_reg (const Field *f);
char *plain_label (const Field *f);
char *widget_value (AwgEditor *self, guint i);
void widget_set_value (AwgEditor *self, guint i, const char *v);
char *field_value (AwgEditor *self, guint i);
gboolean check_value (const Field *f, const char *v, char **reason);
void mark_field (GtkWidget *input, const Field *f, gboolean ok, const char *reason);
GtkWidget *make_grid (void);
void add_fields (AwgEditor *self, GtkWidget *grid, Section section, GtkSizeGroup *labels);

/* editor-peers.c */
void update_peer_rows (AwgEditor *self);
void clear_peer (AwgEditor *self);
GtkWidget *build_peers (AwgEditor *self);
