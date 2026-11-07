/* awg_check_ipacket() against the kernel's verdicts in tests/data/ipacket-cases.txt.
 * usage: test-ipacket CASES_FILE */
#include "awg-common.h"
#include "check.h"

#include <string.h>

int
main (int argc, char **argv)
{
	g_autofree char *text = NULL;
	g_auto (GStrv) lines = NULL;
	g_autoptr (GError) error = NULL;
	guint n = 0;

	if (argc != 2 || !g_file_get_contents (argv[1], &text, NULL, &error)) {
		fprintf (stderr, "usage: %s CASES_FILE (%s)\n", argv[0], error ? error->message : "");
		return 2;
	}

	lines = g_strsplit (text, "\n", -1);
	for (guint i = 0; lines[i]; i++) {
		const char *tab = strchr (lines[i], '\t');
		g_autofree char *reason = NULL, *name = NULL;
		gboolean want, got;

		if (!*lines[i] || lines[i][0] == '#')
			continue;
		if (!tab) {
			fprintf (stderr, "line %u: no tab\n", i + 1);
			return 2;
		}
		want = g_str_has_prefix (lines[i], "OK\t");
		got = awg_check_ipacket (tab + 1, &reason);
		name = g_strdup_printf ("%-3s %-30s %s", want ? "OK" : "ERR", tab + 1, reason ? reason : "");
		CHECK (name, got == want);
		/* Every refusal must say why: that is the point of the check. */
		if (!got)
			CHECK ("  ... with a reason", reason && *reason);
		n++;
	}
	CHECK ("cases file is not empty", n > 20);

	/* Not in the file: values the form and the service pass around. */
	CHECK ("NULL (key not set) is valid", awg_check_ipacket (NULL, NULL));
	CHECK ("empty string is valid", awg_check_ipacket ("", NULL));
	return test_finish ();
}
