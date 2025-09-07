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

#include "ui-tabcomp.h"

#include <dirent.h>

#include <cstring>

#include <gdk/gdk.h>
#include <glib-object.h>

#include "compat.h"
#include "history-list.h"
#include "intl.h"
#include "main-defines.h"
#include "misc.h"	/* expand_tilde() */
#include "options.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"

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
	TabCompEnterFunc enter_func;
	TabCompTabFunc tab_func;
	TabCompTabAppendFunc tab_append_func;

	gpointer enter_data;
	gpointer tab_data;
	gpointer tab_append_data;

	GtkWidget *combo;
	gboolean has_history;
	gchar *history_key;
	gint history_levels;

	gchar *fd_title;
	gboolean fd_folders_only;
	GtkWidget *fd_button;
	gchar *filter;
	gchar *filter_desc;
	gchar *shortcuts;

	guint choices;
};


static void tab_completion_select_show(TabCompData *td);
static gint tab_completion_do(TabCompData *td);
static TabCompData *tab_completion_set_to_entry(GtkWidget *entry);

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

	tab_completion_free_list(td);

	g_autofree gchar *pathl = path_from_utf8(path);
	dp = opendir(pathl);
	if (!dp)
		{
		/* dir not found */
		return;
		}

	while ((dir = readdir(dp)) != nullptr)
		{
		gchar *name = dir->d_name;
		if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0 &&
						(name[0] != '.' || options->file_filter.show_hidden_files))
			{
			g_autofree gchar *abspath = g_build_filename(pathl, name, NULL);

			if (g_file_test(abspath, G_FILE_TEST_IS_DIR))
				{
				g_autofree gchar *dname = g_strconcat(name, G_DIR_SEPARATOR_S, NULL);
				list = g_list_prepend(list, path_to_utf8(dname));
				}
			else
				{
				list = g_list_prepend(list, path_to_utf8(name));
				}
			}
		}
	closedir(dp);

	td->dir_path = g_strdup(path);
	td->file_list = list;
}

static void tab_completion_destroy(gpointer data)
{
	auto td = static_cast<TabCompData *>(data);

	tab_completion_free_list(td);
	g_free(td->history_key);

	g_free(td->fd_title);

	g_free(td->filter);
	g_free(td->filter_desc);
	g_free(td->shortcuts);

	g_free(td);
}

static gchar *tab_completion_get_text(TabCompData *td)
{
	gchar *text;

	text = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(td->entry)));

	if (text[0] == '~')
		{
		g_autofree gchar *t = text;
		text = expand_tilde(text);
		}

	return text;
}

static gboolean tab_completion_emit_enter_signal(TabCompData *td)
{
	if (!td->enter_func) return FALSE;

	g_autofree gchar *text = tab_completion_get_text(td);
	td->enter_func(text, td->enter_data);

	return TRUE;
}

static void tab_completion_emit_tab_signal(TabCompData *td)
{
	if (!td->tab_func) return;

	g_autofree gchar *text = tab_completion_get_text(td);
	td->tab_func(text, td->tab_data);
}

#ifdef TAB_COMPLETION_ENABLE_POPUP_MENU
static void tab_completion_iter_menu_items(GtkWidget *widget, gpointer data)
{
	auto td = static_cast<TabCompData *>(data);
	GtkWidget *child;

	if (!gtk_widget_get_visible(widget)) return;

	child = gtk_bin_get_child(GTK_BIN(widget));
	if (GTK_IS_LABEL(child)) {
		const gchar *text = gtk_label_get_text(GTK_LABEL(child));
		const gchar *entry_text = gq_gtk_entry_get_text(GTK_ENTRY(td->entry));
		const gchar *prefix = filename_from_path(entry_text);
		guint prefix_len = strlen(prefix);

		if (strlen(text) < prefix_len || strncmp(text, prefix, prefix_len) != 0)
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

	td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(widget), "tab_completion_data"));
	if (!td) return;

	g_autofree gchar *buf = g_build_filename(td->dir_path, name, NULL);
	gq_gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
	gtk_editable_set_position(GTK_EDITABLE(td->entry), -1);

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
	const gchar *entry_text = gq_gtk_entry_get_text(GTK_ENTRY(td->entry));
	const gchar *entry_file;
	gchar *ptr;
	gboolean home_exp = FALSE;

	if (entry_text[0] == '\0')
		{
		g_autofree gchar *entry_dir = g_strdup(G_DIR_SEPARATOR_S); /** @FIXME root directory win32 */
		gq_gtk_entry_set_text(GTK_ENTRY(td->entry), entry_dir);
		gtk_editable_set_position(GTK_EDITABLE(td->entry), -1);
		return FALSE;
		}

	g_autofree gchar *entry_dir = nullptr;

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
			gq_gtk_entry_set_text(GTK_ENTRY(td->entry), entry_dir);
			gtk_editable_set_position(GTK_EDITABLE(td->entry), -1);
			}
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
				gq_gtk_entry_set_text(GTK_ENTRY(td->entry), entry_dir);
				gtk_editable_set_position(GTK_EDITABLE(td->entry), -1);
				}

			tab_completion_read_dir(td, entry_dir);
			td->file_list = g_list_sort(td->file_list, simple_sort);
			if (td->file_list && !td->file_list->next)
				{
				const gchar *file;

				file = static_cast<const gchar *>(td->file_list->data);
				g_autofree gchar *buf = g_build_filename(entry_dir, file, NULL);
				if (isdir(buf))
					{
					g_autofree gchar *tmp = buf;
					buf = g_strconcat(buf, G_DIR_SEPARATOR_S, NULL);
					}
				gq_gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
				gtk_editable_set_position(GTK_EDITABLE(td->entry), -1);
				}
#ifdef TAB_COMPLETION_ENABLE_POPUP_MENU
			else
				{
				tab_completion_popup_list(td, td->file_list);
				}
#endif

			return home_exp;
			}

		g_autofree gchar *buf = g_strconcat(entry_dir, G_DIR_SEPARATOR_S, NULL);
		gq_gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
		gtk_editable_set_position(GTK_EDITABLE(td->entry), -1);
		return TRUE;
		}

	ptr = const_cast<gchar *>(filename_from_path(entry_dir));
	if (ptr > entry_dir) ptr--;
	ptr[0] = '\0';

	if (entry_dir[0] == '\0')
		{
		g_free(entry_dir);
		entry_dir = g_strdup(G_DIR_SEPARATOR_S); /** @FIXME win32 */
		}

	if (isdir(entry_dir))
		{
		GList *list;
		GList *poss = nullptr;
		size_t l = strlen(entry_file);

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

				g_autofree gchar *buf = g_build_filename(entry_dir, file, NULL);
				gq_gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
				gtk_editable_set_position(GTK_EDITABLE(td->entry), -1);
				g_list_free(poss);
				return TRUE;
				}

			gboolean done = FALSE;
			auto test_file = static_cast<gchar *>(poss->data);

			while (!done)
				{
				list = poss;
				while (list && !done)
					{
					auto file = static_cast<gchar *>(list->data);
					if (strlen(file) < l || strncmp(test_file, file, l) != 0)
						{
						done = TRUE;
						}
					list = list->next;
					}
				l++;
				}
			l -= 2;
			if (l > 0)
				{
				g_autofree gchar *file = g_strdup(test_file);
				file[l] = '\0';
				g_autofree gchar *buf = g_build_filename(entry_dir, file, NULL);
				gq_gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
				gtk_editable_set_position(GTK_EDITABLE(td->entry), -1);

#ifdef TAB_COMPLETION_ENABLE_POPUP_MENU
				poss = g_list_sort(poss, simple_sort);
				tab_completion_popup_list(td, poss);
#endif

				g_list_free(poss);
				return TRUE;
				}

			g_list_free(poss);
			}
		}

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
		button_allocation.y = parent_allocation.y;
		gtk_widget_size_allocate(button, &button_allocation);
		}
}

static GtkWidget *tab_completion_create_complete_button(GtkWidget *entry, GtkWidget *parent)
{
	GtkWidget *button;

	button = gtk_button_new_from_icon_name(GQ_ICON_GO_LAST, GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_can_focus(button, FALSE);
	g_signal_connect(G_OBJECT(button), "size_allocate",
			 G_CALLBACK(tab_completion_button_size_allocate), parent);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(tab_completion_button_pressed), entry);

	return button;
}

/*
 *----------------------------------------------------------------------------
 * public interface
 *----------------------------------------------------------------------------
 */

GtkWidget *tab_completion_new_with_history(GtkWidget **entry, const gchar *text,
                                           const gchar *history_key, gint max_levels)
{
	GtkWidget *box;
	GtkWidget *combo;
	GtkWidget *combo_entry;
	GtkWidget *button;
	GList *work;
	gint n = 0;

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	combo = gtk_combo_box_text_new_with_entry();
	gq_gtk_box_pack_start(GTK_BOX(box), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);

	combo_entry = gtk_bin_get_child(GTK_BIN(combo));

	button = tab_completion_create_complete_button(combo_entry, combo);
	gq_gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	TabCompData *td = tab_completion_set_to_entry(combo_entry);
	td->combo = combo;
	td->has_history = TRUE;
	td->history_key = g_strdup(history_key);
	td->history_levels = max_levels;

	work = history_list_get_by_key(history_key);
	while (work)
		{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), static_cast<gchar *>(work->data));
		work = work->next;
		n++;
		}

	if (text)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(combo_entry), text);
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
		gq_gtk_entry_set_text(GTK_ENTRY(td->entry), buf);
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
		td->tab_append_func(path, n, td->tab_append_data);
	}
}

GtkWidget *tab_completion_new(GtkWidget **entry, const gchar *text,
                              const gchar *filter, const gchar *filter_desc, const gchar *shortcuts)
{
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *newentry;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	newentry = gtk_entry_new();
	if (text) gq_gtk_entry_set_text(GTK_ENTRY(newentry), text);
	gq_gtk_box_pack_start(GTK_BOX(hbox), newentry, TRUE, TRUE, 0);
	gtk_widget_show(newentry);

	button = tab_completion_create_complete_button(newentry, newentry);
	gq_gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	TabCompData *td = tab_completion_set_to_entry(newentry);
	td->filter = g_strdup(filter);
	td->filter_desc = g_strdup(filter_desc);
	td->shortcuts = g_strdup(shortcuts);

	if (entry) *entry = newentry;
	return hbox;
}

static TabCompData *tab_completion_set_to_entry(GtkWidget *entry)
{
	if (!entry)
		{
		log_printf("Tab completion error: entry != NULL\n");
		return nullptr;
		}

	auto *td = g_new0(TabCompData, 1);
	td->entry = entry;

	g_object_set_data_full(G_OBJECT(entry), "tab_completion_data", td, tab_completion_destroy);

	g_signal_connect(G_OBJECT(entry), "key_press_event",
			 G_CALLBACK(tab_completion_key_pressed), td);
	return td;
}

void tab_completion_set_enter_func(GtkWidget *entry, TabCompEnterFunc enter_func, gpointer data)
{
	auto *td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(entry), "tab_completion_data"));
	if (!td) return;

	td->enter_func = enter_func;
	td->enter_data = data;
}

void tab_completion_set_tab_func(GtkWidget *entry, TabCompTabFunc tab_func, gpointer data)
{
	auto td = static_cast<TabCompData *>(g_object_get_data(G_OBJECT(entry), "tab_completion_data"));

	if (!td) return;

	td->tab_func = tab_func;
	td->tab_data = data;
}

/* Add a callback function called when a new entry is appended to the list */
void tab_completion_set_tab_append_func(GtkWidget *entry, TabCompTabAppendFunc tab_append_func, gpointer data)
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

static void tab_completion_response_cb(GtkFileChooser *chooser, gint response_id, gpointer data)
{
	auto td = static_cast<TabCompData *>(data);

	if (response_id == GTK_RESPONSE_ACCEPT)
		{
		g_autofree gchar *filename = gtk_file_chooser_get_filename(chooser);
		gq_gtk_entry_set_text(GTK_ENTRY(td->entry), filename);
		}

	gq_gtk_widget_destroy(GTK_WIDGET(chooser));

	tab_completion_emit_enter_signal(td);
}

static void tab_completion_select_show(TabCompData *td)
{
	FileChooserDialogData fcdd{};

	fcdd.action = td->fd_folders_only ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN;
	fcdd.accept_text = _("Open");
	fcdd.data = td;
	fcdd.filename = gtk_entry_get_text(GTK_ENTRY(td->entry));
	fcdd.filter = td->filter;
	fcdd.filter_description = td->filter_desc;
	fcdd.history_key = td->history_key;
	fcdd.response_callback = G_CALLBACK(tab_completion_response_cb);
	fcdd.shortcuts = td->shortcuts;
	fcdd.title = td->fd_title;

	GtkFileChooserDialog *dialog = file_chooser_dialog_new(fcdd);

	gq_gtk_widget_show_all(GTK_WIDGET(dialog));
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

	gq_gtk_box_pack_start(GTK_BOX(hbox), td->fd_button, FALSE, FALSE, 0);

	gtk_widget_show(td->fd_button);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
