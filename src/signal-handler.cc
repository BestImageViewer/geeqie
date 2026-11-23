/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "signal-handler.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>

#include <config.h>

#include <gtk/gtk.h>

#include "diagnostic-report.h"
#include "intl.h"
#include "main-defines.h"
#include "ui-file-chooser.h"
#include "ui-utildlg.h"

#if defined(DEBUG) && HAVE_EXECINFO_H
#include <execinfo.h>
#endif

namespace
{

int report_fd = -1;
std::atomic<int> snapshot_fd{-1};
std::atomic<bool> crash_started{false};
static_assert(std::atomic<int>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);
char report_path[] = "/tmp/geeqie-crash-XXXXXX.txt"; // NOTRANSLATE: Crash report filename.
#if defined(DEBUG) && HAVE_EXECINFO_H
bool backtrace_enabled = false;
#endif

void remove_unused_report()
{
	if (report_fd < 0) return;
	close(report_fd);
	report_fd = -1;
	unlink(report_path);
	// The OS closes the final anonymous snapshot at exit. Retain its descriptor
	// in case another thread faults while normal exit callbacks are running.
}

void write_error(const char *text)
{
	// Diagnostics are best effort; do not enter buffered I/O or application logging.
	[[maybe_unused]] const auto written = write(STDERR_FILENO, text, strlen(text));
	if (report_fd >= 0)
		{
		[[maybe_unused]] const auto saved = write(report_fd, text, strlen(text));
		}
}

const char *signal_name(int signo)
{
	switch (signo)
		{
		case SIGABRT: return "Abort"; // NOTRANSLATE: Fatal-signal diagnostic.
		case SIGBUS: return "Bus error"; // NOTRANSLATE: Fatal-signal diagnostic.
		case SIGFPE: return "Floating-point exception"; // NOTRANSLATE: Fatal-signal diagnostic.
		case SIGILL: return "Illegal instruction"; // NOTRANSLATE: Fatal-signal diagnostic.
		case SIGSEGV: return "Invalid memory reference"; // NOTRANSLATE: Fatal-signal diagnostic.
		default: return "Unknown signal"; // NOTRANSLATE: Fatal-signal diagnostic.
		}
}

void fatal_signal_cb(int signo, siginfo_t *info, void *)
{
	crash_started.store(true);
	// The descriptor refers to immutable data. The publisher retains any old
	// descriptor once a crash starts, including a crash on another thread.
	const int snapshot = snapshot_fd.load();
	if (snapshot >= 0)
		{
		char buffer[1024];
		off_t offset = 0;
		ssize_t count;
		while ((count = pread(snapshot, buffer, sizeof(buffer), offset)) > 0)
			{
			[[maybe_unused]] const auto shown = write(STDERR_FILENO, buffer, count);
			if (report_fd >= 0)
				{
				[[maybe_unused]] const auto saved = write(report_fd, buffer, count);
				}
			offset += count;
			}
		}
	write_error("Geeqie fatal error\nSignal: "); // NOTRANSLATE: Fatal-signal diagnostic.
	write_error(signal_name(signo));
	write_error("\n");
	// User-generated signals do not carry a fault address in si_addr.
	if (info && info->si_code > 0 && signo != SIGABRT)
		{
		if (signo == SIGSEGV)
			{
			if (info->si_code == SEGV_MAPERR) write_error("Code: Address not mapped\n"); // NOTRANSLATE: Fatal-signal diagnostic.
			else if (info->si_code == SEGV_ACCERR) write_error("Code: Invalid permissions\n"); // NOTRANSLATE: Fatal-signal diagnostic.
			}
		write_error("Address: 0x"); // NOTRANSLATE: Fatal-signal diagnostic.
		constexpr char digits[] = "0123456789abcdef"; // NOTRANSLATE: Hexadecimal digits.
		char address[(sizeof(uintptr_t) * 2) + 1]{};
		auto value = reinterpret_cast<uintptr_t>(info->si_addr);
		for (size_t i = sizeof(address) - 1; i > 0; --i)
			{
			address[i - 1] = digits[value & 0xf];
			value >>= 4;
			}
		write_error(address);
		write_error("\n");
		}
	if (report_fd >= 0)
		{
		write_error("Crash report: "); // NOTRANSLATE: Fatal-signal diagnostic.
		write_error(report_path);
		write_error("\n");
		}

#if defined(DEBUG) && HAVE_EXECINFO_H
	if (backtrace_enabled)
		{
		write_error("Best-effort developer backtrace (not signal-safe):\n"); // NOTRANSLATE: Developer diagnostic.
		void *frames[64];
		const int count = backtrace(frames, 64);
		backtrace_symbols_fd(frames, count, STDERR_FILENO);
		if (report_fd >= 0) backtrace_symbols_fd(frames, count, report_fd);
		}
#endif

	// exit(), logging, and stack unwinding can run allocators or destructors
	// concurrently with interrupted application code. Leave cleanup to the OS.
	// Re-deliver the original signal to this thread, preserving core-dump behavior.
	struct sigaction action{};
	action.sa_handler = SIG_DFL;
	sigemptyset(&action.sa_mask);
	if (sigaction(signo, &action, nullptr) == 0)
		{
		sigset_t unblocked;
		sigemptyset(&unblocked);
		sigaddset(&unblocked, signo);
		if (pthread_sigmask(SIG_UNBLOCK, &unblocked, nullptr) == 0) raise(signo);
		}
	_exit(128 + signo);
}

} // namespace

const char *signal_handler_report_path()
{
	return report_fd >= 0 ? report_path : nullptr;
}

bool signal_handler_set_snapshot(const std::string &snapshot)
{
	if (crash_started.load()) return false;
	g_autofree gchar *path = nullptr;
	const int fd = g_file_open_tmp("geeqie-settings-XXXXXX", &path, nullptr); // NOTRANSLATE: Anonymous snapshot filename.
	if (fd < 0) return false;
	if (unlink(path) != 0)
		{
		close(fd);
		return false;
		}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	size_t offset = 0;
	while (offset < snapshot.size())
		{
		const auto count = write(fd, snapshot.data() + offset, snapshot.size() - offset);
		if (count <= 0)
			{
			close(fd);
			return false;
			}
		offset += count;
		}
	const int previous = snapshot_fd.exchange(fd);
	if (previous >= 0 && !crash_started.load()) close(previous);
	return true;
}

void setup_sig_handler()
{
	if (report_fd < 0)
		{
		report_fd = mkstemps(report_path, 4);
		if (report_fd >= 0)
			{
			fcntl(report_fd, F_SETFD, FD_CLOEXEC);
			// Other instances must not offer a report that is still being written.
			flock(report_fd, LOCK_EX | LOCK_NB);
			std::atexit(remove_unused_report);
			dprintf(report_fd, "Geeqie crash report\n"); // NOTRANSLATE: Report signature.

			}
		}
#if defined(DEBUG) && HAVE_EXECINFO_H
	backtrace_enabled = g_strcmp0(std::getenv("GQ_CRASH_BACKTRACE"), "1") == 0; // NOTRANSLATE: Developer opt-in variable.
	if (backtrace_enabled)
		{
		// Resolve the unwinder before a crash; this does not make unwinding signal-safe.
		void *frames[1];
		backtrace(frames, 1);
		}
#endif
	struct sigaction action{};
	sigfillset(&action.sa_mask);
	action.sa_sigaction = fatal_signal_cb;
	action.sa_flags = SA_SIGINFO | SA_RESETHAND;
	for (int signo : {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV})
		{
		sigaction(signo, &action, nullptr);
		}
	diagnostic_report_refresh();
	static guint refresh_id = 0;
	if (!refresh_id)
		refresh_id = g_timeout_add_seconds(1, +[](gpointer) -> gboolean
			{
			diagnostic_report_refresh();
			return G_SOURCE_CONTINUE;
			}, nullptr);

}

namespace
{

constexpr const char acknowledged[] = "\nCrash notification acknowledged\n"; // NOTRANSLATE: Persistent report marker.

int pending_report_open(const char *path)
{
	const int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return -1;
	struct stat status{};
	char contents[65537]{};
	if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_uid != getuid() || status.st_size > 65536 ||
	    flock(fd, LOCK_EX | LOCK_NB) != 0)
		{
		close(fd);
		return -1;
		}
	const auto count = read(fd, contents, sizeof(contents) - 1);
	if (count <= 0 || !g_str_has_prefix(contents, "Geeqie ") || // NOTRANSLATE: Report signature.
	    !strstr(contents, "Geeqie fatal error\n") || strstr(contents, acknowledged)) // NOTRANSLATE: Fatal report marker.
		{
		close(fd);
		return -1;
		}
	return fd;
}

struct CrashNotification
{
	std::vector<std::string> paths;
	GtkWidget *selection;
	[[nodiscard]] const char *selected() const
		{
		return paths.at(gtk_drop_down_get_selected(GTK_DROP_DOWN(selection))).c_str();
		}
};

void report_save_selected(GFile *destination, gpointer data)
{
	g_autoptr(GFile) source = G_FILE(data);
	if (!destination) return;
	g_autoptr(GError) error = nullptr;
	if (!g_file_copy(source, destination, G_FILE_COPY_OVERWRITE, nullptr, nullptr, nullptr, &error))
		warning_dialog(_("Unable to save crash report"), error->message, GQ_ICON_DIALOG_ERROR, nullptr);
}

} // namespace

std::vector<std::string> crash_reports_pending(const char *directory)
{
	std::vector<std::string> paths;
	g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
	if (!dir) return paths;
	while (const char *name = g_dir_read_name(dir))
		{
		if (!g_str_has_prefix(name, "geeqie-crash-") || !g_str_has_suffix(name, ".txt") || strlen(name) != 23) continue; // NOTRANSLATE: Report filename pattern.
		g_autofree gchar *path = g_build_filename(directory, name, nullptr);
		const int fd = pending_report_open(path);
		if (fd < 0) continue;
		paths.emplace_back(path);
		close(fd);
		}
	return paths;
}

void crash_report_notification_show(GtkWindow *parent, const char *directory)
{
	auto notification = std::make_unique<CrashNotification>();
	for (const auto &path : crash_reports_pending(directory))
		{
		// Claim notification under the same lock used by report writers and other instances.
		const int fd = pending_report_open(path.c_str());
		if (fd < 0) continue;
		if (lseek(fd, 0, SEEK_END) >= 0 && write(fd, acknowledged, strlen(acknowledged)) == static_cast<ssize_t>(strlen(acknowledged)))
			notification->paths.push_back(path);
		close(fd);
		}
	if (notification->paths.empty()) return;
	auto *dialog = generic_dialog_new(_("Crash report"), "crash-report", GTK_WIDGET(parent), FALSE, // NOTRANSLATE: Dialog role.
		+[](GenericDialog *gd, gpointer) { generic_dialog_close(gd); }, nullptr);
	// Ignore a size saved before the explanatory text was constrained.
	gtk_window_set_default_size(GTK_WINDOW(dialog->dialog), 560, -1);
	gtk_window_set_modal(GTK_WINDOW(dialog->dialog), TRUE);
	auto *message = generic_dialog_add_message(dialog, GQ_ICON_DIALOG_WARNING, _("Geeqie ended unexpectedly"),
	                           _("Diagnostic reports from a previous crash are available below. You can open a report or save a copy to include with a bug report. Files in /tmp may be removed when your computer restarts."), FALSE);
	auto *explanation = gtk_widget_get_last_child(message);
	gtk_label_set_max_width_chars(GTK_LABEL(explanation), 60);
	g_autoptr(GtkStringList) names = gtk_string_list_new(nullptr);
	for (const auto &path : notification->paths) gtk_string_list_append(names, path.c_str());
	notification->selection = gtk_drop_down_new(G_LIST_MODEL(g_object_ref(names)), nullptr);
	gtk_widget_set_tooltip_text(notification->selection, _("Crash report file"));
	gtk_box_append(GTK_BOX(dialog->vbox), notification->selection);
	dialog->data = notification.get();
	generic_dialog_add_button(dialog, nullptr, _("Open report"), +[](GenericDialog *gd, gpointer data)
		{
		const auto *pending = static_cast<CrashNotification *>(data);
		g_autofree gchar *uri = g_filename_to_uri(pending->selected(), nullptr, nullptr);
		g_autoptr(GError) error = nullptr;
		if (!g_app_info_launch_default_for_uri(uri, nullptr, &error))
			warning_dialog(_("Unable to open crash report"), error->message, GQ_ICON_DIALOG_ERROR, gd->dialog);
		}, FALSE);
	generic_dialog_add_button(dialog, nullptr, _("Save a copy…"), +[](GenericDialog *gd, gpointer data)
		{
		const auto *pending = static_cast<CrashNotification *>(data);
		FileDialogData chooser{};
		chooser.action = FileDialogAction::SAVE;
		chooser.title = _("Save crash report");
		chooser.suggested_name = "geeqie-crash.txt"; // NOTRANSLATE: Suggested filename.
		chooser.parent = GTK_WINDOW(gd->dialog);
		chooser.callback = report_save_selected;
		chooser.data = g_file_new_for_path(pending->selected());
		file_dialog_show(chooser);
		}, FALSE);
	generic_dialog_add_button(dialog, nullptr, _("Dismiss"),
		+[](GenericDialog *gd, gpointer) { generic_dialog_close(gd); }, TRUE);
	g_object_set_data_full(G_OBJECT(dialog->dialog), "crash-notification", notification.release(), // NOTRANSLATE: Owned dialog state.
		+[](gpointer data) { delete static_cast<CrashNotification *>(data); });
	gtk_window_present(GTK_WINDOW(dialog->dialog));
}
