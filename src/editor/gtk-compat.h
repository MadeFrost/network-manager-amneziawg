/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The few GTK calls that differ between GTK4 (GNOME Settings) and GTK3
 * (nm-connection-editor). The editor is compiled once for each.
 */
#pragma once

#include <gtk/gtk.h>

#if GTK_CHECK_VERSION(4, 0, 0)
#define entry_get_text(e)           gtk_editable_get_text (GTK_EDITABLE (e))
#define entry_set_text(e, t)        gtk_editable_set_text (GTK_EDITABLE (e), t)
#define entry_set_width_chars(e, n) gtk_editable_set_width_chars (GTK_EDITABLE (e), n)
#define frame_set_child(f, c)       gtk_frame_set_child (GTK_FRAME (f), c)
#define popover_set_child(p, c)     gtk_popover_set_child (GTK_POPOVER (p), c)
#define list_box_append(l, c)       gtk_list_box_append (GTK_LIST_BOX (l), c)
#define list_box_row_set_child(r, c) gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (r), c)
#define box_append(b, c)            gtk_box_append (GTK_BOX (b), c)
#define button_new_from_icon(n)     gtk_button_new_from_icon_name (n)
#define menu_button_set_icon(m, n)  gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (m), n)
#define label_set_wrap(l, w)        gtk_label_set_wrap (GTK_LABEL (l), w)
#define widget_add_class(w, c)      gtk_widget_add_css_class (w, c)
#define widget_remove_class(w, c)   gtk_widget_remove_css_class (w, c)
#else
#define entry_get_text(e)           gtk_entry_get_text (GTK_ENTRY (e))
#define entry_set_text(e, t)        gtk_entry_set_text (GTK_ENTRY (e), t)
#define entry_set_width_chars(e, n) gtk_entry_set_width_chars (GTK_ENTRY (e), n)
#define frame_set_child(f, c)       gtk_container_add (GTK_CONTAINER (f), c)
#define popover_set_child(p, c)     gtk_container_add (GTK_CONTAINER (p), c)
#define list_box_append(l, c)       gtk_container_add (GTK_CONTAINER (l), c)
#define list_box_row_set_child(r, c) gtk_container_add (GTK_CONTAINER (r), c)
#define box_append(b, c)            gtk_container_add (GTK_CONTAINER (b), c)
#define button_new_from_icon(n)     gtk_button_new_from_icon_name (n, GTK_ICON_SIZE_BUTTON)
#define menu_button_set_icon(m, n) \
	gtk_button_set_image (GTK_BUTTON (m), gtk_image_new_from_icon_name (n, GTK_ICON_SIZE_BUTTON))
#define label_set_wrap(l, w)        gtk_label_set_line_wrap (GTK_LABEL (l), w)
#define widget_add_class(w, c)      gtk_style_context_add_class (gtk_widget_get_style_context (w), c)
#define widget_remove_class(w, c)   gtk_style_context_remove_class (gtk_widget_get_style_context (w), c)
#endif
