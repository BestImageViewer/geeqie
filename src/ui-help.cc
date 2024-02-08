/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
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

#include "ui-help.h"

#include <cstdio>
#include <cstring>

#include <config.h>

#include "compat.h"
#include "debug.h"
#include "intl.h"
#include "main-defines.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "window.h"


enum {
	HELP_WINDOW_WIDTH = 650,
	HELP_WINDOW_HEIGHT = 350
};


/*
 *-----------------------------------------------------------------------------
 * 'help' window
 *-----------------------------------------------------------------------------
 */

#define SCROLL_MARKNAME "scroll_point"

static void help_window_scroll(GtkWidget *text, const gchar *key)
{
	gchar *needle;
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GtkTextIter start;
	GtkTextIter end;

	if (!text || !key) return;

	needle = g_strdup_printf("[section:%s]", key);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
	gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);

	if (gtk_text_iter_forward_search(&iter, needle, GTK_TEXT_SEARCH_TEXT_ONLY,
					 &start, &end, nullptr))
		{
		gint line;
		GtkTextMark *mark;

		line = gtk_text_iter_get_line(&start);
		gtk_text_buffer_get_iter_at_line_offset(buffer, &iter, line, 0);
		gtk_text_buffer_place_cursor(buffer, &iter);

		/* apparently only scroll_to_mark works when the textview is not visible yet */

		/* if mark exists, move it instead of creating one for every scroll */
		mark = gtk_text_buffer_get_mark(buffer, SCROLL_MARKNAME);
		if (mark)
			{
			gtk_text_buffer_move_mark(buffer, mark, &iter);
			}
		else
			{
			mark = gtk_text_buffer_create_mark(buffer, SCROLL_MARKNAME, &iter, FALSE);
			}
		gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text), mark, 0.0, TRUE, 0, 0);
		}

	g_free(needle);
}

static void help_window_load_text(GtkWidget *text, const gchar *path)
{
	gchar *pathl;
	FILE *f;
	gchar s_buf[1024];
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GtkTextIter start;
	GtkTextIter end;

	if (!text || !path) return;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));

	gtk_text_buffer_get_bounds(buffer, &start, &end);
	gtk_text_buffer_delete(buffer, &start, &end);

	gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);

	pathl = path_from_utf8(path);
	f = fopen(pathl, "r");
	g_free(pathl);
	if (!f)
		{
		gchar *buf;
		buf = g_strdup_printf(_("Unable to load:\n%s"), path);
		gtk_text_buffer_insert(buffer, &iter, buf, -1);
		g_free(buf);
		}
	else
		{
		while (fgets(s_buf, sizeof(s_buf), f))
			{
			gchar *buf;
			gint l;

			l = strlen(s_buf);

			if (!g_utf8_validate(s_buf, l, nullptr))
				{
				buf = g_locale_to_utf8(s_buf, l, nullptr, nullptr, nullptr);
				if (!buf) buf = g_strdup("\n");
				}
			else
				{
				buf = nullptr;
				}
			gtk_text_buffer_insert_with_tags_by_name(buffer, &iter,
								 (buf) ? buf : s_buf, -1,
								 "monospace", NULL);
			g_free(buf);
			}
		fclose(f);
		}

	gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);
	gtk_text_buffer_place_cursor(buffer, &iter);
	gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(text), &iter, 0.0, TRUE, 0, 0);
}

static gboolean help_window_delete_cb(GtkWidget *widget, GdkEventAny *, gpointer)
{
	gq_gtk_widget_destroy(widget);
	return TRUE;
}

static void help_window_close(GtkWidget *, gpointer data)
{
	auto window = static_cast<GtkWidget *>(data);
	gq_gtk_widget_destroy(window);
}

void help_window_set_key(GtkWidget *window, const gchar *key)
{
	GtkWidget *text;

	if (!window) return;

	text = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(window), "text_widget"));
	if (!text) return;

	gdk_window_raise(gtk_widget_get_window(window));

	if (key) help_window_scroll(text, key);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
void help_window_set_file_unused(GtkWidget *window, const gchar *path, const gchar *key)
{
	GtkWidget *text;

	if (!window || !path) return;

	text = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(window), "text_widget"));
	if (!text) return;

	gdk_window_raise(gtk_widget_get_window(window));

	help_window_load_text(text, path);
	help_window_scroll(text, key);
}
#pragma GCC diagnostic pop

GtkWidget *help_window_new(const gchar *title,
			   const gchar *subclass,
			   const gchar *path, const gchar *key)
{
	GtkWidget *window;
	GtkWidget *text;
	GtkTextBuffer *buffer;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *scrolled;

	/* window */

	window = window_new(subclass, nullptr, nullptr, title);
	DEBUG_NAME(window);
	gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(window), HELP_WINDOW_WIDTH, HELP_WINDOW_HEIGHT);

	g_signal_connect(G_OBJECT(window), "delete_event",
			 G_CALLBACK(help_window_delete_cb), NULL);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gq_gtk_container_add(GTK_WIDGET(window), vbox);
	gtk_widget_show(vbox);

	g_object_set_data(G_OBJECT(window), "text_vbox", vbox);

	/* text window */

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(hbox), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
	gq_gtk_container_add(GTK_WIDGET(scrolled), text);
	gtk_widget_show(text);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
	gtk_text_buffer_create_tag(buffer, "monospace",
				   "family", "monospace", NULL);

	hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), PREF_PAD_BORDER);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gq_gtk_box_pack_end(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = gtk_button_new_from_icon_name(GQ_ICON_CLOSE, GTK_ICON_SIZE_BUTTON);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(help_window_close), window);
	gq_gtk_container_add(GTK_WIDGET(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);

	g_object_set_data(G_OBJECT(window), "text_widget", text);

	help_window_load_text(text, path);

	gtk_widget_show(window);

	help_window_scroll(text, key);

	return window;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
GtkWidget *help_window_get_box_unused(GtkWidget *window)
{
	return static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(window), "text_vbox"));
}
#pragma GCC diagnostic pop

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
