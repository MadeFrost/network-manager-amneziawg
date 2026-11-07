/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Connection editor widget, laid out after GNOME Settings' WireGuard page:
 * interface fields on top, a "Peers" list whose rows open a popover with
 * the peer settings, and the AmneziaWG parameters on a separate tab.
 *
 * Built twice: against GTK4 (GNOME Settings) and GTK3 (nm-connection-editor).
 */
#include "editor-private.h"

typedef struct {
	GObjectClass parent;
} AwgEditorClass;

static void awg_editor_interface_init (NMVpnEditorInterface *iface);

GType awg_editor_get_type (void);
G_DEFINE_TYPE_WITH_CODE (AwgEditor, awg_editor, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (NM_TYPE_VPN_EDITOR, awg_editor_interface_init))

/*****************************************************************************/

static gboolean
is_field_key (const char *key)
{
	guint i;

	for (i = 0; i < N_FIELDS; i++) {
		if (g_str_equal (fields[i].key, key))
			return TRUE;
	}
	return FALSE;
}

static guint
field_index (const char *key)
{
	guint i;

	for (i = 0; i < N_FIELDS && !g_str_equal (fields[i].key, key); i++)
		;
	g_assert (i < N_FIELDS);
	return i;
}

static void
set_field_error (GError **error, const Field *f, gboolean missing, const char *reason)
{
	g_autofree char *name = plain_label (f);

	if (missing)
		g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_MISSING_PROPERTY,
		             _("“%s” is required"), name);
	else if (reason)
		g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             "%s: %s", name, reason);
	else
		g_set_error (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("“%s” is invalid"), name);
}

/* Secrets are kept in the system connection, readable only by root. */
static void
add_secret (NMSettingVpn *s_vpn, const char *key, const char *value)
{
	nm_setting_vpn_add_secret (s_vpn, key, value);
	nm_setting_set_secret_flags (NM_SETTING (s_vpn), key, NM_SETTING_SECRET_FLAG_NONE, NULL);
}

static gboolean
update_connection (NMVpnEditor *iface, NMConnection *connection, GError **error)
{
	AwgEditor *self = (AwgEditor *) iface;
	NMSettingVpn *s_vpn;
	const Field *first_bad = NULL;
	g_autofree char *first_reason = NULL;
	gboolean bad_missing = FALSE, peer_ok = TRUE;
	guint i;

	/* Validate everything so every bad field gets highlighted, as GNOME does. */
	for (i = 0; i < N_FIELDS; i++) {
		g_autofree char *v = field_value (self, i);
		gboolean ok;

		if (fields[i].section == SEC_PEER && !self->has_peer)
			continue;
		g_autofree char *reason = NULL;

		ok = check_value (&fields[i], v, &reason);
		if (fields[i].section == SEC_PEER)
			peer_ok &= ok;
		else
			mark_field (self->inputs[i], &fields[i], ok, reason);

		if (!ok && !first_bad) {
			first_bad = &fields[i];
			bad_missing = !v;
			first_reason = g_steal_pointer (&reason);
		}
	}
	/* IPv4 networks in AllowedIPs need an IPv4 address (see awg_check_families). */
	if (!first_bad && self->has_peer) {
		guint a = field_index (AWG_KEY_ADDRESS), r = field_index (AWG_KEY_ALLOWED_IPS);
		g_autofree char *addr = field_value (self, a);
		g_autofree char *allowed = field_value (self, r);

		if (!awg_check_families (addr, allowed, &first_reason)) {
			first_bad = &fields[a];
			mark_field (self->inputs[a], &fields[a], FALSE, first_reason);
		}
	}
	if (peer_ok)
		widget_remove_class (self->peer_label, "error");
	else
		widget_add_class (self->peer_label, "error");

	if (first_bad) {
		set_field_error (error, first_bad, bad_missing, first_reason);
		return FALSE;
	}
	if (!self->has_peer) {
		g_set_error_literal (error, NM_CONNECTION_ERROR, NM_CONNECTION_ERROR_MISSING_PROPERTY,
		                     _("Add a peer: the server to connect to"));
		return FALSE;
	}

	s_vpn = NM_SETTING_VPN (nm_setting_vpn_new ());
	g_object_set (s_vpn, NM_SETTING_VPN_SERVICE_TYPE, AWG_DBUS_SERVICE, NULL);

	if (self->orig) {
		g_autofree const char **keys = NULL;
		guint n = 0;

		g_autofree const char **skeys = NULL;

		/* Keep everything the form has no field for. */
		keys = nm_setting_vpn_get_data_keys (self->orig, &n);
		for (i = 0; i < n; i++) {
			const char *v = nm_setting_vpn_get_data_item (self->orig, keys[i]);

			if (is_field_key (keys[i]) || g_str_has_suffix (keys[i], "-flags"))
				continue;
			if (awg_key_is_secret (keys[i])) {
				/* Stored in vpn.data by older versions: move it to the secrets. */
				if (!nm_setting_vpn_get_secret (self->orig, keys[i]))
					add_secret (s_vpn, keys[i], v);
			} else
				nm_setting_vpn_add_data_item (s_vpn, keys[i], v);
		}
		skeys = nm_setting_vpn_get_secret_keys (self->orig, &n);
		for (i = 0; i < n; i++) {
			if (!is_field_key (skeys[i]))
				add_secret (s_vpn, skeys[i], nm_setting_vpn_get_secret (self->orig, skeys[i]));
		}
	}

	for (i = 0; i < N_FIELDS; i++) {
		g_autofree char *v = field_value (self, i);

		if (!v)
			continue;
		if (awg_key_is_secret (fields[i].key))
			add_secret (s_vpn, fields[i].key, v);
		else
			nm_setting_vpn_add_data_item (s_vpn, fields[i].key, v);
	}

	nm_connection_add_setting (connection, NM_SETTING (s_vpn));
	return TRUE;
}

static GObject *
get_widget (NMVpnEditor *iface)
{
	return G_OBJECT (((AwgEditor *) iface)->widget);
}

/*****************************************************************************/
/* The form and loading a connection into it */

static void
build_ui (AwgEditor *self)
{
	GtkWidget *outer, *stack, *switcher, *general, *iface_grid, *obfs, *obfs_note, *obfs_grid;

	self->labels = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	/* General: interface + peers */
	general = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	iface_grid = make_grid ();
	add_fields (self, iface_grid, SEC_IFACE, NULL);
	box_append (general, iface_grid);
	box_append (general, build_peers (self));

	/* Obfuscation */
	obfs = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
	obfs_note = gtk_label_new (_("These parameters must match the server. "
	                             "They are taken from the AmneziaWG configuration file; "
	                             "change them only if you know the server settings."));
	label_set_wrap (obfs_note, TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (obfs_note), 60);
	gtk_label_set_xalign (GTK_LABEL (obfs_note), 0.0);
	widget_add_class (obfs_note, "dim-label");
	obfs_grid = make_grid ();
	add_fields (self, obfs_grid, SEC_OBFS, self->labels);
	box_append (obfs, obfs_note);
	box_append (obfs, obfs_grid);

	stack = gtk_stack_new ();
	gtk_stack_set_transition_type (GTK_STACK (stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
	/* Each tab keeps its own size: no empty space under the short one.
	 * Both directions must be non-homogeneous, otherwise GtkStack measures
	 * the hidden tab's wrapping label for a foreign height and asks for a
	 * very wide window. The dialog stretches us to its width anyway. */
	gtk_stack_set_vhomogeneous (GTK_STACK (stack), FALSE);
	gtk_stack_set_hhomogeneous (GTK_STACK (stack), FALSE);
	gtk_stack_add_titled (GTK_STACK (stack), general, "general", _("General"));
	gtk_stack_add_titled (GTK_STACK (stack), obfs, "obfuscation", _("Obfuscation"));

	switcher = gtk_stack_switcher_new ();
	gtk_stack_switcher_set_stack (GTK_STACK_SWITCHER (switcher), GTK_STACK (stack));
	gtk_widget_set_halign (switcher, GTK_ALIGN_CENTER);

	outer = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);
	gtk_widget_set_margin_top (outer, 12);
	gtk_widget_set_margin_bottom (outer, 12);
	box_append (outer, switcher);
	box_append (outer, stack);

#if !GTK_CHECK_VERSION(4, 0, 0)
	gtk_widget_show_all (outer);
#endif
	self->widget = g_object_ref_sink (outer);
}

static void
fill_from_connection (AwgEditor *self, NMConnection *connection)
{
	NMSettingVpn *s_vpn = connection ? nm_connection_get_setting_vpn (connection) : NULL;
	guint i;

	if (s_vpn) {
		self->orig = NM_SETTING_VPN (nm_setting_duplicate (NM_SETTING (s_vpn)));
		self->has_peer = nm_setting_vpn_get_data_item (s_vpn, AWG_KEY_PUBLIC_KEY)
		                 || nm_setting_vpn_get_data_item (s_vpn, AWG_KEY_ENDPOINT);

		for (i = 0; i < N_FIELDS; i++) {
			const char *v = awg_key_is_secret (fields[i].key)
			                    ? nm_setting_vpn_get_secret (s_vpn, fields[i].key)
			                    : nm_setting_vpn_get_data_item (s_vpn, fields[i].key);
			if (fields[i].section == SEC_PEER)
				self->peer_vals[i] = g_strdup (v);
			else if (v)
				widget_set_value (self, i, v);
		}
	}
	update_peer_rows (self);
}

/*****************************************************************************/

static void
awg_editor_init (AwgEditor *self)
{
}

static void
dispose (GObject *object)
{
	AwgEditor *self = (AwgEditor *) object;

	/* signal handlers use g_signal_connect_object() and go away with us */
	g_clear_object (&self->widget);
	clear_peer (self);
	g_clear_object (&self->labels);
	g_clear_object (&self->orig);
	G_OBJECT_CLASS (awg_editor_parent_class)->dispose (object);
}

static void
awg_editor_class_init (AwgEditorClass *klass)
{
	G_OBJECT_CLASS (klass)->dispose = dispose;
}

static void
awg_editor_interface_init (NMVpnEditorInterface *iface)
{
	iface->get_widget = get_widget;
	iface->update_connection = update_connection;
}

AWG_EXPORT NMVpnEditor *
nm_vpn_editor_factory_amneziawg (NMVpnEditorPlugin *plugin, NMConnection *connection, GError **error)
{
	AwgEditor *self;

	awg_i18n_init ();
	self = g_object_new (awg_editor_get_type (), NULL);
	build_ui (self);
	fill_from_connection (self, connection);
	return NM_VPN_EDITOR (self);
}
