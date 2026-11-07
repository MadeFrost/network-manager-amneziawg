/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "awg-common.h"

#include <locale.h>

void
awg_i18n_init (void)
{
	static gsize done = 0;

	if (g_once_init_enter (&done)) {
		bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
		bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
		g_once_init_leave (&done, 1);
	}
}

char **
awg_split_list (const char *value)
{
	g_auto (GStrv) parts = NULL;
	GPtrArray *out = g_ptr_array_new ();
	guint i;

	if (value) {
		parts = g_strsplit_set (value, ", \t", -1);
		for (i = 0; parts[i]; i++) {
			if (*parts[i])
				g_ptr_array_add (out, g_strdup (parts[i]));
		}
	}
	g_ptr_array_add (out, NULL);
	return (char **) g_ptr_array_free (out, FALSE);
}
