/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "spell.h"

#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <libspelling.h>
#include <sys/wait.h>
#include <unistd.h>

#include "debug.h"
#include "ui-misc.h"

namespace
{

gboolean spell_backend_is_usable()
{
	pid_t pid = fork();

	if (pid == -1)
		{
		log_printf("Warning: spell check disabled; failed to probe spelling backend\n");
		return FALSE;
		}

	if (pid == 0)
		{
		const int null_fd = open("/dev/null", O_WRONLY);
		if (null_fd != -1)
			{
			dup2(null_fd, STDERR_FILENO);
			close(null_fd);
			}

		spelling_init();
		SpellingChecker *checker = spelling_checker_get_default();
		gchar **corrections = spelling_checker_list_corrections(checker, "teh");
		g_strfreev(corrections);

		_exit(EXIT_SUCCESS);
		}

	int status;
	while (waitpid(pid, &status, 0) == -1)
		{
		if (errno != EINTR)
			{
			log_printf("Warning: spell check disabled; failed to probe spelling backend\n");
			return FALSE;
			}
		}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS)
		{
		log_printf("Warning: spell check disabled; spelling backend failed correction probe\n");
		return FALSE;
		}

	return TRUE;
}

} // namespace

void spell_text_view_enable(GtkTextView *text_view)
{
	static gsize initialized = 0;

	if (g_once_init_enter(&initialized))
		{
		gsize usable = spell_backend_is_usable() ? 1 : 2;
		if (usable == 1)
			{
			spelling_init();
			}
		g_once_init_leave(&initialized, usable);
		}

	if (initialized != 1) return;

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
	if (!GTK_SOURCE_IS_BUFFER(buffer))
		{
		GtkSourceBuffer *source_buffer = gtk_source_buffer_new(nullptr);

		g_autofree gchar *text = text_buffer_get_text(buffer, TRUE);
		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(source_buffer), text, -1);
		gtk_text_view_set_buffer(text_view, GTK_TEXT_BUFFER(source_buffer));
		g_object_unref(source_buffer);

		buffer = gtk_text_view_get_buffer(text_view);
		}

	SpellingChecker *checker = spelling_checker_get_default();
	SpellingTextBufferAdapter *adapter = spelling_text_buffer_adapter_new(GTK_SOURCE_BUFFER(buffer), checker);

	spelling_text_buffer_adapter_set_enabled(adapter, TRUE);
	gtk_widget_insert_action_group(GTK_WIDGET(text_view), "spelling", G_ACTION_GROUP(adapter));
	gtk_text_view_set_extra_menu(text_view, spelling_text_buffer_adapter_get_menu_model(adapter));

	g_object_set_data_full(G_OBJECT(buffer), "geeqie-spelling-adapter", adapter, g_object_unref);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
