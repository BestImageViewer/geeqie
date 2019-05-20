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
#include "advanced_exif.h"

#include "exif.h"
#include "metadata.h"
#include "filedata.h"
#include "history_list.h"
#include "layout_util.h"
#include "misc.h"
#include "ui_misc.h"
#include "window.h"
#include "dnd.h"

/* FIXME: not needed when bar_exif.c is improved */
#include "bar_exif.h"

#include <math.h>

#define ADVANCED_EXIF_DATA_COLUMN_WIDTH 200

/*
 *-------------------------------------------------------------------
 * EXIF window
 *-------------------------------------------------------------------
 */

typedef struct _ExifWin ExifWin;
struct _ExifWin
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *scrolled;
	GtkWidget *listview;
	GtkWidget *label_file_name;

	FileData *fd;
};

enum {
	EXIF_ADVCOL_ENABLED = 0,
	EXIF_ADVCOL_TAG,
	EXIF_ADVCOL_NAME,
	EXIF_ADVCOL_VALUE,
	EXIF_ADVCOL_FORMAT,
	EXIF_ADVCOL_ELEMENTS,
	EXIF_ADVCOL_DESCRIPTION,
	EXIF_ADVCOL_COUNT
};

gint display_order [6] = {
	EXIF_ADVCOL_DESCRIPTION,
	EXIF_ADVCOL_VALUE,
	EXIF_ADVCOL_NAME,
	EXIF_ADVCOL_TAG,
	EXIF_ADVCOL_FORMAT,
	EXIF_ADVCOL_ELEMENTS
};

static gboolean advanced_exif_row_enabled(const gchar *name)
{
	GList *list;

	if (!name) return FALSE;

	list = history_list_get_by_key("exif_extras");
	while (list)
		{
		if (strcmp(name, (gchar *)(list->data)) == 0) return TRUE;
		list = list->next;
	}

	return FALSE;
}

static void advanced_exif_update(ExifWin *ew)
{
	ExifData *exif;

	GtkListStore *store;
	GtkTreeIter iter;
	ExifData *exif_original;
	ExifItem *item;

	exif = exif_read_fd(ew->fd);

	gtk_widget_set_sensitive(ew->scrolled, !!exif);

	if (!exif) return;

	exif_original = exif_get_original(exif);

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(ew->listview)));
	gtk_list_store_clear(store);

	item = exif_get_first_item(exif_original);
	while (item)
		{
		gchar *tag;
		gchar *tag_name;
		gchar *text;
		gchar *utf8_text;
		const gchar *format;
		gchar *elements;
		gchar *description;

		tag = g_strdup_printf("0x%04x", exif_item_get_tag_id(item));
		tag_name = exif_item_get_tag_name(item);
		format = exif_item_get_format_name(item, TRUE);
		text = exif_item_get_data_as_text(item);
		utf8_text = utf8_validate_or_convert(text);
		g_free(text);
		elements = g_strdup_printf("%d", exif_item_get_elements(item));
		description = exif_item_get_description(item);
		if (!description || *description == '\0')
			{
			g_free(description);
			description = g_strdup(tag_name);
			}

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
				EXIF_ADVCOL_ENABLED, advanced_exif_row_enabled(tag_name),
				EXIF_ADVCOL_TAG, tag,
				EXIF_ADVCOL_NAME, tag_name,
				EXIF_ADVCOL_VALUE, utf8_text,
				EXIF_ADVCOL_FORMAT, format,
				EXIF_ADVCOL_ELEMENTS, elements,
				EXIF_ADVCOL_DESCRIPTION, description, -1);
		g_free(tag);
		g_free(utf8_text);
		g_free(elements);
		g_free(description);
		g_free(tag_name);
		item = exif_get_next_item(exif_original);
		}
	exif_free_fd(ew->fd, exif);

}

static void advanced_exif_clear(ExifWin *ew)
{
	GtkListStore *store;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(ew->listview)));
	gtk_list_store_clear(store);
}

void advanced_exif_set_fd(GtkWidget *window, FileData *fd)
{
	ExifWin *ew;

	ew = g_object_get_data(G_OBJECT(window), "advanced_exif_data");
	if (!ew) return;

	/* store this, advanced view toggle needs to reload data */
	file_data_unref(ew->fd);
	ew->fd = file_data_ref(fd);

	gtk_label_set_text(GTK_LABEL(ew->label_file_name), (ew->fd) ? ew->fd->path : "");

	advanced_exif_clear(ew);
	advanced_exif_update(ew);
}

static GtkTargetEntry advanced_exif_drag_types[] = {
	{ "text/plain", 0, TARGET_TEXT_PLAIN }
};
static gint n_exif_drag_types = 1;


static void advanced_exif_dnd_get(GtkWidget *listview, GdkDragContext *context,
				  GtkSelectionData *selection_data, guint info,
				  guint time, gpointer data)
{
	//ExifWin *ew = data;
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(sel, NULL, &iter))
		{
		GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(listview));
		gchar *key;

		gtk_tree_model_get(store, &iter, EXIF_ADVCOL_NAME, &key, -1);
		gtk_selection_data_set_text(selection_data, key, -1);
		//printf("%s\n",key);
		g_free(key);
		}

}


static void advanced_exif_dnd_begin(GtkWidget *listview, GdkDragContext *context, gpointer data)
{
	//ExifWin *ew = data;
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected(sel, NULL, &iter))
		{
		GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(listview));
		gchar *key;

		gtk_tree_model_get(store, &iter, EXIF_ADVCOL_NAME, &key, -1);

		dnd_set_drag_label(listview, context, key);
		g_free(key);
		}
}



static void advanced_exif_add_column(GtkWidget *listview, const gchar *title, gint n, gboolean sizable)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, title);

	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_column_set_sort_column_id(column, n);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", n);
	gtk_tree_view_append_column(GTK_TREE_VIEW(listview), column);

	if (sizable)
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_fixed_width(column, ADVANCED_EXIF_DATA_COLUMN_WIDTH);
		}
	else
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
		}
}

void advanced_exif_close(ExifWin *ew)
{
	if (!ew) return;

	gtk_widget_destroy(ew->window);
}

static void advanced_exif_destroy(GtkWidget *widget, gpointer data)
{
	ExifWin *ew = data;
	file_data_unref(ew->fd);
	g_free(ew);
}

static gint advanced_exif_sort_cb(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	gint n = GPOINTER_TO_INT(data);
	gint ret = 0;

	switch (n)
		{
		case EXIF_ADVCOL_DESCRIPTION:
		case EXIF_ADVCOL_VALUE:
		case EXIF_ADVCOL_NAME:
		case EXIF_ADVCOL_TAG:
		case EXIF_ADVCOL_FORMAT:
		case EXIF_ADVCOL_ELEMENTS:
			{
			gchar *s1, *s2;

			gtk_tree_model_get(model, a, n, &s1, -1);
			gtk_tree_model_get(model, b, n, &s2, -1);

			if (!s1 || !s2)
				{
			  	if (!s1 && !s2) break;
			  	ret = s1 ? 1 : -1;
				}
			else
				{
			  	ret = g_utf8_collate(s1, s2);
				}

			g_free(s1);
			g_free(s2);
			}
			break;

    		default:
       			g_return_val_if_reached(0);
		}

	return ret;
}

static gboolean advanced_exif_mouseclick(GtkWidget *widget,
						GdkEventButton *bevent, gpointer data)
{
	ExifWin *ew = data;
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	GtkTreeModel *store;
	gchar *value;
	GList *cols;
	gint col_num;
	GtkClipboard *clipboard;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(ew->listview), &path, &column);
	if (path && column)
		{
		store = gtk_tree_view_get_model(GTK_TREE_VIEW(ew->listview));
		gtk_tree_model_get_iter(store, &iter, path);

		cols = gtk_tree_view_get_columns(GTK_TREE_VIEW(ew->listview));
		col_num = g_list_index(cols, (gpointer)column);
		gtk_tree_model_get(store, &iter, display_order[col_num], &value, -1);

		clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		gtk_clipboard_set_text(clipboard, value, -1);

		g_list_free(cols);
		g_free(value);
		}

	return TRUE;
}

static gboolean advanced_exif_keypress(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	ExifWin *ew = data;
	gboolean stop_signal = FALSE;

	if (event->state & GDK_CONTROL_MASK)
		{
		switch (event->keyval)
			{
			case 'W': case 'w':
				advanced_exif_close(ew);
				stop_signal = TRUE;
				break;
			}
		} // if (event->state & GDK_CONTROL...
	if (!stop_signal && is_help_key(event))
		{
		help_window_show("GuideOtherWindowsExif.html");
		stop_signal = TRUE;
		}

	return stop_signal;
} // static gboolean advanced_exif_...

GtkWidget *advanced_exif_new(void)
{
	ExifWin *ew;
	GtkListStore *store;
	GdkGeometry geometry;
	GtkTreeSortable *sortable;
	GtkWidget *box;
	gint n;

	ew = g_new0(ExifWin, 1);

	ew->window = window_new(GTK_WINDOW_TOPLEVEL, "view", NULL, NULL, _("Metadata"));
	DEBUG_NAME(ew->window);

	geometry.min_width = 900;
	geometry.min_height = 600;
	gtk_window_set_geometry_hints(GTK_WINDOW(ew->window), NULL, &geometry, GDK_HINT_MIN_SIZE);

	gtk_window_set_resizable(GTK_WINDOW(ew->window), TRUE);

	g_object_set_data(G_OBJECT(ew->window), "advanced_exif_data", ew);
	g_signal_connect_after(G_OBJECT(ew->window), "destroy",
			       G_CALLBACK(advanced_exif_destroy), ew);

	ew->vbox = gtk_vbox_new(FALSE, PREF_PAD_GAP);
	gtk_container_add(GTK_CONTAINER(ew->window), ew->vbox);
	gtk_widget_show(ew->vbox);

	box = gtk_hbox_new(FALSE, 0);

	ew->label_file_name = gtk_label_new("");
	gtk_label_set_ellipsize(GTK_LABEL(ew->label_file_name), PANGO_ELLIPSIZE_START);
	gtk_label_set_selectable(GTK_LABEL(ew->label_file_name), TRUE);
	gtk_misc_set_alignment(GTK_MISC(ew->label_file_name), 0.5, 0.5);
	gtk_box_pack_start(GTK_BOX(box), ew->label_file_name, TRUE, TRUE, 0);
	gtk_widget_show(ew->label_file_name);

	gtk_box_pack_start(GTK_BOX(ew->vbox), box, FALSE, FALSE, 0);
	gtk_widget_show(box);


	store = gtk_list_store_new(7, G_TYPE_BOOLEAN,
				      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* set up sorting */
	sortable = GTK_TREE_SORTABLE(store);
	for (n = EXIF_ADVCOL_DESCRIPTION; n <= EXIF_ADVCOL_ELEMENTS; n++)
		gtk_tree_sortable_set_sort_func(sortable, n, advanced_exif_sort_cb,
				  		GINT_TO_POINTER(n), NULL);

	/* set initial sort order */
    	gtk_tree_sortable_set_sort_column_id(sortable, EXIF_ADVCOL_NAME, GTK_SORT_ASCENDING);

	ew->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(ew->listview), TRUE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ew->listview), TRUE);

	advanced_exif_add_column(ew->listview, _("Description"), EXIF_ADVCOL_DESCRIPTION, FALSE);
	advanced_exif_add_column(ew->listview, _("Value"), EXIF_ADVCOL_VALUE, TRUE);
	advanced_exif_add_column(ew->listview, _("Name"), EXIF_ADVCOL_NAME, FALSE);
	advanced_exif_add_column(ew->listview, _("Tag"), EXIF_ADVCOL_TAG, FALSE);
	advanced_exif_add_column(ew->listview, _("Format"), EXIF_ADVCOL_FORMAT, FALSE);
	advanced_exif_add_column(ew->listview, _("Elements"), EXIF_ADVCOL_ELEMENTS, FALSE);


	gtk_drag_source_set(ew->listview,
			   GDK_BUTTON1_MASK | GDK_BUTTON2_MASK,
			   advanced_exif_drag_types, n_exif_drag_types,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);

	g_signal_connect(G_OBJECT(ew->listview), "drag_data_get",
			 G_CALLBACK(advanced_exif_dnd_get), ew);

	g_signal_connect(G_OBJECT(ew->listview), "drag_begin",
			 G_CALLBACK(advanced_exif_dnd_begin), ew);

	g_signal_connect(G_OBJECT(ew->window), "key_press_event",
			 G_CALLBACK(advanced_exif_keypress), ew);

	g_signal_connect(G_OBJECT(ew->listview), "button_release_event",
			G_CALLBACK(advanced_exif_mouseclick), ew);

	ew->scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(ew->scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ew->scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_box_pack_start(GTK_BOX(ew->vbox), ew->scrolled, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(ew->scrolled), ew->listview);
	gtk_widget_show(ew->listview);
	gtk_widget_show(ew->scrolled);

	gtk_widget_show(ew->window);
	return ew->window;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
