/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The service's kernel interfaces: running ip/awg, creating, labelling and
 * deleting awg-nmN, and removing the ones a crashed service left behind.
 */
#include "iface.h"

#include <NetworkManager.h>

#include <net/if.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Interfaces we create are named IFNAME_PREFIX<N> and labelled (ifalias)
 * OWNER_TAG<pid> start=<start time> of the service that owns them. */
#define IFNAME_PREFIX AWG_IFNAME_PREFIX
#define OWNER_TAG "nm-amneziawg pid="
/* /proc/<pid>/comm: the executable name cut to 15 characters. */
#define SERVICE_COMM "nm-amneziawg-se"

static const char *
find_bin (const char *const *candidates)
{
	for (; *candidates; candidates++) {
		if (g_file_test (*candidates, G_FILE_TEST_IS_EXECUTABLE))
			return *candidates;
	}
	return NULL;
}

static const char *
ip_bin (void)
{
	static const char *const c[] = { "/usr/sbin/ip", "/usr/bin/ip", "/sbin/ip", "/bin/ip", NULL };
	return find_bin (c);
}

static const char *
awg_bin (void)
{
	static const char *const c[] = { "/usr/bin/awg", "/usr/local/bin/awg", "/usr/sbin/awg", NULL };
	return find_bin (c);
}

/* awg quotes the offending config text in its errors, e.g.
 * "Key is not the correct length or format: `<the key>'". That text can be
 * a private key: hide everything between ` and ' before it reaches the log. */
char *
awg_redact_quoted (const char *text)
{
	GString *out = g_string_new (NULL);
	const char *p = text;

	while (*p) {
		const char *open = strchr (p, '`'), *close = open ? strchr (open + 1, '\'') : NULL;

		if (!close) {
			g_string_append (out, p);
			break;
		}
		g_string_append_len (out, p, open - p);
		g_string_append (out, "`(hidden)'");
		p = close + 1;
	}
	return g_string_free (out, FALSE);
}

/* Runs argv; stdin_data (may contain secrets) is fed via a pipe, never argv,
 * and quoted text from the error output is hidden (see awg_redact_quoted). */
gboolean
awg_run (const char *const *argv, const char *stdin_data, GError **error)
{
	g_autoptr (GSubprocess) proc = NULL;
	g_autofree char *err_out = NULL;
	GSubprocessFlags flags = G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_PIPE;

	if (!argv[0]) {
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             "required program not found (is amneziawg-tools / iproute2 installed?)");
		return FALSE;
	}
	if (stdin_data)
		flags |= G_SUBPROCESS_FLAGS_STDIN_PIPE;

	proc = g_subprocess_newv (argv, flags, error);
	if (!proc)
		return FALSE;
	if (!g_subprocess_communicate_utf8 (proc, stdin_data, NULL, NULL, &err_out, error))
		return FALSE;
	if (!g_subprocess_get_successful (proc)) {
		g_autofree char *cmd = g_strjoinv (" ", (char **) argv);
		g_autofree char *msg = NULL;

		if (err_out)
			g_strstrip (err_out);
		msg = stdin_data && err_out ? awg_redact_quoted (err_out) : g_strdup (err_out ? err_out : "");
		g_set_error (error, NM_VPN_PLUGIN_ERROR, NM_VPN_PLUGIN_ERROR_LAUNCH_FAILED,
		             "'%s' failed: %s", cmd, msg);
		return FALSE;
	}
	return TRUE;
}

static char *
read_sys (const char *dir, const char *name, const char *file)
{
	g_autofree char *path = g_build_filename (dir, name, file, NULL);
	char *text = NULL;

	if (!g_file_get_contents (path, &text, NULL, NULL))
		return NULL;
	return g_strstrip (text);
}

/* Start time of a process (field 22 of /proc/<pid>/stat, clock ticks since
 * boot): together with the PID it names one process, even after PID reuse. */
static guint64
pid_start_time (long pid)
{
	g_autofree char *path = g_strdup_printf ("/proc/%ld/stat", pid);
	g_autofree char *stat = NULL;
	g_auto (GStrv) fields = NULL;
	const char *p;

	if (!g_file_get_contents (path, &stat, NULL, NULL))
		return 0;
	/* The command name (field 2) may contain spaces: skip past its ')'. */
	p = strrchr (stat, ')');
	if (!p)
		return 0;
	fields = g_strsplit (p + 2, " ", 0);
	/* fields[0] is field 3 (state), so field 22 is fields[19]. */
	return g_strv_length (fields) > 19 ? g_ascii_strtoull (fields[19], NULL, 10) : 0;
}

/* A running nm-amneziawg-service. Only root can run one (NM starts it), so a
 * process of another user with the same name does not count. */
static gboolean
pid_is_service (long pid)
{
	g_autofree char *path = g_strdup_printf ("/proc/%ld", pid);
	g_autofree char *comm_path = g_strdup_printf ("/proc/%ld/comm", pid);
	g_autofree char *comm = NULL;
	struct stat st;

	if (stat (path, &st) != 0 || st.st_uid != 0)
		return FALSE;
	if (!g_file_get_contents (comm_path, &comm, NULL, NULL))
		return FALSE;
	return g_str_equal (g_strstrip (comm), SERVICE_COMM);
}

/* The owner recorded in an interface label: "pid=N" or "pid=N start=T". */
static gboolean
owner_alive (const char *label)
{
	const char *start;
	char *end;
	long pid = strtol (label, &end, 10);

	if (pid <= 0 || !pid_is_service (pid))
		return FALSE;
	start = strstr (end, " start=");
	return !start || g_ascii_strtoull (start + 7, NULL, 10) == pid_start_time (pid);
}

/* Another nm-amneziawg-service (another active connection) is running. */
static gboolean
other_service_running (void)
{
	g_autoptr (GDir) proc = g_dir_open ("/proc", 0, NULL);
	const char *name;

	while (proc && (name = g_dir_read_name (proc))) {
		char *end;
		long pid = strtol (name, &end, 10);

		if (!*end && pid > 0 && pid != (long) getpid () && pid_is_service (pid))
			return TRUE;
	}
	return FALSE;
}

/* A service killed hard (SIGKILL, crash, OOM) cannot delete its interface.
 * The leftover keeps its peer and keeps talking to the server with the same
 * key, so the server now and then switches to it and sends our traffic
 * there. Remove such interfaces before connecting: those whose owner is
 * gone, and unlabelled ones (made by 0.3.5 and older) when no other service
 * is running, so another active connection is never touched. */
void
awg_iface_remove_stale (void)
{
	const char *sys = "/sys/class/net";
	g_autoptr (GDir) dir = g_dir_open (sys, 0, NULL);
	const char *name;
	int others = -1; /* not checked yet */

	while (dir && (name = g_dir_read_name (dir))) {
		g_autofree char *uevent = NULL, *alias = NULL;
		gboolean stale;
		long owner = 0;

		if (!g_str_has_prefix (name, IFNAME_PREFIX))
			continue;
		uevent = read_sys (sys, name, "uevent");
		if (!uevent || !strstr (uevent, "DEVTYPE=amneziawg"))
			continue;
		alias = read_sys (sys, name, "ifalias");
		if (alias && g_str_has_prefix (alias, OWNER_TAG)) {
			owner = strtol (alias + strlen (OWNER_TAG), NULL, 10);
			stale = owner != (long) getpid () && !owner_alive (alias + strlen (OWNER_TAG));
		} else if (!alias || !*alias) {
			if (others < 0)
				others = other_service_running ();
			stale = !others;
		} else
			stale = FALSE; /* relabelled by someone: not ours to judge */
		if (!stale)
			continue;

		g_message ("removing %s left behind by a service that did not exit cleanly%s", name,
		           owner ? "" : " (unlabelled)");
		{
			g_autoptr (GError) error = NULL;
			const char *argv[] = { ip_bin (), "link", "del", "dev", name, NULL };
			if (!awg_run (argv, NULL, &error))
				g_message ("could not remove %s: %s", name, error->message);
		}
	}
}

char *
awg_iface_pick_name (void)
{
	guint i;

	for (i = 0; i < 100; i++) {
		char *name = g_strdup_printf (IFNAME_PREFIX "%u", i);
		if (if_nametoindex (name) == 0)
			return name;
		g_free (name);
	}
	return NULL;
}

gboolean
awg_iface_create (const char *ifname, GError **error)
{
	const char *add[] = { ip_bin (), "link", "add", "dev", ifname, "type", "amneziawg", NULL };
	g_autofree char *tag = NULL;
	g_autoptr (GError) err = NULL;

	if (!awg_run (add, NULL, error)) {
		g_prefix_error (error, "is the amneziawg kernel module installed? ");
		return FALSE;
	}
	tag = g_strdup_printf (OWNER_TAG "%ld start=%" G_GUINT64_FORMAT, (long) getpid (), pid_start_time (getpid ()));
	{
		const char *label[] = { ip_bin (), "link", "set", "dev", ifname, "alias", tag, NULL };
		if (!awg_run (label, NULL, &err))
			g_message ("could not label %s: %s", ifname, err->message);
	}
	return TRUE;
}

gboolean
awg_iface_configure (const char *ifname, const char *setconf, guint32 mtu, GError **error)
{
	const char *conf[] = { awg_bin (), "setconf", ifname, "/dev/stdin", NULL };
	char mtu_buf[16];
	const char *up[] = { ip_bin (), "link", "set", "dev", ifname, "mtu", mtu_buf, "up", NULL };

	g_snprintf (mtu_buf, sizeof mtu_buf, "%u", mtu);
	return awg_run (conf, setconf, error) && awg_run (up, NULL, error);
}

void
awg_iface_delete (const char *ifname)
{
	const char *argv[] = { ip_bin (), "link", "del", "dev", ifname, NULL };
	g_autoptr (GError) error = NULL;

	if (!awg_run (argv, NULL, &error))
		g_message ("cleanup of %s: %s", ifname, error->message);
}
