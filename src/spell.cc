/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "spell.h"

#include <cerrno>
#include <csignal>
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
		signal(SIGABRT, SIG_DFL);

		const int null_fd = open("/dev/null", O_WRONLY);
		if (null_fd != -1)
			{
			dup2(null_fd, STDOUT_FILENO);
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

void spell_text_view_menu_cb(GtkGestureClick *gesture, gint, gdouble x, gdouble y, gpointer data)
{
	auto *text_view = GTK_TEXT_VIEW(data);
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
	auto *adapter = static_cast<SpellingTextBufferAdapter *>(g_object_get_data(G_OBJECT(buffer), "geeqie-spelling-adapter"));

	if (!adapter) return;

	gint buffer_x;
	gint buffer_y;
	gtk_text_view_window_to_buffer_coords(text_view, GTK_TEXT_WINDOW_WIDGET, x, y, &buffer_x, &buffer_y);

	GtkTextIter iter;
	gint trailing;
	if (gtk_text_view_get_iter_at_position(text_view, &iter, &trailing, buffer_x, buffer_y))
		{
		if (trailing > 0 && !gtk_text_iter_ends_word(&iter))
			{
			gtk_text_iter_forward_char(&iter);
			}

		if (!gtk_text_iter_starts_word(&iter) && !gtk_text_iter_inside_word(&iter) && gtk_text_iter_ends_word(&iter))
			{
			gtk_text_iter_backward_char(&iter);
			}
		else if (!gtk_text_iter_starts_word(&iter) && !gtk_text_iter_inside_word(&iter))
			{
			GtkTextIter next = iter;
			if (gtk_text_iter_forward_char(&next) && gtk_text_iter_inside_word(&next))
				{
				iter = next;
				}
			else
				{
				GtkTextIter previous = iter;
				if (gtk_text_iter_backward_char(&previous) && gtk_text_iter_inside_word(&previous))
					{
					iter = previous;
					}
				}
			}

		GtkTextIter selection_start;
		GtkTextIter selection_end;
		if (!gtk_text_buffer_get_selection_bounds(buffer, &selection_start, &selection_end) ||
		    !gtk_text_iter_in_range(&iter, &selection_start, &selection_end))
			{
			gtk_text_buffer_place_cursor(buffer, &iter);
			}
		}

	spelling_text_buffer_adapter_update_corrections(adapter);
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
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

	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
	g_signal_connect(gesture, "pressed", G_CALLBACK(spell_text_view_menu_cb), text_view);
	gtk_widget_add_controller(GTK_WIDGET(text_view), GTK_EVENT_CONTROLLER(gesture));
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
