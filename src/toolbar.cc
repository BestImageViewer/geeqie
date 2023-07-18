/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2017 The Geeqie Team
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
#include "toolbar.h"

#include "layout-util.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "pixbuf-util.h"
#include "ui-menu.h"
#include "editors.h"

/** Implements the user-definable toolbar function
 * Called from the Preferences/toolbar tab
 **/

struct ToolbarData
{
	GtkWidget *widget;
	GtkWidget *vbox;
	GtkWidget *add_button;

	LayoutWindow *lw;
};

struct ToolbarButtonData
{
	GtkWidget *button;
	GtkWidget *button_label;
	GtkWidget *image;

	const gchar *name; /* GtkActionEntry terminology */
	const gchar *stock_id;
};

static ToolbarData *toolbarlist[2];

struct UseableToolbarItems
{
	const gchar *name; /* GtkActionEntry terminology */
	const gchar *label;
	const gchar *stock_id;
};

/** @FIXME Should be created by program from menu_entries[]
 * in layout-util.cc */
 /** The user is limited to selecting from this list of menu items
  * plus any desktop files
  **/
static constexpr UseableToolbarItems useable_toolbar_items[] = {
	{"FirstImage",	N_("First Image"), GTK_STOCK_GOTO_TOP},
	{"PrevImage",	N_("Previous Image"), GTK_STOCK_GO_UP},
	{"NextImage",	N_("Next Image"), GTK_STOCK_GO_DOWN},
	{"LastImage",	N_("Last Image"), GTK_STOCK_GOTO_BOTTOM},
	{"Back",	N_("Back"), GTK_STOCK_GO_BACK},
	{"Forward",	N_("Forward"), GTK_STOCK_GO_FORWARD},
	{"Home",	N_("Home"), GTK_STOCK_HOME},
	{"Up",	N_("Up"), GTK_STOCK_GO_UP},
	{"FirstPage",	N_("First page"), GTK_STOCK_MEDIA_PREVIOUS},
	{"LastPage",	N_("Last Page"), GTK_STOCK_MEDIA_NEXT},
	{"NextPage",	N_("Next page"), GTK_STOCK_MEDIA_FORWARD},
	{"PrevPage",	N_("Previous Page"), GTK_STOCK_MEDIA_REWIND},
	{"ImageForward",	N_("Image Forward"), GTK_STOCK_GOTO_LAST},
	{"ImageBack",	N_("Image Back"), GTK_STOCK_GOTO_FIRST},
	{"NewWindow",	N_("New window"), GTK_STOCK_NEW},
	{"NewCollection",	N_("New collection"), GTK_STOCK_INDEX},
	{"OpenCollection",	N_("Open collection"), GTK_STOCK_OPEN},
	{"Search",	N_("Search"), GTK_STOCK_FIND},
	{"FindDupes",	N_("Find duplicates"), GTK_STOCK_FIND},
	{"NewFolder",	N_("New folder"),GTK_STOCK_DIRECTORY},
	{"Copy",	N_("Copy"), GTK_STOCK_COPY},
	{"Move",	N_("Move"), PIXBUF_INLINE_ICON_MOVE},
	{"Rename",	N_("Rename"), PIXBUF_INLINE_ICON_RENAME},
	{"Delete",	N_("Delete"), GTK_STOCK_DELETE},
	{"CloseWindow",	N_("Close Window"), GTK_STOCK_CLOSE},
	{"PanView",	N_("Pan view"), PIXBUF_INLINE_ICON_PANORAMA},
	{"OpenArchive",	N_("Open Archive"), PIXBUF_INLINE_ARCHIVE},
	{"SelectAll",	N_("Select all"), PIXBUF_INLINE_ICON_SELECT_ALL},
	{"SelectNone",	N_("Select none"), PIXBUF_INLINE_ICON_SELECT_NONE},
	{"SelectInvert",	N_("Select invert"), PIXBUF_INLINE_ICON_SELECT_INVERT},
	{"ShowFileFilter",	N_("Show file filter"), PIXBUF_INLINE_ICON_FILE_FILTER},
	{"RectangularSelection",	N_("Select rectangle"), PIXBUF_INLINE_ICON_SELECT_RECTANGLE},
	{"Print",	N_("Print"), GTK_STOCK_PRINT},
	{"Preferences",	N_("Preferences"), GTK_STOCK_PREFERENCES},
	{"LayoutConfig",	N_("Configure this window"), GTK_STOCK_PREFERENCES},
	{"Maintenance",	N_("Cache maintenance"), PIXBUF_INLINE_ICON_MAINTENANCE},
	{"RotateCW",	N_("Rotate clockwise 90°"), PIXBUF_INLINE_ICON_CW},
	{"RotateCCW",	N_("Rotate counterclockwise 90°"), PIXBUF_INLINE_ICON_CCW},
	{"Rotate180",	N_("Rotate 180°"), PIXBUF_INLINE_ICON_180},
	{"Mirror",	N_("Mirror"), PIXBUF_INLINE_ICON_MIRROR},
	{"Flip",	N_("Flip"), PIXBUF_INLINE_ICON_FLIP},
	{"AlterNone",	N_("Original state"), PIXBUF_INLINE_ICON_ORIGINAL},
	{"ZoomIn",	N_("Zoom in"), GTK_STOCK_ZOOM_IN},
	{"ZoomOut",	N_("Zoom out"), GTK_STOCK_ZOOM_OUT},
	{"Zoom100",	N_("Zoom 1:1"), GTK_STOCK_ZOOM_100},
	{"ZoomFit",	N_("Zoom to fit"), GTK_STOCK_ZOOM_FIT},
	{"ZoomFillHor",	N_("Fit Horizontaly"), PIXBUF_INLINE_ICON_ZOOMFILLHOR},
	{"ZoomFillVert",	N_("Fit vertically"), PIXBUF_INLINE_ICON_ZOOMFILLVERT},
	{"Zoom200",	N_("Zoom 2:1"), GTK_STOCK_FILE},
	{"Zoom300",	N_("Zoom 3:1"), GTK_STOCK_FILE},
	{"Zoom400",	N_("Zoom 4:1"), GTK_STOCK_FILE},
	{"Zoom50",	N_("Zoom 1:2"), GTK_STOCK_FILE},
	{"Zoom33",	N_("Zoom1:3"), GTK_STOCK_FILE},
	{"Zoom25",	N_("Zoom 1:4"), GTK_STOCK_FILE},
	{"ConnectZoomIn",	N_("Connected Zoom in"), GTK_STOCK_ZOOM_IN},
	{"SplitPaneSync",	N_("Split Pane Sync"), PIXBUF_INLINE_SPLIT_PANE_SYNC},
	{"Grayscale",	N_("Grayscale"), PIXBUF_INLINE_ICON_GRAYSCALE},
	{"OverUnderExposed",	N_("Over Under Exposed"), PIXBUF_INLINE_ICON_EXPOSURE},
	{"ShowInfoPixel",	N_("Pixel Info"), 	GTK_STOCK_COLOR_PICKER},
	{"IgnoreAlpha",	N_("Ignore Alpha"), 	GTK_STOCK_STRIKETHROUGH},
	{"ExifRotate",	N_("Exif rotate"),	GTK_STOCK_ORIENTATION_PORTRAIT},
	{"UseColorProfiles",	N_("Use color profiles"),	GTK_STOCK_SELECT_COLOR},
	{"SaveMetadata",	N_("Save metadata"),	GTK_STOCK_SAVE},
	{"HideTools",	N_("Hide file list"), PIXBUF_INLINE_ICON_HIDETOOLS},
	{"SlideShowPause",	N_("Pause slideshow"), GTK_STOCK_MEDIA_PAUSE},
	{"SlideShowFaster",	N_("Slideshow Faster"), GTK_STOCK_FILE},
	{"SlideShowSlower",	N_("Slideshow Slower"), GTK_STOCK_FILE},
	{"Refresh",	N_("Refresh"), GTK_STOCK_REFRESH},
	{"HelpContents",	N_("Help"), GTK_STOCK_HELP},
	{"ExifWin",	N_("Exif window"), PIXBUF_INLINE_ICON_EXIF},
	{"Thumbnails",	N_("Show thumbnails"), PIXBUF_INLINE_ICON_THUMB},
	{"ShowMarks",	N_("Show marks"), PIXBUF_INLINE_ICON_MARKS},
	{"DrawRectangle",	N_("Draw Rectangle"), PIXBUF_INLINE_ICON_DRAW_RECTANGLE},
	{"FloatTools",	N_("Float file list"), PIXBUF_INLINE_ICON_FLOAT},
	{"SBar",	N_("Info sidebar"), PIXBUF_INLINE_ICON_INFO},
	{"SBarSort",	N_("Sort manager"), PIXBUF_INLINE_ICON_SORT},
	{"Quit",	N_("Quit"), GTK_STOCK_QUIT},
};

/**
 * @brief
 * @param widget Not used
 * @param data Pointer to vbox list item
 * @param up Up/Down movement
 * @param single_step Move up/down one step, or to top/bottom
 *
 */
static void toolbar_item_move(GtkWidget *UNUSED(widget), gpointer data,
									gboolean up, gboolean single_step)
{
	auto list_item = static_cast<GtkWidget *>(data);
	GtkWidget *box;
	gint pos = 0;

	if (!list_item) return;
	box = gtk_widget_get_ancestor(list_item, GTK_TYPE_BOX);
	if (!box) return;

	gtk_container_child_get(GTK_CONTAINER(box), list_item, "position", &pos, NULL);

	if (single_step)
		{
		pos = up ? (pos - 1) : (pos + 1);
		if (pos < 0) pos = 0;
		}
	else
		{
		pos = up ? 0 : -1;
		}

	gtk_box_reorder_child(GTK_BOX(box), list_item, pos);
}

static void toolbar_item_move_up_cb(GtkWidget *widget, gpointer data)
{
	toolbar_item_move(widget, data, TRUE, TRUE);
}

static void toolbar_item_move_down_cb(GtkWidget *widget, gpointer data)
{
	toolbar_item_move(widget, data, FALSE, TRUE);
}

static void toolbar_item_move_top_cb(GtkWidget *widget, gpointer data)
{
	toolbar_item_move(widget, data, TRUE, FALSE);
}

static void toolbar_item_move_bottom_cb(GtkWidget *widget, gpointer data)
{
	toolbar_item_move(widget, data, FALSE, FALSE);
}

static void toolbar_item_delete_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	gtk_widget_destroy(GTK_WIDGET(data));
}

static void toolbar_menu_popup(GtkWidget *widget)
{
	GtkWidget *menu;

	menu = popup_menu_short_lived();

	if (widget)
		{
		menu_item_add_stock(menu, _("Move to _top"), GTK_STOCK_GOTO_TOP, G_CALLBACK(toolbar_item_move_top_cb), widget);
		menu_item_add_stock(menu, _("Move _up"), GTK_STOCK_GO_UP, G_CALLBACK(toolbar_item_move_up_cb), widget);
		menu_item_add_stock(menu, _("Move _down"), GTK_STOCK_GO_DOWN, G_CALLBACK(toolbar_item_move_down_cb), widget);
		menu_item_add_stock(menu, _("Move to _bottom"), GTK_STOCK_GOTO_BOTTOM, G_CALLBACK(toolbar_item_move_bottom_cb), widget);
		menu_item_add_divider(menu);
		menu_item_add_stock(menu, _("Remove"), GTK_STOCK_DELETE, G_CALLBACK(toolbar_item_delete_cb), widget);
		menu_item_add_divider(menu);
		}

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static gboolean toolbar_press_cb(GtkGesture *UNUSED(gesture), int UNUSED(n_press), double UNUSED(x), double UNUSED(y), gpointer data)
{
	auto button_data = static_cast<ToolbarButtonData *>(data);

	toolbar_menu_popup(button_data->button);

	return TRUE;
}

static void get_toolbar_item(const gchar *name, gchar **label, gchar **stock_id)
{
	*label = nullptr;
	*stock_id = nullptr;

	for (const auto& l : useable_toolbar_items)
		{
		if (g_strcmp0(l.name, name) == 0)
			{
			*label = g_strdup(gettext(l.label));
			*stock_id = g_strdup(l.stock_id);
			break;
			}
		}
}


static void toolbar_item_free(ToolbarButtonData *tbbd)
{
	if (!tbbd) return;

	g_free(const_cast<gchar *>(tbbd->name));
	g_free(const_cast<gchar *>(tbbd->stock_id));
	g_free(const_cast<ToolbarButtonData *>(tbbd));
}

static void toolbar_button_free(GtkWidget *widget)
{
	g_free(g_object_get_data(G_OBJECT(widget), "toolbar_add_name"));
	g_free(g_object_get_data(G_OBJECT(widget), "toolbar_add_label"));
	g_free(g_object_get_data(G_OBJECT(widget), "toolbar_add_stock_id"));
}

static void toolbarlist_add_button(const gchar *name, const gchar *label,
									const gchar *stock_id, GtkBox *box)
{
	ToolbarButtonData *toolbar_entry;
	GtkWidget *hbox;
	GtkGesture *gesture;

	toolbar_entry = g_new(ToolbarButtonData,1);
	toolbar_entry->button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(toolbar_entry->button), GTK_RELIEF_NONE);
	gtk_box_pack_start(GTK_BOX(box), toolbar_entry->button, FALSE, FALSE, 0);
	gtk_widget_show(toolbar_entry->button);

	g_object_set_data_full(G_OBJECT(toolbar_entry->button), "toolbarbuttondata",
	toolbar_entry, reinterpret_cast<GDestroyNotify>(toolbar_item_free));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);
	gtk_container_add(GTK_CONTAINER(toolbar_entry->button), hbox);
	gtk_widget_show(hbox);

	toolbar_entry->button_label = gtk_label_new(label);
	toolbar_entry->name = g_strdup(name);
	toolbar_entry->stock_id = g_strdup(stock_id);

#ifdef HAVE_GTK4
	gesture = gtk_gesture_click_new();
	gtk_widget_add_controller(toolbar_entry->button, GTK_EVENT_CONTROLLER(gesture));
#else
	gesture = gtk_gesture_multi_press_new(toolbar_entry->button);
#endif
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), MOUSE_BUTTON_RIGHT);
	g_signal_connect(gesture, "released", G_CALLBACK(toolbar_press_cb), toolbar_entry);

	if (toolbar_entry->stock_id)
		{
		GdkPixbuf *pixbuf;
		gchar *iconl;
		iconl = path_from_utf8(toolbar_entry->stock_id);
		pixbuf = gdk_pixbuf_new_from_file(iconl, nullptr);
		g_free(iconl);
		if (pixbuf)
			{
			GdkPixbuf *scaled;
			gint w, h;

			w = h = 16;
			gtk_icon_size_lookup(GTK_ICON_SIZE_BUTTON, &w, &h);

			scaled = gdk_pixbuf_scale_simple(pixbuf, w, h,
							 GDK_INTERP_BILINEAR);
			toolbar_entry->image = gtk_image_new_from_pixbuf(scaled);

			g_object_unref(scaled);
			g_object_unref(pixbuf);
			}
		else
			{
			toolbar_entry->image = gtk_image_new_from_stock(toolbar_entry->stock_id,
														GTK_ICON_SIZE_BUTTON);
			}
		}
	else
		{
		toolbar_entry->image = gtk_image_new_from_icon_name("go-jump",
														GTK_ICON_SIZE_BUTTON);
		}
	gtk_box_pack_start(GTK_BOX(hbox), toolbar_entry->image, FALSE, FALSE, 0);
	gtk_widget_show(toolbar_entry->image);
	gtk_box_pack_start(GTK_BOX(hbox), toolbar_entry->button_label, FALSE, FALSE, 0);
	gtk_widget_show(toolbar_entry->button_label);
}

static void toolbarlist_add_cb(GtkWidget *widget, gpointer data)
{
	auto name = static_cast<const gchar *>(g_object_get_data(G_OBJECT(widget), "toolbar_add_name"));
	auto label = static_cast<const gchar *>(g_object_get_data(G_OBJECT(widget), "toolbar_add_label"));
	auto stock_id = static_cast<const gchar *>(g_object_get_data(G_OBJECT(widget), "toolbar_add_stock_id"));
	auto tbbd = static_cast<ToolbarData *>(data);

	toolbarlist_add_button(name, label, stock_id, GTK_BOX(tbbd->vbox));
}

static void get_desktop_data(const gchar *name, gchar **label, gchar **stock_id)
{
	GList *editors_list;
	GList *work;
	*label = nullptr;
	*stock_id = nullptr;

	editors_list = editor_list_get();
	work = editors_list;
	while (work)
		{
		auto editor = static_cast<const EditorDescription *>(work->data);

		if (g_strcmp0(name, editor->key) == 0)
			{
			*label = g_strdup(editor->name);
			*stock_id = g_strconcat(editor->icon, ".desktop", NULL);
			break;
			}
		work = work->next;
		}
	g_list_free(editors_list);
}

static void toolbar_menu_add_popup(GtkWidget *UNUSED(widget), gpointer data)
{
	GtkWidget *menu;
	GList *editors_list;
	GList *work;
	auto toolbarlist = static_cast<ToolbarData *>(data);

	menu = popup_menu_short_lived();

	/* get standard menu item data */
	for (const auto& l : useable_toolbar_items)
		{
		GtkWidget *item;
		item = menu_item_add_stock(menu, gettext(l.label), l.stock_id,
										G_CALLBACK(toolbarlist_add_cb), toolbarlist);
		g_object_set_data(G_OBJECT(item), "toolbar_add_name", g_strdup(l.name));
		g_object_set_data(G_OBJECT(item), "toolbar_add_label", g_strdup(gettext(l.label)));
		g_object_set_data(G_OBJECT(item), "toolbar_add_stock_id", g_strdup(l.stock_id));
		g_signal_connect(G_OBJECT(item), "destroy", G_CALLBACK(toolbar_button_free), item);
		}

	/* get desktop file data */
	editors_list = editor_list_get();
	work = editors_list;
	while (work)
		{
		auto editor = static_cast<const EditorDescription *>(work->data);

		GtkWidget *item;
		gchar *icon = g_strconcat(editor->icon, ".desktop", NULL);

		item = menu_item_add_stock(menu, editor->name, icon,
										G_CALLBACK(toolbarlist_add_cb), toolbarlist);
		g_object_set_data(G_OBJECT(item), "toolbar_add_name", g_strdup(editor->key));
		g_object_set_data(G_OBJECT(item), "toolbar_add_label", g_strdup(editor->name));
		g_object_set_data(G_OBJECT(item), "toolbar_add_stock_id", icon);
		g_signal_connect(G_OBJECT(item), "destroy", G_CALLBACK(toolbar_button_free), item);
		work = work->next;
		}
	g_list_free(editors_list);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static gboolean toolbar_menu_add_cb(GtkWidget *widget, gpointer data)
{
	auto toolbarlist = static_cast<ToolbarData *>(data);

	toolbar_menu_add_popup(widget, toolbarlist);
	return TRUE;
}

/**
 * @brief For each layoutwindow, clear toolbar and reload with current selection
 * @param bar Main or Status toolbar
 *
 */
void toolbar_apply(ToolbarType bar)
{
	LayoutWindow *lw;
	GList *work_windows;
	GList *work_toolbar;

	work_windows = layout_window_list;
	while (work_windows)
		{
		lw = static_cast<LayoutWindow *>(work_windows->data);

		layout_toolbar_clear(lw, bar);

		work_toolbar = gtk_container_get_children(GTK_CONTAINER(toolbarlist[bar]->vbox));
		while (work_toolbar)
			{
			auto button = static_cast<GtkButton *>(work_toolbar->data);
			ToolbarButtonData *tbbd;

			tbbd = static_cast<ToolbarButtonData *>(g_object_get_data(G_OBJECT(button),"toolbarbuttondata"));
			layout_toolbar_add(lw, bar, tbbd->name);

			work_toolbar = work_toolbar->next;
			}
		g_list_free(work_toolbar);

		work_windows = work_windows->next;
		}

}

/**
 * @brief Load the current toolbar items into the vbox
 * @param lw
 * @param box The vbox displayed in the preferences Toolbar tab
 * @param bar Main or Status toolbar
 *
 * Get the current contents of the toolbar, both menu items
 * and desktop items, and load them into the vbox
 */
static void toolbarlist_populate(LayoutWindow *lw, GtkBox *box, ToolbarType bar)
{
	GList *work = g_list_first(lw->toolbar_actions[bar]);

	while (work)
		{
		auto name = static_cast<gchar *>(work->data);
		gchar *label;
		gchar *icon;
		work = work->next;

		if (file_extension_match(name, ".desktop"))
			{
			get_desktop_data(name, &label, &icon);
			}
		else
			{
			get_toolbar_item(name, &label, &icon);
			}
		toolbarlist_add_button(name, label, icon, box);
		}
}

GtkWidget *toolbar_select_new(LayoutWindow *lw, ToolbarType bar)
{
	GtkWidget *scrolled;
	GtkWidget *tbar;
	GtkWidget *add_box;

	if (!lw) return nullptr;

	if (!toolbarlist[bar])
		{
		toolbarlist[bar] = g_new0(ToolbarData, 1);
		}
	toolbarlist[bar]->lw = lw;

	toolbarlist[bar]->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_widget_show(toolbarlist[bar]->widget);

	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
							GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gtk_box_pack_start(GTK_BOX(toolbarlist[bar]->widget), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	toolbarlist[bar]->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(toolbarlist[bar]->vbox);
	gtk_container_add(GTK_CONTAINER(scrolled), toolbarlist[bar]->vbox);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(gtk_bin_get_child(GTK_BIN(scrolled))),
																GTK_SHADOW_NONE);

	add_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_show(add_box);
	gtk_box_pack_end(GTK_BOX(toolbarlist[bar]->widget), add_box, FALSE, FALSE, 0);
	tbar = pref_toolbar_new(add_box, GTK_TOOLBAR_ICONS);
	toolbarlist[bar]->add_button = pref_toolbar_button(tbar, GTK_STOCK_ADD, "NULL", FALSE,
											_("Add Toolbar Item"),
											G_CALLBACK(toolbar_menu_add_cb), toolbarlist[bar]);
	gtk_widget_show(toolbarlist[bar]->add_button);

	toolbarlist_populate(lw,GTK_BOX(toolbarlist[bar]->vbox), bar);

	return toolbarlist[bar]->widget;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
