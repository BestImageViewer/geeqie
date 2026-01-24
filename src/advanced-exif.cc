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

#include "advanced-exif.h"

#include <array>
#include <cstring>
#include <string>

#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <pango/pango.h>

#include <config.h>

#include "compat.h"
#include "dnd.h"
#include "exif.h"
#include "filedata.h"
#include "history-list.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "ui-misc.h"
#include "window.h"

struct ExifData;
struct ExifItem;

namespace
{

/*
 *-------------------------------------------------------------------
 * EXIF window
 *-------------------------------------------------------------------
 */

struct ExifWin
{
	GtkWidget *window;
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

constexpr gint display_order[6] = {
	EXIF_ADVCOL_DESCRIPTION,
	EXIF_ADVCOL_VALUE,
	EXIF_ADVCOL_NAME,
	EXIF_ADVCOL_TAG,
	EXIF_ADVCOL_FORMAT,
	EXIF_ADVCOL_ELEMENTS
};

constexpr gint ADVANCED_EXIF_DATA_COLUMN_WIDTH = 200;

#if !HAVE_GTK4
constexpr std::array<GtkTargetEntry, 1> advanced_exif_drag_types{{
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
}};
#endif

} // namespace

static gboolean advanced_exif_row_enabled(const gchar *name)
{
	if (!name) return FALSE;

	return g_list_find_custom(history_list_get_by_key("exif_extras"), name, reinterpret_cast<GCompareFunc>(strcmp)) ? TRUE : FALSE;
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
		g_autofree gchar *tag = g_strdup_printf("0x%04x", exif_item_get_tag_id(item));
		g_autofree gchar *tag_name = exif_item_get_tag_name(item);
		g_autofree gchar *text = exif_item_get_data_as_text(item, exif);
		g_autofree gchar *utf8_text = utf8_validate_or_convert(text);
		const gchar *format = exif_item_get_format_name(item, TRUE);
		const gint elements = exif_item_get_elements(item);
		g_autofree gchar *description = exif_item_get_description(item);
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
		                   EXIF_ADVCOL_ELEMENTS, std::to_string(elements).c_str(),
		                   EXIF_ADVCOL_DESCRIPTION, description,
		                   -1);
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

	ew = static_cast<ExifWin *>(g_object_get_data(G_OBJECT(window), "advanced_exif_data"));
	if (!ew) return;

	/* store this, advanced view toggle needs to reload data */
	file_data_unref(ew->fd);
	ew->fd = file_data_ref(fd);

	gtk_label_set_text(GTK_LABEL(ew->label_file_name), (ew->fd) ? ew->fd->path : "");

	advanced_exif_clear(ew);
	advanced_exif_update(ew);
}

#if !HAVE_GTK4
static void advanced_exif_dnd_get(GtkWidget *listview, GdkDragContext *,
				  GtkSelectionData *selection_data,
				  guint, guint, gpointer)
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected(sel, nullptr, &iter)) return;

	GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(listview));

	g_autofree gchar *key = nullptr;
	gtk_tree_model_get(store, &iter, EXIF_ADVCOL_NAME, &key, -1);

	gtk_selection_data_set_text(selection_data, key, -1);
}


static void advanced_exif_dnd_begin(GtkWidget *listview, GdkDragContext *context, gpointer)
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected(sel, nullptr, &iter)) return;

	GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(listview));

	g_autofree gchar *key = nullptr;
	gtk_tree_model_get(store, &iter, EXIF_ADVCOL_NAME, &key, -1);

	dnd_set_drag_label(listview, context, key);
}
#endif


static void advanced_exif_add_column(GtkWidget *listview, const gchar *title, gint n, gboolean sizable)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, title);

	if (sizable)
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_fixed_width(column, ADVANCED_EXIF_DATA_COLUMN_WIDTH);
		}
	else
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
		}

	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_column_set_sort_column_id(column, n);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", n);
	gtk_tree_view_append_column(GTK_TREE_VIEW(listview), column);
}

static void advanced_exif_window_get_geometry(ExifWin *ew)
{
	LayoutWindow *lw = get_current_layout();
	if (!ew || !lw) return;

#if HAVE_GTK4
	GdkSurface *surface;

	surface = gtk_native_get_surface(GTK_NATIVE(ew->window));
	if (!surface)
		{
		return;
		}

	lw->options.advanced_exif_window = widget_get_position_geometry(surface);
#else
	lw->options.advanced_exif_window = widget_get_position_geometry(ew->window);
#endif
}

static void advanced_exif_close(ExifWin *ew)
{
	if (!ew) return;

	advanced_exif_window_get_geometry(ew);
	file_data_unref(ew->fd);

	gq_gtk_widget_destroy(ew->window);

	g_free(ew);
}

static gboolean advanced_exif_delete_cb(GtkWidget *, GdkEvent *, gpointer data)
{
	auto ew = static_cast<ExifWin *>(data);

	if (!ew) return FALSE;

	advanced_exif_window_get_geometry(ew);
	file_data_unref(ew->fd);

	g_free(ew);

	return FALSE;
}

static gint advanced_exif_sort_cb(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	gint n = GPOINTER_TO_INT(data);

	if (n < EXIF_ADVCOL_TAG || n > EXIF_ADVCOL_DESCRIPTION) g_return_val_if_reached(0);

	return gq_gtk_tree_iter_utf8_collate(model, a, b, n);
}

#if HAVE_GTK4
static gboolean advanced_exif_mouseclick(GtkGestureClick *, gint, gdouble, gdouble, gpointer data)
#else
static gboolean advanced_exif_mouseclick(GtkWidget *, GdkEventButton *, gpointer data)
#endif
{
	auto ew = static_cast<ExifWin *>(data);
	g_autoptr(GtkTreePath) path = nullptr;
	GtkTreeViewColumn *column;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(ew->listview), &path, &column);
	if (path && column)
		{
		GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(ew->listview));
		GtkTreeIter iter;
		gtk_tree_model_get_iter(store, &iter, path);

		g_autoptr(GList) cols = gtk_tree_view_get_columns(GTK_TREE_VIEW(ew->listview));
		const gint col_num = g_list_index(cols, column);

		g_autofree gchar *value = nullptr;
		gtk_tree_model_get(store, &iter, display_order[col_num], &value, -1);

#if HAVE_GTK4
		GdkDisplay *display = gdk_display_get_default();
		GdkClipboard *clipboard = gdk_display_get_primary_clipboard(display);

		gdk_clipboard_set_text(clipboard, value);
#else
		GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);

		gtk_clipboard_set_text(clipboard, value, -1);
#endif

		gtk_tree_view_set_search_column(GTK_TREE_VIEW(ew->listview), gtk_tree_view_column_get_sort_column_id(column));
		}

	return TRUE;
}

static gboolean advanced_exif_keypress(GtkEventControllerKey *, guint keyval, guint, GdkModifierType state, gpointer data)
{
	auto ew = static_cast<ExifWin *>(data);
	gboolean stop_signal = FALSE;

	if (state & GDK_CONTROL_MASK)
		{
		switch (keyval)
			{
			case GDK_KEY_w:
			case GDK_KEY_W:
				advanced_exif_close(ew);
				stop_signal = TRUE;
				break;
			default:
				break;
			}
		}

	if (!stop_signal && is_help_key(keyval, state))
		{
		help_window_show("GuideOtherWindowsExif.html");
		stop_signal = TRUE;
		}

	return stop_signal;
}

static gboolean search_function_cb(GtkTreeModel *model, gint column, const gchar *key, GtkTreeIter *iter, gpointer)
{
	g_autofree gchar *field_contents = nullptr;
	gtk_tree_model_get(model, iter, column, &field_contents, -1);

	g_autofree gchar *field_contents_nocase = g_utf8_casefold(field_contents, -1);
	g_autofree gchar *key_nocase = g_utf8_casefold(key, -1);

	return g_strstr_len(field_contents_nocase, -1, key_nocase) == nullptr;
}

static void exif_window_help_cb(GtkWidget *, gpointer)
{
	help_window_show("GuideOtherWindowsExif.html");
}

static void exif_window_close(ExifWin *ew)
{
	gq_gtk_widget_destroy(ew->window);
}

static void exif_window_close_cb(GtkWidget *, gpointer data)
{
	auto ew = static_cast<ExifWin *>(data);

	exif_window_close(ew);
}

GtkWidget *advanced_exif_new(LayoutWindow *lw)
{
	ExifWin *ew;
	GtkListStore *store;
	GtkTreeSortable *sortable;
	GtkWidget *box;
	GtkWidget *button_box;
	GtkWidget *hbox;
	GtkEventController *controller;

	ew = g_new0(ExifWin, 1);

	ew->window = window_new("view", nullptr, _("Metadata"));
	DEBUG_NAME(ew->window);

#if HAVE_GTK4
    gtk_widget_set_size_request(GTK_WIDGET(ew->window), 900, 600);
    gtk_window_set_default_size(GTK_WINDOW(ew->window), 900, 600);
#else
	GdkGeometry geometry;

	geometry.min_width = 900;
	geometry.min_height = 600;

	gtk_window_set_geometry_hints(GTK_WINDOW(ew->window), nullptr, &geometry, GDK_HINT_MIN_SIZE);
#endif

	gtk_window_set_resizable(GTK_WINDOW(ew->window), TRUE);

	gq_gtk_window_resize(GTK_WINDOW(ew->window), lw->options.advanced_exif_window.width, lw->options.advanced_exif_window.height);
	if (lw->options.advanced_exif_window.x != 0 && lw->options.advanced_exif_window.y != 0)
		{
		gq_gtk_window_move(GTK_WINDOW(ew->window), lw->options.advanced_exif_window.x, lw->options.advanced_exif_window.y);
		}

	g_object_set_data(G_OBJECT(ew->window), "advanced_exif_data", ew);
	g_signal_connect(G_OBJECT(ew->window), "delete_event", G_CALLBACK(advanced_exif_delete_cb), ew);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gq_gtk_container_add(ew->window, vbox);
	gtk_widget_show(vbox);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	ew->label_file_name = gtk_label_new("");
	gtk_label_set_ellipsize(GTK_LABEL(ew->label_file_name), PANGO_ELLIPSIZE_START);
	gtk_label_set_selectable(GTK_LABEL(ew->label_file_name), TRUE);
	gtk_label_set_xalign(GTK_LABEL(ew->label_file_name), 0.5);
	gtk_label_set_yalign(GTK_LABEL(ew->label_file_name), 0.5);

	gq_gtk_box_pack_start(GTK_BOX(box), ew->label_file_name, TRUE, TRUE, 0);
	gtk_widget_show(ew->label_file_name);

	gq_gtk_box_pack_start(GTK_BOX(vbox), box, FALSE, FALSE, 0);
	gtk_widget_show(box);


	store = gtk_list_store_new(7, G_TYPE_BOOLEAN,
				      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/* set up sorting */
	sortable = GTK_TREE_SORTABLE(store);
	for (gint n = EXIF_ADVCOL_TAG; n <= EXIF_ADVCOL_DESCRIPTION; n++)
		gtk_tree_sortable_set_sort_func(sortable, n, advanced_exif_sort_cb,
		                                GINT_TO_POINTER(n), nullptr);

	/* set initial sort order */
	gtk_tree_sortable_set_sort_column_id(sortable, EXIF_ADVCOL_NAME, GTK_SORT_ASCENDING);

	ew->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ew->listview), TRUE);

	advanced_exif_add_column(ew->listview, _("Description"), EXIF_ADVCOL_DESCRIPTION, FALSE);
	advanced_exif_add_column(ew->listview, _("Value"), EXIF_ADVCOL_VALUE, TRUE);
	advanced_exif_add_column(ew->listview, _("Name"), EXIF_ADVCOL_NAME, FALSE);
	advanced_exif_add_column(ew->listview, _("Tag"), EXIF_ADVCOL_TAG, FALSE);
	advanced_exif_add_column(ew->listview, _("Format"), EXIF_ADVCOL_FORMAT, FALSE);
	advanced_exif_add_column(ew->listview, _("Elements"), EXIF_ADVCOL_ELEMENTS, FALSE);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(ew->listview), TRUE);
	gtk_tree_view_set_search_column(GTK_TREE_VIEW(ew->listview), EXIF_ADVCOL_DESCRIPTION);
	gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(ew->listview), search_function_cb, ew, nullptr);

#if !HAVE_GTK4
	gq_gtk_drag_source_set(ew->listview,
	                    static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
	                    advanced_exif_drag_types.data(), advanced_exif_drag_types.size(),
	                    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));

	gq_drag_g_signal_connect(G_OBJECT(ew->listview), "drag_data_get",
			 G_CALLBACK(advanced_exif_dnd_get), ew);

	gq_drag_g_signal_connect(G_OBJECT(ew->listview), "drag_begin",
			 G_CALLBACK(advanced_exif_dnd_begin), ew);
#endif

#if HAVE_GTK4
	controller = gtk_event_controller_key_new();
	gtk_widget_add_controller(ew->window, controller);
	g_object_unref(controller);
#else
	controller = gtk_event_controller_key_new(ew->window);
#endif
	g_signal_connect(controller, "key-pressed", G_CALLBACK(advanced_exif_keypress), ew);

#if HAVE_GTK4
	GtkGesture *click = gtk_gesture_click_new();

	gtk_widget_add_controller(ew->listview, GTK_EVENT_CONTROLLER(click));
	g_signal_connect(click, "released", G_CALLBACK(advanced_exif_mouseclick), ew);
	g_object_unref(click);
#else
	g_signal_connect(G_OBJECT(ew->listview), "button_release_event", G_CALLBACK(advanced_exif_mouseclick), ew);
#endif

	ew->scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(ew->scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ew->scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gq_gtk_box_pack_start(GTK_BOX(vbox), ew->scrolled, TRUE, TRUE, 0);
	gq_gtk_container_add(ew->scrolled, ew->listview);
	gtk_widget_show(ew->listview);
	gtk_widget_show(ew->scrolled);

	button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_end(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_SPACE);
	gq_gtk_box_pack_end(GTK_BOX(button_box), hbox, FALSE, FALSE, 0);

	GtkWidget *button_help = pref_button_new(hbox, GQ_ICON_HELP, _("Help"), G_CALLBACK(exif_window_help_cb), ew);
	gtk_widget_set_tooltip_text(button_help, "F1");
	gtk_widget_set_sensitive(button_help, TRUE);

	GtkWidget *button_close = pref_button_new(hbox, GQ_ICON_CLOSE, _("Close"), G_CALLBACK(exif_window_close_cb), ew);
	gtk_widget_set_tooltip_text(button_close, _("Ctrl-W"));
	gtk_widget_set_sensitive(button_close, TRUE);

	gq_gtk_widget_show_all(button_box);

	gtk_widget_show(ew->window);
	return ew->window;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
