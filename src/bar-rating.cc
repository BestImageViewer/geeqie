/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2022 The Geeqie Team
 *
 * Author: Colin Clark
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
#include "bar-rating.h"

#include "bar.h"
#include "filedata.h"
#include "metadata.h"
#include "rcfile.h"
#include "ui-misc.h"

/**
 * @file
 * Rating values as specified by: \n
 * Adobe XMP Basic namespace \n
 * -1 Rejected \n
 * 0 Unrated \n
 * 1 to 5 Rating value
 */

struct PaneRatingData
{
	PaneData pane;
	GtkWidget *widget;
	GtkWidget *radio_button_first;
	FileData *fd;
};

static void bar_pane_rating_update(PaneRatingData *prd)
{
	guint64 rating;
	GSList *list;

	rating = metadata_read_int(prd->fd, RATING_KEY, 0);

	list = gtk_radio_button_get_group(GTK_RADIO_BUTTON(prd->radio_button_first));

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_slist_nth_data(list, 5 - rating)), TRUE);
}

static void bar_pane_rating_set_fd(GtkWidget *pane, FileData *fd)
{
	PaneRatingData *prd;

	prd = static_cast<PaneRatingData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!prd) return;

	file_data_unref(prd->fd);
	prd->fd = file_data_ref(fd);

	bar_pane_rating_update(prd);
}

static void bar_pane_rating_write_config(GtkWidget *pane, GString *outstr, gint indent)
{
	PaneRatingData *prd;

	prd = static_cast<PaneRatingData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!prd) return;

	WRITE_NL();
	WRITE_STRING("<pane_rating ");
	write_char_option(outstr, indent, "id", prd->pane.id);
	write_char_option(outstr, indent, "title", gtk_label_get_text(GTK_LABEL(prd->pane.title)));
	WRITE_BOOL(prd->pane, expanded);
	WRITE_STRING("/>");
}

static void bar_pane_rating_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto prd = static_cast<PaneRatingData *>(data);

	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE | NOTIFY_HISTMAP | NOTIFY_PIXBUF)) && fd == prd->fd)
		{
		DEBUG_1("Notify pane_rating: %s %04x", fd->path, type);
		bar_pane_rating_update(prd);
		}
}

static void bar_pane_rating_destroy(GtkWidget *, gpointer data)
{
	auto prd = static_cast<PaneRatingData *>(data);

	file_data_unregister_notify_func(bar_pane_rating_notify_cb, prd);
	file_data_unref(prd->fd);
	g_free(prd->pane.id);
	g_free(prd);
}

static void bar_pane_rating_selected_cb(GtkToggleButton *togglebutton, gpointer data)
{
	auto prd = static_cast<PaneRatingData *>(data);
	GSList *list;
	gint i;
	gchar *rating;

	if (gtk_toggle_button_get_active(togglebutton))
		{
		list = gtk_radio_button_get_group(GTK_RADIO_BUTTON(togglebutton));
		i = 0;

		while (list)
			{
			if (list->data == togglebutton)
				{
				rating = g_strdup_printf("%d", 5 - i);
				metadata_write_string(prd->fd, RATING_KEY, rating);
				g_free(rating);
				break;
				}

			i++;
			list = list->next;
			}
		}
}

static GtkWidget *bar_pane_rating_new(const gchar *id, const gchar *title, gboolean expanded)
{
	PaneRatingData *prd;
	GtkWidget *radio_rejected;
	GtkWidget *radio_unrated;
	GtkWidget *radio_rating;
	GtkWidget *row_1;
	GtkWidget *row_2;
	gint i;
	gchar *i_str;

	prd = g_new0(PaneRatingData, 1);

	prd->pane.pane_set_fd = bar_pane_rating_set_fd;
	prd->pane.pane_write_config = bar_pane_rating_write_config;
	prd->pane.title = bar_pane_expander_title(title);
	prd->pane.id = g_strdup(id);
	prd->pane.type = PANE_RATING;

	prd->pane.expanded = expanded;

	prd->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	g_object_set_data(G_OBJECT(prd->widget), "pane_data", prd);
	g_signal_connect(G_OBJECT(prd->widget), "destroy", G_CALLBACK(bar_pane_rating_destroy), prd);

	row_1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	gtk_box_pack_start(GTK_BOX(prd->widget), row_1, FALSE, FALSE, 0);

	radio_rejected = gtk_radio_button_new_with_label(nullptr, _("Rejected"));
	gtk_box_pack_start(GTK_BOX(row_1), radio_rejected, FALSE, FALSE, 0);
	g_signal_connect(radio_rejected, "released", G_CALLBACK(bar_pane_rating_selected_cb), prd);

	radio_unrated = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_rejected), _("Unrated"));
	gtk_box_pack_start(GTK_BOX(row_1), radio_unrated, FALSE, FALSE, 0);
	g_signal_connect(radio_unrated, "released", G_CALLBACK(bar_pane_rating_selected_cb), prd);

	row_2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	gtk_box_pack_start(GTK_BOX(prd->widget), row_2, FALSE, FALSE, 0);

	i = 1;
	while (i <= 5)
		{
		i_str = g_strdup_printf("%d", i);

		radio_rating = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(radio_rejected), i_str);
		g_signal_connect(radio_rating, "released", G_CALLBACK(bar_pane_rating_selected_cb), prd);

		gtk_box_pack_start(GTK_BOX(row_2), radio_rating, FALSE, FALSE, 1);

		g_free(i_str);
		i++;
		}

	prd->radio_button_first = radio_rating;

	gtk_widget_show(prd->widget);

	file_data_register_notify_func(bar_pane_rating_notify_cb, prd, NOTIFY_PRIORITY_LOW);

	return prd->widget;
}

GtkWidget *bar_pane_rating_new_from_config(const gchar **attribute_names, const gchar **attribute_values)
{
	gchar *title = nullptr;
	gchar *id = g_strdup("rating");
	gboolean expanded = TRUE;
	GtkWidget *ret;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("id", id)) continue;
		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_BOOL_FULL("expanded", expanded)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	bar_pane_translate_title(PANE_RATING, id, &title);
	ret = bar_pane_rating_new(id, title, expanded);

	g_free(title);
	g_free(id);
	return ret;
}

void bar_pane_rating_update_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values)
{
	PaneRatingData *prd;
	gchar *title = nullptr;

	prd = static_cast<PaneRatingData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!prd) return;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_CHAR_FULL("id", prd->pane.id)) continue;
		if (READ_BOOL_FULL("expanded", prd->pane.expanded)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	bar_update_expander(pane);
	bar_pane_rating_update(prd);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
