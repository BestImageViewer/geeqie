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

#include "ui-bookmark.h"

#include <cstdlib>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <pango/pango.h>

#include "compat.h"
#include "debug.h"
#include "history-list.h"
#include "intl.h"
#include "main-defines.h"
#include "misc.h"
#include "pixbuf-util.h"
#include "typedefs.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "ui-utildlg.h"
#include "uri-utils.h"

/*
 *-----------------------------------------------------------------------------
 * bookmarks
 *-----------------------------------------------------------------------------
 */

#define BOOKMARK_DATA_KEY "bookmarkdata"
#define MARKER_PATH "[path]"
#define MARKER_ICON "[icon]"

namespace
{

constexpr gint bookmark_drop_types_n = 3;
constexpr gint bookmark_drag_types_n = 2;

} // namespace

struct BookButtonData;

struct BookMarkData
{
	GtkWidget *widget;
	GtkWidget *box;
	const gchar *key;

	void (*select_func)(const gchar *path, gpointer data);
	gpointer select_data;

	gboolean no_defaults;
	gboolean editable;
	gboolean only_directories;

	BookButtonData *active_button;
};

struct BookButtonData
{
	GtkWidget *button;
	GtkWidget *image;
	GtkWidget *label;

	gchar *key;
	gchar *name;
	gchar *path;
	gchar *icon;
	gchar *parent;
};

struct BookPropData
{
	GtkWidget *name_entry;
	GtkWidget *path_entry;
	GtkWidget *icon_entry;

	BookButtonData *bb;
};

enum {
	TARGET_URI_LIST,
	TARGET_X_URL,
	TARGET_TEXT_PLAIN
};

static GtkTargetEntry bookmark_drop_types[] = {
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("x-url/http"),    0, TARGET_X_URL },
	{ const_cast<gchar *>("_NETSCAPE_URL"), 0, TARGET_X_URL }
};

static GtkTargetEntry bookmark_drag_types[] = {
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("text/plain"),    0, TARGET_TEXT_PLAIN }
};

static GList *bookmark_widget_list = nullptr;
static GList *bookmark_default_list = nullptr;


static void bookmark_populate_all(const gchar *key);


static BookButtonData *bookmark_from_string(const gchar *text)
{
	BookButtonData *b;
	const gchar *path_ptr;
	const gchar *icon_ptr;

	b = g_new0(BookButtonData, 1);

	if (!text)
		{
		b->name = g_strdup(_("New Bookmark"));
		b->path = g_strdup(homedir());
		b->key = nullptr;
		return b;
		}

	b->key = g_strdup(text);

	path_ptr = strstr(text, MARKER_PATH);
	icon_ptr = strstr(text, MARKER_ICON);

	if (path_ptr && icon_ptr && icon_ptr < path_ptr)
		{
		log_printf("warning, bookmark icon must be after path\n");
		return nullptr;
		}

	if (path_ptr)
		{
		gint l;

		l = path_ptr - text;
		b->name = g_strndup(text, l);
		path_ptr += strlen(MARKER_PATH);
		if (icon_ptr)
			{
			l = icon_ptr - path_ptr;
			b->path = g_strndup(path_ptr, l);
			}
		else
			{
			b->path = g_strdup(path_ptr);
			}
		}
	else
		{
		b->name = g_strdup(text);
		b->path = g_strdup("");
		}

	if (icon_ptr)
		{
		icon_ptr += strlen(MARKER_ICON);
		b->icon = g_strdup(icon_ptr);
		}

	return b;
}

static void bookmark_free(BookButtonData *b)
{
	if (!b) return;

	g_free(b->name);
	g_free(b->path);
	g_free(b->icon);
	g_free(b->key);
	g_free(b->parent);
	g_free(b);
}

static gchar *bookmark_string(const gchar *name, const gchar *path, const gchar *icon)
{
	if (!name) name = _("New Bookmark");

	if (icon)
		{
		return g_strdup_printf("%s" MARKER_PATH "%s" MARKER_ICON "%s", name, path, icon);
		}

	return g_strdup_printf("%s" MARKER_PATH "%s", name, path);
}

static void bookmark_select_cb(GtkWidget *button, gpointer data)
{
	auto bm = static_cast<BookMarkData *>(data);
	BookButtonData *b;

	b = static_cast<BookButtonData *>(g_object_get_data(G_OBJECT(button), "bookbuttondata"));
	if (!b) return;

	if (bm->select_func) bm->select_func(b->path, bm->select_data);
}

static void bookmark_edit_destroy_cb(GtkWidget *, gpointer data)
{
	auto p = static_cast<BookPropData *>(data);

	bookmark_free(p->bb);
	g_free(p);
}

static void bookmark_edit_cancel_cb(GenericDialog *, gpointer)
{
}

static void bookmark_edit_ok_cb(GenericDialog *, gpointer data)
{
	auto p = static_cast<BookPropData *>(data);
	const gchar *name;
	gchar *path;
	const gchar *icon;
	gchar *new_string;

	name = gq_gtk_entry_get_text(GTK_ENTRY(p->name_entry));
	path = remove_trailing_slash(gq_gtk_entry_get_text(GTK_ENTRY(p->path_entry)));
	icon = gq_gtk_entry_get_text(GTK_ENTRY(p->icon_entry));

	new_string = bookmark_string(name, path, icon);

	if (p->bb->key)
		{
		history_list_item_change(p->bb->parent, p->bb->key, new_string);
		}
	else
		{
		history_list_add_to_key(p->bb->parent, new_string, 0);
		}

	if (path && strlen(path) > 0) tab_completion_append_to_history(p->path_entry, path);
	if (icon && strlen(icon) > 0) tab_completion_append_to_history(p->icon_entry, icon);

	g_free(path);
	g_free(new_string);

	bookmark_populate_all(p->bb->parent);
}

/* simply pass NULL for text to turn this into a 'new bookmark' dialog */

static void bookmark_edit(const gchar *key, const gchar *text, GtkWidget *parent)
{
	BookPropData *p;
	GenericDialog *gd;
	GtkWidget *table;
	GtkWidget *label;
	const gchar *icon;

	if (!key) key = "bookmarks";

	p = g_new0(BookPropData, 1);

	p->bb = bookmark_from_string(text);
	p->bb->parent = g_strdup(key);

	gd = generic_dialog_new(_("Edit Bookmark"), "bookmark_edit",
				parent, TRUE,
				bookmark_edit_cancel_cb, p);
	g_signal_connect(G_OBJECT(gd->dialog), "destroy",
			 G_CALLBACK(bookmark_edit_destroy_cb), p);

	generic_dialog_add_message(gd, nullptr, _("Edit Bookmark"), nullptr, FALSE);

	generic_dialog_add_button(gd, GQ_ICON_OK, "OK",
				  bookmark_edit_ok_cb, TRUE);

	table = pref_table_new(gd->vbox, 3, 2, FALSE, TRUE);
	pref_table_label(table, 0, 0, _("Name:"), GTK_ALIGN_END);

	p->name_entry = gtk_entry_new();
	gtk_widget_set_size_request(p->name_entry, 300, -1);
	if (p->bb->name) gq_gtk_entry_set_text(GTK_ENTRY(p->name_entry), p->bb->name);
	gq_gtk_grid_attach_default(GTK_GRID(table), p->name_entry, 1, 2, 0, 1);
	generic_dialog_attach_default(gd, p->name_entry);
	gtk_widget_show(p->name_entry);

	pref_table_label(table, 0, 1, _("Path:"), GTK_ALIGN_END);

	label = tab_completion_new_with_history(&p->path_entry, p->bb->path,
						"bookmark_path", -1, nullptr, nullptr);
	tab_completion_add_select_button(p->path_entry, nullptr, TRUE);
	gq_gtk_grid_attach_default(GTK_GRID(table), label, 1, 2, 1, 2);
	generic_dialog_attach_default(gd, p->path_entry);
	gtk_widget_show(label);

	pref_table_label(table, 0, 2, _("Icon:"), GTK_ALIGN_END);

	icon = p->bb->icon;
	if (!icon) icon = "";
	label = tab_completion_new_with_history(&p->icon_entry, icon,
						"bookmark_icons", -1, nullptr, nullptr);
	tab_completion_add_select_button(p->icon_entry, _("Select icon"), FALSE);
	gq_gtk_grid_attach_default(GTK_GRID(table), label, 1, 2, 2, 3);
	generic_dialog_attach_default(gd, p->icon_entry);
	gtk_widget_show(label);

	gtk_widget_show(gd->dialog);
}

static void bookmark_move(BookMarkData *bm, GtkWidget *button, gint direction)
{
	BookButtonData *b;
	gint p;
	GList *list;
	const gchar *key_holder;

	if (!bm->editable) return;

	b = static_cast<BookButtonData *>(g_object_get_data(G_OBJECT(button), "bookbuttondata"));
	if (!b) return;

	list = gtk_container_get_children(GTK_CONTAINER(bm->box));
	p = g_list_index(list, button);
	g_list_free(list);

	if (p < 0 || p + direction < 0) return;

	key_holder = bm->key;
	bm->key = "_TEMPHOLDER";
	history_list_item_move(key_holder, b->key, -direction);
	bookmark_populate_all(key_holder);
	bm->key = key_holder;

	gtk_box_reorder_child(GTK_BOX(bm->box), button, p + direction);
}

static void bookmark_menu_prop_cb(GtkWidget *widget, gpointer data)
{
	auto bm = static_cast<BookMarkData *>(data);

	if (!bm->active_button) return;

	bookmark_edit(bm->key, bm->active_button->key, widget);
}

static void bookmark_menu_move(BookMarkData *bm, gint direction)
{
	if (!bm->active_button) return;

	bookmark_move(bm, bm->active_button->button, direction);
}

static void bookmark_menu_up_cb(GtkWidget *, gpointer data)
{
	bookmark_menu_move(static_cast<BookMarkData *>(data), -1);
}

static void bookmark_menu_down_cb(GtkWidget *, gpointer data)
{
	bookmark_menu_move(static_cast<BookMarkData *>(data), 1);
}

static void bookmark_menu_remove_cb(GtkWidget *, gpointer data)
{
	auto bm = static_cast<BookMarkData *>(data);

	if (!bm->active_button) return;

	history_list_item_remove(bm->key, bm->active_button->key);
	bookmark_populate_all(bm->key);
}

static void bookmark_menu_popup(BookMarkData *bm, GtkWidget *button, gint, guint32, gboolean local)
{
	GtkWidget *menu;
	BookButtonData *b;

	b = static_cast<BookButtonData *>(g_object_get_data(G_OBJECT(button), "bookbuttondata"));
	if (!b) return;

	bm->active_button = b;

	menu = popup_menu_short_lived();
	menu_item_add_icon_sensitive(menu, _("_Properties..."), PIXBUF_INLINE_ICON_PROPERTIES, bm->editable,
		      G_CALLBACK(bookmark_menu_prop_cb), bm);
	menu_item_add_icon_sensitive(menu, _("Move _up"), GQ_ICON_GO_UP, bm->editable,
		      G_CALLBACK(bookmark_menu_up_cb), bm);
	menu_item_add_icon_sensitive(menu, _("Move _down"), GQ_ICON_GO_DOWN, bm->editable,
		      G_CALLBACK(bookmark_menu_down_cb), bm);
	menu_item_add_icon_sensitive(menu, _("_Remove"), GQ_ICON_REMOVE, bm->editable,
		      G_CALLBACK(bookmark_menu_remove_cb), bm);

	if (local)
		{
		gtk_menu_popup_at_widget(GTK_MENU(menu), button, GDK_GRAVITY_NORTH_EAST, GDK_GRAVITY_CENTER, nullptr);
		}
	else
		{
		gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
		}
}

static gboolean bookmark_press_cb(GtkWidget *button, GdkEventButton *event, gpointer data)
{
	auto bm = static_cast<BookMarkData *>(data);

	if (event->button != MOUSE_BUTTON_RIGHT) return FALSE;

	bookmark_menu_popup(bm, button, event->button, event->time, FALSE);

	return TRUE;
}

static gboolean bookmark_keypress_cb(GtkWidget *button, GdkEventKey *event, gpointer data)
{
	auto bm = static_cast<BookMarkData *>(data);

	switch (event->keyval)
		{
		case GDK_KEY_F10:
			if (!(event->state & GDK_CONTROL_MASK)) return FALSE;
			/* fall through */
		case GDK_KEY_Menu:
			bookmark_menu_popup(bm, button, 0, event->time, TRUE);
			return TRUE;
			break;
		case GDK_KEY_Up:
			if (event->state & GDK_SHIFT_MASK)
				{
				bookmark_move(bm, button, -1);
				return TRUE;
				}
			break;
		case GDK_KEY_Down:
			if (event->state & GDK_SHIFT_MASK)
				{
				bookmark_move(bm, button, 1);
				return TRUE;
				}
			break;
		default:
			break;
		}

	return FALSE;
}

static void bookmark_drag_set_data(GtkWidget *button,
				   GdkDragContext *context, GtkSelectionData *selection_data,
				   guint, guint, gpointer data)
{
	auto bm = static_cast<BookMarkData *>(data);
	BookButtonData *b;
	GList *list = nullptr;

	return;
	if (gdk_drag_context_get_dest_window(context) == gtk_widget_get_window(bm->widget)) return;

	b = static_cast<BookButtonData *>(g_object_get_data(G_OBJECT(button), "bookbuttondata"));
	if (!b) return;

	list = g_list_append(list, b->path);

	gchar **uris = uris_from_pathlist(list);
	gboolean ret = gtk_selection_data_set_uris(selection_data, uris);
	if (!ret)
		{
		char *str = g_strjoinv("\r\n", uris);
		ret = gtk_selection_data_set_text(selection_data, str, -1);
		g_free(str);
		}

	g_strfreev(uris);
	g_list_free(list);
}

static void bookmark_drag_begin(GtkWidget *button, GdkDragContext *context, gpointer)
{
	GdkPixbuf *pixbuf;
	GdkModifierType mask;
	gint x;
	gint y;
	GtkAllocation allocation;
	GdkSeat *seat;
	GdkDevice *device;

	gtk_widget_get_allocation(button, &allocation);

	pixbuf = gdk_pixbuf_get_from_window(gtk_widget_get_window(button),
					    allocation.x, allocation.y,
					    allocation.width, allocation.height);
	seat = gdk_display_get_default_seat(gdk_window_get_display(gtk_widget_get_window(button)));
	device = gdk_seat_get_pointer(seat);
	gdk_window_get_device_position(gtk_widget_get_window(button), device, &x, &y, &mask);

	gtk_drag_set_icon_pixbuf(context, pixbuf,
				 x - allocation.x, y - allocation.y);
	g_object_unref(pixbuf);
}

static gboolean bookmark_path_tooltip_cb(GtkWidget *button, gpointer)
{
	BookButtonData *b;

	b = static_cast<BookButtonData *>(g_object_get_data(G_OBJECT(button), "bookbuttondata"));
	gtk_widget_set_tooltip_text(GTK_WIDGET(button), b->path);

	return FALSE;
}

static void bookmark_populate(BookMarkData *bm)
{
	GtkBox *box;
	GList *work;
	GList *children;

	box = GTK_BOX(bm->box);
	children = gtk_container_get_children(GTK_CONTAINER(box));
	work = children;
	while (work)
		{
		GtkWidget *widget = GTK_WIDGET(work->data);
		work = work->next;
		gq_gtk_widget_destroy(widget);
		}

	if (!bm->no_defaults && !history_list_get_by_key(bm->key))
		{
		gchar *buf;
		gchar *path;

		if (!bookmark_default_list)
			{
			buf = bookmark_string(_("Home"), homedir(), nullptr);
			history_list_add_to_key(bm->key, buf, 0);
			g_free(buf);

			if (g_strcmp0(bm->key, "shortcuts") != 0)
				{
				buf = bookmark_string(".", g_strdup(history_list_find_last_path_by_key("path_list")), nullptr);
				history_list_add_to_key(bm->key, buf, 0);
				g_free(buf);
				}

			path = g_build_filename(homedir(), "Desktop", NULL);
			if (isname(path))
				{
				buf = bookmark_string(_("Desktop"), path, nullptr);
				history_list_add_to_key(bm->key, buf, 0);
				g_free(buf);
				}
			g_free(path);
			}

		work = bookmark_default_list;
		while (work && work->next)
			{
			gchar *name;

			name = static_cast<gchar *>(work->data);
			work = work->next;
			path = static_cast<gchar *>(work->data);
			work = work->next;

			if (strcmp(name, ".") == 0)
				{
				if (g_strcmp0(bm->key, "shortcuts") != 0)
					{
					buf = bookmark_string(name, g_strdup(history_list_find_last_path_by_key("path_list")), nullptr);
					}
				else
					{
					continue;
					}
				}
			else
				{
				buf = bookmark_string(name, path, nullptr);
				}
			history_list_add_to_key(bm->key, buf, 0);
			g_free(buf);
			}
		}

	work = history_list_get_by_key(bm->key);
	work = g_list_last(work);
	while (work)
		{
		BookButtonData *b;

		b = bookmark_from_string(static_cast<const gchar *>(work->data));
		if (b)
			{
			if (strcmp(b->name, ".") == 0)
				{
				gchar *buf;

				b->path = g_strdup(history_list_find_last_path_by_key("path_list"));
				buf = bookmark_string(".", b->path, b->icon);
				history_list_item_change("bookmarks", b->key, buf);
				b->key = g_strdup(buf);
				g_free(buf);
				}
			GtkWidget *box;

			b->button = gtk_button_new();
			gtk_button_set_relief(GTK_BUTTON(b->button), GTK_RELIEF_NONE);
			gq_gtk_box_pack_start(GTK_BOX(bm->box), b->button, FALSE, FALSE, 0);
			gtk_widget_show(b->button);

			g_object_set_data_full(G_OBJECT(b->button), "bookbuttondata",
					       b, reinterpret_cast<GDestroyNotify>(bookmark_free));

			box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);
			gq_gtk_container_add(GTK_WIDGET(b->button), box);
			gtk_widget_show(box);

			if (b->icon)
				{
				GdkPixbuf *pixbuf = nullptr;
				gchar *iconl;

				iconl = path_from_utf8(b->icon);
				pixbuf = gdk_pixbuf_new_from_file(iconl, nullptr);

				if (isfile(b->icon))
					{
					pixbuf = gdk_pixbuf_new_from_file(iconl, nullptr);
					}
				else
					{
					gint w;
					gint h;

					w = h = 16;
					gtk_icon_size_lookup(GTK_ICON_SIZE_BUTTON, &w, &h);

					pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), b->icon, w, GTK_ICON_LOOKUP_NO_SVG, nullptr);
					}

				g_free(iconl);
				if (pixbuf)
					{
					GdkPixbuf *scaled;
					gint w;
					gint h;

					w = h = 16;
					gtk_icon_size_lookup(GTK_ICON_SIZE_BUTTON, &w, &h);

					scaled = gdk_pixbuf_scale_simple(pixbuf, w, h,
									 GDK_INTERP_BILINEAR);
					b->image = gtk_image_new_from_pixbuf(scaled);
					g_object_unref(scaled);
					g_object_unref(pixbuf);
					}
				else
					{
					b->image = gtk_image_new_from_icon_name(GQ_ICON_DIRECTORY, GTK_ICON_SIZE_BUTTON);
					}
				}
			else
				{
				b->image = gtk_image_new_from_icon_name(GQ_ICON_DIRECTORY, GTK_ICON_SIZE_BUTTON);
				}
			gq_gtk_box_pack_start(GTK_BOX(box), b->image, FALSE, FALSE, 0);
			gtk_widget_show(b->image);

			b->label = gtk_label_new(b->name);
			gq_gtk_box_pack_start(GTK_BOX(box), b->label, FALSE, FALSE, 0);
			gtk_widget_show(b->label);

			g_signal_connect(G_OBJECT(b->button), "clicked",
					 G_CALLBACK(bookmark_select_cb), bm);
			g_signal_connect(G_OBJECT(b->button), "button_press_event",
					 G_CALLBACK(bookmark_press_cb), bm);
			g_signal_connect(G_OBJECT(b->button), "key_press_event",
					 G_CALLBACK(bookmark_keypress_cb), bm);

			gtk_drag_source_set(b->button, GDK_BUTTON1_MASK,
					    bookmark_drag_types, bookmark_drag_types_n,
					    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
			g_signal_connect(G_OBJECT(b->button), "drag_data_get",
					 G_CALLBACK(bookmark_drag_set_data), bm);
			g_signal_connect(G_OBJECT(b->button), "drag_begin",
					 G_CALLBACK(bookmark_drag_begin), bm);

			gtk_widget_set_has_tooltip(GTK_WIDGET(b->button), TRUE);
			g_signal_connect(G_OBJECT(b->button), "query_tooltip", G_CALLBACK(bookmark_path_tooltip_cb), bm);
			}

		work = work->prev;
		}
}

static void bookmark_populate_all(const gchar *key)
{
	GList *work;

	if (!key) return;

	work = bookmark_widget_list;
	while (work)
		{
		BookMarkData *bm;

		bm = static_cast<BookMarkData *>(work->data);
		work = work->next;

		if (strcmp(bm->key, key) == 0)
			{
			bookmark_populate(bm);
			}
		}
}

static void bookmark_dnd_get_data(GtkWidget *, GdkDragContext *,
				  gint, gint,
				  GtkSelectionData *selection_data, guint,
				  guint, gpointer data)
{
	auto bm = static_cast<BookMarkData *>(data);
	GList *list = nullptr;
	GList *errors = nullptr;
	GList *work;
	gchar *real_path;
	gchar **uris;

	if (!bm->editable) return;

 	uris = gtk_selection_data_get_uris(selection_data);
 	if (uris)
		{
		list = uri_pathlist_from_uris(uris, &errors);
		if(errors)
			{
			warning_dialog_dnd_uri_error(errors);
			g_list_free_full(errors, g_free);
			}
		g_strfreev(uris);

		work = list;
		while (work)
			{
			auto path = static_cast<gchar *>(work->data);
			gchar *buf;

			work = work->next;

			if (bm->only_directories && !isdir(path)) continue;

			real_path = realpath(path, nullptr);

			if (strstr(real_path, get_collections_dir()) && isfile(path))
				{
				buf = bookmark_string(filename_from_path(path), path, PIXBUF_INLINE_COLLECTION);
				}
			else if (isfile(path))
				{
				buf = bookmark_string(filename_from_path(path), path, GQ_ICON_FILE);
				}
			else
				{
				buf = bookmark_string(filename_from_path(path), path, nullptr);
				}
			history_list_add_to_key(bm->key, buf, 0);
			g_free(buf);
			g_free(real_path);
			}

		g_list_free_full(list, g_free);

		bookmark_populate_all(bm->key);
		}
}

static void bookmark_list_destroy(gpointer data)
{
	auto bm = static_cast<BookMarkData *>(data);

	bookmark_widget_list = g_list_remove(bookmark_widget_list, bm);

	g_free(const_cast<gchar *>(bm->key));
	g_free(bm);
}

GtkWidget *bookmark_list_new(const gchar *key,
			     void (*select_func)(const gchar *path, gpointer data), gpointer select_data)
{
	GtkWidget *scrolled;
	BookMarkData *bm;

	if (!key) key = "bookmarks";

	bm = g_new0(BookMarkData, 1);
	bm->key = g_strdup(key);

	bm->select_func = select_func;
	bm->select_data = select_data;

	bm->no_defaults = FALSE;
	bm->editable = TRUE;
	bm->only_directories = FALSE;

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);

	PangoLayout *layout;
	gint width;
	gint height;

	layout = gtk_widget_create_pango_layout(GTK_WIDGET(scrolled), "reasonable width");
	pango_layout_get_pixel_size(layout, &width, &height);
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scrolled), width);
	g_object_unref(layout);

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	bm->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gq_gtk_container_add(GTK_WIDGET(scrolled), bm->box);
	gtk_widget_show(bm->box);

	bookmark_populate(bm);

	g_object_set_data_full(G_OBJECT(bm->box), BOOKMARK_DATA_KEY, bm, bookmark_list_destroy);
	g_object_set_data(G_OBJECT(scrolled), BOOKMARK_DATA_KEY, bm);
	bm->widget = scrolled;

	gtk_drag_dest_set(scrolled,
			  static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP),
			  bookmark_drop_types, bookmark_drop_types_n,
			  static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	g_signal_connect(G_OBJECT(scrolled), "drag_data_received",
			 G_CALLBACK(bookmark_dnd_get_data), bm);

	bookmark_widget_list = g_list_append(bookmark_widget_list, bm);

	return scrolled;
}

void bookmark_list_set_key(GtkWidget *list, const gchar *key)
{
	BookMarkData *bm;

	if (!list || !key) return;

	bm = static_cast<BookMarkData *>(g_object_get_data(G_OBJECT(list), BOOKMARK_DATA_KEY));
	if (!bm) return;

	if (bm->key && strcmp(bm->key, key) == 0) return;

	g_free(const_cast<gchar *>(bm->key));
	bm->key = g_strdup(key);

	bookmark_populate(bm);
}

void bookmark_list_set_no_defaults(GtkWidget *list, gboolean no_defaults)
{
	BookMarkData *bm;

	bm = static_cast<BookMarkData *>(g_object_get_data(G_OBJECT(list), BOOKMARK_DATA_KEY));
	if (!bm) return;

	bm->no_defaults = no_defaults;
}

void bookmark_list_set_editable(GtkWidget *list, gboolean editable)
{
	BookMarkData *bm;

	bm = static_cast<BookMarkData *>(g_object_get_data(G_OBJECT(list), BOOKMARK_DATA_KEY));
	if (!bm) return;

	bm->editable = editable;
}

void bookmark_list_set_only_directories(GtkWidget *list, gboolean only_directories)
{
	BookMarkData *bm;

	bm = static_cast<BookMarkData *>(g_object_get_data(G_OBJECT(list), BOOKMARK_DATA_KEY));
	if (!bm) return;

	bm->only_directories = only_directories;
}

void bookmark_list_add(GtkWidget *list, const gchar *name, const gchar *path)
{
	BookMarkData *bm;

	bm = static_cast<BookMarkData *>(g_object_get_data(G_OBJECT(list), BOOKMARK_DATA_KEY));
	if (!bm) return;

	const gchar *icon = nullptr;

	if (isfile(path))
		{
		g_autofree gchar *real_path = realpath(path, nullptr);

		if (strstr(real_path, get_collections_dir()))
			{
			icon = PIXBUF_INLINE_COLLECTION;
			}
		else
			{
			icon = GQ_ICON_FILE;
			}
		}

	g_autofree gchar *buf = bookmark_string(name, path, icon);
	history_list_add_to_key(bm->key, buf, 0);

	bookmark_populate_all(bm->key);
}

/**
 * @brief Allows apps to set up the defaults
 */
void bookmark_add_default(const gchar *name, const gchar *path)
{
	if (!name || !path) return;
	bookmark_default_list = g_list_append(bookmark_default_list, g_strdup(name));
	bookmark_default_list = g_list_append(bookmark_default_list, g_strdup(path));
}

/*
 *-----------------------------------------------------------------------------
 * combo with history key
 *-----------------------------------------------------------------------------
 */

struct HistoryComboData
{
	GtkWidget *combo;
	GtkWidget *entry;
	gchar *history_key;
	gint history_levels;
};

static void history_combo_destroy(gpointer data)
{
	auto hc = static_cast<HistoryComboData *>(data);

	g_free(hc->history_key);
	g_free(data);
}

/* if text is NULL, entry is set to the most recent item */
GtkWidget *history_combo_new(GtkWidget **entry, const gchar *text,
			     const gchar *history_key, gint max_levels)
{
	HistoryComboData *hc;
	GList *work;
	gint n = 0;

	hc = g_new0(HistoryComboData, 1);
	hc->history_key = g_strdup(history_key);
	hc->history_levels = max_levels;

	hc->combo = gtk_combo_box_text_new_with_entry();

	hc->entry = gtk_bin_get_child(GTK_BIN(hc->combo));

	g_object_set_data_full(G_OBJECT(hc->combo), "history_combo_data", hc, history_combo_destroy);
	g_object_set_data(G_OBJECT(hc->entry), "history_combo_data", hc);

	work = history_list_get_by_key(hc->history_key);
	while (work)
		{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(hc->combo), static_cast<gchar *>(work->data));
		work = work->next;
		n++;
		}

	if (text)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(hc->entry), text);
		}
	else if (n > 0)
		{
		gtk_combo_box_set_active(GTK_COMBO_BOX(hc->combo), 0);
		}

	if (entry) *entry = hc->entry;
	return hc->combo;
}

/* if text is NULL, current entry text is used
 * widget can be the combo or entry widget
 */
void history_combo_append_history(GtkWidget *widget, const gchar *text)
{
	HistoryComboData *hc;
	gchar *new_text;

	hc = static_cast<HistoryComboData *>(g_object_get_data(G_OBJECT(widget), "history_combo_data"));
	if (!hc)
		{
		log_printf("widget is not a history combo\n");
		return;
		}

	if (text)
		{
		new_text = g_strdup(text);
		}
	else
		{
		new_text = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(hc->entry)));
		}

	if (new_text && strlen(new_text) > 0)
		{
		GtkTreeModel *store;
		GList *work;

		history_list_add_to_key(hc->history_key, new_text, hc->history_levels);

		gtk_combo_box_set_active(GTK_COMBO_BOX(hc->combo), -1);

		store = gtk_combo_box_get_model(GTK_COMBO_BOX(hc->combo));
		gtk_list_store_clear(GTK_LIST_STORE(store));

		work = history_list_get_by_key(hc->history_key);
		while (work)
			{
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(hc->combo), static_cast<gchar *>(work->data));
			work = work->next;
			}
		}

	g_free(new_text);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
