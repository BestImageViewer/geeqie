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

#include <cstring>
#include <dirent.h>
#include <sys/types.h>

#include "main.h"
#include "ui-tabcomp.h"

#include "history-list.h"
#include "misc.h"	/* expand_tilde() */
#include "ui-fileops.h"
#include "ui-spinner.h"
#include "ui-utildlg.h"

/* define this to enable a pop-up menu that shows possible matches
 * #define TAB_COMPLETION_ENABLE_POPUP_MENU
 */
#define TAB_COMPLETION_ENABLE_POPUP_MENU 1
#define TAB_COMP_POPUP_MAX 1000

#ifdef TAB_COMPLETION_ENABLE_POPUP_MENU
#include "ui-menu.h"
#endif


/**
 * @file
 * ----------------------------------------------------------------
 * Tab completion routines, can be connected to any gtkentry widget
 * using the tab_completion_add_to_entry() function.
 *
 * Use remove_trailing_slash() to strip the trailing G_DIR_SEPARATOR.
 *
 * ----------------------------------------------------------------
 */

struct TabCompData
{
	GtkWidget *entry;
	gchar *dir_path;
	GList *file_list;
	void (*enter_func)(const gchar *, gpointer);
	void (*tab_func)(const gchar *, gpointer);
	void (*tab_append_func)(const gchar *, gpointer, gint);

	gpointer enter_data;
	gpointer tab_data;
	gpointer tab_append_data;

	GtkWidget *combo;
	gboolean has_history;
	gchar *history_key;
	gint history_levels;

	FileDialog *fd;
	gchar *fd_title;
	gboolean fd_folders_only;
	GtkWidget *fd_button;
	gchar *filter;
	gchar *filter_desc;

	guint choices;
};


static void tab_completion_select_show(TabCompData *td);
static gint tab_completion_do(TabCompData *td);

static void tab_completion_free_list(TabCompData *td)
{
	g_free(td->dir_path);
	td->dir_path = nullptr;

	g_list_free_full(td->file_list, g_free);
	td->file_list = nullptr;
}

static void tab_completion_read_dir(TabCompData *td, const gchar *path)
{
	DIR *dp;
	struct dirent *dir;
	GList *list = nullptr;
	gchar *pathl;

	tab_completion_free_list(td);

	pathl = path_from_utf8(path);
	dp = opendir(pathl);
	if (!dp)
		{
		/* dir not found */
		g_free(pathl);
		return;
		}
	while ((dir = readdir(dp)) != nullptr)
		{
		gchar *name = dir->d_name;
		if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0 &&
						(name[0] != '.' || options->file_filter.show_hidden_files))
			{
			gchar *abspath = g_build_filename(pathl, name, NULL);

			if (g_file_test(abspath, G_FILE_TEST_IS_DIR))
				{
				gchar *dname = g_strconcat(name, G_DIR_SEPARATOR_S, NULL);
				list = g_list_prepend(list, path_to_utf8(dname));
				g_free(dname);
				}
			else
				{
				list = g_list_prepend(list, path_to_utf8(name));
				}
			g_free(abspath);
			}
		}
	closedir(dp);

	td->dir_path = g_strdup(path);
	td->file_list = list;
	g_free(pathl);
}

static void tab_completion_destroy(GtkWidget *, gpointer data)
{
	auto td = static_cast<TabCompData *>(data);

	tab_completion_free_list(td);
	g_free(td->history_key);

	if (td->fd) file_dialog_close(td->fd);
	g_free(td->fd_title);

	g_free(td->filter);
	g_free(td->filter_desc);

	g_free(td);
}

static gchar *tab_completion_get_text(TabCompData *td)
{
	gchar *text;

	text = g_strdup(gtk_entry_get_text(GTK_ENTRY(td->entry)));

	if (text[0] == '~')
		{
		gchar *t = text;
		text = expand_tilde(text);
		g_free(t);
		}

	return text;
}

static gboolean tab_completion_emit_enter_signal(TabCompData *td)
{
	gchar *text;
	if (!td->enter_func) return FALSE;

	text = tab_completion_get_text(td);
	td->enter_func(text, td->enter_data);
	g_free(text);

	return TRUE;
}

static void tab_completion_emit_tab_signal(TabCompData *td)
{
	gchar *text;
	if (!td->tab_func) return;

	text = tab_completion_get_text(td);
	td->tab_func(text, td->tab_data);
	g_free(text);
}

#ifdef TAB_COMPLETION_ENABLE_POPUP_MENU
void tab_completion_iter_menu_items(GtkWidget *widget, gpointer data)
{
	auto td = static_cast<TabCompData *>(data);
	GtkWidget *child;

	if (!gtk_widget_get_visible(widget)) return;

	child = gtk_bin_get_child(GTK_BIN(widget));
	if (GTK_IS_LABEL(child)) {
		const gchar *text = gtk_label_get_text(GTK_LABEL(child));
		const gchar *entry_text = gtk_entry_get_text(GTK_ENTRY(td->entry));
		const gchar *prefix = filename_from_path(entry_text);
		guint prefix_len = strlen(prefix);

		if (strlen(text) < prefix_len || strncmp(text, prefix, prefix_len))
			{
			/* Hide menu items not matching */
			gtk_widget_hide(widget);
			}
		else
			{
			/* Count how many choices are left in the menu */
			td->choices++;
			}
	}
}

static gboolean tab_completion_popup_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto td = static_cast<TabCompData *>(data);

	if (event->keyval == GDK_KEY_Tab ||
	    event->keyval == GDK_KEY_BackSpace ||
	    (event->keyval >= 0x20 && event->keyval <= 0xFF) )
		{
		if (event->keyval >= 0x20 && event->keyval <= 0xFF)
			{
			gchar buf[2];
			gint p = -1;

			buf[0] = event->keyval;
			buf[1] = '\0';
			gtk_editable_insert_text(GTK_EDITABLE(td->entry), buf, 1, &p);
			gtk_editable_set_position(GTK_EDITABLE(td->entry), -1);

			/* Reduce the number of entries in the menu */
			td->choices = 0;
			gtk_container_foreach(GTK_CONTAINER(widget), tab_completion_iter_menu_items, td);
			if (td->choices > 1) return TRUE; /* multiple choices */
			if (td->choices > 0) tab_completion_do(td); /* one choice */
			}

		/* close the menu */
		gtk_menu_popdown(GTK_MENU(widget));
		/* doing this does not emit the "selection done" signal, unref it ourselves */
		g_object_unref(widget);
		return TRUE;
		}

	return FALSE;
}

static void tab_completion_popup_cb(GtkWidget *widget, gpointer data)
{
	auto name = static_cast<gchar *>(data);
	TabCompData *td;
	gchar *buf;

	td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(widget), "tab_completion_data"));
	if (!td) return;

	buf = g_build_filename(td->dir_path, name, NULL);
	gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
	gtk_editable_set_position(GTK_EDITABLE(td->entry), strlen(buf));
	g_free(buf);

	tab_completion_emit_tab_signal(td);
}

static void tab_completion_popup_list(TabCompData *td, GList *list)
{
	GtkWidget *menu;
	GList *work;
	gint count = 0;

	if (!list) return;

#if 0
	/*
	 * well, the menu would be too long anyway...
	 * (listing /dev causes gtk+ window allocation errors, -> too big a window)
	 * this is why menu popups are disabled, this really should be a popup scrollable listview.
	 */
	if (g_list_length(list) > 200) return;
#endif

	menu = popup_menu_short_lived();

	work = list;
	while (work && count < TAB_COMP_POPUP_MAX)
		{
		auto name = static_cast<gchar *>(work->data);
		GtkWidget *item;

		item = menu_item_add_simple(menu, name, G_CALLBACK(tab_completion_popup_cb), name);
		g_object_set_data(G_OBJECT(item), "tab_completion_data", td);

		work = work->next;
		count++;
		}

	g_signal_connect(G_OBJECT(menu), "key_press_event",
			 G_CALLBACK(tab_completion_popup_key_press), td);

	gtk_menu_popup_at_widget(GTK_MENU(menu), td->entry, GDK_GRAVITY_NORTH_EAST, GDK_GRAVITY_NORTH, nullptr);
}

#ifndef CASE_SORT
#define CASE_SORT strcmp
#endif

static gint simple_sort(gconstpointer a, gconstpointer b)
{
	return CASE_SORT((gchar *)a, (gchar *)b);
}

#endif

static gboolean tab_completion_do(TabCompData *td)
{
	const gchar *entry_text = gtk_entry_get_text(GTK_ENTRY(td->entry));
	const gchar *entry_file;
	gchar *entry_dir;
	gchar *ptr;
	gboolean home_exp = FALSE;

	if (entry_text[0] == '\0')
		{
		entry_dir = g_strdup(G_DIR_SEPARATOR_S); /** @FIXME root directory win32 */
		gtk_entry_set_text(GTK_ENTRY(td->entry), entry_dir);
		gtk_editable_set_position(GTK_EDITABLE(td->entry), strlen(entry_dir));
		g_free(entry_dir);
		return FALSE;
		}

	/* home dir expansion */
	if (entry_text[0] == '~')
		{
		entry_dir = expand_tilde(entry_text);
		home_exp = TRUE;
		}
	else
		{
		entry_dir = g_strdup(entry_text);
		}

	if (isfile(entry_dir))
		{
		if (home_exp)
			{
			gtk_entry_set_text(GTK_ENTRY(td->entry), entry_dir);
			gtk_editable_set_position(GTK_EDITABLE(td->entry), strlen(entry_dir));
			}
		g_free(entry_dir);
		return home_exp;
		}

	entry_file = filename_from_path(entry_text);

	if (isdir(entry_dir) && strcmp(entry_file, ".") != 0 && strcmp(entry_file, "..") != 0)
		{
		ptr = entry_dir + strlen(entry_dir) - 1;
		if (ptr[0] == G_DIR_SEPARATOR)
			{
			if (home_exp)
				{
				gtk_entry_set_text(GTK_ENTRY(td->entry), entry_dir);
				gtk_editable_set_position(GTK_EDITABLE(td->entry), strlen(entry_dir));
				}

			tab_completion_read_dir(td, entry_dir);
			td->file_list = g_list_sort(td->file_list, simple_sort);
			if (td->file_list && !td->file_list->next)
				{
				gchar *buf;
				const gchar *file;

				file = static_cast<const gchar *>(td->file_list->data);
				buf = g_build_filename(entry_dir, file, NULL);
				if (isdir(buf))
					{
					gchar *tmp = g_strconcat(buf, G_DIR_SEPARATOR_S, NULL);
					g_free(buf);
					buf = tmp;
					}
				gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
				gtk_editable_set_position(GTK_EDITABLE(td->entry), strlen(buf));
				g_free(buf);
				}

#ifdef TAB_COMPLETION_ENABLE_POPUP_MENU

			else
				{
				tab_completion_popup_list(td, td->file_list);
				}
#endif

			g_free(entry_dir);
			return home_exp;
			}
		else
			{
			gchar *buf = g_strconcat(entry_dir, G_DIR_SEPARATOR_S, NULL);
			gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
			gtk_editable_set_position(GTK_EDITABLE(td->entry), strlen(buf));
			g_free(buf);
			g_free(entry_dir);
			return TRUE;
			}
		}

	ptr = const_cast<gchar *>(filename_from_path(entry_dir));
	if (ptr > entry_dir) ptr--;
	ptr[0] = '\0';

	if (strlen(entry_dir) == 0)
		{
		g_free(entry_dir);
		entry_dir = g_strdup(G_DIR_SEPARATOR_S); /** @FIXME win32 */
		}

	if (isdir(entry_dir))
		{
		GList *list;
		GList *poss = nullptr;
		gint l = strlen(entry_file);

		if (!td->dir_path || !td->file_list || strcmp(td->dir_path, entry_dir) != 0)
			{
			tab_completion_read_dir(td, entry_dir);
			}

		list = td->file_list;
		while (list)
			{
			auto file = static_cast<gchar *>(list->data);
			if (strncmp(entry_file, file, l) == 0)
				{
				poss = g_list_prepend(poss, file);
				}
			list = list->next;
			}

		if (poss)
			{
			if (!poss->next)
				{
				auto file = static_cast<gchar *>(poss->data);
				gchar *buf;

				buf = g_build_filename(entry_dir, file, NULL);
				gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
				gtk_editable_set_position(GTK_EDITABLE(td->entry), strlen(buf));
				g_free(buf);
				g_list_free(poss);
				g_free(entry_dir);
				return TRUE;
				}
			else
				{
				gsize c = strlen(entry_file);
				gboolean done = FALSE;
				auto test_file = static_cast<gchar *>(poss->data);

				while (!done)
					{
					list = poss;
					if (!list) done = TRUE;
					while (list && !done)
						{
						auto file = static_cast<gchar *>(list->data);
						if (strlen(file) < c || strncmp(test_file, file, c) != 0)
							{
							done = TRUE;
							}
						list = list->next;
						}
					c++;
					}
				c -= 2;
				if (c > 0)
					{
					gchar *file;
					gchar *buf;
					file = g_strdup(test_file);
					file[c] = '\0';
					buf = g_build_filename(entry_dir, file, NULL);
					gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
					gtk_editable_set_position(GTK_EDITABLE(td->entry), strlen(buf));

#ifdef TAB_COMPLETION_ENABLE_POPUP_MENU

					poss = g_list_sort(poss, simple_sort);
					tab_completion_popup_list(td, poss);

#endif

					g_free(file);
					g_free(buf);
					g_list_free(poss);
					g_free(entry_dir);
					return TRUE;
					}
				}
			g_list_free(poss);
			}
		}

	g_free(entry_dir);

	return FALSE;
}

static gboolean tab_completion_key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto td = static_cast<TabCompData *>(data);
	gboolean stop_signal = FALSE;

	switch (event->keyval)
		{
		case GDK_KEY_Tab:
			if (!(event->state & GDK_CONTROL_MASK))
				{
				if (tab_completion_do(td))
					{
					tab_completion_emit_tab_signal(td);
					}
				stop_signal = TRUE;
				}
			break;
		case GDK_KEY_Return: case GDK_KEY_KP_Enter:
			if (td->fd_button &&
			    (event->state & GDK_CONTROL_MASK))
				{
				tab_completion_select_show(td);
				stop_signal = TRUE;
				}
			else if (tab_completion_emit_enter_signal(td))
				{
				stop_signal = TRUE;
				}
			break;
		default:
			break;
		}

	if (stop_signal) g_signal_stop_emission_by_name(G_OBJECT(widget), "key_press_event");

	return (stop_signal);
}

static void tab_completion_button_pressed(GtkWidget *, gpointer data)
{
	TabCompData *td;
	auto entry = static_cast<GtkWidget *>(data);

	td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(entry), "tab_completion_data"));

	if (!td) return;

	if (!gtk_widget_has_focus(entry))
		{
		gtk_widget_grab_focus(entry);
		}

	if (tab_completion_do(td))
		{
		tab_completion_emit_tab_signal(td);
		}
}

static void tab_completion_button_size_allocate(GtkWidget *button, GtkAllocation *allocation, gpointer data)
{
	auto parent = static_cast<GtkWidget *>(data);
	GtkAllocation parent_allocation;
	gtk_widget_get_allocation(parent, &parent_allocation);

	if (allocation->height > parent_allocation.height)
		{
		GtkAllocation button_allocation;

		gtk_widget_get_allocation(button, &button_allocation);
		button_allocation.height = parent_allocation.height;
		button_allocation.y = parent_allocation.y +
			(parent_allocation.height - parent_allocation.height) / 2;
		gtk_widget_size_allocate(button, &button_allocation);
		}
}

static GtkWidget *tab_completion_create_complete_button(GtkWidget *entry, GtkWidget *parent)
{
	GtkWidget *button;
	GtkWidget *icon;
	GdkPixbuf *pixbuf;

	button = gtk_button_new();
	gtk_widget_set_can_focus(button, FALSE);
	g_signal_connect(G_OBJECT(button), "size_allocate",
			 G_CALLBACK(tab_completion_button_size_allocate), parent);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(tab_completion_button_pressed), entry);

	pixbuf = gdk_pixbuf_new_from_inline(-1, icon_tabcomp, FALSE, nullptr);
	icon = gtk_image_new_from_pixbuf(pixbuf);
	g_object_unref(pixbuf);

	gtk_container_add(GTK_CONTAINER(button), icon);
	gtk_widget_show(icon);

	return button;
}

/*
 *----------------------------------------------------------------------------
 * public interface
 *----------------------------------------------------------------------------
 */

GtkWidget *tab_completion_new_with_history(GtkWidget **entry, const gchar *text,
					   const gchar *history_key, gint max_levels,
					   void (*enter_func)(const gchar *, gpointer), gpointer data)
{
	GtkWidget *box;
	GtkWidget *combo;
	GtkWidget *combo_entry;
	GtkWidget *button;
	GList *work;
	TabCompData *td;
	gint n = 0;

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	combo = gtk_combo_box_text_new_with_entry();
	gtk_box_pack_start(GTK_BOX(box), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);

	combo_entry = gtk_bin_get_child(GTK_BIN(combo));

	button = tab_completion_create_complete_button(combo_entry, combo);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	tab_completion_add_to_entry(combo_entry, enter_func, nullptr, nullptr, data);

	td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(combo_entry), "tab_completion_data"));
	if (!td) return nullptr; /* this should never happen! */

	td->combo = combo;
	td->has_history = TRUE;
	td->history_key = g_strdup(history_key);
	td->history_levels = max_levels;

	work = history_list_get_by_key(td->history_key);

	work = history_list_get_by_key(history_key);
	while (work)
		{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), static_cast<gchar *>(work->data));
		work = work->next;
		n++;
		}

	if (text)
		{
		gtk_entry_set_text(GTK_ENTRY(combo_entry), text);
		}
	else if (n > 0)
		{
		gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
		}

	if (entry) *entry = combo_entry;
	return box;
}

const gchar *tab_completion_set_to_last_history(GtkWidget *entry)
{
	auto td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(entry), "tab_completion_data"));
	const gchar *buf;

	if (!td || !td->has_history) return nullptr;

	buf = history_list_find_last_path_by_key(td->history_key);
	if (buf)
		{
		gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
		}

	return buf;
}

void tab_completion_append_to_history(GtkWidget *entry, const gchar *path)
{
	TabCompData *td;
	GtkTreeModel *store;
	GList *work;
	gint n = 0;

	td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(entry), "tab_completion_data"));

	if (!path) return;

	if (!td || !td->has_history) return;

	history_list_add_to_key(td->history_key, path, td->history_levels);

	gtk_combo_box_set_active(GTK_COMBO_BOX(td->combo), -1);

	store = gtk_combo_box_get_model(GTK_COMBO_BOX(td->combo));
	gtk_list_store_clear(GTK_LIST_STORE(store));

	work = history_list_get_by_key(td->history_key);
	while (work)
		{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(td->combo), static_cast<gchar *>(work->data));
		work = work->next;
		n++;
		}

	if (td->tab_append_func) {
		td->tab_append_func(path, td->tab_append_data, n);
	}
}

GtkWidget *tab_completion_new(GtkWidget **entry, const gchar *text,
			      void (*enter_func)(const gchar *, gpointer), const gchar *filter, const gchar *filter_desc, gpointer data)
{
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *newentry;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	newentry = gtk_entry_new();
	if (text) gtk_entry_set_text(GTK_ENTRY(newentry), text);
	gtk_box_pack_start(GTK_BOX(hbox), newentry, TRUE, TRUE, 0);
	gtk_widget_show(newentry);

	button = tab_completion_create_complete_button(newentry, newentry);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	tab_completion_add_to_entry(newentry, enter_func, filter, filter_desc, data);
	if (entry) *entry = newentry;
	return hbox;
}

void tab_completion_add_to_entry(GtkWidget *entry, void (*enter_func)(const gchar *, gpointer), const gchar *filter, const gchar *filter_desc, gpointer data)
{
	TabCompData *td;
	if (!entry)
		{
		log_printf("Tab completion error: entry != NULL\n");
		return;
		}

	td = g_new0(TabCompData, 1);

	td->entry = entry;
	td->enter_func = enter_func;
	td->enter_data = data;
	td->filter = g_strdup(filter);
	td->filter_desc = g_strdup(filter_desc);

	g_object_set_data(G_OBJECT(td->entry), "tab_completion_data", td);

	g_signal_connect(G_OBJECT(entry), "key_press_event",
			 G_CALLBACK(tab_completion_key_pressed), td);
	g_signal_connect(G_OBJECT(entry), "destroy",
			 G_CALLBACK(tab_completion_destroy), td);
}

void tab_completion_add_tab_func(GtkWidget *entry, void (*tab_func)(const gchar *, gpointer), gpointer data)
{
	auto td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(entry), "tab_completion_data"));

	if (!td) return;

	td->tab_func = tab_func;
	td->tab_data = data;
}

/* Add a callback function called when a new entry is appended to the list */
void tab_completion_add_append_func(GtkWidget *entry, void (*tab_append_func)(const gchar *, gpointer, gint), gpointer data)
{
	auto td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(entry), "tab_completion_data"));

	if (!td) return;

	td->tab_append_func = tab_append_func;
	td->tab_append_data = data;
}

gchar *remove_trailing_slash(const gchar *path)
{
	gint l;

	if (!path) return nullptr;

	l = strlen(path);
	while (l > 1 && path[l - 1] == G_DIR_SEPARATOR) l--;

	return g_strndup(path, l);
}

static void tab_completion_select_cancel_cb(FileDialog *fd, gpointer data)
{
	auto td = static_cast<TabCompData *>(data);

	td->fd = nullptr;
	file_dialog_close(fd);
}

static void tab_completion_select_ok_cb(FileDialog *fd, gpointer data)
{
	auto td = static_cast<TabCompData *>(data);

	gtk_entry_set_text(GTK_ENTRY(td->entry), gtk_entry_get_text(GTK_ENTRY(fd->entry)));

	tab_completion_select_cancel_cb(fd, data);

	tab_completion_emit_enter_signal(td);
}

static void tab_completion_select_show(TabCompData *td)
{
	const gchar *title;
	const gchar *path;
	const gchar *filter = nullptr;
	gchar *filter_desc = nullptr;

	if (td->fd)
		{
		gtk_window_present(GTK_WINDOW(GENERIC_DIALOG(td->fd)->dialog));
		return;
		}

	title = (td->fd_title) ? td->fd_title : _("Select path");
	td->fd = file_dialog_new(title, "select_path", td->entry,
				 tab_completion_select_cancel_cb, td);
	file_dialog_add_button(td->fd, GQ_ICON_OK, "OK",
				 tab_completion_select_ok_cb, TRUE);

	generic_dialog_add_message(GENERIC_DIALOG(td->fd), nullptr, title, nullptr, FALSE);

	if (td->filter)
		{
		filter = td->filter;
		}
	else
		{
		filter = "*";
		}
	if (td->filter_desc)
		{
		filter_desc = td->filter_desc;
		}
	else
		{
		filter_desc = _("All files");
		}

	path = gtk_entry_get_text(GTK_ENTRY(td->entry));
	if (strlen(path) == 0) path = nullptr;
	if (td->fd_folders_only)
		{
		file_dialog_add_path_widgets(td->fd, nullptr, path, td->history_key, nullptr, nullptr);
		}
	else
		{
		file_dialog_add_path_widgets(td->fd, nullptr, path, td->history_key, filter, filter_desc);
		}

	gtk_widget_show(GENERIC_DIALOG(td->fd)->dialog);
}

static void tab_completion_select_pressed(GtkWidget *, gpointer data)
{
	auto td = static_cast<TabCompData *>(data);

	tab_completion_select_show(td);
}

void tab_completion_add_select_button(GtkWidget *entry, const gchar *title, gboolean folders_only)
{
	TabCompData *td;
	GtkWidget *parent;
	GtkWidget *hbox;

	td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(entry), "tab_completion_data"));

	if (!td) return;

	g_free(td->fd_title);
	td->fd_title = g_strdup(title);
	td->fd_folders_only = folders_only;

	if (td->fd_button) return;

	parent = (td->combo) ? td->combo : td->entry;

	hbox = gtk_widget_get_parent(parent);
	if (!GTK_IS_BOX(hbox)) return;

	td->fd_button = gtk_button_new_with_label("...");
	g_signal_connect(G_OBJECT(td->fd_button), "size_allocate",
			 G_CALLBACK(tab_completion_button_size_allocate), parent);
	g_signal_connect(G_OBJECT(td->fd_button), "clicked",
			 G_CALLBACK(tab_completion_select_pressed), td);

	gtk_box_pack_start(GTK_BOX(hbox), td->fd_button, FALSE, FALSE, 0);

	gtk_widget_show(td->fd_button);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
