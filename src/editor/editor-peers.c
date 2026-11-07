/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The "Peers" list and the popover with the peer settings, as on GNOME
 * Settings' WireGuard page. The popover edits a copy: values take effect
 * on "Apply".
 */
#include "editor-private.h"

static const char *
peer_field (AwgEditor *self, const char *key)
{
	guint i;

	for (i = 0; i < N_FIELDS; i++) {
		if (g_str_equal (fields[i].key, key))
			return self->peer_vals[i];
	}
	return NULL;
}

void
update_peer_rows (AwgEditor *self)
{
	const char *title = peer_field (self, AWG_KEY_ENDPOINT);

	if (!title)
		title = peer_field (self, AWG_KEY_PUBLIC_KEY);
	gtk_label_set_text (GTK_LABEL (self->peer_label), title ? title : _("Unsaved peer"));

	gtk_widget_set_visible (self->peer_row, self->has_peer);
	gtk_widget_set_visible (self->empty_row, !self->has_peer);

	/* The plugin drives a single tunnel to one server. */
	gtk_widget_set_sensitive (self->add_peer_btn, !self->has_peer);
	gtk_widget_set_tooltip_text (self->add_peer_btn,
	                             self->has_peer ? _("Only one peer is supported") : _("Add peer"));
}

void
clear_peer (AwgEditor *self)
{
	guint i;

	for (i = 0; i < N_FIELDS; i++)
		g_clear_pointer (&self->peer_vals[i], g_free);
}

/* Popover opened: show the applied values, drop stale error marks. */
static void
peer_popover_shown (GtkWidget *popover, AwgEditor *self)
{
	guint i;

	for (i = 0; i < N_FIELDS; i++) {
		if (fields[i].section != SEC_PEER)
			continue;
		widget_set_value (self, i, self->peer_vals[i]);
		mark_field (self->inputs[i], &fields[i], TRUE, NULL);
	}
}

static void
peer_apply_clicked (GtkWidget *button, AwgEditor *self)
{
	gboolean ok = TRUE;
	guint i;

	for (i = 0; i < N_FIELDS; i++) {
		g_autofree char *v = NULL, *reason = NULL;
		gboolean field_ok;

		if (fields[i].section != SEC_PEER)
			continue;
		v = widget_value (self, i);
		field_ok = check_value (&fields[i], v, &reason);
		mark_field (self->inputs[i], &fields[i], field_ok, reason);
		ok &= field_ok;
	}
	if (!ok)
		return;

	clear_peer (self);
	for (i = 0; i < N_FIELDS; i++) {
		if (fields[i].section == SEC_PEER)
			self->peer_vals[i] = widget_value (self, i);
	}
	gtk_popover_popdown (GTK_POPOVER (self->peer_popover));
	update_peer_rows (self);
	g_signal_emit_by_name (self, "changed");
}

static void
add_peer_clicked (GtkWidget *button, AwgEditor *self)
{
	clear_peer (self);
	self->has_peer = TRUE;
	update_peer_rows (self);
	gtk_popover_popup (GTK_POPOVER (self->peer_popover));
	g_signal_emit_by_name (self, "changed");
}

static void
remove_peer_clicked (GtkWidget *button, AwgEditor *self)
{
	clear_peer (self);
	self->has_peer = FALSE;
	update_peer_rows (self);
	g_signal_emit_by_name (self, "changed");
}

/* Popover with the peer fields: wireguard-peer.ui */
static GtkWidget *
build_peer_popover (AwgEditor *self)
{
	GtkWidget *popover = gtk_popover_new (
#if !GTK_CHECK_VERSION(4, 0, 0)
		NULL
#endif
	);
	GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	GtkWidget *grid = make_grid ();
	GtkWidget *apply = gtk_button_new_with_mnemonic (_("_Apply"));

	gtk_popover_set_position (GTK_POPOVER (popover), GTK_POS_LEFT);
	gtk_widget_set_margin_start (box, 10);
	gtk_widget_set_margin_end (box, 10);
	gtk_widget_set_margin_top (box, 10);
	gtk_widget_set_margin_bottom (box, 10);
	gtk_widget_set_margin_top (grid, 6);
	gtk_widget_set_size_request (grid, 420, -1);
	add_fields (self, grid, SEC_PEER, NULL);

	gtk_widget_set_halign (apply, GTK_ALIGN_END);
	widget_add_class (apply, "suggested-action");
	g_signal_connect_object (apply, "clicked", G_CALLBACK (peer_apply_clicked), self, 0);

	box_append (box, grid);
	box_append (box, apply);
	popover_set_child (popover, box);
#if !GTK_CHECK_VERSION(4, 0, 0)
	gtk_widget_show_all (box);
#endif
	g_signal_connect_object (popover, "show", G_CALLBACK (peer_popover_shown), self, 0);
	return popover;
}

/* "Peers" header with "+" and a framed list, as on the WireGuard page. */
GtkWidget *
build_peers (AwgEditor *self)
{
	GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	GtkWidget *header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *title = gtk_label_new (NULL);
	GtkWidget *frame = gtk_frame_new (NULL);
	GtkWidget *list = gtk_list_box_new ();
	GtkWidget *row_box, *configure, *remove, *empty;
	g_autofree char *markup = g_markup_printf_escaped ("<b>%s</b>", _("_Peers"));

	gtk_label_set_markup_with_mnemonic (GTK_LABEL (title), markup);
	gtk_label_set_xalign (GTK_LABEL (title), 0.0);
	gtk_widget_set_hexpand (title, TRUE);
	gtk_widget_set_margin_top (header, 12);

	self->add_peer_btn = button_new_from_icon ("list-add-symbolic");
	widget_add_class (self->add_peer_btn, "flat");
	gtk_label_set_mnemonic_widget (GTK_LABEL (title), self->add_peer_btn);
	g_signal_connect_object (self->add_peer_btn, "clicked", G_CALLBACK (add_peer_clicked), self, 0);
	box_append (header, title);
	box_append (header, self->add_peer_btn);

	gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);

	/* "No peers set up" */
	self->empty_row = gtk_list_box_row_new ();
	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (self->empty_row), FALSE);
	empty = gtk_label_new (_("No peers set up"));
	gtk_label_set_xalign (GTK_LABEL (empty), 0.0);
	gtk_widget_set_margin_start (empty, 12);
	gtk_widget_set_margin_end (empty, 12);
	gtk_widget_set_margin_top (empty, 14);
	gtk_widget_set_margin_bottom (empty, 14);
	list_box_row_set_child (self->empty_row, empty);
	list_box_append (list, self->empty_row);

	/* the peer: label, options popover, remove */
	self->peer_row = gtk_list_box_row_new ();
	gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (self->peer_row), FALSE);
	row_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_widget_set_margin_start (row_box, 12);
	gtk_widget_set_margin_end (row_box, 12);
	gtk_widget_set_margin_top (row_box, 8);
	gtk_widget_set_margin_bottom (row_box, 8);

	self->peer_label = gtk_label_new (NULL);
	gtk_label_set_xalign (GTK_LABEL (self->peer_label), 0.0);
	gtk_label_set_ellipsize (GTK_LABEL (self->peer_label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand (self->peer_label, TRUE);

	configure = gtk_menu_button_new ();
	menu_button_set_icon (configure, "emblem-system-symbolic");
#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_menu_button_set_has_frame (GTK_MENU_BUTTON (configure), FALSE);
#else
	gtk_button_set_relief (GTK_BUTTON (configure), GTK_RELIEF_NONE);
#endif
	gtk_widget_set_tooltip_text (configure, _("Options…"));
	gtk_menu_button_set_direction (GTK_MENU_BUTTON (configure), GTK_ARROW_LEFT);
	self->peer_popover = build_peer_popover (self);
	gtk_menu_button_set_popover (GTK_MENU_BUTTON (configure), self->peer_popover);

	remove = button_new_from_icon ("edit-delete-symbolic");
	widget_add_class (remove, "destructive-action");
	gtk_widget_set_tooltip_text (remove, _("Remove"));
	g_signal_connect_object (remove, "clicked", G_CALLBACK (remove_peer_clicked), self, 0);

	box_append (row_box, self->peer_label);
	box_append (row_box, configure);
	box_append (row_box, remove);
	list_box_row_set_child (self->peer_row, row_box);
	list_box_append (list, self->peer_row);

	frame_set_child (frame, list);
	box_append (vbox, header);
	box_append (vbox, frame);
	return vbox;
}
