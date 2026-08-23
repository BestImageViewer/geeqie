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

#include <string>

#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <pango/pango.h>

#include <config.h>

#include "accelerators.h"
#include "actions.h"
#include "dnd.h"
#include "exif.h"
#include "filedata.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "misc.h"
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

enum {
	EXIF_ADVCOL_TAG = 0,
	EXIF_ADVCOL_NAME,
	EXIF_ADVCOL_VALUE,
	EXIF_ADVCOL_FORMAT,
	EXIF_ADVCOL_ELEMENTS,
	EXIF_ADVCOL_DESCRIPTION,
	EXIF_ADVCOL_COUNT
};

struct AdvancedExifRow
{
	GObject parent_instance;
	gchar *values[EXIF_ADVCOL_COUNT];
};

struct AdvancedExifRowClass
{
	GObjectClass parent_class;
};

struct ExifWin
{
	GtkWidget *window;
	GtkWidget *scrolled;
	GtkWidget *column_view;
	GtkWidget *label_file_name;
	GListStore *store;
	GtkSingleSelection *selection;

	FileData *fd;
};

constexpr gint ADVANCED_EXIF_DATA_COLUMN_WIDTH = 200;

constexpr gchar ADVANCED_EXIF_ROW_DATA[] = "advanced-exif-row-data";
constexpr gchar ADVANCED_EXIF_COLUMN_DATA[] = "advanced-exif-column-data";
constexpr gchar ADVANCED_EXIF_LIST_ITEM_DATA[] = "advanced-exif-list-item-data";

G_DEFINE_TYPE(AdvancedExifRow, advanced_exif_row, G_TYPE_OBJECT)

void advanced_exif_row_finalize(GObject *object)
{
	auto *row = reinterpret_cast<AdvancedExifRow *>(object);

	for (gint n = EXIF_ADVCOL_TAG; n < EXIF_ADVCOL_COUNT; n++)
		{
		g_free(row->values[n]);
		}

	G_OBJECT_CLASS(advanced_exif_row_parent_class)->finalize(object);
}

void advanced_exif_row_class_init(AdvancedExifRowClass *row_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(row_class);
	object_class->finalize = advanced_exif_row_finalize;
}

void advanced_exif_row_init(AdvancedExifRow *)
{
}

AdvancedExifRow *advanced_exif_row_new(const gchar *tag, const gchar *name, const gchar *value,
                                       const gchar *format, const gchar *elements, const gchar *description)
{
	auto *row = reinterpret_cast<AdvancedExifRow *>(g_object_new(advanced_exif_row_get_type(), nullptr));

	row->values[EXIF_ADVCOL_TAG] = g_strdup(tag);
	row->values[EXIF_ADVCOL_NAME] = g_strdup(name);
	row->values[EXIF_ADVCOL_VALUE] = g_strdup(value);
	row->values[EXIF_ADVCOL_FORMAT] = g_strdup(format);
	row->values[EXIF_ADVCOL_ELEMENTS] = g_strdup(elements);
	row->values[EXIF_ADVCOL_DESCRIPTION] = g_strdup(description);

	return row;
}

} // namespace

static void advanced_exif_update(ExifWin *ew)
{
	ExifData *exif;

	ExifData *exif_original;
	ExifItem *item;

	exif = exif_read_fd(ew->fd);

	gtk_widget_set_sensitive(ew->scrolled, !!exif);

	if (!exif) return;

	exif_original = exif_get_original(exif);

	g_list_store_remove_all(ew->store);

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

		const std::string elements_text = std::to_string(elements);
		AdvancedExifRow *row = advanced_exif_row_new(tag, tag_name, utf8_text, format,
		                                                elements_text.c_str(), description);
		g_list_store_append(ew->store, row);
		g_object_unref(row);
		item = exif_get_next_item(exif_original);
		}
	exif_free_fd(ew->fd, exif);

}

static void advanced_exif_clear(ExifWin *ew)
{
	g_list_store_remove_all(ew->store);
}

static GdkContentProvider *advanced_exif_dnd_prepare(GtkDragSource *source, gdouble, gdouble, gpointer data)
{
	auto *ew = static_cast<ExifWin *>(data);
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
	auto *row = static_cast<AdvancedExifRow *>(g_object_get_data(G_OBJECT(widget), ADVANCED_EXIF_ROW_DATA));
	if (!row) return nullptr;

	auto *list_item = static_cast<GtkListItem *>(g_object_get_data(G_OBJECT(widget), ADVANCED_EXIF_LIST_ITEM_DATA));
	gtk_single_selection_set_selected(ew->selection, gtk_list_item_get_position(list_item));

	const gchar *key = row->values[EXIF_ADVCOL_NAME];

	return key ? gdk_content_provider_new_typed(G_TYPE_STRING, key) : nullptr;
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

static void advanced_exif_cell_clicked(GtkGestureClick *gesture, gint, gdouble, gdouble, gpointer data)
{
	auto *ew = static_cast<ExifWin *>(data);
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	auto *row = static_cast<AdvancedExifRow *>(g_object_get_data(G_OBJECT(widget), ADVANCED_EXIF_ROW_DATA));
	if (!row) return;

	auto *list_item = static_cast<GtkListItem *>(g_object_get_data(G_OBJECT(widget), ADVANCED_EXIF_LIST_ITEM_DATA));
	gtk_single_selection_set_selected(ew->selection, gtk_list_item_get_position(list_item));

	const gint column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), ADVANCED_EXIF_COLUMN_DATA));
	const gchar *value = row->values[column];
	if (!value) return;

	GdkClipboard *clipboard = gdk_display_get_primary_clipboard(gdk_display_get_default());
	gdk_clipboard_set_text(clipboard, value);
}

static void advanced_exif_factory_setup(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer data)
{
	auto *ew = static_cast<ExifWin *>(data);
	GtkWidget *label = gtk_label_new(nullptr);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(label, TRUE);
	g_object_set_data(G_OBJECT(label), ADVANCED_EXIF_COLUMN_DATA,
	                  g_object_get_data(G_OBJECT(factory), ADVANCED_EXIF_COLUMN_DATA));
	g_object_set_data(G_OBJECT(label), ADVANCED_EXIF_LIST_ITEM_DATA, list_item);

	GtkGesture *click = gtk_gesture_click_new();
	g_signal_connect(click, "released", G_CALLBACK(advanced_exif_cell_clicked), ew);
	gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(click));

	GtkDragSource *drag_source = gtk_drag_source_new();
	gtk_drag_source_set_actions(drag_source, GDK_ACTION_COPY);
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_source), 0);
	g_signal_connect(drag_source, "prepare", G_CALLBACK(advanced_exif_dnd_prepare), ew);
	gtk_widget_add_controller(label, GTK_EVENT_CONTROLLER(drag_source));

	gtk_list_item_set_child(list_item, label);
}

static void advanced_exif_factory_bind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	GtkWidget *label = gtk_list_item_get_child(list_item);
	auto *row = static_cast<AdvancedExifRow *>(gtk_list_item_get_item(list_item));
	const gint column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(label), ADVANCED_EXIF_COLUMN_DATA));

	gtk_label_set_text(GTK_LABEL(label), row->values[column]);
	g_object_set_data(G_OBJECT(label), ADVANCED_EXIF_ROW_DATA, row);
}

static void advanced_exif_factory_unbind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	GtkWidget *label = gtk_list_item_get_child(list_item);
	g_object_set_data(G_OBJECT(label), ADVANCED_EXIF_ROW_DATA, nullptr);
}

static gint advanced_exif_sort_cb(gconstpointer item_a, gconstpointer item_b, gpointer data)
{
	const gint column = GPOINTER_TO_INT(data);
	const auto *row_a = static_cast<const AdvancedExifRow *>(item_a);
	const auto *row_b = static_cast<const AdvancedExifRow *>(item_b);

	if (row_a->values[column] && row_b->values[column])
		{
		return g_utf8_collate(row_a->values[column], row_b->values[column]);
		}
	if (!row_a->values[column] && !row_b->values[column]) return 0;

	return row_a->values[column] ? 1 : -1;
}

static GtkColumnViewColumn *advanced_exif_add_column(ExifWin *ew, const gchar *title,
                                                     gint column_id, gboolean sizable)
{
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	g_object_set_data(G_OBJECT(factory), ADVANCED_EXIF_COLUMN_DATA, GINT_TO_POINTER(column_id));
	g_signal_connect(factory, "setup", G_CALLBACK(advanced_exif_factory_setup), ew);
	g_signal_connect(factory, "bind", G_CALLBACK(advanced_exif_factory_bind), nullptr);
	g_signal_connect(factory, "unbind", G_CALLBACK(advanced_exif_factory_unbind), nullptr);

	GtkColumnViewColumn *column = gtk_column_view_column_new(title, factory);
	gtk_column_view_column_set_resizable(column, TRUE);
	if (sizable) gtk_column_view_column_set_fixed_width(column, ADVANCED_EXIF_DATA_COLUMN_WIDTH);

	GtkSorter *sorter = GTK_SORTER(gtk_custom_sorter_new(advanced_exif_sort_cb,
	                                                   GINT_TO_POINTER(column_id), nullptr));
	gtk_column_view_column_set_sorter(column, sorter);
	g_object_unref(sorter);
	gtk_column_view_append_column(GTK_COLUMN_VIEW(ew->column_view), column);
	g_object_unref(column);

	return column;
}

static void advanced_exif_window_get_geometry(ExifWin *ew)
{
	LayoutWindow *lw = get_current_layout();
	if (!ew || !lw) return;

	lw->options.advanced_exif_window = widget_get_position_geometry(ew->window);
}

static void advanced_exif_close_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto ew = static_cast<ExifWin *>(data);

	if (!ew) return;

	advanced_exif_window_get_geometry(ew);
	file_data_unref(ew->fd);
	g_clear_object(&ew->selection);
	g_clear_object(&ew->store);

	gtk_window_destroy(GTK_WINDOW(ew->window));

	g_free(ew);
}

static gboolean advanced_exif_delete_cb(GtkWidget *, gpointer data)
{
	auto ew = static_cast<ExifWin *>(data);

	if (!ew) return FALSE;

	advanced_exif_window_get_geometry(ew);
	file_data_unref(ew->fd);
	g_clear_object(&ew->selection);
	g_clear_object(&ew->store);

	g_free(ew);

	return FALSE;
}

static void exif_window_context_help_cb(GSimpleAction *, GVariant *, gpointer)
{
	help_window_show("GuideOtherWindowsExif.html");
}

static void exif_window_help_cb(GtkWidget *, gpointer)
{
	help_window_show("GuideOtherWindowsExif.html");
}

static void exif_window_close(ExifWin *ew)
{
	gtk_window_destroy(GTK_WINDOW(ew->window));
}

static void exif_window_close_cb(GtkWidget *, gpointer data)
{
	auto ew = static_cast<ExifWin *>(data);

	exif_window_close(ew);
}

/* const ActionDef advanced_exif_actions[]
 */
#include "advanced-exif-actions.inc"

const ActionDef *get_advanced_exif_actions()
{
	return advanced_exif_actions;
}

GtkWidget *advanced_exif_new(LayoutWindow *lw)
{
	ExifWin *ew;
	GtkWidget *box;
	GtkWidget *button_box;
	GtkWidget *hbox;

	ew = g_new0(ExifWin, 1);

	ew->window = window_new("view", nullptr, _("Metadata"));
	DEBUG_NAME(ew->window);

    gtk_widget_set_size_request(GTK_WIDGET(ew->window), 900, 600);
    gtk_window_set_default_size(GTK_WINDOW(ew->window), 900, 600);

	gtk_window_set_resizable(GTK_WINDOW(ew->window), TRUE);

	gtk_window_set_default_size(GTK_WINDOW(ew->window), lw->options.advanced_exif_window.width, lw->options.advanced_exif_window.height);
	if (lw->options.advanced_exif_window.x != 0 && lw->options.advanced_exif_window.y != 0)
		{
		}

	g_object_set_data(G_OBJECT(ew->window), "advanced_exif_data", ew);
	g_signal_connect(G_OBJECT(ew->window), "close-request", G_CALLBACK(advanced_exif_delete_cb), ew);

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_window_set_child(GTK_WINDOW(ew->window), vbox);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	ew->label_file_name = gtk_label_new("");
	gtk_label_set_ellipsize(GTK_LABEL(ew->label_file_name), PANGO_ELLIPSIZE_START);
	gtk_label_set_selectable(GTK_LABEL(ew->label_file_name), TRUE);
	gtk_label_set_xalign(GTK_LABEL(ew->label_file_name), 0.5);
	gtk_label_set_yalign(GTK_LABEL(ew->label_file_name), 0.5);

	gtk_widget_set_hexpand(ew->label_file_name, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(box))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(ew->label_file_name, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(box))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(box), ew->label_file_name);

	gtk_box_append(GTK_BOX(vbox), box);


	ew->store = g_list_store_new(advanced_exif_row_get_type());
	ew->column_view = gtk_column_view_new(nullptr);

	GtkSorter *view_sorter = gtk_column_view_get_sorter(GTK_COLUMN_VIEW(ew->column_view));
	GtkSortListModel *sort_model = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(ew->store)),
	                                                     g_object_ref(view_sorter));
	ew->selection = GTK_SINGLE_SELECTION(gtk_single_selection_new(G_LIST_MODEL(sort_model)));
	gtk_single_selection_set_autoselect(ew->selection, FALSE);
	gtk_single_selection_set_can_unselect(ew->selection, TRUE);
	gtk_column_view_set_model(GTK_COLUMN_VIEW(ew->column_view), GTK_SELECTION_MODEL(ew->selection));

	advanced_exif_add_column(ew, _("Description"), EXIF_ADVCOL_DESCRIPTION, FALSE);
	advanced_exif_add_column(ew, _("Value"), EXIF_ADVCOL_VALUE, TRUE);
	GtkColumnViewColumn *name_column = advanced_exif_add_column(ew, _("Name"), EXIF_ADVCOL_NAME, FALSE);
	advanced_exif_add_column(ew, _("Tag"), EXIF_ADVCOL_TAG, FALSE);
	advanced_exif_add_column(ew, _("Format"), EXIF_ADVCOL_FORMAT, FALSE);
	advanced_exif_add_column(ew, _("Elements"), EXIF_ADVCOL_ELEMENTS, FALSE);
	gtk_column_view_sort_by_column(GTK_COLUMN_VIEW(ew->column_view), name_column, GTK_SORT_ASCENDING);

	ew->scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(ew->scrolled), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ew->scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
	gtk_widget_set_hexpand(ew->scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(vbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(ew->scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(vbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(vbox), ew->scrolled);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ew->scrolled), ew->column_view);

	button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_valign(button_box, GTK_ALIGN_START);
	gtk_box_append(GTK_BOX(vbox), button_box);
	gtk_widget_set_halign(button_box, GTK_ALIGN_END);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_SPACE);
	gtk_box_append(GTK_BOX(button_box), hbox);

	GtkWidget *button_help = pref_button_new(hbox, GQ_ICON_HELP, _("Help"), G_CALLBACK(exif_window_help_cb), ew);
	g_autofree gchar *help_accel = action_accelerator_label("app.help-contents");
	gtk_widget_set_tooltip_text(button_help, help_accel);
	gtk_widget_set_sensitive(button_help, TRUE);

	GtkWidget *button_close = pref_button_new(hbox, GQ_ICON_CLOSE, _("Close"), G_CALLBACK(exif_window_close_cb), ew);
	g_autofree gchar *close_accel = action_accelerator_label("win.advanced-exif-win-close");
	gtk_widget_set_tooltip_text(button_close, close_accel);
	gtk_widget_set_sensitive(button_close, TRUE);

	GApplication *app = g_application_get_default();
	register_actions_from_table(GTK_APPLICATION(app), ew->window, advanced_exif_actions, get_keyfile_merged(), ew);

	gtk_window_present(GTK_WINDOW(ew->window));
	return ew->window;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
