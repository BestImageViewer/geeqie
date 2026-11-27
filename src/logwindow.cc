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

#include "logwindow.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <config.h>

#include "accelerators.h"
#include "actions.h"
#include "diagnostic-report.h"
#include "intl.h"
#include "layout.h"
#include "log-capture.h"
#ifdef DEBUG
#  include "main-defines.h"
#endif
#include "misc.h"
#include "options.h"
#include "ui-misc.h"
#include "ui-utildlg.h"
#include "window.h"

struct LogWindow
{
	GtkWidget *window;
	GtkWidget *text;
	GtkTextTag *color_tags[LOG_COUNT];
#ifdef DEBUG
	enum SearchDirection {
		SEARCH_BACKWARDS,
		SEARCH_FORWARDS
	};

	GtkWidget *regexp_box;
	GtkWidget *search_entry_box;
	gboolean highlight_all;
	GtkWidget *level_spin;
	GtkWidget *source_button;
	GtkWidget *delta_button;
	GtkWidget *domain_checks[static_cast<guint>(DebugCategory::COUNT)];
	GtkWidget *filter_status;
	GtkWidget *capture_button;
	GtkWidget *capture_path;
	GtkWidget *copy_path_button;
	guint controls_id;
	bool syncing;
#endif
};

static LogWindow *logwindow = nullptr;

static void log_run_command_cb(GSimpleAction *, GVariant *, gpointer data)
{
	if (!options->log_window.action || !*options->log_window.action) return;
	auto *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data));
	if (!gtk_text_buffer_get_has_selection(buffer))
		{
		GtkTextIter cursor;
		gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
		GtkTextIter start = cursor;
		gtk_text_iter_set_line_offset(&start, 0);
		gtk_text_iter_forward_to_line_end(&cursor);
		gtk_text_buffer_select_range(buffer, &start, &cursor);
		}
	GtkTextIter start;
	GtkTextIter end;
	if (!gtk_text_buffer_get_selection_bounds(buffer, &start, &end)) return;
	g_autofree gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
	g_autofree gchar *quoted = g_shell_quote(text);
	g_autofree gchar *command = g_strdup_printf("%s %s", options->log_window.action, quoted);
	runcmd(command);
}

static const ActionDef log_actions[] =
{
	{"win.log-win-run-command", nullptr, N_("Run command on selected log text"), log_run_command_cb,
	 nullptr, ActionStateType::NONE, FALSE, nullptr, 0, nullptr, true},
	{nullptr, nullptr, nullptr, nullptr, nullptr, ActionStateType::NONE, FALSE, nullptr, 0, nullptr}
};

const ActionDef *get_log_actions()
{
	return log_actions;
}

static gboolean log_command_shortcut_cb(GtkEventControllerKey *controller, guint keyval, guint, GdkModifierType state, gpointer data)
{
	auto *window = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller));
	auto *root = gtk_widget_get_root(window);
	if (root && gtk_root_get_focus(root) == data)
		return activate_context_shortcut(window, log_actions, keyval, state);
	return FALSE;
}

static gboolean log_window_key_pressed_cb(GtkEventControllerKey *controller, guint keyval, guint, GdkModifierType, gpointer)
{
	if (keyval != GDK_KEY_Escape) return FALSE;
	gtk_widget_set_visible(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)), FALSE);
	return TRUE;
}

static void text_view_set_line_wrap(GtkTextView *text_view, bool line_wrap)
{
	gtk_text_view_set_wrap_mode(text_view, line_wrap ? GTK_WRAP_WORD : GTK_WRAP_NONE);
}

#ifdef DEBUG
static void log_window_controls_sync(LogWindow *logwin)
{
	logwin->syncing = true;
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(logwin->level_spin), get_debug_level());
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(logwin->source_button), get_debug_source());
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(logwin->delta_button), get_debug_timing());
	g_autofree gchar *domains = get_debug_domains();
	g_auto(GStrv) selected = g_strsplit(domains, ",", -1);
	for (guint i = 0; i < G_N_ELEMENTS(logwin->domain_checks); ++i)
		{
		const auto category = static_cast<DebugCategory>(i);
		const bool active = strcmp(domains, "all") == 0 || g_strv_contains(selected, get_debug_category_name(category));
		gtk_check_button_set_active(GTK_CHECK_BUTTON(logwin->domain_checks[i]), active);
		}
	g_autofree gchar *regexp = get_regexp();
	g_autofree gchar *status = g_strdup_printf(_("Verbosity: %d; subsystems: %s; source: %s; delta: %s; regex: %s"),
	                                         get_debug_level(), domains, get_debug_source() ? _("on") : _("off"),
	                                         get_debug_timing() ? _("on") : _("off"), regexp && *regexp ? regexp : _("none"));
	gtk_label_set_text(GTK_LABEL(logwin->filter_status), status);
	const bool capturing = log_capture_active();
	gtk_button_set_label(GTK_BUTTON(logwin->capture_button), capturing ? _("Stop capture") : _("Start capture"));
	g_autofree gchar *path = log_capture_get_path();
	g_autofree gchar *error = log_capture_get_error();
	g_autofree gchar *description = error ? g_strdup_printf(_("Capture failed: %s (%s)"), error, path ? path : "") :
	                              path && log_capture_limit_reached() ? g_strdup_printf(_("Capture stopped at the %zu MiB limit: %s"), log_capture_max_bytes / (1024 * 1024), path) :
	                              path ? g_strdup_printf(capturing ? _("Capturing: %s") : _("Saved: %s"), path) :
	                                     g_strdup(_("No capture file"));
	gtk_label_set_text(GTK_LABEL(logwin->capture_path), description);
	gtk_widget_set_tooltip_text(logwin->capture_path, description);
	gtk_widget_set_sensitive(logwin->copy_path_button, path != nullptr);
	logwin->syncing = false;
}

static gboolean log_window_controls_cb(gpointer data)
{
	auto *logwin = static_cast<LogWindow *>(data);
	if (!gtk_widget_get_visible(logwin->window) && !log_capture_active())
		{
		logwin->controls_id = 0;
		return G_SOURCE_REMOVE;
		}
	log_window_controls_sync(logwin);
	return G_SOURCE_CONTINUE;
}

static void log_window_domains_cb(GtkCheckButton *, gpointer data)
{
	auto *logwin = static_cast<LogWindow *>(data);
	if (logwin->syncing) return;
	g_autoptr(GString) domains = g_string_new(nullptr);
	for (guint i = 0; i < G_N_ELEMENTS(logwin->domain_checks); ++i)
		{
		if (!gtk_check_button_get_active(GTK_CHECK_BUTTON(logwin->domain_checks[i]))) continue;
		if (domains->len) g_string_append_c(domains, ',');
		g_string_append(domains, get_debug_category_name(static_cast<DebugCategory>(i)));
		}
	set_debug_domains(domains->len ? domains->str : "none");
	log_window_controls_sync(logwin);
}

static void log_window_capture_cb(GtkButton *, gpointer data)
{
	auto *logwin = static_cast<LogWindow *>(data);
	g_autoptr(GError) error = nullptr;
	if (log_capture_active())
		{
		log_capture_stop(&error);
		}
	else
		{
		if (get_debug_level() == 0) set_debug_level(2);
		log_window_controls_sync(logwin);
		const auto report = diagnostic_report_create(get_current_layout());
		log_capture_start(report.c_str(), &error);
		}
	log_window_controls_sync(logwin);
	if (error)
		{
		warning_dialog(_("Unable to capture log"), error->message, GQ_ICON_DIALOG_ERROR, logwin->window);
		}
}

static void log_window_pause_cb(GtkWidget *, gpointer)
{
	options->log_window.paused = !options->log_window.paused;
}

static void log_window_line_wrap_cb(GtkWidget *, gpointer data)
{
	options->log_window.line_wrap = !options->log_window.line_wrap;

	text_view_set_line_wrap(GTK_TEXT_VIEW(data), options->log_window.line_wrap);
}

static void log_window_timer_data_cb(GtkToggleButton *button, gpointer data)
{
	auto *logwin = static_cast<LogWindow *>(data);
	if (logwin->syncing) return;
	options->log_window.timer_data = gtk_toggle_button_get_active(button);
	set_debug_timing(options->log_window.timer_data);
	log_window_controls_sync(logwin);
}

static void log_window_source_cb(GtkToggleButton *button, gpointer data)
{
	auto *logwin = static_cast<LogWindow *>(data);
	if (logwin->syncing) return;
	set_debug_source(gtk_toggle_button_get_active(button));
	log_window_controls_sync(logwin);
}

static void log_window_regexp_cb(GtkWidget *text_entry, gpointer data)
{
	const char *new_regexp = gtk_editable_get_text(GTK_EDITABLE(text_entry));
	set_regexp(new_regexp);
	log_window_controls_sync(static_cast<LogWindow *>(data));
}

static void remove_green_bg(LogWindow *logwin)
{
	GtkTextIter start_find;
	GtkTextIter start_match;
	GtkTextIter end_match;
	GtkTextBuffer *buffer;
	gint offset;

	const char *text = gtk_editable_get_text(GTK_EDITABLE(logwin->search_entry_box));
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));
	gtk_text_buffer_get_start_iter(buffer, &start_find);

	while (gtk_text_iter_forward_search(&start_find, text, GTK_TEXT_SEARCH_VISIBLE_ONLY,  &start_match, &end_match, nullptr))
		{
		GSList *list = gtk_text_iter_get_tags(&start_match);
		for (GSList *work = list; work; work = work->next)
			{
			g_autofree gchar *tag_name = nullptr;
			g_object_get(work->data, "name", &tag_name, NULL);
			if (g_strcmp0(tag_name, "green_bg") == 0)
				{
				gtk_text_buffer_remove_tag_by_name(buffer, "green_bg", &start_match, &end_match);
				}
			}
		g_slist_free(list);

		offset = gtk_text_iter_get_offset(&end_match);
		gtk_text_buffer_get_iter_at_offset(buffer, &start_find, offset);
		}
}

static void search_activate_event(GtkEntry *, LogWindow *logwin)
{
	GtkTextIter start_find;
	GtkTextIter start_match;
	GtkTextIter end_match;
	GtkTextBuffer *buffer;
	GtkTextMark *cursor_mark;
	GtkTextIter cursor_iter;
	gint offset;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));
	const char *text = gtk_editable_get_text(GTK_EDITABLE(logwin->search_entry_box));

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

template<LogWindow::SearchDirection direction>
static void search_keypress_event_cb(GtkButton *, LogWindow *logwin)
{
	GtkTextIter start_find;
	GtkTextIter start_match;
	GtkTextIter end_match;
	GtkTextIter cursor_iter;

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));
	gtk_text_buffer_get_start_iter(buffer, &start_find);

	g_autofree gchar *selected = nullptr;
	const char *text = gtk_editable_get_text(GTK_EDITABLE(logwin->search_entry_box));
	if (text[0] == '\0')
		{
		GtkTextIter start_sel;
		GtkTextIter end_sel;
		if (gtk_text_buffer_get_selection_bounds(buffer, &start_sel, &end_sel))
			{
			selected = gtk_text_buffer_get_text(buffer, &start_sel, &end_sel, FALSE);
			text = selected;
			entry_set_text(GTK_ENTRY(logwin->search_entry_box), text);
			}
		}

	if (logwin->highlight_all)
		{
		while (gtk_text_iter_forward_search(&start_find, text, GTK_TEXT_SEARCH_VISIBLE_ONLY, &start_match, &end_match, nullptr))
			{
			gtk_text_buffer_apply_tag_by_name(buffer, "gray_bg", &start_match, &end_match);
			const gint offset = gtk_text_iter_get_offset(&end_match);
			gtk_text_buffer_get_iter_at_offset(buffer, &start_find, offset);
			}
		}

	auto text_iter_search = (direction == LogWindow::SEARCH_BACKWARDS) ? gtk_text_iter_backward_search : gtk_text_iter_forward_search;

	GtkTextMark *cursor_mark = gtk_text_buffer_get_insert(buffer);
	gtk_text_buffer_get_iter_at_mark(buffer, &cursor_iter, cursor_mark);

	if (text_iter_search(&cursor_iter, text, GTK_TEXT_SEARCH_VISIBLE_ONLY, &start_match, &end_match, nullptr))
		{
		remove_green_bg(logwin);

		gtk_text_buffer_apply_tag_by_name(buffer, "green_bg", &start_match, &end_match);

		if (direction == LogWindow::SEARCH_BACKWARDS)
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
}

static gboolean all_keypress_event_cb(GtkToggleButton *widget, LogWindow *logwin)
{
	logwin->highlight_all = gtk_toggle_button_get_active(widget);

	return FALSE;
}

static void debug_changed_cb(GtkSpinButton *widget, gpointer data)
{
	auto *logwin = static_cast<LogWindow *>(data);
	if (logwin->syncing) return;
	set_debug_level(gtk_spin_button_get_value_as_int(widget));
	log_window_controls_sync(logwin);
}

static void search_entry_icon_cb(GtkEntry *search_entry_box, GtkEntryIconPosition pos, GdkEvent *, gpointer user_data)
{
	if (pos != GTK_ENTRY_ICON_SECONDARY) return;

	entry_set_text(search_entry_box, "");

	auto *logwin = static_cast<LogWindow *>(user_data);
	GtkTextIter start_find;
	GtkTextIter end_find;

	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logwin->text));
	gtk_text_buffer_get_start_iter(buffer, &start_find);
	gtk_text_buffer_get_end_iter(buffer, &end_find);
	gtk_text_buffer_remove_tag_by_name(buffer, "gray_bg", &start_find, &end_find);
	gtk_text_buffer_remove_tag_by_name(buffer, "green_bg", &start_find, &end_find);
}

static void filter_entry_icon_cb(GtkEntry *entry, GtkEntryIconPosition, GdkEvent *, gpointer)
{
	const gchar *blank = "";
	entry_set_text(entry, blank);
	set_regexp(blank);
}
#endif

static gboolean hide_on_close_request(GtkWindow *window, gpointer)
{
	gtk_widget_set_visible(GTK_WIDGET(window), FALSE);

	return TRUE; /* prevent destruction */
}

static LogWindow *log_window_create(GdkRectangle log_window)
{
	LogWindow *logwin;
	GtkWidget *text;
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GtkWidget *win_vbox;

	logwin = g_new0(LogWindow, 1);

	GtkWidget *window = window_new("log", nullptr, _("Log"));
	DEBUG_NAME(window);
	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	gtk_window_set_child(GTK_WINDOW(window), win_vbox);

	gtk_window_set_default_size(GTK_WINDOW(window), log_window.width, log_window.height);

	g_signal_connect(window, "close-request",  G_CALLBACK(hide_on_close_request), nullptr);

	GtkWidget *scrolledwin = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwin),
				       GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolledwin), true);

	gtk_widget_set_hexpand(scrolledwin, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(win_vbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(scrolledwin, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(win_vbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(win_vbox), scrolledwin);

	text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
	text_view_set_line_wrap(GTK_TEXT_VIEW(text), options->log_window.line_wrap);
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
	gtk_text_buffer_get_start_iter(buffer, &iter);
	gtk_text_buffer_create_mark(buffer, "end", &iter, FALSE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolledwin), text);

	auto *command_controller = gtk_event_controller_key_new();
	gtk_event_controller_set_name(command_controller, "log-command-shortcut");
	gtk_event_controller_set_propagation_phase(command_controller, GTK_PHASE_CAPTURE);
	g_signal_connect(command_controller, "key-pressed", G_CALLBACK(log_command_shortcut_cb), text);
	gtk_widget_add_controller(window, command_controller);

	GtkEventController *controller = gtk_event_controller_key_new();
	g_signal_connect(controller, "key-pressed", G_CALLBACK(log_window_key_pressed_cb), text);
	gtk_widget_add_controller(window, controller);

#ifdef DEBUG
	gtk_text_buffer_create_tag(buffer, "gray_bg", "background", "gray", NULL);
	gtk_text_buffer_create_tag(buffer, "green_bg", "background", "#00FF00", NULL);

	GtkWidget *hbox = pref_box_new(win_vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	logwin->level_spin = pref_spin_new(hbox, _("Verbosity:"), nullptr, DEBUG_LEVEL_MIN, DEBUG_LEVEL_MAX, 1, 0,
	                                 get_debug_level(), G_CALLBACK(debug_changed_cb), logwin);
	gtk_widget_set_tooltip_text(logwin->level_spin, _("0: disabled; 1: summaries and failures; 2: decisions; 3: per-file details; 4: rendering internals"));

	auto *subsystems = gtk_menu_button_new();
	gtk_menu_button_set_label(GTK_MENU_BUTTON(subsystems), _("Subsystems"));
	gtk_box_append(GTK_BOX(hbox), subsystems);
	auto *popover = gtk_popover_new();
	auto *domain_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	gtk_popover_set_child(GTK_POPOVER(popover), domain_box);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(subsystems), popover);
	auto *domain_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_append(GTK_BOX(domain_box), domain_buttons);
	auto *all_domains = gtk_button_new_with_label(_("All"));
	auto *no_domains = gtk_button_new_with_label(_("None"));
	gtk_box_append(GTK_BOX(domain_buttons), all_domains);
	gtk_box_append(GTK_BOX(domain_buttons), no_domains);
	g_signal_connect(all_domains, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data)
		{
		set_debug_domains("all");
		log_window_controls_sync(static_cast<LogWindow *>(data));
		}), logwin);
	g_signal_connect(no_domains, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data)
		{
		set_debug_domains("none");
		log_window_controls_sync(static_cast<LogWindow *>(data));
		}), logwin);
	constexpr const gchar *domain_labels[] = {N_("General"), N_("Thumbnails"), N_("Cache"), N_("Images"), N_("Rendering"), N_("Metadata"), N_("Files"), N_("User interface"), N_("Duplicates"), N_("Configuration"), N_("Collections"), N_("External commands")};
	static_assert(G_N_ELEMENTS(domain_labels) == static_cast<guint>(DebugCategory::COUNT));
	for (guint i = 0; i < G_N_ELEMENTS(domain_labels); ++i)
		{
		auto *check = gtk_check_button_new_with_label(_(domain_labels[i]));
		logwin->domain_checks[i] = check;
		gtk_widget_set_tooltip_text(check, get_debug_category_name(static_cast<DebugCategory>(i)));
		gtk_box_append(GTK_BOX(domain_box), check);
		g_signal_connect(check, "toggled", G_CALLBACK(log_window_domains_cb), logwin);
		}

	GtkWidget *pause = gtk_toggle_button_new_with_label(_("Pause"));
	gtk_widget_set_tooltip_text(pause, _("Pause scrolling"));
	gtk_box_append(GTK_BOX(hbox), pause);
	g_signal_connect(pause, "toggled", G_CALLBACK(log_window_pause_cb), nullptr);

	GtkWidget *wrap = gtk_toggle_button_new_with_label(_("Wrap"));
	gtk_widget_set_tooltip_text(wrap, _("Enable line wrap"));
	gtk_box_append(GTK_BOX(hbox), wrap);
	g_signal_connect(wrap, "toggled", G_CALLBACK(log_window_line_wrap_cb), text);

	hbox = pref_box_new(win_vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	GtkWidget *timer_data = gtk_toggle_button_new_with_label(_("Delta"));
	gtk_widget_set_tooltip_text(timer_data, _("Show time since the previous debug record on each thread"));
	gtk_box_append(GTK_BOX(hbox), timer_data);
	if (options->log_window.timer_data)
		{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(timer_data), TRUE);
		}
	g_signal_connect(timer_data, "toggled", G_CALLBACK(log_window_timer_data_cb), logwin);
	logwin->delta_button = timer_data;

	GtkWidget *source = gtk_toggle_button_new_with_label(_("Source"));
	gtk_widget_set_tooltip_text(source, _("Include source file, line, and function in debug records"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(source), get_debug_source());
	gtk_box_append(GTK_BOX(hbox), source);
	g_signal_connect(source, "toggled", G_CALLBACK(log_window_source_cb), logwin);
	logwin->source_button = source;

	GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(hbox), search_box);

	logwin->search_entry_box = gtk_entry_new();
	gtk_box_append(GTK_BOX(search_box), logwin->search_entry_box);
	ui_entry_set_icon_from_icon_name(GTK_ENTRY(logwin->search_entry_box), GTK_ENTRY_ICON_PRIMARY, GQ_ICON_FIND);
	ui_entry_set_icon_from_icon_name(GTK_ENTRY(logwin->search_entry_box), GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	gtk_widget_set_tooltip_text(logwin->search_entry_box, _("Search for text in log window"));
	g_signal_connect(logwin->search_entry_box, "icon-press", G_CALLBACK(search_entry_icon_cb), logwin);
	g_signal_connect(logwin->search_entry_box, "activate", G_CALLBACK(search_activate_event), logwin);

	GtkWidget *backwards_button = ui_button_new_from_icon_name(GQ_ICON_PAN_UP);
	gtk_widget_set_tooltip_text(backwards_button, _("Search backwards"));
	gtk_box_append(GTK_BOX(search_box), backwards_button);
	g_signal_connect(backwards_button, "clicked", G_CALLBACK(search_keypress_event_cb<LogWindow::SEARCH_BACKWARDS>), logwin);

	GtkWidget *forwards_button = ui_button_new_from_icon_name(GQ_ICON_PAN_DOWN);
	gtk_widget_set_tooltip_text(forwards_button, _("Search forwards"));
	gtk_box_append(GTK_BOX(search_box), forwards_button);
	g_signal_connect(forwards_button, "clicked", G_CALLBACK(search_keypress_event_cb<LogWindow::SEARCH_FORWARDS>), logwin);

	GtkWidget *all_button = gtk_toggle_button_new();

	gtk_button_set_child(GTK_BUTTON(all_button), ui_image_new_from_icon_name(GQ_ICON_SELECT_ALL));

	gtk_widget_set_tooltip_text(all_button, _("Highlight all"));
	gtk_box_append(GTK_BOX(search_box), all_button);

	g_signal_connect(all_button, "toggled", G_CALLBACK(all_keypress_event_cb), logwin);

	hbox = pref_box_new(win_vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Filter regexp"));

	logwin->regexp_box = gtk_entry_new();
	gtk_box_append(GTK_BOX(hbox), logwin->regexp_box);
	gtk_widget_set_hexpand(logwin->regexp_box, TRUE);
	ui_entry_set_icon_from_icon_name(GTK_ENTRY(logwin->regexp_box), GTK_ENTRY_ICON_SECONDARY, GQ_ICON_CLEAR);
	g_signal_connect(G_OBJECT(logwin->regexp_box), "activate",
	                 G_CALLBACK(log_window_regexp_cb), logwin);
	g_signal_connect(logwin->regexp_box, "icon-press", G_CALLBACK(filter_entry_icon_cb), nullptr);

	logwin->filter_status = gtk_label_new(nullptr);
	gtk_label_set_xalign(GTK_LABEL(logwin->filter_status), 0.0);
	gtk_label_set_wrap(GTK_LABEL(logwin->filter_status), TRUE);
	gtk_label_set_wrap_mode(GTK_LABEL(logwin->filter_status), PANGO_WRAP_WORD_CHAR);
	gtk_label_set_max_width_chars(GTK_LABEL(logwin->filter_status), 80);
	gtk_box_append(GTK_BOX(win_vbox), logwin->filter_status);
	hbox = pref_box_new(win_vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	logwin->capture_button = gtk_button_new_with_label(_("Start capture"));
	g_autofree gchar *capture_tip = g_strdup_printf(_("Capture to a unique temporary file, up to %zu MiB. Verbosity 2 is selected if logging is disabled. Capture continues when this window is hidden."), log_capture_max_bytes / (1024 * 1024));
	gtk_widget_set_tooltip_text(logwin->capture_button, capture_tip);
	gtk_box_append(GTK_BOX(hbox), logwin->capture_button);
	g_signal_connect(logwin->capture_button, "clicked", G_CALLBACK(log_window_capture_cb), logwin);
	logwin->capture_path = gtk_label_new(nullptr);
	gtk_label_set_selectable(GTK_LABEL(logwin->capture_path), TRUE);
	gtk_label_set_xalign(GTK_LABEL(logwin->capture_path), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(logwin->capture_path), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_hexpand(logwin->capture_path, TRUE);
	gtk_box_append(GTK_BOX(hbox), logwin->capture_path);
	logwin->copy_path_button = gtk_button_new_with_label(_("Copy path"));
	gtk_box_append(GTK_BOX(hbox), logwin->copy_path_button);
	g_signal_connect(logwin->copy_path_button, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data)
		{
		auto *logwin = static_cast<LogWindow *>(data);
		g_autofree gchar *path = log_capture_get_path();
		if (path) gdk_clipboard_set_text(gtk_widget_get_clipboard(logwin->window), path);
		}), logwin);
#endif

	constexpr const gchar *tags[] = { "black", "blue", "orange", "red" };
	for (int i = 0; i < LOG_COUNT; ++i)
		{
		g_autofree gchar *tag_name = g_strdup_printf("%s_foreground", tags[i]);

		logwin->color_tags[i] = gtk_text_buffer_create_tag(buffer, tag_name,
		                                                   "foreground", tags[i],
		                                                   "family", "MonoSpace", // NOTRANSLATE: Font description.
		                                                   NULL);
		}

	register_actions_from_table(GTK_APPLICATION(g_application_get_default()), window, log_actions, get_keyfile_merged(), text);

	logwin->window = window;
	logwin->text = text;
	return logwin;
}

static void log_window_show(LogWindow *logwin)
{
	GtkTextView *text = GTK_TEXT_VIEW(logwin->text);
	GtkTextBuffer *buffer;
	GtkTextMark *mark;

	buffer = gtk_text_view_get_buffer(text);
	mark = gtk_text_buffer_get_mark(buffer, "end");
	gtk_text_view_scroll_mark_onscreen(text, mark);

	gtk_window_present(GTK_WINDOW(logwin->window));
#ifdef DEBUG
	log_window_controls_sync(logwin);
	if (!logwin->controls_id) logwin->controls_id = g_timeout_add_seconds(1, log_window_controls_cb, logwin);
#endif

	log_window_append("", LOG_NORMAL); // to flush memorized lines

#ifdef DEBUG
	g_autofree gchar *regexp = get_regexp();
	if (regexp != nullptr)
		{
		entry_set_text(GTK_ENTRY(logwin->regexp_box), regexp);
		}
#endif
}

void log_window_new(LayoutWindow *lw)
{
	if (logwindow == nullptr)
		{
		logwindow = log_window_create(lw->options.log_window);

		lw->log_window = logwindow->window;
		}

	log_window_show(logwindow);
}

struct LogMsg {
	LogMsg() = default;
	LogMsg(const gchar *text, LogType type)
		: text(text)
		, type(type)
	{}
	std::string text;
	LogType type;
};

static void log_window_insert_text(GtkTextBuffer *buffer, GtkTextIter *iter,
				   const gchar *text, GtkTextTag *tag)
{
	if (!text || !*text) return;

	g_autofree gchar *str_utf8 = utf8_validate_or_convert(text);
	gtk_text_buffer_insert_with_tags(buffer, iter, str_utf8, -1, tag, NULL);
}

void log_window_trim_buffer(GtkTextBuffer *buffer, gint line_limit)
{
	constexpr gint character_limit = 262144;
	line_limit = line_limit > 0 ? std::min(line_limit, 99999) : 10000;
	GtkTextIter start;
	GtkTextIter end;
	gtk_text_buffer_get_start_iter(buffer, &start);
	end = start;
	const gint excess_lines = gtk_text_buffer_get_line_count(buffer) - line_limit;
	if (excess_lines > 0) gtk_text_iter_forward_lines(&end, excess_lines);
	const gint excess_characters = gtk_text_buffer_get_char_count(buffer) - character_limit;
	if (excess_characters > gtk_text_iter_get_offset(&end))
		{
		gtk_text_buffer_get_iter_at_offset(buffer, &end, excess_characters);
		}
	if (gtk_text_iter_compare(&start, &end) < 0) gtk_text_buffer_delete(buffer, &start, &end);
}

void log_window_append(const gchar *str, LogType type)
{
	GtkTextView *text;
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	static std::deque<LogMsg> memory;
	static std::size_t memory_bytes = 0;
	const gint line_limit = options->log_window_lines > 0 ? std::min(options->log_window_lines, 99999) : 10000;
	constexpr std::size_t byte_limit = 1024 * 1024;

	if (logwindow == nullptr)
		{
		if (*str)
			{
			const std::size_t length = strlen(str);
			if (length > byte_limit) return;
			memory.emplace_front(str, type);
			memory_bytes += length;
			while (memory.size() > static_cast<std::size_t>(line_limit) || memory_bytes > byte_limit)
				{
				memory_bytes -= memory.back().text.size();
				memory.pop_back();
				}
			}
		return;
		}

	text = GTK_TEXT_VIEW(logwindow->text);
	buffer = gtk_text_view_get_buffer(text);

	gtk_text_buffer_get_end_iter(buffer, &iter);

	std::for_each(memory.crbegin(), memory.crend(), [buffer, &iter](const LogMsg &oldest_msg)
		{
		log_window_insert_text(buffer, &iter, oldest_msg.text.c_str(), logwindow->color_tags[oldest_msg.type]);
		});
	memory.clear();
	memory_bytes = 0;

	log_window_insert_text(buffer, &iter, str, logwindow->color_tags[type]);

	log_window_trim_buffer(buffer, line_limit);

	if (!options->log_window.paused)
		{
		if (gtk_widget_get_visible(GTK_WIDGET(text)))
			{
			GtkTextMark *mark;

			mark = gtk_text_buffer_get_mark(buffer, "end");
			gtk_text_view_scroll_mark_onscreen(text, mark);
			}
		}

}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
