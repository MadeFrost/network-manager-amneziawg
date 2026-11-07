/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Secrets helper ("auth-dialog") for the AmneziaWG VPN plugin.
 *
 * GNOME Shell's and nm-applet's secret agents run the auth-dialog named in
 * the plugin's .name file whenever NetworkManager asks them for VPN secrets,
 * including when GNOME Settings opens a connection for editing. Without one,
 * GNOME Shell never answers and the request hangs.
 *
 * AmneziaWG keys are stored in the system connection (secret flags NONE), so
 * there is nothing to ask the user: this helper hands the stored secrets it
 * received back unchanged (as other VPN auth dialogs do). Returning nothing
 * leaves GNOME Settings with an empty "Private key" field.
 *
 * Protocol (legacy, non external-UI mode):
 *   stdin:  DATA_KEY=/DATA_VAL=/SECRET_KEY=/SECRET_VAL= lines, then "DONE\n\n"
 *   stdout: "key\nvalue\n" pairs, terminated by an empty line pair "\n\n"
 *   stdin:  "QUIT\n\n" when the caller is done; then exit 0.
 */
#include <NetworkManager.h>

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

static void
wait_for_quit (void)
{
	char line[256];

	/* Returns on "QUIT" or when the caller closes our stdin. */
	while (fgets (line, sizeof line, stdin)) {
		if (strncmp (line, "QUIT", 4) == 0)
			break;
	}
}

int
main (int argc, char *argv[])
{
	g_autofree char *uuid = NULL, *name = NULL, *service = NULL;
	gboolean interactive = FALSE, reprompt = FALSE, external_ui = FALSE;
	g_auto (GStrv) hints = NULL;
	g_autoptr (GOptionContext) ctx = NULL;
	g_autoptr (GError) error = NULL;
	g_autoptr (GHashTable) data = NULL, secrets = NULL;
	GOptionEntry entries[] = {
		{ "reprompt", 'r', 0, G_OPTION_ARG_NONE, &reprompt, "Reprompt for passwords", NULL },
		{ "uuid", 'u', 0, G_OPTION_ARG_STRING, &uuid, "UUID of VPN connection", NULL },
		{ "name", 'n', 0, G_OPTION_ARG_STRING, &name, "Name of VPN connection", NULL },
		{ "service", 's', 0, G_OPTION_ARG_STRING, &service, "VPN service type", NULL },
		{ "allow-interaction", 'i', 0, G_OPTION_ARG_NONE, &interactive, "Allow user interaction", NULL },
		{ "external-ui-mode", 0, 0, G_OPTION_ARG_NONE, &external_ui, "External UI mode", NULL },
		{ "hint", 't', 0, G_OPTION_ARG_STRING_ARRAY, &hints, "Hints from the VPN plugin", NULL },
		{ NULL },
	};

	ctx = g_option_context_new ("- AmneziaWG auth dialog");
	g_option_context_add_main_entries (ctx, entries, NULL);
	if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
		fprintf (stderr, "Error parsing options: %s\n", error->message);
		return 1;
	}
	if (!uuid || !name || !service) {
		fprintf (stderr, "A connection UUID, name and VPN service type are required\n");
		return 1;
	}

	/* Read what the agent sends (it must never block writing to us). A short
	 * or empty input is not an error: there is just nothing to hand back. */
	nm_vpn_service_plugin_read_vpn_details (STDIN_FILENO, &data, &secrets);

	{
		g_autoptr (GString) names = g_string_new (NULL);
		GHashTableIter iter;
		const char *key, *value;

		if (secrets) {
			g_hash_table_iter_init (&iter, secrets);
			while (g_hash_table_iter_next (&iter, (gpointer *) &key, (gpointer *) &value)) {
				/* Hand the stored secrets back unchanged. */
				if (value && *value && !strchr (key, '\n') && !strchr (value, '\n'))
					printf ("%s\n%s\n", key, value);
				g_string_append_printf (names, "%s%s", names->len ? ", " : "", key);
			}
		}
		/* Key names only, never values: helps diagnose secret problems. */
		openlog ("nm-amneziawg-auth-dialog", 0, LOG_USER);
		syslog (LOG_INFO, "request for \"%s\"%s%s: %u data items, secrets received: %s",
		        name, interactive ? " (interactive)" : "", reprompt ? " (reprompt)" : "",
		        data ? g_hash_table_size (data) : 0, names->len ? names->str : "none");
		closelog ();
	}
	printf ("\n\n");
	fflush (stdout);

	wait_for_quit ();
	return 0;
}
