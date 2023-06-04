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

#include "main.h"
#include "bar-comment.h"

#include "bar.h"
#include "metadata.h"
#include "filedata.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "rcfile.h"
#include "layout.h"

#ifdef HAVE_SPELL
#include <gspell/gspell.h>
#endif

static void bar_pane_comment_changed(GtkTextBuffer *buffer, gpointer data);

/*
 *-------------------------------------------------------------------
 * keyword / comment utils
 *-------------------------------------------------------------------
 */



typedef struct _PaneCommentData PaneCommentData;
struct _PaneCommentData
{
	PaneData pane;
	GtkWidget *widget;
	GtkWidget *comment_view;
	FileData *fd;
	gchar *key;
	gint height;
};


static void bar_pane_comment_write(PaneCommentData *pcd)
{
	gchar *comment;

	if (!pcd->fd) return;

	comment = text_widget_text_pull(pcd->comment_view);

	metadata_write_string(pcd->fd, pcd->key, comment);
	g_free(comment);
}


static void bar_pane_comment_update(PaneCommentData *pcd)
{
	gchar *comment = NULL;
	gchar *orig_comment = NULL;
	const gchar *comment_not_null;
	gshort rating;
	GtkTextBuffer *comment_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pcd->comment_view));

	orig_comment = text_widget_text_pull(pcd->comment_view);
	if (g_strcmp0(pcd->key, "Xmp.xmp.Rating") == 0)
		{
		rating = metadata_read_int(pcd->fd, pcd->key, 0);
		comment = g_strdup_printf("%d", rating);
		}
	else
		{
		comment = metadata_read_string(pcd->fd, pcd->key, METADATA_PLAIN);
		}
	comment_not_null = (comment) ? comment : "";

	if (strcmp(orig_comment, comment_not_null) != 0)
		{
		g_signal_handlers_block_by_func(comment_buffer, (gpointer)bar_pane_comment_changed, pcd);
		gtk_text_buffer_set_text(comment_buffer, comment_not_null, -1);
		g_signal_handlers_unblock_by_func(comment_buffer, (gpointer)bar_pane_comment_changed, pcd);
		}
	g_free(comment);
	g_free(orig_comment);

	gtk_widget_set_sensitive(pcd->comment_view, (pcd->fd != NULL));
}

static void bar_pane_comment_set_selection(PaneCommentData *pcd, gboolean append)
{
	GList *list = NULL;
	GList *work;
	gchar *comment = NULL;

	comment = text_widget_text_pull(pcd->comment_view);

	list = layout_selection_list(pcd->pane.lw);
	list = file_data_process_groups_in_selection(list, FALSE, NULL);

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;
		if (fd == pcd->fd) continue;

		if (append)
			{
			metadata_append_string(fd, pcd->key, comment);
			}
		else
			{
			metadata_write_string(fd, pcd->key, comment);
			}
		}

	filelist_free(list);
	g_free(comment);
}

static void bar_pane_comment_sel_add_cb(GtkWidget *UNUSED(button), gpointer data)
{
	auto pcd = static_cast<PaneCommentData *>(data);

	bar_pane_comment_set_selection(pcd, TRUE);
}

static void bar_pane_comment_sel_replace_cb(GtkWidget *UNUSED(button), gpointer data)
{
	auto pcd = static_cast<PaneCommentData *>(data);

	bar_pane_comment_set_selection(pcd, FALSE);
}


static void bar_pane_comment_set_fd(GtkWidget *bar, FileData *fd)
{
	PaneCommentData *pcd;

	pcd = static_cast<PaneCommentData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pcd) return;

	file_data_unref(pcd->fd);
	pcd->fd = file_data_ref(fd);

	bar_pane_comment_update(pcd);
}

static gint bar_pane_comment_event(GtkWidget *bar, GdkEvent *event)
{
	PaneCommentData *pcd;

	pcd = static_cast<PaneCommentData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pcd) return FALSE;

	if (gtk_widget_has_focus(pcd->comment_view)) return gtk_widget_event(pcd->comment_view, event);

	return FALSE;
}

static void bar_pane_comment_write_config(GtkWidget *pane, GString *outstr, gint indent)
{
	PaneCommentData *pcd;
	gint w, h;

	pcd = static_cast<PaneCommentData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pcd) return;

	gtk_widget_get_size_request(GTK_WIDGET(pane), &w, &h);

	if (!g_strcmp0(pcd->pane.id, "title"))
		{
		pcd->height = h;
		}
	if (!g_strcmp0(pcd->pane.id, "comment"))
		{
		pcd->height = h;
		}
	if (!g_strcmp0(pcd->pane.id, "rating"))
		{
		pcd->height = h;
		}
	if (!g_strcmp0(pcd->pane.id, "headline"))
		{
		pcd->height = h;
		}

	WRITE_NL(); WRITE_STRING("<pane_comment ");
	write_char_option(outstr, indent, "id", pcd->pane.id);
	write_char_option(outstr, indent, "title", gtk_label_get_text(GTK_LABEL(pcd->pane.title)));
	WRITE_BOOL(pcd->pane, expanded);
	WRITE_CHAR(*pcd, key);
	WRITE_INT(*pcd, height);
	WRITE_STRING("/>");
}

static void bar_pane_comment_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto pcd = static_cast<PaneCommentData *>(data);
	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE | NOTIFY_METADATA)) && fd == pcd->fd)
		{
		DEBUG_1("Notify pane_comment: %s %04x", fd->path, type);

		bar_pane_comment_update(pcd);
		}
}

static void bar_pane_comment_changed(GtkTextBuffer *UNUSED(buffer), gpointer data)
{
	auto pcd = static_cast<PaneCommentData *>(data);

	bar_pane_comment_write(pcd);
}


static void bar_pane_comment_populate_popup(GtkTextView *UNUSED(textview), GtkMenu *menu, gpointer data)
{
	auto pcd = static_cast<PaneCommentData *>(data);

	menu_item_add_divider(GTK_WIDGET(menu));
	menu_item_add_stock(GTK_WIDGET(menu), _("Add text to selected files"), GTK_STOCK_ADD, G_CALLBACK(bar_pane_comment_sel_add_cb), pcd);
	menu_item_add_stock(GTK_WIDGET(menu), _("Replace existing text in selected files"), GTK_STOCK_CONVERT, G_CALLBACK(bar_pane_comment_sel_replace_cb), data);
}

static void bar_pane_comment_destroy(GtkWidget *UNUSED(widget), gpointer data)
{
	auto pcd = static_cast<PaneCommentData *>(data);

	file_data_unregister_notify_func(bar_pane_comment_notify_cb, pcd);

	file_data_unref(pcd->fd);
	g_free(pcd->key);

	g_free(pcd->pane.id);

	g_free(pcd);
}


static GtkWidget *bar_pane_comment_new(const gchar *id, const gchar *title, const gchar *key, gboolean expanded, gint height)
{
	PaneCommentData *pcd;
	GtkWidget *scrolled;
	GtkTextBuffer *buffer;
#ifdef HAVE_SPELL
	GspellTextView *gspell_view;
#endif

	pcd = g_new0(PaneCommentData, 1);

	pcd->pane.pane_set_fd = bar_pane_comment_set_fd;
	pcd->pane.pane_event = bar_pane_comment_event;
	pcd->pane.pane_write_config = bar_pane_comment_write_config;
	pcd->pane.title = bar_pane_expander_title(title);
	pcd->pane.id = g_strdup(id);
	pcd->pane.type = PANE_COMMENT;

	pcd->pane.expanded = expanded;

	pcd->key = g_strdup(key);
	pcd->height = height;

	scrolled = gtk_scrolled_window_new(NULL, NULL);

	pcd->widget = scrolled;
	g_object_set_data(G_OBJECT(pcd->widget), "pane_data", pcd);
	g_signal_connect(G_OBJECT(pcd->widget), "destroy",
			 G_CALLBACK(bar_pane_comment_destroy), pcd);

	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_widget_set_size_request(pcd->widget, -1, height);
	gtk_widget_show(scrolled);

	pcd->comment_view = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(pcd->comment_view), GTK_WRAP_WORD);
	gtk_container_add(GTK_CONTAINER(scrolled), pcd->comment_view);
	g_signal_connect(G_OBJECT(pcd->comment_view), "populate-popup",
			 G_CALLBACK(bar_pane_comment_populate_popup), pcd);
	gtk_widget_show(pcd->comment_view);

#ifdef HAVE_SPELL
	if (g_strcmp0(key, "Xmp.xmp.Rating") != 0)
		{
		if (options->metadata.check_spelling)
			{
			gspell_view = gspell_text_view_get_from_gtk_text_view(GTK_TEXT_VIEW(pcd->comment_view));
			gspell_text_view_basic_setup(gspell_view);
			}
	}
#endif

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pcd->comment_view));
	g_signal_connect(G_OBJECT(buffer), "changed",
			 G_CALLBACK(bar_pane_comment_changed), pcd);


	file_data_register_notify_func(bar_pane_comment_notify_cb, pcd, NOTIFY_PRIORITY_LOW);

	return pcd->widget;
}

GtkWidget *bar_pane_comment_new_from_config(const gchar **attribute_names, const gchar **attribute_values)
{
	gchar *title = NULL;
	gchar *key = g_strdup(COMMENT_KEY);
	gboolean expanded = TRUE;
	gint height = 50;
	gchar *id = g_strdup("comment");
	GtkWidget *ret;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_CHAR_FULL("key", key)) continue;
		if (READ_BOOL_FULL("expanded", expanded)) continue;
		if (READ_INT_FULL("height", height)) continue;
		if (READ_CHAR_FULL("id", id)) continue;


		log_printf("unknown attribute %s = %s\n", option, value);
		}

	if (!g_strcmp0(id, "title"))
		{
		options->info_title.height = height;
		}
	if (!g_strcmp0(id, "comment"))
		{
		options->info_comment.height = height;
		}
	if (!g_strcmp0(id, "rating"))
		{
		options->info_rating.height = height;
		}
	if (!g_strcmp0(id, "headline"))
		{
		options->info_headline.height = height;
		}

	bar_pane_translate_title(PANE_COMMENT, id, &title);
	ret = bar_pane_comment_new(id, title, key, expanded, height);
	g_free(title);
	g_free(key);
	g_free(id);
	return ret;
}

void bar_pane_comment_update_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values)
{
	PaneCommentData *pcd;

	pcd = static_cast<PaneCommentData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pcd) return;

	gchar *title = NULL;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_CHAR_FULL("key", pcd->key)) continue;
		if (READ_BOOL_FULL("expanded", pcd->pane.expanded)) continue;
		if (READ_INT_FULL("height", pcd->height)) continue;
		if (READ_CHAR_FULL("id", pcd->pane.id)) continue;


		log_printf("unknown attribute %s = %s\n", option, value);
		}

	if (title)
		{
		bar_pane_translate_title(PANE_COMMENT, pcd->pane.id, &title);
		gtk_label_set_text(GTK_LABEL(pcd->pane.title), title);
		g_free(title);
		}
	gtk_widget_set_size_request(pcd->widget, -1, pcd->height);
	bar_update_expander(pane);
	bar_pane_comment_update(pcd);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
