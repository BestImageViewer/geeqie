/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "main.h"
#include "logwindow.h"

#include "misc.h"
#include "ui-misc.h"
#include "window.h"

struct LogWindow
{
	GtkWidget *window;
	GtkWidget *scrolledwin;
	GtkWidget *text;
	GtkTextTag *color_tags[LOG_COUNT];
	gint lines;
	GtkWidget *regexp_box;
	GtkWidget *bar;
	GtkWidget *pause;
	GtkWidget *wrap;
	GtkWidget *timer_data;
	GtkWidget *debug_level;
	gint debug_value; /**< Not used */
	GtkWidget *search_entry_box;
	gboolean highlight_all;
};

enum LogWindowSearchDirection {
	LOGWINDOW_SEARCH_BACKWARDS,
	LOGWINDOW_SEARCH_FORWARDS
};

static LogWindow *logwindow = nullptr;

static void hide_cb(GtkWidget *UNUSED(widget), LogWindow *UNUSED(logwin))
{
}

static gboolean iter_char_search_cb(gunichar ch, gpointer data)
{
	gboolean ret;

	if (ch == GPOINTER_TO_UINT(data))
		{
		ret = TRUE;
		}
	else
		{
		ret = FALSE;
		}

	return ret;
}

/**
 * @brief Handle escape and F1 keys
 * @param UNUSED
 * @param event
 * @param logwin
 * @returns
 *
 * If escape key pressed, hide log window. \n
 * If no text selected, form a selection bounded by space characters or
 * start and end of line. \n
 * If F1 pressed, execute command line program: \n
 * <options->log_window.action> <selected text>
 *
*/
static gboolean key_pressed(GtkWidget *UNUSED(widget), GdkEventKey *event, LogWindow *logwin)
{
	GtkTextBuffer *buffer;
	GtkTextIter chr_end;
	GtkTextIter chr_start;
	GtkTextIter cursor_iter;
	GtkTextIter line_end;
	GtkTextIter line_start;
	GtkTextMark *cursor_mark;
	gchar *cmd_line;
	gchar *sel_text;

	if (event && event->keyval == GDK_KEY_Escape)
		gtk_widget_hide(logwin->window);

	if (event && event->keyval == GDK_KEY_F1)
		{
		if (strlen(options->log_window.action) > 0)
			{
			buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));

			if (!gtk_text_buffer_get_has_selection(buffer))
				{
				cursor_mark = gtk_text_buffer_get_insert(buffer);
				gtk_text_buffer_get_iter_at_mark(buffer, &cursor_iter, cursor_mark);

				line_start = cursor_iter;
				gtk_text_iter_set_line_offset(&line_start, 0);
				line_end = cursor_iter;
				gtk_text_iter_forward_to_line_end(&line_end);

				chr_start = cursor_iter;
				gtk_text_iter_backward_find_char(&chr_start, static_cast<GtkTextCharPredicate>(iter_char_search_cb), GUINT_TO_POINTER(' '), &line_start);

				chr_end = cursor_iter;
				gtk_text_iter_forward_find_char(&chr_end, static_cast<GtkTextCharPredicate>(iter_char_search_cb), GUINT_TO_POINTER(' '), &line_end);

				gtk_text_buffer_select_range(buffer, &chr_start, &chr_end);
				}

			if (gtk_text_buffer_get_selection_bounds(gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text)), &chr_start, &chr_end))
				{
				sel_text = gtk_text_buffer_get_text( gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text)), &chr_start, &chr_end, FALSE);

				cmd_line = g_strconcat(options->log_window.action, " ", sel_text, NULL);

				runcmd(cmd_line);

				g_free(sel_text);
				g_free(cmd_line);
				}
			}
		}

	return FALSE;
}


static void log_window_pause_cb(GtkWidget *UNUSED(widget), gpointer UNUSED(data))
{
	options->log_window.paused = !options->log_window.paused;
}

static void log_window_line_wrap_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	auto logwin = static_cast<LogWindow *>(data);

	options->log_window.line_wrap = !options->log_window.line_wrap;

	if (options->log_window.line_wrap)
		{
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logwin->text), GTK_WRAP_WORD);
		}
	else
		{
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(logwin->text), GTK_WRAP_NONE);
		}
}

static void log_window_timer_data_cb(GtkWidget *UNUSED(widget), gpointer UNUSED(data))
{
	options->log_window.timer_data = !options->log_window.timer_data;
}

static void log_window_regexp_cb(GtkWidget *text_entry, gpointer UNUSED(data))
{
	gchar *new_regexp;

	new_regexp = g_strdup(gtk_entry_get_text(GTK_ENTRY(text_entry)));
	set_regexp(new_regexp);
	g_free(new_regexp);
}

static void remove_green_bg(LogWindow *logwin)
{
	GtkTextIter start_find;
	GtkTextIter start_match;
	GtkTextIter end_match;
	GtkTextBuffer *buffer;
	GSList *list;
	const gchar *text;
	gchar *tag_name;
	gint offset;

	text = gtk_entry_get_text(GTK_ENTRY(logwin->search_entry_box));
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));
	gtk_text_buffer_get_start_iter(buffer, &start_find);

	while (gtk_text_iter_forward_search(&start_find, text, GTK_TEXT_SEARCH_VISIBLE_ONLY,  &start_match, &end_match, nullptr))
		{
		list = gtk_text_iter_get_tags(&start_match);
		while (list)
			{
			g_object_get(list->data, "name", &tag_name, NULL);
			if (g_strcmp0(tag_name, "green_bg") == 0)
				{
				gtk_text_buffer_remove_tag_by_name(buffer, "green_bg", &start_match, &end_match);
				}
			list = list->next;
			}
		g_slist_free(list);

		offset = gtk_text_iter_get_offset(&end_match);
		gtk_text_buffer_get_iter_at_offset(buffer, &start_find, offset);
		}
}

static void search_activate_event(GtkEntry *UNUSED(widget), LogWindow *logwin)
{
	GtkTextIter start_find;
	GtkTextIter start_match;
	GtkTextIter end_match;
	GtkTextBuffer *buffer;
	GtkTextMark *cursor_mark;
	GtkTextIter cursor_iter;
	const gchar *text;
	gint offset;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));
	text = gtk_entry_get_text(GTK_ENTRY(logwin->search_entry_box));

	if (logwin->highlight_all)
		{
		gtk_text_buffer_get_start_iter(buffer, &start_find);

		while (gtk_text_iter_forward_search(&start_find, text, GTK_TEXT_SEARCH_VISIBLE_ONLY, &start_match, &end_match, nullptr))
			{
			gtk_text_buffer_apply_tag_by_name(buffer, "gray_bg", &start_match, &end_match);
			offset = gtk_text_iter_get_offset(&end_match);
			gtk_text_buffer_get_iter_at_offset(buffer, &start_find, offset);
			}
		}
	else
		{
		cursor_mark = gtk_text_buffer_get_insert(buffer);
		gtk_text_buffer_get_iter_at_mark(buffer, &cursor_iter, cursor_mark);

		if (gtk_text_iter_forward_search(&cursor_iter, text, GTK_TEXT_SEARCH_VISIBLE_ONLY, &start_match, &end_match, nullptr))
			{
			gtk_text_buffer_apply_tag_by_name(buffer, "gray_bg", &start_match, &end_match);
			}
		}
}

static gboolean search_keypress_event(GtkWidget *UNUSED(widget), GdkEventKey *UNUSED(event), LogWindow *logwin, LogWindowSearchDirection direction)
{
	GtkTextIter start_find;
	GtkTextIter start_match;
	GtkTextIter end_match;
	GtkTextIter start_sel;
	GtkTextIter end_sel;
	const gchar *text;
	GtkTextBuffer *buffer;
	GtkTextMark *cursor_mark;
	GtkTextIter cursor_iter;
	gint offset;
	gboolean match_found = FALSE;
	gboolean selected;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));
	gtk_text_buffer_get_start_iter(buffer, &start_find);

	text = gtk_entry_get_text(GTK_ENTRY(logwin->search_entry_box));
	if (strlen(text) == 0)
		{
		selected = gtk_text_buffer_get_selection_bounds(buffer, &start_sel, &end_sel);
		if (selected)
			{
			text = gtk_text_buffer_get_text(buffer, &start_sel, &end_sel, FALSE);
			gtk_entry_set_text(GTK_ENTRY(logwin->search_entry_box), text);
			}
		}

	if (logwin->highlight_all)
		{
		while (gtk_text_iter_forward_search(&start_find, text, GTK_TEXT_SEARCH_VISIBLE_ONLY, &start_match, &end_match, nullptr))
			{
			gtk_text_buffer_apply_tag_by_name(buffer, "gray_bg", &start_match, &end_match);
			offset = gtk_text_iter_get_offset(&end_match);
			gtk_text_buffer_get_iter_at_offset(buffer, &start_find, offset);
			}
		}

	cursor_mark = gtk_text_buffer_get_insert(buffer);
	gtk_text_buffer_get_iter_at_mark(buffer, &cursor_iter, cursor_mark);

	if (direction == LOGWINDOW_SEARCH_BACKWARDS)
		{
		match_found = gtk_text_iter_backward_search( &cursor_iter, text, GTK_TEXT_SEARCH_VISIBLE_ONLY,  &start_match, &end_match, nullptr);
		}
	else
		{
		match_found = gtk_text_iter_forward_search( &cursor_iter, text, GTK_TEXT_SEARCH_VISIBLE_ONLY,  &start_match, &end_match, nullptr);
		}

	if (match_found)
		{
		remove_green_bg(logwin);

		gtk_text_buffer_apply_tag_by_name(buffer, "green_bg",  &start_match, &end_match);

		if (direction == LOGWINDOW_SEARCH_BACKWARDS)
			{
			gtk_text_buffer_place_cursor(buffer, &start_match);
			}
		else
			{
			gtk_text_buffer_place_cursor(buffer, &end_match);
			}

		cursor_mark = gtk_text_buffer_get_insert(buffer);
		gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(logwin->text), cursor_mark, 0.2, FALSE, 0.0, 0.0);
        }

	return FALSE;
}

static gboolean backwards_keypress_event_cb(GtkWidget *widget, GdkEventKey *event, LogWindow *logwin)
{
	search_keypress_event(widget, event, logwin, LOGWINDOW_SEARCH_BACKWARDS);

	return FALSE;
}

static gboolean forwards_keypress_event_cb(GtkWidget *widget, GdkEventKey *event, LogWindow *logwin)
{
	search_keypress_event(widget, event, logwin, LOGWINDOW_SEARCH_FORWARDS);

	return FALSE;
}

static gboolean all_keypress_event_cb(GtkToggleButton *widget, LogWindow *logwin)
{
	logwin->highlight_all = gtk_toggle_button_get_active(widget);

	return FALSE;
}

static gboolean debug_changed_cb(GtkSpinButton *widget, LogWindow *UNUSED(logwin))
{
	set_debug_level((gtk_spin_button_get_value(widget)));

	return FALSE;
}

static void search_entry_icon_cb(GtkEntry *UNUSED(entry), GtkEntryIconPosition pos, GdkEvent *UNUSED(event), gpointer userdata)
{
	auto logwin = static_cast<LogWindow *>(userdata);
	GtkTextIter start_find;
	GtkTextIter end_find;
	GtkTextBuffer *buffer;

	if (pos == GTK_ENTRY_ICON_SECONDARY)
		{
		gtk_entry_set_text(GTK_ENTRY(logwin->search_entry_box), "");

		buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));
		gtk_text_buffer_get_start_iter(buffer, &start_find);
		gtk_text_buffer_get_end_iter(buffer, &end_find);
		gtk_text_buffer_remove_tag_by_name(buffer, "gray_bg", &start_find, &end_find);
		gtk_text_buffer_remove_tag_by_name(buffer, "green_bg", &start_find, &end_find);
		}
}

static void filter_entry_icon_cb(GtkEntry *entry, GtkEntryIconPosition UNUSED(pos), GdkEvent *UNUSED(event), gpointer UNUSED(userdata))
{
	const gchar *blank = "";
	gtk_entry_set_text(entry, blank);
	set_regexp(blank);
}

static LogWindow *log_window_create(LayoutWindow *lw)
{
	LogWindow *logwin;
	GtkWidget *window;
	GtkWidget *scrolledwin;
	GtkWidget *text;
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GtkWidget *win_vbox;
	GtkWidget *textbox;
	GtkWidget *hbox;
	GtkWidget *label = nullptr;
	GtkWidget *search_box;
	GtkWidget *backwards_button;
	GtkWidget *forwards_button;
	GtkWidget *all_button;
	GtkIconTheme *theme;
	GdkPixbuf *pixbuf;
	GtkWidget *image = nullptr;

	logwin = g_new0(LogWindow, 1);

	window = window_new(GTK_WINDOW_TOPLEVEL, "log", nullptr, nullptr, _("Log"));
	DEBUG_NAME(window);
	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	gtk_container_add(GTK_CONTAINER(window), win_vbox);
	gtk_widget_show(win_vbox);

	gtk_window_resize(GTK_WINDOW(window), lw->options.log_window.w,
											lw->options.log_window.h);
	gtk_window_move(GTK_WINDOW(window), lw->options.log_window.x, lw->options.log_window.y);

	g_signal_connect(G_OBJECT(window), "delete_event",
			 G_CALLBACK(gtk_widget_hide_on_delete), NULL);
	g_signal_connect(G_OBJECT(window), "key_press_event",
			 G_CALLBACK(key_pressed), logwin);
	g_signal_connect(G_OBJECT(window), "hide",
			 G_CALLBACK(hide_cb), logwin);
	gtk_widget_realize(window);

	scrolledwin = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwin),
				       GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledwin),
					    GTK_SHADOW_IN);

	gtk_box_pack_start(GTK_BOX(win_vbox), scrolledwin, TRUE, TRUE, 0);
	gtk_widget_show(scrolledwin);

	text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
	if (options->log_window.line_wrap)
		{
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
		}
	else
		{
		gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_NONE);
		}
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
	gtk_text_buffer_get_start_iter(buffer, &iter);
	gtk_text_buffer_create_mark(buffer, "end", &iter, FALSE);
	gtk_container_add(GTK_CONTAINER(scrolledwin), text);
	gtk_widget_show(text);

#ifdef DEBUG
	gtk_text_buffer_create_tag(buffer, "gray_bg", "background", "gray", NULL);
	gtk_text_buffer_create_tag(buffer, "green_bg", "background", "#00FF00", NULL);

	hbox = pref_box_new(win_vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	gtk_widget_show(hbox);
	logwin->debug_level = pref_spin_new_int(hbox, _("Debug level:"), nullptr, 0, 4, 1, get_debug_level(), &logwin->debug_value);
	g_signal_connect(logwin->debug_level, "value-changed", G_CALLBACK(debug_changed_cb), logwin);

	logwin->pause = gtk_toggle_button_new();
	label = gtk_label_new("Pause");
	gtk_widget_set_tooltip_text(GTK_WIDGET(logwin->pause), _("Pause scrolling"));
	gtk_container_add(GTK_CONTAINER(logwin->pause), label) ;
	gtk_box_pack_start(GTK_BOX(hbox),logwin->pause, FALSE, FALSE, 0) ;
	g_signal_connect(logwin->pause, "toggled", G_CALLBACK(log_window_pause_cb), logwin);
	gtk_widget_show_all(logwin->pause);

	logwin->wrap = gtk_toggle_button_new();
	label = gtk_label_new("Wrap");
	gtk_widget_set_tooltip_text(GTK_WIDGET(logwin->wrap), _("Enable line wrap"));
	gtk_container_add(GTK_CONTAINER(logwin->wrap), label) ;
	gtk_box_pack_start(GTK_BOX(hbox),logwin->wrap, FALSE, FALSE, 0) ;
	g_signal_connect(logwin->wrap, "toggled", G_CALLBACK(log_window_line_wrap_cb), logwin);
	gtk_widget_show_all(logwin->wrap);

	logwin->timer_data = gtk_toggle_button_new();
	label = gtk_label_new("Timer");
	gtk_widget_set_tooltip_text(GTK_WIDGET(logwin->timer_data), _("Enable timer data"));
	gtk_container_add(GTK_CONTAINER(logwin->timer_data), label) ;
	gtk_box_pack_start(GTK_BOX(hbox),logwin->timer_data, FALSE, FALSE, 0) ;
	if (options->log_window.timer_data)
		{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(logwin->timer_data), TRUE);
		}
	g_signal_connect(logwin->timer_data, "toggled", G_CALLBACK(log_window_timer_data_cb), logwin);
	gtk_widget_show_all(logwin->timer_data);

	search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(hbox), search_box);
	gtk_widget_show(search_box);

	logwin->search_entry_box = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(search_box), logwin->search_entry_box, FALSE, FALSE, 0);
	gtk_widget_show(logwin->search_entry_box);
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(logwin->search_entry_box), GTK_ENTRY_ICON_PRIMARY, "edit-find");
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(logwin->search_entry_box), GTK_ENTRY_ICON_SECONDARY, "edit-clear");
	gtk_widget_show(search_box);
	gtk_widget_set_tooltip_text(logwin->search_entry_box, _("Search for text in log window"));
	g_signal_connect(logwin->search_entry_box, "icon-press", G_CALLBACK(search_entry_icon_cb), logwin);
	g_signal_connect(logwin->search_entry_box, "activate", G_CALLBACK(search_activate_event), logwin);

	theme = gtk_icon_theme_get_default();
	pixbuf = gtk_icon_theme_load_icon(theme, "pan-up-symbolic", 20, GTK_ICON_LOOKUP_GENERIC_FALLBACK, nullptr);
	image = gtk_image_new_from_pixbuf(pixbuf);
	backwards_button = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(backwards_button), GTK_WIDGET(image));
	gtk_widget_set_tooltip_text(backwards_button, _("Search backwards"));
	gtk_box_pack_start(GTK_BOX(search_box), backwards_button, FALSE, FALSE, 0);
	gtk_widget_show(backwards_button);
	g_signal_connect(backwards_button, "button_release_event", G_CALLBACK(backwards_keypress_event_cb), logwin);
	g_object_unref(pixbuf);

	pixbuf = gtk_icon_theme_load_icon(theme, "pan-down-symbolic", 20, GTK_ICON_LOOKUP_GENERIC_FALLBACK, nullptr);
	image = gtk_image_new_from_pixbuf(pixbuf);
	forwards_button = gtk_button_new();
	gtk_button_set_image(GTK_BUTTON(forwards_button), GTK_WIDGET(image));
	gtk_widget_set_tooltip_text(forwards_button, _("Search forwards"));
	gtk_box_pack_start(GTK_BOX(search_box), forwards_button, FALSE, FALSE, 0);
	gtk_widget_show(forwards_button);
	g_signal_connect(forwards_button, "button_release_event", G_CALLBACK(forwards_keypress_event_cb), logwin);
	g_object_unref(pixbuf);

	pixbuf = gtk_icon_theme_load_icon(theme, "edit-select-all-symbolic", 20, GTK_ICON_LOOKUP_GENERIC_FALLBACK, nullptr);
	image = gtk_image_new_from_pixbuf(pixbuf);
	all_button = gtk_toggle_button_new();
	gtk_button_set_image(GTK_BUTTON(all_button), GTK_WIDGET(image));
	gtk_widget_set_tooltip_text(GTK_WIDGET(all_button), _("Highlight all"));
	gtk_box_pack_start(GTK_BOX(search_box), all_button, FALSE, FALSE, 0) ;
	g_signal_connect(all_button, "toggled", G_CALLBACK(all_keypress_event_cb), logwin);
	gtk_widget_show_all(all_button);
	g_object_unref(pixbuf);

	pref_label_new(hbox, _("Filter regexp"));

	textbox = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), textbox, FALSE, FALSE, 0);
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(textbox), GTK_ENTRY_ICON_SECONDARY, "edit-clear");
	gtk_widget_show(textbox);
	g_signal_connect(G_OBJECT(textbox), "activate",
			 G_CALLBACK(log_window_regexp_cb), logwin);
	g_signal_connect(textbox, "icon-press", G_CALLBACK(filter_entry_icon_cb), logwin);
#endif

	logwin->window = window;
	logwin->scrolledwin = scrolledwin;
	logwin->text = text;
	logwin->lines = 1;
	logwin->regexp_box = textbox;
	lw->log_window = logwin->window;
	return logwin;
}

static void log_window_init(LogWindow *logwin)
{
	GtkTextBuffer *buffer;

	g_assert(logwin != nullptr);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));

	logwin->color_tags[LOG_NORMAL] = gtk_text_buffer_create_tag (buffer,
							"black_foreground", "foreground", "black",
							"family", "MonoSpace", NULL);
	logwin->color_tags[LOG_MSG] = gtk_text_buffer_create_tag (buffer,
							"blue_foreground", "foreground", "blue",
							"family", "MonoSpace", NULL);
	logwin->color_tags[LOG_WARN] = gtk_text_buffer_create_tag (buffer,
							"orange_foreground", "foreground", "orange",
							"family", "MonoSpace", NULL);
	logwin->color_tags[LOG_ERROR] = gtk_text_buffer_create_tag (buffer,
							"red_foreground", "foreground", "red",
							"family", "MonoSpace", NULL);
}

static void log_window_show(LogWindow *logwin)
{
	GtkTextView *text = GTK_TEXT_VIEW(logwin->text);
	GtkTextBuffer *buffer;
	GtkTextMark *mark;
	gchar *regexp;

	buffer = gtk_text_view_get_buffer(text);
	mark = gtk_text_buffer_get_mark(buffer, "end");
	gtk_text_view_scroll_mark_onscreen(text, mark);

	gtk_window_present(GTK_WINDOW(logwin->window));

	log_window_append("", LOG_NORMAL); // to flush memorized lines

	regexp = g_strdup(get_regexp());
	if (regexp != nullptr)
		{
		gtk_entry_set_text(GTK_ENTRY(logwin->regexp_box), regexp);
		g_free(regexp);
		}
}

void log_window_new(LayoutWindow *lw)
{
	if (logwindow == nullptr)
		{
		LogWindow *logwin;

		logwin = log_window_create(lw);
		log_window_init(logwin);
		logwindow = logwin;
		}

	log_window_show(logwindow);
}

struct LogMsg {
	gchar *text;
	LogType type;
};

static void log_window_insert_text(GtkTextBuffer *buffer, GtkTextIter *iter,
				   const gchar *text, GtkTextTag *tag)
{
	gchar *str_utf8;

	if (!text || !*text) return;

	str_utf8 = utf8_validate_or_convert(text);
	gtk_text_buffer_insert_with_tags(buffer, iter, str_utf8, -1, tag, NULL);
	g_free(str_utf8);
}

void log_window_append(const gchar *str, LogType type)
{
	GtkTextView *text;
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	static GList *memory = nullptr;

	if (logwindow == nullptr)
		{
		if (*str) {
			auto msg = g_new(LogMsg, 1);

			msg->text = g_strdup(str);
			msg->type = type;

			memory = g_list_prepend(memory, msg);

			while (g_list_length(memory) >= static_cast<guint>(options->log_window_lines))
				{
				GList *work = g_list_last(memory);
				auto oldest_msg = static_cast<LogMsg *>(work->data);

				g_free(oldest_msg->text);
				memory = g_list_delete_link(memory, work);
				}
			}
		return;
		}

	text = GTK_TEXT_VIEW(logwindow->text);
	buffer = gtk_text_view_get_buffer(text);

	if (options->log_window_lines > 0 && logwindow->lines >= options->log_window_lines)
		{
		GtkTextIter start, end;

		gtk_text_buffer_get_start_iter(buffer, &start);
		end = start;
		gtk_text_iter_forward_lines(&end, logwindow->lines - options->log_window_lines);
		gtk_text_buffer_delete(buffer, &start, &end);
		}

	gtk_text_buffer_get_end_iter(buffer, &iter);

	{
	GList *work = g_list_last(memory);

	while (work)
		{
		GList *prev;
		auto oldest_msg = static_cast<LogMsg *>(work->data);

		log_window_insert_text(buffer, &iter, oldest_msg->text,
									logwindow->color_tags[oldest_msg->type]);

		prev = work->prev;
		memory = g_list_delete_link(memory, work);
		work = prev;
		}
	}

	log_window_insert_text(buffer, &iter, str, logwindow->color_tags[type]);

	if (!options->log_window.paused)
		{
		if (gtk_widget_get_visible(GTK_WIDGET(text)))
			{
			GtkTextMark *mark;

			mark = gtk_text_buffer_get_mark(buffer, "end");
			gtk_text_view_scroll_mark_onscreen(text, mark);
			}
		}

	logwindow->lines = gtk_text_buffer_get_line_count(buffer);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
