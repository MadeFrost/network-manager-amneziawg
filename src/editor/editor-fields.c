/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The editor's fields: what each key looks like and the widget for it.
 */
#include "editor-private.h"

#include <string.h>

const Field fields[] = {
	/* [Interface]: same wording as GNOME Settings' WireGuard page */
	{ AWG_KEY_PRIVATE_KEY, N_("_Private Key"), N_("The 256 bit private key in base64 encoding"),
	  NULL, SEC_IFACE, W_SECRET },
	{ AWG_KEY_ADDRESS, N_("_Address"),
	  N_("IPv4 and/or IPv6 addresses of this device with prefix length, separated by commas"),
	  "10.8.1.2/32", SEC_IFACE, W_ENTRY },
	{ AWG_KEY_DNS, N_("_DNS"), N_("DNS servers and search domains, separated by commas"),
	  "1.1.1.1, 1.0.0.1", SEC_IFACE, W_ENTRY },
	{ "ListenPort", N_("_Listen Port"),
	  N_("A port to listen on. If set to 'automatic', the port will be chosen randomly when the interface comes up."),
	  NULL, SEC_IFACE, W_SPIN, N_("automatic") },
	{ AWG_KEY_MTU, N_("_MTU"), NULL, NULL, SEC_IFACE, W_SPIN, N_("automatic"), N_("bytes") },

	/* [Peer]: the WireGuard peer popover */
	{ AWG_KEY_ENDPOINT, N_("_Endpoint"),
	  N_("An endpoint IP or hostname, followed by a colon, and then a port number."),
	  "vpn.example.com:51820", SEC_PEER, W_ENTRY },
	{ AWG_KEY_PUBLIC_KEY, N_("_Public key"), N_("The server's 256 bit public key in base64 encoding"),
	  NULL, SEC_PEER, W_ENTRY },
	{ AWG_KEY_PRESHARED_KEY, N_("P_re-shared key"),
	  N_("A base64 preshared key. Optional, and may be omitted. Adds an additional layer of symmetric-key cryptography."),
	  NULL, SEC_PEER, W_SECRET },
	{ AWG_KEY_ALLOWED_IPS, N_("Allowed _IP addresses"),
	  N_("Traffic to these networks is sent through the tunnel. 0.0.0.0/0, ::/0 sends all traffic."),
	  "0.0.0.0/0, ::/0", SEC_PEER, W_ENTRY },
	{ "Peer-PersistentKeepalive", N_("P_ersistent keepalive"),
	  N_("How often to send an empty packet to keep the connection alive behind NAT"),
	  NULL, SEC_PEER, W_SPIN, N_("off"), N_("seconds") },

	/* AmneziaWG */
	{ "Jc", N_("Junk packet count"), N_("Number of junk packets sent before the handshake"),
	  NULL, SEC_OBFS, W_SPIN },
	{ "Jmin", N_("Junk packet min size"), NULL, NULL, SEC_OBFS, W_SPIN, NULL, N_("bytes") },
	{ "Jmax", N_("Junk packet max size"), NULL, NULL, SEC_OBFS, W_SPIN, NULL, N_("bytes") },
	{ "S1", N_("Init packet padding"), N_("Random bytes added to the handshake initiation packet"),
	  NULL, SEC_OBFS, W_SPIN, NULL, N_("bytes") },
	{ "S2", N_("Response packet padding"), N_("Random bytes added to the handshake response packet"),
	  NULL, SEC_OBFS, W_SPIN, NULL, N_("bytes") },
	{ "S3", N_("Cookie packet padding"), N_("Random bytes added to the cookie reply packet"),
	  NULL, SEC_OBFS, W_SPIN, NULL, N_("bytes") },
	{ "S4", N_("Transport packet padding"), N_("Random bytes added to every data packet"),
	  NULL, SEC_OBFS, W_SPIN, NULL, N_("bytes") },
	{ "H1", N_("Init packet header"), N_("Message type of the handshake initiation packet: a number or a range"),
	  NULL, SEC_OBFS, W_ENTRY },
	{ "H2", N_("Response packet header"), N_("Message type of the handshake response packet: a number or a range"),
	  NULL, SEC_OBFS, W_ENTRY },
	{ "H3", N_("Cookie packet header"), N_("Message type of the cookie reply packet: a number or a range"),
	  NULL, SEC_OBFS, W_ENTRY },
	{ "H4", N_("Transport packet header"), N_("Message type of data packets: a number or a range"),
	  NULL, SEC_OBFS, W_ENTRY },
	{ "I1", N_("Signature packet 1"), N_("Packet sent before the handshake to imitate another protocol, e.g. <b 0x…><r 16>"),
	  NULL, SEC_OBFS, W_ENTRY },
	{ "I2", N_("Signature packet 2"), NULL, NULL, SEC_OBFS, W_ENTRY },
	{ "I3", N_("Signature packet 3"), NULL, NULL, SEC_OBFS, W_ENTRY },
	{ "I4", N_("Signature packet 4"), NULL, NULL, SEC_OBFS, W_ENTRY },
	{ "I5", N_("Signature packet 5"), NULL, NULL, SEC_OBFS, W_ENTRY },
};

G_STATIC_ASSERT (G_N_ELEMENTS (fields) == N_FIELDS);

/* The registry entry of a field; every field is a known key. */
const AwgKey *
field_reg (const Field *f)
{
	const AwgKey *k = awg_key_find (f->key);

	g_assert (k);
	return k;
}

/* Label text without the mnemonic underscore, for error messages. */
char *
plain_label (const Field *f)
{
	GString *s = g_string_new (NULL);
	const char *p;

	for (p = _(f->label); *p; p++) {
		if (*p != '_')
			g_string_append_c (s, *p);
	}
	return g_string_free (s, FALSE);
}

/* Value currently typed into the widget, or NULL for "not set". */
char *
widget_value (AwgEditor *self, guint i)
{
	GtkWidget *w = self->inputs[i];

	if (fields[i].kind == W_SPIN) {
		int v;

		gtk_spin_button_update (GTK_SPIN_BUTTON (w));
		v = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (w));
		return v > 0 ? g_strdup_printf ("%d", v) : NULL;
	} else {
		char *t = g_strstrip (g_strdup (entry_get_text (w)));
		if (!*t)
			g_clear_pointer (&t, g_free);
		return t;
	}
}

void
widget_set_value (AwgEditor *self, guint i, const char *v)
{
	if (fields[i].kind == W_SPIN)
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (self->inputs[i]), v ? g_ascii_strtod (v, NULL) : 0);
	else
		entry_set_text (self->inputs[i], v ? v : "");
}

/* Value that would be saved: peer fields come from the applied peer. */
char *
field_value (AwgEditor *self, guint i)
{
	if (fields[i].section == SEC_PEER)
		return self->has_peer ? g_strdup (self->peer_vals[i]) : NULL;
	return widget_value (self, i);
}

/* reason: if non-NULL, gets a translated explanation (NULL when missing). */
gboolean
check_value (const Field *f, const char *v, char **reason)
{
	return awg_key_check (field_reg (f), v, reason);
}

/* Red field; the tooltip says what is wrong. */
void
mark_field (GtkWidget *input, const Field *f, gboolean ok, const char *reason)
{
	g_autofree char *tip = NULL;

	if (ok)
		widget_remove_class (input, "error");
	else
		widget_add_class (input, "error");
	if (!ok && reason)
		tip = f->tooltip ? g_strdup_printf ("%s\n\n%s", reason, _(f->tooltip)) : g_strdup (reason);
	gtk_widget_set_tooltip_text (input, tip ? tip : f->tooltip ? _(f->tooltip) : NULL);
}

static void
input_changed (GtkWidget *w, AwgEditor *self)
{
	g_signal_emit_by_name (self, "changed");
}

#if GTK_CHECK_VERSION(4, 0, 0)
static void
toggle_secret (GtkEntry *entry, GtkEntryIconPosition pos, gpointer user_data)
#else
static void
toggle_secret (GtkEntry *entry, GtkEntryIconPosition pos, GdkEvent *event, gpointer user_data)
#endif
{
	gtk_entry_set_visibility (entry, !gtk_entry_get_visibility (entry));
}

/* Show "automatic"/"off" for 0, like the WireGuard page. */
static gboolean
spin_output (GtkSpinButton *spin, gpointer zero_text)
{
	if (gtk_spin_button_get_value_as_int (spin) != 0)
		return FALSE;
	entry_set_text (GTK_WIDGET (spin), _(zero_text));
	return TRUE;
}

static int
spin_input (GtkSpinButton *spin, double *new_value, gpointer zero_text)
{
	const char *t = entry_get_text (GTK_WIDGET (spin));

	if (!*t || g_utf8_collate (t, _(zero_text)) == 0) {
		*new_value = 0;
		return TRUE;
	}
	return FALSE;
}

GtkWidget *
make_grid (void)
{
	GtkWidget *grid = gtk_grid_new ();

	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
	return grid;
}

static GtkWidget *
make_input (AwgEditor *self, const Field *f)
{
	GtkWidget *w;

	if (f->kind == W_SPIN) {
		w = gtk_spin_button_new_with_range (0, field_reg (f)->max, 1);
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), 0);
		gtk_widget_set_halign (w, GTK_ALIGN_START);
		entry_set_width_chars (w, 10);
		if (f->zero_text) {
			/* new_with_range() makes it digits-only, which would reject the text */
			gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (w), FALSE);
			/* wide enough for the translated "automatic"/"off" */
			entry_set_width_chars (w, MAX (10, (int) g_utf8_strlen (_(f->zero_text), -1) + 1));
			g_signal_connect (w, "output", G_CALLBACK (spin_output), (gpointer) f->zero_text);
			g_signal_connect (w, "input", G_CALLBACK (spin_input), (gpointer) f->zero_text);
			spin_output (GTK_SPIN_BUTTON (w), (gpointer) f->zero_text);
		}
		/* peer fields only take effect on "Apply" in the popover */
		if (f->section != SEC_PEER)
			g_signal_connect_object (w, "value-changed", G_CALLBACK (input_changed), self, 0);
		return w;
	}

	w = gtk_entry_new ();
	gtk_widget_set_hexpand (w, TRUE);
	if (f->placeholder)
		gtk_entry_set_placeholder_text (GTK_ENTRY (w), f->placeholder);
	if (f->kind == W_SECRET) {
		gtk_entry_set_visibility (GTK_ENTRY (w), FALSE);
		gtk_entry_set_input_purpose (GTK_ENTRY (w), GTK_INPUT_PURPOSE_PASSWORD);
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (w), GTK_ENTRY_ICON_SECONDARY, "dialog-password");
		gtk_entry_set_icon_tooltip_text (GTK_ENTRY (w), GTK_ENTRY_ICON_SECONDARY,
		                                 g_str_equal (f->key, AWG_KEY_PRIVATE_KEY)
		                                     ? _("Show/hide private key")
		                                     : _("Show/hide pre-shared key"));
		g_signal_connect (w, "icon-press", G_CALLBACK (toggle_secret), NULL);
	}
	if (f->section != SEC_PEER)
		g_signal_connect_object (w, "changed", G_CALLBACK (input_changed), self, 0);
	return w;
}

void
add_fields (AwgEditor *self, GtkWidget *grid, Section section, GtkSizeGroup *labels)
{
	int row = 0;
	guint i;

	for (i = 0; i < N_FIELDS; i++) {
		const Field *f = &fields[i];
		GtkWidget *label, *input;

		if (f->section != section)
			continue;

		input = make_input (self, f);
		if (section == SEC_OBFS) {
			/* Show the config key too: users compare these with the .conf. */
			g_autofree char *text = g_strdup_printf ("%s (%s)", _(f->label), f->key);
			label = gtk_label_new (text);
		} else {
			label = gtk_label_new_with_mnemonic (_(f->label));
			gtk_label_set_mnemonic_widget (GTK_LABEL (label), input);
		}
		gtk_label_set_xalign (GTK_LABEL (label), 1.0);
		if (labels)
			gtk_size_group_add_widget (labels, label);
		if (f->tooltip)
			gtk_widget_set_tooltip_text (input, _(f->tooltip));

		gtk_grid_attach (GTK_GRID (grid), label, 0, row, 1, 1);
		gtk_grid_attach (GTK_GRID (grid), input, 1, row, f->unit ? 1 : 2, 1);
		if (f->unit) {
			GtkWidget *unit = gtk_label_new (_(f->unit));
			gtk_widget_set_halign (unit, GTK_ALIGN_START);
			gtk_widget_set_hexpand (unit, TRUE);
			gtk_grid_attach (GTK_GRID (grid), unit, 2, row, 1, 1);
		}
		self->inputs[i] = input;
		row++;
	}
}
