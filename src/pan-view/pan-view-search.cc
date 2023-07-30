/*
 * Copyright (C) 2006 John Ellis
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

#include "pan-view-search.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

#include <glib-object.h>

#include "compat.h"
#include "filedata.h"
#include "image.h"
#include "intl.h"
#include "main-defines.h"
#include "misc.h"
#include "pan-calendar.h"
#include "pan-item.h"
#include "pan-types.h"
#include "pan-util.h"
#include "pan-view.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"

PanViewSearchUi *pan_search_ui_new(PanWindow *pw)
{
	auto ui = g_new0(PanViewSearchUi, 1);
	GtkWidget *combo;
	GtkWidget *hbox;

	// Build the actual search UI.
	ui->search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_spacer(ui->search_box, 0);
	pref_label_new(ui->search_box, _("Find:"));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gq_gtk_box_pack_start(GTK_BOX(ui->search_box), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);

	combo = tab_completion_new_with_history(&ui->search_entry, "", "pan_view_search", -1,
						pan_search_activate_cb, pw);
	gq_gtk_box_pack_start(GTK_BOX(hbox), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);

	ui->search_label = gtk_label_new("");
	gq_gtk_box_pack_start(GTK_BOX(hbox), ui->search_label, TRUE, TRUE, 0);
	gtk_widget_show(ui->search_label);

	// Build the spin-button to show/hide the search UI.
	ui->search_button = gtk_toggle_button_new();
	gtk_button_set_relief(GTK_BUTTON(ui->search_button), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click(ui->search_button, FALSE);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	gq_gtk_container_add(GTK_WIDGET(ui->search_button), hbox);
	gtk_widget_show(hbox);
	ui->search_button_arrow = gtk_image_new_from_icon_name(GQ_ICON_PAN_UP, GTK_ICON_SIZE_BUTTON);
	gq_gtk_box_pack_start(GTK_BOX(hbox), ui->search_button_arrow, FALSE, FALSE, 0);
	gtk_widget_show(ui->search_button_arrow);
	pref_label_new(hbox, _("Find"));

	g_signal_connect(G_OBJECT(ui->search_button), "clicked",
			 G_CALLBACK(pan_search_toggle_cb), pw);

	return ui;
}

void pan_search_ui_destroy(PanViewSearchUi **ui_ptr)
{
	if (ui_ptr == nullptr || *ui_ptr == nullptr) return;

	g_free(*ui_ptr);
	*ui_ptr = nullptr;
}

static void pan_search_status(PanWindow *pw, const gchar *text)
{
	gtk_label_set_text(GTK_LABEL(pw->search_ui->search_label), (text) ? text : "");
}

static gint pan_search_by_path(PanWindow *pw, const gchar *path)
{
	PanItem *pi;
	GList *list;
	GList *found;
	PanItemType type;
	gchar *buf;

	type = (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE) ? PAN_ITEM_IMAGE : PAN_ITEM_THUMB;

	list = pan_item_find_by_path(pw, type, path, FALSE, FALSE);
	if (!list) return FALSE;

	found = g_list_find(list, pw->click_pi);
	if (found && found->next)
		{
		found = found->next;
		pi = static_cast<PanItem *>(found->data);
		}
	else
		{
		pi = static_cast<PanItem *>(list->data);
		}

	pan_info_update(pw, pi);
	image_scroll_to_point(pw->imd, pi->x + pi->width / 2, pi->y + pi->height / 2, 0.5, 0.5);

	buf = g_strdup_printf("%s ( %d / %u )",
	                      (path[0] == G_DIR_SEPARATOR) ? _("path found") : _("filename found"),
	                      g_list_index(list, pi) + 1,
	                      g_list_length(list));
	pan_search_status(pw, buf);
	g_free(buf);

	g_list_free(list);

	return TRUE;
}

static gboolean pan_search_by_partial(PanWindow *pw, const gchar *text)
{
	PanItem *pi;
	GList *list;
	GList *found;
	PanItemType type;
	gchar *buf;

	type = (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE) ? PAN_ITEM_IMAGE : PAN_ITEM_THUMB;

	list = pan_item_find_by_path(pw, type, text, TRUE, FALSE);
	if (!list) list = pan_item_find_by_path(pw, type, text, FALSE, TRUE);
	if (!list)
		{
		gchar *needle;

		needle = g_utf8_strdown(text, -1);
		list = pan_item_find_by_path(pw, type, needle, TRUE, TRUE);
		g_free(needle);
		}
	if (!list) return FALSE;

	found = g_list_find(list, pw->click_pi);
	if (found && found->next)
		{
		found = found->next;
		pi = static_cast<PanItem *>(found->data);
		}
	else
		{
		pi = static_cast<PanItem *>(list->data);
		}

	pan_info_update(pw, pi);
	image_scroll_to_point(pw->imd, pi->x + pi->width / 2, pi->y + pi->height / 2, 0.5, 0.5);

	buf = g_strdup_printf("%s ( %d / %u )",
	                      _("partial match"),
	                      g_list_index(list, pi) + 1,
	                      g_list_length(list));
	pan_search_status(pw, buf);
	g_free(buf);

	g_list_free(list);

	return TRUE;
}

static gboolean valid_date_separator(gchar c)
{
	return (c == '/' || c == '-' || c == ' ' || c == '.' || c == ',');
}

static GList *pan_search_by_date_val(PanWindow *pw, PanItemType type,
				     gint year, gint month, gint day,
				     const gchar *key)
{
	GList *list = nullptr;
	GList *work;

	work = g_list_last(pw->list_static);
	while (work)
		{
		PanItem *pi;

		pi = static_cast<PanItem *>(work->data);
		work = work->prev;

		if (pi->fd && (pi->type == type || type == PAN_ITEM_NONE) &&
		    ((!key && !pi->key) || (key && pi->key && strcmp(key, pi->key) == 0)))
			{
			struct tm *tl;

			tl = localtime(&pi->fd->date);
			if (tl)
				{
				gint match;

				match = (tl->tm_year == year - 1900);
				if (match && month >= 0) match = (tl->tm_mon == month - 1);
				if (match && day > 0) match = (tl->tm_mday == day);

				if (match) list = g_list_prepend(list, pi);
				}
			}
		}

	return g_list_reverse(list);
}

static gboolean pan_search_by_date(PanWindow *pw, const gchar *text)
{
	PanItem *pi = nullptr;
	GList *list = nullptr;
	GList *found;
	gint year;
	gint month = -1;
	gint day = -1;
	gchar *ptr;
	gchar *mptr;
	struct tm *lt;
	time_t t;
	gchar *message;
	gchar *buf;
	gchar *buf_count;

	if (!text) return FALSE;

	ptr = const_cast<gchar *>(text);
	while (*ptr != '\0')
		{
		if (!g_unichar_isdigit(*ptr) && !valid_date_separator(*ptr)) return FALSE;
		ptr++;
		}

	t = time(nullptr);
	if (t == -1) return FALSE;
	lt = localtime(&t);
	if (!lt) return FALSE;

	if (valid_date_separator(*text))
		{
		year = -1;
		mptr = const_cast<gchar *>(text);
		}
	else
		{
		year = static_cast<gint>(strtol(text, &mptr, 10));
		if (mptr == text) return FALSE;
		}

	if (*mptr != '\0' && valid_date_separator(*mptr))
		{
		gchar *dptr;

		mptr++;
		month = strtol(mptr, &dptr, 10);
		if (dptr == mptr)
			{
			if (valid_date_separator(*dptr))
				{
				month = lt->tm_mon + 1;
				dptr++;
				}
			else
				{
				month = -1;
				}
			}
		if (dptr != mptr && *dptr != '\0' && valid_date_separator(*dptr))
			{
			gchar *eptr;
			dptr++;
			day = strtol(dptr, &eptr, 10);
			if (dptr == eptr)
				{
				day = lt->tm_mday;
				}
			}
		}

	if (year == -1)
		{
		year = lt->tm_year + 1900;
		}
	else if (year < 100)
		{
		if (year > 70)
			year+= 1900;
		else
			year+= 2000;
		}

	if (year < 1970 ||
	    month < -1 || month == 0 || month > 12 ||
	    day < -1 || day == 0 || day > 31) return FALSE;

	t = pan_date_to_time(year, month, day);
	if (t < 0) return FALSE;

	if (pw->layout == PAN_LAYOUT_CALENDAR)
		{
		list = pan_search_by_date_val(pw, PAN_ITEM_BOX, year, month, day, "day");
		}
	else
		{
		PanItemType type;

		type = (pw->size > PAN_IMAGE_SIZE_THUMB_LARGE) ? PAN_ITEM_IMAGE : PAN_ITEM_THUMB;
		list = pan_search_by_date_val(pw, type, year, month, day, nullptr);
		}

	if (list)
		{
		found = g_list_find(list, pw->search_pi);
		if (found && found->next)
			{
			found = found->next;
			pi = static_cast<PanItem *>(found->data);
			}
		else
			{
			pi = static_cast<PanItem *>(list->data);
			}
		}

	pw->search_pi = pi;

	if (pw->layout == PAN_LAYOUT_CALENDAR && pi && pi->type == PAN_ITEM_BOX)
		{
		pan_info_update(pw, nullptr);
		pan_calendar_update(pw, pi);
		image_scroll_to_point(pw->imd,
				      pi->x + pi->width / 2,
				      pi->y + pi->height / 2, 0.5, 0.5);
		}
	else if (pi)
		{
		pan_info_update(pw, pi);
		image_scroll_to_point(pw->imd,
				      pi->x - PAN_BOX_BORDER * 5 / 2,
				      pi->y, 0.0, 0.5);
		}

	if (month > 0)
		{
		buf = pan_date_value_string(t, PAN_DATE_LENGTH_MONTH);
		if (day > 0)
			{
			gchar *tmp;
			tmp = buf;
			buf = g_strdup_printf("%d %s", day, tmp);
			g_free(tmp);
			}
		}
	else
		{
		buf = pan_date_value_string(t, PAN_DATE_LENGTH_YEAR);
		}

	if (pi)
		{
		buf_count = g_strdup_printf("( %d / %u )",
		                            g_list_index(list, pi) + 1,
		                            g_list_length(list));
		}
	else
		{
		buf_count = g_strdup_printf("(%s)", _("no match"));
		}

	message = g_strdup_printf("%s %s %s", _("Date:"), buf, buf_count);
	g_free(buf);
	g_free(buf_count);
	pan_search_status(pw, message);
	g_free(message);

	g_list_free(list);

	return TRUE;
}

void pan_search_activate_cb(const gchar *text, gpointer data)
{
	auto pw = static_cast<PanWindow *>(data);

	if (!text) return;

	tab_completion_append_to_history(pw->search_ui->search_entry, text);

	if (pan_search_by_path(pw, text)) return;

	if ((pw->layout == PAN_LAYOUT_TIMELINE ||
	     pw->layout == PAN_LAYOUT_CALENDAR) &&
	    pan_search_by_date(pw, text))
		{
		return;
		}

	if (pan_search_by_partial(pw, text)) return;

	pan_search_status(pw, _("no match"));
}

void pan_search_activate(PanWindow *pw)
{
	gchar *text;

	text = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(pw->search_ui->search_entry)));
	pan_search_activate_cb(text, pw);
	g_free(text);
}

void pan_search_toggle_cb(GtkWidget *button, gpointer data)
{
	auto pw = static_cast<PanWindow *>(data);
	PanViewSearchUi *ui = pw->search_ui;
	gboolean visible;
	GtkWidget *parent;

	visible = gtk_widget_get_visible(ui->search_box);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)) == visible) return;

	if (visible)
		{
		gtk_widget_hide(ui->search_box);

		parent = gtk_widget_get_parent(ui->search_button_arrow);

		gtk_container_remove(GTK_CONTAINER(parent), ui->search_button_arrow);
		ui->search_button_arrow = gtk_image_new_from_icon_name(GQ_ICON_PAN_UP, GTK_ICON_SIZE_BUTTON);

		gq_gtk_box_pack_start(GTK_BOX(parent), ui->search_button_arrow, FALSE, FALSE, 0);
		gtk_box_reorder_child(GTK_BOX(parent), ui->search_button_arrow, 0);

		gtk_widget_show(ui->search_button_arrow);
		}
	else
		{
		gtk_widget_show(ui->search_box);

		parent = gtk_widget_get_parent(ui->search_button_arrow);

		gtk_container_remove(GTK_CONTAINER(parent), ui->search_button_arrow);
		ui->search_button_arrow = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_BUTTON);

		gq_gtk_box_pack_start(GTK_BOX(parent), ui->search_button_arrow, FALSE, FALSE, 0);
		gtk_box_reorder_child(GTK_BOX(parent), ui->search_button_arrow, 0);

		gtk_widget_show(ui->search_button_arrow);
		gtk_widget_grab_focus(ui->search_entry);
		}
}

void pan_search_toggle_visible(PanWindow *pw, gboolean enable)
{
	PanViewSearchUi *ui = pw->search_ui;
	if (pw->fs) return;

	if (enable)
		{
		if (gtk_widget_get_visible(ui->search_box))
			{
			gtk_widget_grab_focus(ui->search_entry);
			}
		else
			{
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->search_button), TRUE);
			}
		}
	else
		{
		if (gtk_widget_get_visible(ui->search_entry))
			{
			if (gtk_widget_has_focus(ui->search_entry))
				{
				gtk_widget_grab_focus(GTK_WIDGET(pw->imd->widget));
				}
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->search_button), FALSE);
			}
		}
}
