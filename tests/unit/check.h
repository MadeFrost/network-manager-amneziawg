/* Minimal test helpers: CHECK() prints one line per check, test_finish()
 * returns the exit code. Exit code 77 means "skipped" (Meson, Automake). */
#pragma once

#include <stdio.h>

#define TEST_SKIP 77

static int test_failures;

#define CHECK(name, cond)                                                     \
	do {                                                                      \
		int ok_ = !!(cond);                                                   \
		printf ("%s %s\n", ok_ ? "  ok  " : "  FAIL", name);                  \
		if (!ok_) {                                                           \
			test_failures++;                                                  \
			printf ("        at %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
		}                                                                     \
	} while (0)

static inline int
test_finish (void)
{
	if (test_failures)
		printf ("%d check(s) failed\n", test_failures);
	else
		printf ("all checks passed\n");
	return test_failures ? 1 : 0;
}
