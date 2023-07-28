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

#include "main.h"
#include "layout.h"

#include "filedata.h"
#include "history-list.h"
#include "image.h"
#include "image-overlay.h"
#include "layout-config.h"
#include "layout-image.h"
#include "layout-util.h"
#include "logwindow.h"
#include "menu.h"
#include "pixbuf-util.h"
#include "utilops.h"
#include "view-dir.h"
#include "view-file.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "window.h"
#include "metadata.h"
#include "rcfile.h"
#include "bar.h"
#include "bar-sort.h"
#include "preferences.h"
#include "shortcuts.h"
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#define MAINWINDOW_DEF_WIDTH 700
#define MAINWINDOW_DEF_HEIGHT 500

#define MAIN_WINDOW_DIV_HPOS (MAINWINDOW_DEF_WIDTH / 2)
#define MAIN_WINDOW_DIV_VPOS (MAINWINDOW_DEF_HEIGHT / 2)

#define TOOLWINDOW_DEF_WIDTH 260
#define TOOLWINDOW_DEF_HEIGHT 450

#define PROGRESS_WIDTH 150
#define ZOOM_LABEL_WIDTH 120

#define PANE_DIVIDER_SIZE 10


GList *layout_window_list = nullptr;
LayoutWindow *current_lw = nullptr;

static void layout_list_scroll_to_subpart(LayoutWindow *lw, const gchar *needle);


/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

gboolean layout_valid(LayoutWindow **lw)
{
	if (*lw == nullptr)
		{
		if (current_lw) *lw = current_lw;
		else if (layout_window_list) *lw = static_cast<LayoutWindow *>(layout_window_list->data);
		return (*lw != nullptr);
		}
	return (g_list_find(layout_window_list, *lw) != nullptr);
}

LayoutWindow *layout_find_by_image(ImageWindow *imd)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		auto lw = static_cast<LayoutWindow *>(work->data);
		work = work->next;

		if (lw->image == imd) return lw;
		}

	return nullptr;
}

LayoutWindow *layout_find_by_image_fd(ImageWindow *imd)
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		auto lw = static_cast<LayoutWindow *>(work->data);
		work = work->next;

		if (lw->image->image_fd == imd->image_fd)
			return lw;
		}

	return nullptr;
}

LayoutWindow *layout_find_by_layout_id(const gchar *id)
{
	GList *work;

	if (!id || !id[0]) return nullptr;

	if (strcmp(id, LAYOUT_ID_CURRENT) == 0)
		{
		if (current_lw) return current_lw;
		if (layout_window_list) return static_cast<LayoutWindow *>(layout_window_list->data);
		return nullptr;
		}

	work = layout_window_list;
	while (work)
		{
		auto lw = static_cast<LayoutWindow *>(work->data);
		work = work->next;

		if (lw->options.id && strcmp(id, lw->options.id) == 0)
			return lw;
		}

	return nullptr;
}

gchar *layout_get_unique_id()
{
	char id[10];
	gint i;

	i = 1;
	while (TRUE)
		{
		g_snprintf(id, sizeof(id), "lw%d", i);
		if (!layout_find_by_layout_id(id))
			{
			return g_strdup(id);
			}
		i++;
		}
}

static void layout_set_unique_id(LayoutWindow *lw)
{
	char id[10];
	gint i;
	if (lw->options.id && lw->options.id[0]) return; /* id is already set */

	g_free(lw->options.id);
	lw->options.id = nullptr;

	i = 1;
	while (TRUE)
		{
		g_snprintf(id, sizeof(id), "lw%d", i);
		if (!layout_find_by_layout_id(id))
			{
			lw->options.id = g_strdup(id);
			return;
			}
		i++;
		}
}

static gboolean layout_set_current_cb(GtkWidget *, GdkEventFocus *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	current_lw = lw;
	return FALSE;
}

static void layout_box_folders_changed_cb(GtkWidget *widget, gpointer)
{
	LayoutWindow *lw;
	GList *work;

/** @FIXME this is probably not the correct way to implement this */
	work = layout_window_list;
	while (work)
		{
		lw = static_cast<LayoutWindow *>(work->data);
		lw->options.folder_window.vdivider_pos = gtk_paned_get_position(GTK_PANED(widget));
		work = work->next;
		}
}

/*
 *-----------------------------------------------------------------------------
 * menu, toolbar, and dir view
 *-----------------------------------------------------------------------------
 */

static void layout_path_entry_changed_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gchar *buf;

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(widget)) < 0) return;

	buf = g_strdup(gtk_entry_get_text(GTK_ENTRY(lw->path_entry)));
	if (!lw->dir_fd || strcmp(buf, lw->dir_fd->path) != 0)
		{
		layout_set_path(lw, buf);
		}

	g_free(buf);
}

static void layout_path_entry_tab_cb(const gchar *path, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gchar *buf;

	buf = g_strdup(path);
	parse_out_relatives(buf);

	if (isdir(buf))
		{
		if ((!lw->dir_fd || strcmp(lw->dir_fd->path, buf) != 0) && layout_set_path(lw, buf))
			{
			gtk_widget_grab_focus(GTK_WIDGET(lw->path_entry));
			gint pos = -1;
			/* put the G_DIR_SEPARATOR back, if we are in tab completion for a dir and result was path change */
			gtk_editable_insert_text(GTK_EDITABLE(lw->path_entry), G_DIR_SEPARATOR_S, -1, &pos);
			gtk_editable_set_position(GTK_EDITABLE(lw->path_entry),
						  strlen(gtk_entry_get_text(GTK_ENTRY(lw->path_entry))));
			}
		}
	else if (lw->dir_fd)
		{
		gchar *base = remove_level_from_path(buf);

		if (strcmp(lw->dir_fd->path, base) == 0)
			{
			layout_list_scroll_to_subpart(lw, filename_from_path(buf));
			}
		g_free(base);
		}

	g_free(buf);
}

static void layout_path_entry_cb(const gchar *path, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gchar *buf;

	buf = g_strdup(path);

	if (!download_web_file(buf, FALSE, lw))
		{
		parse_out_relatives(buf);

		layout_set_path(lw, buf);
		}

	g_free(buf);
}

static void layout_vd_select_cb(ViewDir *, FileData *fd, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_set_fd(lw, fd);
}

static void layout_path_entry_tab_append_cb(const gchar *, gpointer data, gint n)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (!lw || !lw->back_button) return;
	if (!layout_valid(&lw)) return;

	/* Enable back button if it makes sense */
	gtk_widget_set_sensitive(lw->back_button, (n > 1));
}

static gboolean path_entry_tooltip_cb(GtkWidget *widget, gpointer)
{
	GList *box_child_list;
	GtkComboBox *path_entry;
	gchar *current_path;

	box_child_list = gtk_container_get_children(GTK_CONTAINER(widget));
	path_entry = static_cast<GtkComboBox *>(box_child_list->data);
	current_path = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(path_entry));
	gtk_widget_set_tooltip_text(GTK_WIDGET(widget), current_path);

	g_free(current_path);
	g_list_free(box_child_list);

	return FALSE;
}

static GtkWidget *layout_tool_setup(LayoutWindow *lw)
{
	GtkWidget *box;
	GtkWidget *box_folders;
	GtkWidget *scd;
	GtkWidget *menu_tool_bar;
	GtkWidget *tabcomp;
	GtkWidget *menu_bar;
	GtkWidget *toolbar;
	GtkWidget *scroll_window;

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	if (!options->expand_menu_toolbar)
		{
		menu_bar = layout_actions_menu_bar(lw);

		toolbar = layout_actions_toolbar(lw, TOOLBAR_MAIN);
		scroll_window = gtk_scrolled_window_new(nullptr, nullptr);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_window), GTK_POLICY_AUTOMATIC,GTK_POLICY_NEVER);
		gtk_container_add(GTK_CONTAINER(scroll_window), menu_bar);

		gtk_widget_show(scroll_window);
		gtk_widget_show(menu_bar);
		if (lw->options.toolbar_hidden) gtk_widget_hide(toolbar);

		gtk_box_pack_start(GTK_BOX(box), scroll_window, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
		}
	else
		{
		menu_tool_bar = layout_actions_menu_tool_bar(lw);
		DEBUG_NAME(menu_tool_bar);
		gtk_widget_show(menu_tool_bar);
		gtk_box_pack_start(GTK_BOX(lw->main_box), lw->menu_tool_bar, FALSE, FALSE, 0);
		if (lw->options.toolbar_hidden) gtk_widget_hide(lw->toolbar[TOOLBAR_MAIN]);
		}

	tabcomp = tab_completion_new_with_history(&lw->path_entry, nullptr, "path_list", -1,
						  layout_path_entry_cb, lw);
	DEBUG_NAME(tabcomp);
	tab_completion_add_tab_func(lw->path_entry, layout_path_entry_tab_cb, lw);
	tab_completion_add_append_func(lw->path_entry, layout_path_entry_tab_append_cb, lw);
	gtk_box_pack_start(GTK_BOX(box), tabcomp, FALSE, FALSE, 0);
	gtk_widget_show(tabcomp);
	gtk_widget_set_has_tooltip(GTK_WIDGET(tabcomp), TRUE);
	g_signal_connect(G_OBJECT(tabcomp), "query_tooltip", G_CALLBACK(path_entry_tooltip_cb), lw);

	g_signal_connect(G_OBJECT(gtk_widget_get_parent(gtk_widget_get_parent(lw->path_entry))), "changed",
			 G_CALLBACK(layout_path_entry_changed_cb), lw);

	box_folders = GTK_WIDGET(gtk_hpaned_new());
	DEBUG_NAME(box_folders);
	gtk_box_pack_start(GTK_BOX(box), box_folders, TRUE, TRUE, 0);

	lw->vd = vd_new(lw);

	vd_set_select_func(lw->vd, layout_vd_select_cb, lw);

	lw->dir_view = lw->vd->widget;
	DEBUG_NAME(lw->dir_view);
	gtk_paned_add2(GTK_PANED(box_folders), lw->dir_view);
	gtk_widget_show(lw->dir_view);

	scd = shortcuts_new_default(lw);
	DEBUG_NAME(scd);
	gtk_paned_add1(GTK_PANED(box_folders), scd);
	gtk_paned_set_position(GTK_PANED(box_folders), lw->options.folder_window.vdivider_pos);

	gtk_widget_show(box_folders);

	g_signal_connect(G_OBJECT(box_folders), "notify::position",
			 G_CALLBACK(layout_box_folders_changed_cb), lw);

	gtk_widget_show(box);

	return box;
}

/*
 *-----------------------------------------------------------------------------
 * sort button (and menu)
 *-----------------------------------------------------------------------------
 */

static void layout_sort_menu_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw;
	SortType type;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	lw = static_cast<LayoutWindow *>(submenu_item_get_data(widget));
	if (!lw) return;

	type = static_cast<SortType>GPOINTER_TO_INT(data);

	if (type == SORT_EXIFTIME || type == SORT_EXIFTIMEDIGITIZED || type == SORT_RATING)
		{
		vf_read_metadata_in_idle(lw->vf);
		}
	layout_sort_set(lw, type, lw->sort_ascend);
}

static void layout_sort_menu_ascend_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_sort_set(lw, lw->sort_method, !lw->sort_ascend);
}

static void layout_sort_menu_hide_cb(GtkWidget *widget, gpointer)
{
	/* destroy the menu */
	g_object_unref(widget);
}

static void layout_sort_button_press_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;

	menu = submenu_add_sort(nullptr, G_CALLBACK(layout_sort_menu_cb), lw, FALSE, FALSE, TRUE, lw->sort_method);

	/* take ownership of menu */
#ifdef GTK_OBJECT_FLOATING
	/* GTK+ < 2.10 */
	g_object_ref(G_OBJECT(menu));
	gtk_object_sink(GTK_OBJECT(menu));
#else
	/* GTK+ >= 2.10 */
	g_object_ref_sink(G_OBJECT(menu));
#endif

	/* ascending option */
	menu_item_add_divider(menu);
	menu_item_add_check(menu, _("Ascending"), lw->sort_ascend, G_CALLBACK(layout_sort_menu_ascend_cb), lw);

	g_signal_connect(G_OBJECT(menu), "selection_done",
			 G_CALLBACK(layout_sort_menu_hide_cb), NULL);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static GtkWidget *layout_sort_button(LayoutWindow *lw, GtkWidget *box)
{
	GtkWidget *button;
	GtkWidget *frame;
	GtkWidget *image;

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);
	gtk_widget_show(frame);

	image = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_BUTTON);
	button = gtk_button_new_with_label(sort_type_get_text(lw->sort_method));
	gtk_button_set_image(GTK_BUTTON(button), image);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(layout_sort_button_press_cb), lw);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_button_set_image_position(GTK_BUTTON(button), GTK_POS_RIGHT);

	gtk_container_add(GTK_CONTAINER(frame), button);
	gtk_widget_show(button);

	return button;
}

static void layout_zoom_menu_cb(GtkWidget *widget, gpointer data)
{
	ZoomMode mode;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	mode = static_cast<ZoomMode>GPOINTER_TO_INT(data);
	options->image.zoom_mode = mode;
}

static void layout_scroll_menu_cb(GtkWidget *widget, gpointer data)
{
	guint scroll_type;

	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	scroll_type = GPOINTER_TO_UINT(data);
	options->image.scroll_reset_method = scroll_type;
	image_options_sync();
}

static void layout_zoom_menu_hide_cb(GtkWidget *widget, gpointer)
{
	/* destroy the menu */
	g_object_unref(widget);
}

static void layout_zoom_button_press_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;

	menu = submenu_add_zoom(nullptr, G_CALLBACK(layout_zoom_menu_cb),
			lw, FALSE, FALSE, TRUE, options->image.zoom_mode);

	/* take ownership of menu */
#ifdef GTK_OBJECT_FLOATING
	/* GTK+ < 2.10 */
	g_object_ref(G_OBJECT(menu));
	gtk_object_sink(GTK_OBJECT(menu));
#else
	/* GTK+ >= 2.10 */
	g_object_ref_sink(G_OBJECT(menu));
#endif

	menu_item_add_divider(menu);

	menu_item_add_radio(menu, _("Scroll to top left corner"),
			GUINT_TO_POINTER(SCROLL_RESET_TOPLEFT),
			options->image.scroll_reset_method == SCROLL_RESET_TOPLEFT,
			G_CALLBACK(layout_scroll_menu_cb),
			GUINT_TO_POINTER(SCROLL_RESET_TOPLEFT));
	menu_item_add_radio(menu, _("Scroll to image center"),
			GUINT_TO_POINTER(SCROLL_RESET_CENTER),
			options->image.scroll_reset_method == SCROLL_RESET_CENTER,
			G_CALLBACK(layout_scroll_menu_cb),
			GUINT_TO_POINTER(SCROLL_RESET_CENTER));
	menu_item_add_radio(menu, _("Keep the region from previous image"),
			GUINT_TO_POINTER(SCROLL_RESET_NOCHANGE),
			options->image.scroll_reset_method == SCROLL_RESET_NOCHANGE,
			G_CALLBACK(layout_scroll_menu_cb),
			GUINT_TO_POINTER(SCROLL_RESET_NOCHANGE));

	g_signal_connect(G_OBJECT(menu), "selection_done",
			 G_CALLBACK(layout_zoom_menu_hide_cb), NULL);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static GtkWidget *layout_zoom_button(LayoutWindow *lw, GtkWidget *box, gint size, gboolean)
{
	GtkWidget *button;
	GtkWidget *frame;
	GtkWidget *image;

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	if (size) gtk_widget_set_size_request(frame, size, -1);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);

	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);

	gtk_widget_show(frame);

	image = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_BUTTON);
	button = gtk_button_new_with_label("1:1");
	gtk_button_set_image(GTK_BUTTON(button), image);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(layout_zoom_button_press_cb), lw);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_button_set_image_position(GTK_BUTTON(button), GTK_POS_RIGHT);

	gtk_container_add(GTK_CONTAINER(frame), button);
	gtk_widget_show(button);

	return button;
}

/*
 *-----------------------------------------------------------------------------
 * status bar
 *-----------------------------------------------------------------------------
 */


void layout_status_update_progress(LayoutWindow *lw, gdouble val, const gchar *text)
{
	static gdouble meta = 0;

	if (!layout_valid(&lw)) return;
	if (!lw->info_progress_bar) return;

	/* Give priority to the loading meta data message
	 */
	if(!g_strcmp0(text, "Loading thumbs..."))
		{
		if (meta)
			{
			return;
			}
		}
	else
		{
		meta = val;
		}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(lw->info_progress_bar), val);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(lw->info_progress_bar),
									val ? ((text) ? text : " ") : " ");
}

void layout_status_update_info(LayoutWindow *lw, const gchar *text)
{
	gchar *buf = nullptr;
	gint hrs;
	gint min;
	gdouble sec;
	GString *delay;

	if (!layout_valid(&lw)) return;

	if (!text)
		{
		guint n;
		gint64 n_bytes = 0;

		n = layout_list_count(lw, &n_bytes);

		if (n)
			{
			guint s;
			gint64 s_bytes = 0;
			gchar *ss;

			if (layout_image_slideshow_active(lw))
				{

				if (!layout_image_slideshow_paused(lw))
					{
					delay = g_string_new(_(" Slideshow ["));
					}
				else
					{
					delay = g_string_new(_(" Paused ["));
					}
				hrs = options->slideshow.delay / (36000);
				min = (options->slideshow.delay -(36000 * hrs))/600;
				sec = static_cast<gdouble>(options->slideshow.delay -(36000 * hrs)-(min * 600)) / 10;

				if (hrs > 0)
					{
					g_string_append_printf(delay, "%dh ", hrs);
					}
				if (min > 0)
					{
					g_string_append_printf(delay, "%dm ", min);
					}
				g_string_append_printf(delay, "%.1fs]", sec);

				ss = g_strdup(delay->str);

				g_string_free(delay, TRUE);
				}
			else
				{
				ss = g_strdup("");
				}

			s = layout_selection_count(lw, &s_bytes);

			layout_bars_new_selection(lw, s);

			if (s > 0)
				{
				gchar *b = text_from_size_abrev(n_bytes);
				gchar *sb = text_from_size_abrev(s_bytes);
				buf = g_strdup_printf(_("%s, %d files (%s, %d)%s"), b, n, sb, s, ss);
				g_free(b);
				g_free(sb);
				g_free(ss);
				}
			else if (n > 0)
				{
				gchar *b = text_from_size_abrev(n_bytes);
				buf = g_strdup_printf(_("%s, %d files%s"), b, n, ss);
				g_free(b);
				g_free(ss);
				}
			else
				{
				buf = g_strdup_printf(_("%d files%s"), n, ss);
				g_free(ss);
				}

			text = buf;

			image_osd_update(lw->image);
			}
		else
			{
			text = "";
			}
	}

	if (lw->info_status) gtk_label_set_text(GTK_LABEL(lw->info_status), text);
	g_free(buf);
}

void layout_status_update_image(LayoutWindow *lw)
{
	guint64 n;
	FileData *fd;
	gint page_total;
	gint page_num;

	if (!layout_valid(&lw) || !lw->image) return;
	if (!lw->info_zoom || !lw->info_details) return; /*called from layout_style_set */

	n = layout_list_count(lw, nullptr);

	if (!n)
		{
		gtk_button_set_label(GTK_BUTTON(lw->info_zoom), "");
		gtk_label_set_text(GTK_LABEL(lw->info_details), "");
		}
	else
		{
		gchar *text;
		gchar *b;

		text = image_zoom_get_as_text(lw->image);
		gtk_button_set_label(GTK_BUTTON(lw->info_zoom), text);
		g_free(text);

		b = image_get_fd(lw->image) ? text_from_size(image_get_fd(lw->image)->size) : g_strdup("0");

		if (lw->image->unknown)
			{
			if (image_get_path(lw->image) && !access_file(image_get_path(lw->image), R_OK))
				{
				text = g_strdup_printf(_("(no read permission) %s bytes"), b);
				}
			else
				{
				text = g_strdup_printf(_("( ? x ? ) %s bytes"), b);
				}
			}
		else
			{
			gint width, height;
			fd = image_get_fd(lw->image);
			page_total = fd->page_total;
			page_num = fd->page_num + 1;
			image_get_image_size(lw->image, &width, &height);

			if (page_total > 1)
				{
				text = g_strdup_printf(_("( %d x %d ) %s bytes %s%d%s%d%s"), width, height, b, "[", page_num, "/", page_total, "]");
				}
			else
				{
				text = g_strdup_printf(_("( %d x %d ) %s bytes"), width, height, b);
				}
			}

		g_signal_emit_by_name (lw->image->pr, "update-pixel");

		g_free(b);

		gtk_label_set_text(GTK_LABEL(lw->info_details), text);
		g_free(text);
		}
	layout_util_sync_color(lw); /* update color button */
}

void layout_status_update_all(LayoutWindow *lw)
{
	layout_status_update_progress(lw, 0.0, nullptr);
	layout_status_update_info(lw, nullptr);
	layout_status_update_image(lw);
	layout_util_status_update_write(lw);
}

static GtkWidget *layout_status_label(gchar *text, GtkWidget *box, gboolean start, gint size, gboolean expand)
{
	GtkWidget *label;
	GtkWidget *frame;

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	if (size) gtk_widget_set_size_request(frame, size, -1);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	if (start)
		{
		gtk_box_pack_start(GTK_BOX(box), frame, expand, expand, 0);
		}
	else
		{
		gtk_box_pack_end(GTK_BOX(box), frame, expand, expand, 0);
		}
	gtk_widget_show(frame);

	label = gtk_label_new(text ? text : "");
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_container_add(GTK_CONTAINER(frame), label);
	gtk_widget_show(label);

	return label;
}

static void layout_status_setup(LayoutWindow *lw, GtkWidget *box, gboolean small_format)
{
	GtkWidget *hbox;
	GtkWidget *toolbar;
	GtkWidget *toolbar_frame;

	if (lw->info_box) return;

	if (small_format)
		{
		lw->info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		DEBUG_NAME(lw->info_box);
		}
	else
		{
		lw->info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(lw->info_box);
		}
	gtk_box_pack_end(GTK_BOX(box), lw->info_box, FALSE, FALSE, 0);
	gtk_widget_show(lw->info_box);

	if (small_format)
		{
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(hbox);
		gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
	else
		{
		hbox = lw->info_box;
		}
	lw->info_progress_bar = gtk_progress_bar_new();
	DEBUG_NAME(lw->info_progress_bar);
	gtk_widget_set_size_request(lw->info_progress_bar, PROGRESS_WIDTH, -1);

	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(lw->info_progress_bar), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(lw->info_progress_bar), TRUE);

	gtk_box_pack_start(GTK_BOX(hbox), lw->info_progress_bar, FALSE, FALSE, 0);
	gtk_widget_show(lw->info_progress_bar);

	lw->info_sort = layout_sort_button(lw, hbox);
	gtk_widget_set_tooltip_text(GTK_WIDGET(lw->info_sort), _("Select sort order"));
	gtk_widget_show(lw->info_sort);

	lw->info_status = layout_status_label(nullptr, lw->info_box, TRUE, 0, (!small_format));
	DEBUG_NAME(lw->info_status);
	gtk_widget_set_tooltip_text(GTK_WIDGET(lw->info_status), _("Folder contents (files selected)\nSlideshow [time interval]"));

	if (small_format)
		{
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(hbox);
		gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
	lw->info_details = layout_status_label(nullptr, hbox, TRUE, 0, TRUE);
	DEBUG_NAME(lw->info_details);
	gtk_widget_set_tooltip_text(GTK_WIDGET(lw->info_details), _("(Image dimensions) Image size [page n of m]"));
	toolbar = layout_actions_toolbar(lw, TOOLBAR_STATUS);

	toolbar_frame = gtk_frame_new(nullptr);
	DEBUG_NAME(toolbar_frame);
	gtk_frame_set_shadow_type(GTK_FRAME(toolbar_frame), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(toolbar_frame), toolbar);
	gtk_widget_show(toolbar_frame);
	gtk_widget_show(toolbar);
	gtk_box_pack_end(GTK_BOX(hbox), toolbar_frame, FALSE, FALSE, 0);
	lw->info_zoom = layout_zoom_button(lw, hbox, ZOOM_LABEL_WIDTH, TRUE);
	gtk_widget_set_tooltip_text(GTK_WIDGET(lw->info_zoom), _("Select zoom and scroll mode"));
	gtk_widget_show(lw->info_zoom);

	if (small_format)
		{
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(hbox);
		gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
	lw->info_pixel = layout_status_label(nullptr, hbox, FALSE, 0, small_format); /* expand only in small format */
	DEBUG_NAME(lw->info_pixel);
	gtk_widget_set_tooltip_text(GTK_WIDGET(lw->info_pixel), _("[Pixel x,y coord]: (Pixel R,G,B value)"));
	if (!lw->options.show_info_pixel) gtk_widget_hide(gtk_widget_get_parent(lw->info_pixel));
}

/*
 *-----------------------------------------------------------------------------
 * views
 *-----------------------------------------------------------------------------
 */

static GtkWidget *layout_tools_new(LayoutWindow *lw)
{
	lw->dir_view = layout_tool_setup(lw);
	return lw->dir_view;
}

static void layout_list_status_cb(ViewFile *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_status_update_info(lw, nullptr);
}

static void layout_list_thumb_cb(ViewFile *, gdouble val, const gchar *text, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_status_update_progress(lw, val, text);
}

static void layout_list_sync_thumb(LayoutWindow *lw)
{
	if (lw->vf) vf_thumb_set(lw->vf, lw->options.show_thumbnails);
}

static void layout_list_sync_file_filter(LayoutWindow *lw)
{
	if (lw->vf) vf_file_filter_set(lw->vf, lw->options.show_file_filter);
}

static GtkWidget *layout_list_new(LayoutWindow *lw)
{
	lw->vf = vf_new(lw->options.file_view_type, nullptr);
	vf_set_layout(lw->vf, lw);

	vf_set_status_func(lw->vf, layout_list_status_cb, lw);
	vf_set_thumb_status_func(lw->vf, layout_list_thumb_cb, lw);

	vf_marks_set(lw->vf, lw->options.show_marks);

	layout_list_sync_thumb(lw);
	layout_list_sync_file_filter(lw);

	return lw->vf->widget;
}

static void layout_list_sync_marks(LayoutWindow *lw)
{
	if (lw->vf) vf_marks_set(lw->vf, lw->options.show_marks);
}

static void layout_list_scroll_to_subpart(LayoutWindow *lw, const gchar *)
{
	if (!lw) return;
}

GList *layout_list(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	if (lw->vf) return vf_get_list(lw->vf);

	return nullptr;
}

guint layout_list_count(LayoutWindow *lw, gint64 *bytes)
{
	if (!layout_valid(&lw)) return 0;

	if (lw->vf) return vf_count(lw->vf, bytes);

	return 0;
}

FileData *layout_list_get_fd(LayoutWindow *lw, gint index)
{
	if (!layout_valid(&lw)) return nullptr;

	if (lw->vf) return vf_index_get_data(lw->vf, index);

	return nullptr;
}

gint layout_list_get_index(LayoutWindow *lw, FileData *fd)
{
	if (!layout_valid(&lw) || !fd) return -1;

	if (lw->vf) return vf_index_by_fd(lw->vf, fd);

	return -1;
}

void layout_list_sync_fd(LayoutWindow *lw, FileData *fd)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_by_fd(lw->vf, fd);
}

static void layout_list_sync_sort(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_sort_set(lw->vf, lw->sort_method, lw->sort_ascend);
}

GList *layout_selection_list(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	if (layout_image_get_collection(lw, nullptr))
		{
		FileData *fd;

		fd = layout_image_get_fd(lw);
		if (fd) return g_list_append(nullptr, file_data_ref(fd));
		return nullptr;
		}

	if (lw->vf) return vf_selection_get_list(lw->vf);

	return nullptr;
}

GList *layout_selection_list_by_index(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	if (lw->vf) return vf_selection_get_list_by_index(lw->vf);

	return nullptr;
}

guint layout_selection_count(LayoutWindow *lw, gint64 *bytes)
{
	if (!layout_valid(&lw)) return 0;

	if (lw->vf) return vf_selection_count(lw->vf, bytes);

	return 0;
}

void layout_select_all(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_all(lw->vf);
}

void layout_select_none(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_none(lw->vf);
}

void layout_select_invert(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_select_invert(lw->vf);
}

void layout_select_list(LayoutWindow *lw, GList *list)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf)
		{
		vf_select_list(lw->vf, list);
		}
}

void layout_mark_to_selection(LayoutWindow *lw, gint mark, MarkToSelectionMode mode)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_mark_to_selection(lw->vf, mark, mode);
}

void layout_selection_to_mark(LayoutWindow *lw, gint mark, SelectionToMarkMode mode)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_selection_to_mark(lw->vf, mark, mode);

	layout_status_update_info(lw, nullptr); /* osd in fullscreen mode */
}

void layout_mark_filter_toggle(LayoutWindow *lw, gint mark)
{
	if (!layout_valid(&lw)) return;

	if (lw->vf) vf_mark_filter_toggle(lw->vf, mark);
}


/*
 *-----------------------------------------------------------------------------
 * access
 *-----------------------------------------------------------------------------
 */

const gchar *layout_get_path(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;
	return lw->dir_fd ? lw->dir_fd->path : nullptr;
}

static void layout_sync_path(LayoutWindow *lw)
{
	if (!lw->dir_fd) return;

	if (lw->path_entry) gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);

	if (lw->vd) vd_set_fd(lw->vd, lw->dir_fd);
	if (lw->vf) vf_set_fd(lw->vf, lw->dir_fd);
}

gboolean layout_set_path(LayoutWindow *lw, const gchar *path)
{
	FileData *fd;
	gboolean ret;

	if (!path) return FALSE;

	fd = file_data_new_group(path);
	ret = layout_set_fd(lw, fd);
	file_data_unref(fd);
	return ret;
}


gboolean layout_set_fd(LayoutWindow *lw, FileData *fd)
{
	gboolean have_file = FALSE;
	gboolean dir_changed = TRUE;
	gchar *last_image;

	if (!layout_valid(&lw)) return FALSE;

	if (!fd || !isname(fd->path)) return FALSE;
	if (lw->dir_fd && fd == lw->dir_fd)
		{
		return TRUE;
		}

	if (isdir(fd->path))
		{
		if (lw->dir_fd)
			{
			file_data_unregister_real_time_monitor(lw->dir_fd);
			file_data_unref(lw->dir_fd);
			}
		lw->dir_fd = file_data_ref(fd);
		file_data_register_real_time_monitor(fd);

		last_image = get_recent_viewed_folder_image(fd->path);
		if (last_image)
			{
			fd = file_data_new_group(last_image);
			g_free(last_image);

			if (isfile(fd->path)) have_file = TRUE;
			}

		}
	else
		{
		gchar *base;

		base = remove_level_from_path(fd->path);
		if (lw->dir_fd && strcmp(lw->dir_fd->path, base) == 0)
			{
			g_free(base);
			dir_changed = FALSE;
			}
		else if (isdir(base))
			{
			if (lw->dir_fd)
				{
				file_data_unregister_real_time_monitor(lw->dir_fd);
				file_data_unref(lw->dir_fd);
				}
			lw->dir_fd = file_data_new_dir(base);
			file_data_register_real_time_monitor(lw->dir_fd);
			g_free(base);
			}
		else
			{
			g_free(base);
			return FALSE;
			}
		if (isfile(fd->path)) have_file = TRUE;
		}

	if (lw->path_entry)
		{
		history_chain_append_end(lw->dir_fd->path);
		tab_completion_append_to_history(lw->path_entry, lw->dir_fd->path);
		}
	layout_sync_path(lw);
	layout_list_sync_sort(lw);

	if (have_file)
		{
		gint row;

		row = layout_list_get_index(lw, fd);
		if (row >= 0)
			{
			layout_image_set_index(lw, row);
			}
		else
			{
			layout_image_set_fd(lw, fd);
			}
		}
	else if (!options->lazy_image_sync)
		{
		layout_image_set_index(lw, 0);
		}

	if (options->metadata.confirm_on_dir_change && dir_changed)
		metadata_write_queue_confirm(FALSE, nullptr, nullptr);

	if (lw->vf && (options->read_metadata_in_idle || (lw->sort_method == SORT_EXIFTIME || lw->sort_method == SORT_EXIFTIMEDIGITIZED || lw->sort_method == SORT_RATING)))
		{
		vf_read_metadata_in_idle(lw->vf);
		}

	return TRUE;
}

static void layout_refresh_lists(LayoutWindow *lw)
{
	if (lw->vd) vd_refresh(lw->vd);

	if (lw->vf)
		{
		vf_refresh(lw->vf);
		vf_thumb_update(lw->vf);
		}
}

void layout_refresh(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	DEBUG_1("layout refresh");

	layout_refresh_lists(lw);

	if (lw->image) layout_image_refresh(lw);
}

void layout_thumb_set(LayoutWindow *lw, gboolean enable)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.show_thumbnails == enable) return;

	lw->options.show_thumbnails = enable;

	layout_util_sync_thumb(lw);
	layout_list_sync_thumb(lw);
}

void layout_file_filter_set(LayoutWindow *lw, gboolean enable)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.show_file_filter == enable) return;

	lw->options.show_file_filter = enable;

	layout_util_sync_file_filter(lw);
	layout_list_sync_file_filter(lw);
}

void layout_marks_set(LayoutWindow *lw, gboolean enable)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.show_marks == enable) return;

	lw->options.show_marks = enable;

	layout_util_sync_marks(lw);
	layout_list_sync_marks(lw);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
gboolean layout_thumb_get_unused(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return lw->options.show_thumbnails;
}

gboolean layout_marks_get_unused(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return lw->options.show_marks;
}
#pragma GCC diagnostic pop

void layout_sort_set(LayoutWindow *lw, SortType type, gboolean ascend)
{
	if (!layout_valid(&lw)) return;
	if (lw->sort_method == type && lw->sort_ascend == ascend) return;

	lw->sort_method = type;
	lw->sort_ascend = ascend;

	if (lw->info_sort) gtk_button_set_label(GTK_BUTTON(lw->info_sort), sort_type_get_text(type));
	layout_list_sync_sort(lw);
}

gboolean layout_sort_get(LayoutWindow *lw, SortType *type, gboolean *ascend)
{
	if (!layout_valid(&lw)) return FALSE;

	if (type) *type = lw->sort_method;
	if (ascend) *ascend = lw->sort_ascend;

	return TRUE;
}

gboolean layout_geometry_get(LayoutWindow *lw, gint *x, gint *y, gint *w, gint *h)
{
	GdkWindow *window;
	if (!layout_valid(&lw)) return FALSE;

	window = gtk_widget_get_window(lw->window);
	gdk_window_get_root_origin(window, x, y);
	*w = gdk_window_get_width(window);
	*h = gdk_window_get_height(window);

	return TRUE;
}

gboolean layout_geometry_get_dividers(LayoutWindow *lw, gint *h, gint *v)
{
	GtkAllocation h_allocation;
	GtkAllocation v_allocation;

	if (!layout_valid(&lw)) return FALSE;

	if (lw->h_pane)
		{
		GtkWidget *child = gtk_paned_get_child1(GTK_PANED(lw->h_pane));
		gtk_widget_get_allocation(child, &h_allocation);
		}

	if (lw->v_pane)
		{
		GtkWidget *child = gtk_paned_get_child1(GTK_PANED(lw->v_pane));
		gtk_widget_get_allocation(child, &v_allocation);
		}

	if (lw->h_pane && h_allocation.x >= 0)
		{
		*h = h_allocation.width;
		}
	else if (h != &lw->options.main_window.hdivider_pos)
		{
		*h = lw->options.main_window.hdivider_pos;
		}

	if (lw->v_pane && v_allocation.x >= 0)
		{
		*v = v_allocation.height;
		}
	else if (v != &lw->options.main_window.vdivider_pos)
		{
		*v = lw->options.main_window.vdivider_pos;
		}

	return TRUE;
}

void layout_views_set(LayoutWindow *lw, DirViewType dir_view_type, FileViewType file_view_type)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.dir_view_type == dir_view_type && lw->options.file_view_type == file_view_type) return;

	lw->options.dir_view_type = dir_view_type;
	lw->options.file_view_type = file_view_type;

	layout_style_set(lw, -1, nullptr);
}

void layout_views_set_sort(LayoutWindow *lw, SortType method, gboolean ascend)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.dir_view_list_sort.method == method && lw->options.dir_view_list_sort.ascend == ascend) return;

	lw->options.dir_view_list_sort.method = method;
	lw->options.dir_view_list_sort.ascend = ascend;

	layout_style_set(lw, -1, nullptr);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
gboolean layout_views_get_unused(LayoutWindow *lw, DirViewType *dir_view_type, FileViewType *file_view_type)
{
	if (!layout_valid(&lw)) return FALSE;

	*dir_view_type = lw->options.dir_view_type;
	*file_view_type = lw->options.file_view_type;

	return TRUE;
}
#pragma GCC diagnostic pop

/*
 *-----------------------------------------------------------------------------
 * location utils
 *-----------------------------------------------------------------------------
 */

static gboolean layout_location_single(LayoutLocation l)
{
	return (l == LAYOUT_LEFT ||
		l == LAYOUT_RIGHT ||
		l == LAYOUT_TOP ||
		l == LAYOUT_BOTTOM);
}

static gboolean layout_location_vertical(LayoutLocation l)
{
	return (l & LAYOUT_TOP ||
		l & LAYOUT_BOTTOM);
}

static gboolean layout_location_first(LayoutLocation l)
{
	return (l & LAYOUT_TOP ||
		l & LAYOUT_LEFT);
}

static LayoutLocation layout_grid_compass(LayoutWindow *lw)
{
	if (layout_location_single(lw->dir_location)) return lw->dir_location;
	if (layout_location_single(lw->file_location)) return lw->file_location;
	return lw->image_location;
}

static void layout_location_compute(LayoutLocation l1, LayoutLocation l2,
				    GtkWidget *s1, GtkWidget *s2,
				    GtkWidget **d1, GtkWidget **d2)
{
	LayoutLocation l;

	l = static_cast<LayoutLocation>(l1 & l2);	/* get common compass direction */
	l = static_cast<LayoutLocation>(l1 - l);	/* remove it */

	if (layout_location_first(l))
		{
		*d1 = s1;
		*d2 = s2;
		}
	else
		{
		*d1 = s2;
		*d2 = s1;
		}
}

/*
 *-----------------------------------------------------------------------------
 * tools window (for floating/hidden)
 *-----------------------------------------------------------------------------
 */

gboolean layout_geometry_get_tools(LayoutWindow *lw, gint *x, gint *y, gint *w, gint *h, gint *divider_pos)
{
	GdkWindow *window;
	GtkAllocation allocation;
	if (!layout_valid(&lw)) return FALSE;

	if (!lw->tools || !gtk_widget_get_visible(lw->tools))
		{
		/* use the stored values (sort of breaks success return value) */

		*divider_pos = lw->options.float_window.vdivider_pos;

		return FALSE;
		}

	window = gtk_widget_get_window(lw->tools);
	gdk_window_get_root_origin(window, x, y);
	*w = gdk_window_get_width(window);
	*h = gdk_window_get_height(window);
	gtk_widget_get_allocation(gtk_paned_get_child1(GTK_PANED(lw->tools_pane)), &allocation);

	if (gtk_orientable_get_orientation(GTK_ORIENTABLE(lw->tools_pane)) == GTK_ORIENTATION_VERTICAL)
		{
		*divider_pos = allocation.height;
		}
	else
		{
		*divider_pos = allocation.width;
		}

	return TRUE;
}

gboolean layout_geometry_get_log_window(LayoutWindow *lw, gint *x, gint *y,
														gint *w, gint *h)
{
	GdkWindow *window;

	if (!layout_valid(&lw)) return FALSE;

	if (!lw->log_window)
		{
		return FALSE;
		}

	window = gtk_widget_get_window(lw->log_window);
	gdk_window_get_root_origin(window, x, y);
	*w = gdk_window_get_width(window);
	*h = gdk_window_get_height(window);

	return TRUE;
}

static void layout_tools_geometry_sync(LayoutWindow *lw)
{
	layout_geometry_get_tools(lw, &lw->options.float_window.x, &lw->options.float_window.y,
				  &lw->options.float_window.w, &lw->options.float_window.h, &lw->options.float_window.vdivider_pos);
}

static void layout_tools_hide(LayoutWindow *lw, gboolean hide)
{
	if (!lw->tools) return;

	if (hide)
		{
		if (gtk_widget_get_visible(lw->tools))
			{
			layout_tools_geometry_sync(lw);
			gtk_widget_hide(lw->tools);
			}
		}
	else
		{
		if (!gtk_widget_get_visible(lw->tools))
			{
			gtk_widget_show(lw->tools);
			if (lw->vf) vf_refresh(lw->vf);
			}
		}

	lw->options.tools_hidden = hide;
}

static gboolean layout_tools_delete_cb(GtkWidget *, GdkEventAny *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_tools_float_toggle(lw);

	return TRUE;
}

static void layout_tools_setup(LayoutWindow *lw, GtkWidget *tools, GtkWidget *files)
{
	GtkWidget *vbox;
	GtkWidget *w1, *w2;
	gboolean vertical;
	gboolean new_window = FALSE;

	vertical = (layout_location_single(lw->image_location) && !layout_location_vertical(lw->image_location)) ||
		   (!layout_location_single(lw->image_location) && layout_location_vertical(layout_grid_compass(lw)));
	/* for now, tools/dir are always first in order */
	w1 = tools;
	w2 = files;

	if (!lw->tools)
		{
		GdkGeometry geometry;
		GdkWindowHints hints;

		lw->tools = window_new(GTK_WINDOW_TOPLEVEL, "tools", PIXBUF_INLINE_ICON_TOOLS, nullptr, _("Tools"));
		DEBUG_NAME(lw->tools);
		g_signal_connect(G_OBJECT(lw->tools), "delete_event",
				 G_CALLBACK(layout_tools_delete_cb), lw);
		layout_keyboard_init(lw, lw->tools);

		if (options->save_window_positions)
			{
			hints = GDK_HINT_USER_POS;
			}
		else
			{
			hints = static_cast<GdkWindowHints>(0);
			}

		geometry.min_width = DEFAULT_MINIMAL_WINDOW_SIZE;
		geometry.min_height = DEFAULT_MINIMAL_WINDOW_SIZE;
		geometry.base_width = TOOLWINDOW_DEF_WIDTH;
		geometry.base_height = TOOLWINDOW_DEF_HEIGHT;
		gtk_window_set_geometry_hints(GTK_WINDOW(lw->tools), nullptr, &geometry,
					      static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE | hints));


		gtk_window_set_resizable(GTK_WINDOW(lw->tools), TRUE);
		gtk_container_set_border_width(GTK_CONTAINER(lw->tools), 0);
		if (options->expand_menu_toolbar) gtk_container_remove(GTK_CONTAINER(lw->main_box), lw->menu_tool_bar);

		new_window = TRUE;
		}
	else
		{
		layout_tools_geometry_sync(lw);
		/* dump the contents */
		gtk_widget_destroy(gtk_bin_get_child(GTK_BIN(lw->tools)));
		}

	layout_actions_add_window(lw, lw->tools);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(vbox);
	gtk_container_add(GTK_CONTAINER(lw->tools), vbox);
	if (options->expand_menu_toolbar) gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(lw->menu_tool_bar), FALSE, FALSE, 0);
	gtk_widget_show(vbox);

	layout_status_setup(lw, vbox, TRUE);

	if (vertical)
		{
		lw->tools_pane = gtk_vpaned_new();
		DEBUG_NAME(lw->tools_pane);
		}
	else
		{
		lw->tools_pane = gtk_hpaned_new();
		DEBUG_NAME(lw->tools_pane);
		}
	gtk_box_pack_start(GTK_BOX(vbox), lw->tools_pane, TRUE, TRUE, 0);
	gtk_widget_show(lw->tools_pane);

	gtk_paned_pack1(GTK_PANED(lw->tools_pane), w1, FALSE, TRUE);
	gtk_paned_pack2(GTK_PANED(lw->tools_pane), w2, TRUE, TRUE);

	gtk_widget_show(tools);
	gtk_widget_show(files);

	if (new_window)
		{
		if (options->save_window_positions)
			{
			gtk_window_set_default_size(GTK_WINDOW(lw->tools), lw->options.float_window.w, lw->options.float_window.h);
			gtk_window_move(GTK_WINDOW(lw->tools), lw->options.float_window.x, lw->options.float_window.y);
			}
		else
			{
			if (vertical)
				{
				gtk_window_set_default_size(GTK_WINDOW(lw->tools),
							    TOOLWINDOW_DEF_WIDTH, TOOLWINDOW_DEF_HEIGHT);
				}
			else
				{
				gtk_window_set_default_size(GTK_WINDOW(lw->tools),
							    TOOLWINDOW_DEF_HEIGHT, TOOLWINDOW_DEF_WIDTH);
				}
			}
		}

	if (!options->save_window_positions)
		{
		if (vertical)
			{
			lw->options.float_window.vdivider_pos = MAIN_WINDOW_DIV_VPOS;
			}
		else
			{
			lw->options.float_window.vdivider_pos = MAIN_WINDOW_DIV_HPOS;
			}
		}

	gtk_paned_set_position(GTK_PANED(lw->tools_pane), lw->options.float_window.vdivider_pos);
}

/*
 *-----------------------------------------------------------------------------
 * glue (layout arrangement)
 *-----------------------------------------------------------------------------
 */

static void layout_grid_compute(LayoutWindow *lw,
				GtkWidget *image, GtkWidget *tools, GtkWidget *files,
				GtkWidget **w1, GtkWidget **w2, GtkWidget **w3)
{
	/* heh, this was fun */

	if (layout_location_single(lw->dir_location))
		{
		if (layout_location_first(lw->dir_location))
			{
			*w1 = tools;
			layout_location_compute(lw->file_location, lw->image_location, files, image, w2, w3);
			}
		else
			{
			*w3 = tools;
			layout_location_compute(lw->file_location, lw->image_location, files, image, w1, w2);
			}
		}
	else if (layout_location_single(lw->file_location))
		{
		if (layout_location_first(lw->file_location))
			{
			*w1 = files;
			layout_location_compute(lw->dir_location, lw->image_location, tools, image, w2, w3);
			}
		else
			{
			*w3 = files;
			layout_location_compute(lw->dir_location, lw->image_location, tools, image, w1, w2);
			}
		}
	else
		{
		/* image */
		if (layout_location_first(lw->image_location))
			{
			*w1 = image;
			layout_location_compute(lw->file_location, lw->dir_location, files, tools, w2, w3);
			}
		else
			{
			*w3 = image;
			layout_location_compute(lw->file_location, lw->dir_location, files, tools, w1, w2);
			}
		}
}

void layout_split_change(LayoutWindow *lw, ImageSplitMode mode)
{
	GtkWidget *image;
	gint i;

	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i])
			{
			gtk_widget_hide(lw->split_images[i]->widget);
			if (gtk_widget_get_parent(lw->split_images[i]->widget) != lw->utility_paned)
				gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(lw->split_images[i]->widget)), lw->split_images[i]->widget);
			}
		}
	gtk_container_remove(GTK_CONTAINER(lw->utility_paned), lw->split_image_widget);

	image = layout_image_setup_split(lw, mode);

	gtk_paned_pack1(GTK_PANED(lw->utility_paned), image, TRUE, FALSE);
	gtk_widget_show(image);
	layout_util_sync(lw);
}

static void layout_grid_setup(LayoutWindow *lw)
{
	gint priority_location;
	GtkWidget *h;
	GtkWidget *v;
	GtkWidget *w1, *w2, *w3;

	GtkWidget *image_sb; /* image together with sidebars in utility box */
	GtkWidget *tools;
	GtkWidget *files;

	layout_actions_setup(lw);

	lw->group_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(lw->group_box);
	if (options->expand_menu_toolbar)
		{
		gtk_box_pack_end(GTK_BOX(lw->main_box), lw->group_box, TRUE, TRUE, 0);
		}
	else
		{
		gtk_box_pack_start(GTK_BOX(lw->main_box), lw->group_box, TRUE, TRUE, 0);
		}
	gtk_widget_show(lw->group_box);

	priority_location = layout_grid_compass(lw);

	if (lw->utility_box)
		{
		layout_split_change(lw, lw->split_mode); /* this re-creates image frame for the new configuration */
		image_sb = lw->utility_box;
		DEBUG_NAME(image_sb);
		}
	else
		{
		GtkWidget *image; /* image or split images together */
		image = layout_image_setup_split(lw, lw->split_mode);
		image_sb = layout_bars_prepare(lw, image);
		DEBUG_NAME(image_sb);
		}

	tools = layout_tools_new(lw);
	DEBUG_NAME(tools);
	files = layout_list_new(lw);
	DEBUG_NAME(files);


	if (lw->options.tools_float || lw->options.tools_hidden)
		{
		gtk_box_pack_start(GTK_BOX(lw->group_box), image_sb, TRUE, TRUE, 0);
		gtk_widget_show(image_sb);

		layout_tools_setup(lw, tools, files);

		image_grab_focus(lw->image);

		return;
		}
	else if (lw->tools)
		{
		layout_tools_geometry_sync(lw);
		gtk_widget_destroy(lw->tools);
		lw->tools = nullptr;
		lw->tools_pane = nullptr;
		}

	layout_status_setup(lw, lw->group_box, FALSE);

	layout_grid_compute(lw, image_sb, tools, files, &w1, &w2, &w3);

	v = lw->v_pane = gtk_vpaned_new();
	DEBUG_NAME(v);

	h = lw->h_pane = gtk_hpaned_new();
	DEBUG_NAME(h);

	if (!layout_location_vertical(static_cast<LayoutLocation>(priority_location)))
		{
		GtkWidget *tmp;

		tmp = v;
		v = h;
		h = tmp;
		}

	gtk_box_pack_start(GTK_BOX(lw->group_box), v, TRUE, TRUE, 0);

	if (!layout_location_first(static_cast<LayoutLocation>(priority_location)))
		{
		gtk_paned_pack1(GTK_PANED(v), h, FALSE, TRUE);
		gtk_paned_pack2(GTK_PANED(v), w3, TRUE, TRUE);

		gtk_paned_pack1(GTK_PANED(h), w1, FALSE, TRUE);
		gtk_paned_pack2(GTK_PANED(h), w2, TRUE, TRUE);
		}
	else
		{
		gtk_paned_pack1(GTK_PANED(v), w1, FALSE, TRUE);
		gtk_paned_pack2(GTK_PANED(v), h, TRUE, TRUE);

		gtk_paned_pack1(GTK_PANED(h), w2, FALSE, TRUE);
		gtk_paned_pack2(GTK_PANED(h), w3, TRUE, TRUE);
		}

	gtk_widget_show(image_sb);
	gtk_widget_show(tools);
	gtk_widget_show(files);

	gtk_widget_show(v);
	gtk_widget_show(h);

	/* fix to have image pane visible when it is left and priority widget */
	if (lw->options.main_window.hdivider_pos == -1 &&
	    w1 == image_sb &&
	    !layout_location_vertical(static_cast<LayoutLocation>(priority_location)) &&
	    layout_location_first(static_cast<LayoutLocation>(priority_location)))
		{
		gtk_widget_set_size_request(image_sb, 200, -1);
		}

	gtk_paned_set_position(GTK_PANED(lw->h_pane), lw->options.main_window.hdivider_pos);
	gtk_paned_set_position(GTK_PANED(lw->v_pane), lw->options.main_window.vdivider_pos);

	image_grab_focus(lw->image);
}

void layout_style_set(LayoutWindow *lw, gint style, const gchar *order)
{
	FileData *dir_fd;
	gint i;

	if (!layout_valid(&lw)) return;

	if (style != -1)
		{
		LayoutLocation d, f, i;

		layout_config_parse(style, order, &d,  &f, &i);

		if (lw->dir_location == d &&
		    lw->file_location == f &&
		    lw->image_location == i) return;

		lw->dir_location = d;
		lw->file_location = f;
		lw->image_location = i;
		}

	/* remember state */

	/* layout_image_slideshow_stop(lw); slideshow should survive */
	layout_image_full_screen_stop(lw);

	dir_fd = lw->dir_fd;
	if (dir_fd) file_data_unregister_real_time_monitor(dir_fd);
	lw->dir_fd = nullptr;

	layout_geometry_get_dividers(lw, &lw->options.main_window.hdivider_pos, &lw->options.main_window.vdivider_pos);

	/* preserve utility_box (image + sidebars), menu_bar and toolbars to be reused later in layout_grid_setup */
	/* lw->image is preserved together with lw->utility_box */
	if (lw->utility_box) gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(lw->utility_box)), lw->utility_box);

	if (options->expand_menu_toolbar)
		{
		if (lw->toolbar[TOOLBAR_STATUS]) gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(lw->toolbar[TOOLBAR_STATUS])), lw->toolbar[TOOLBAR_STATUS]);

		if (lw->menu_tool_bar) gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(lw->menu_tool_bar)), lw->menu_tool_bar);
		}
	else
		{
		if (lw->menu_bar) gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(lw->menu_bar)), lw->menu_bar);
			for (i = 0; i < TOOLBAR_COUNT; i++)
				if (lw->toolbar[i]) gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(lw->toolbar[i])), lw->toolbar[i]);
		}

	/* clear it all */

	lw->h_pane = nullptr;
	lw->v_pane = nullptr;

	lw->path_entry = nullptr;
	lw->dir_view = nullptr;
	lw->vd = nullptr;

	lw->file_view = nullptr;
	lw->vf = nullptr;

	lw->info_box = nullptr;
	lw->info_progress_bar = nullptr;
	lw->info_sort = nullptr;
	lw->info_status = nullptr;
	lw->info_details = nullptr;
	lw->info_pixel = nullptr;
	lw->info_zoom = nullptr;

	gtk_container_remove(GTK_CONTAINER(lw->main_box), lw->group_box);
	lw->group_box = nullptr;

	/* re-fill */

	layout_grid_setup(lw);
	layout_tools_hide(lw, lw->options.tools_hidden);

	layout_util_sync(lw);
	layout_status_update_all(lw);

	/* sync */

	if (image_get_fd(lw->image))
		{
		layout_set_fd(lw, image_get_fd(lw->image));
		}
	else
		{
		layout_set_fd(lw, dir_fd);
		}
	image_top_window_set_sync(lw->image, (lw->options.tools_float || lw->options.tools_hidden));

	/* clean up */

	file_data_unref(dir_fd);
}

void layout_colors_update()
{
	GList *work;

	work = layout_window_list;
	while (work)
		{
		gint i;
		auto lw = static_cast<LayoutWindow *>(work->data);
		work = work->next;

		if (!lw->image) continue;

		for (i = 0; i < MAX_SPLIT_IMAGES; i++)
			{
			if (!lw->split_images[i]) continue;
			image_background_set_color_from_options(lw->split_images[i], !!lw->full_screen);
			}

		image_background_set_color_from_options(lw->image, !!lw->full_screen);
		}
}

void layout_tools_float_toggle(LayoutWindow *lw)
{
	gboolean popped;

	if (!lw) return;

	if (!lw->options.tools_hidden)
		{
		popped = !lw->options.tools_float;
		}
	else
		{
		popped = TRUE;
		}

	if (lw->options.tools_float == popped)
		{
		if (popped && lw->options.tools_hidden)
			{
			layout_tools_float_set(lw, popped, FALSE);
			}
		}
	else
		{
		if (lw->options.tools_float)
			{
			layout_tools_float_set(lw, FALSE, FALSE);
			}
		else
			{
			layout_tools_float_set(lw, TRUE, FALSE);
			}
		}
}

void layout_tools_hide_toggle(LayoutWindow *lw)
{
	if (!lw) return;

	layout_tools_float_set(lw, lw->options.tools_float, !lw->options.tools_hidden);
}

void layout_tools_float_set(LayoutWindow *lw, gboolean popped, gboolean hidden)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.tools_float == popped && lw->options.tools_hidden == hidden) return;

	if (lw->options.tools_float == popped && lw->options.tools_float && lw->tools)
		{
		layout_tools_hide(lw, hidden);
		return;
		}

	lw->options.tools_float = popped;
	lw->options.tools_hidden = hidden;

	layout_style_set(lw, -1, nullptr);
}

gboolean layout_tools_float_get(LayoutWindow *lw, gboolean *popped, gboolean *hidden)
{
	if (!layout_valid(&lw)) return FALSE;

	*popped = lw->options.tools_float;
	*hidden = lw->options.tools_hidden;

	return TRUE;
}

void layout_toolbar_toggle(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;
	if (!lw->toolbar[TOOLBAR_MAIN]) return;

	lw->options.toolbar_hidden = !lw->options.toolbar_hidden;

	if (lw->options.toolbar_hidden)
		{
		if (gtk_widget_get_visible(lw->toolbar[TOOLBAR_MAIN])) gtk_widget_hide(lw->toolbar[TOOLBAR_MAIN]);
		}
	else
		{
		if (!gtk_widget_get_visible(lw->toolbar[TOOLBAR_MAIN])) gtk_widget_show(lw->toolbar[TOOLBAR_MAIN]);
		}
}

void layout_info_pixel_set(LayoutWindow *lw, gboolean show)
{
	GtkWidget *frame;

	if (!layout_valid(&lw)) return;
	if (!lw->info_pixel) return;

	lw->options.show_info_pixel = show;

	frame = gtk_widget_get_parent(lw->info_pixel);
	if (!lw->options.show_info_pixel)
		{
		gtk_widget_hide(frame);
		}
	else
		{
		gtk_widget_show(frame);
		}

	g_signal_emit_by_name (lw->image->pr, "update-pixel");
}

/*
 *-----------------------------------------------------------------------------
 * configuration
 *-----------------------------------------------------------------------------
 */

#define CONFIG_WINDOW_DEF_WIDTH		600
#define CONFIG_WINDOW_DEF_HEIGHT	400

struct LayoutConfig
{
	LayoutWindow *lw;

	GtkWidget *configwindow;
	GtkWidget *home_path_entry;
	GtkWidget *layout_widget;

	LayoutOptions options;
};

static gint layout_config_delete_cb(GtkWidget *w, GdkEventAny *event, gpointer data);

static void layout_config_close_cb(GtkWidget *, gpointer data)
{
	auto lc = static_cast<LayoutConfig *>(data);

	gtk_widget_destroy(lc->configwindow);
	free_layout_options_content(&lc->options);
	g_free(lc);
}

static gint layout_config_delete_cb(GtkWidget *w, GdkEventAny *, gpointer data)
{
	layout_config_close_cb(w, data);
	return TRUE;
}

static void layout_config_apply_cb(GtkWidget *, gpointer data)
{
	auto lc = static_cast<LayoutConfig *>(data);

	g_free(lc->options.order);
	lc->options.order = layout_config_get(lc->layout_widget, &lc->options.style);

	config_entry_to_option(lc->home_path_entry, &lc->options.home_path, remove_trailing_slash);

	layout_apply_options(lc->lw, &lc->options);
}

static void layout_config_help_cb(GtkWidget *, gpointer)
{
	help_window_show("GuideOptionsLayout.html");
}

static void layout_config_ok_cb(GtkWidget *widget, gpointer data)
{
	auto lc = static_cast<LayoutConfig *>(data);
	layout_config_apply_cb(widget, lc);
	layout_config_close_cb(widget, lc);
}

static void home_path_set_current_cb(GtkWidget *, gpointer data)
{
	auto lc = static_cast<LayoutConfig *>(data);
	gtk_entry_set_text(GTK_ENTRY(lc->home_path_entry), layout_get_path(lc->lw));
}

static void startup_path_set_current_cb(GtkWidget *widget, gpointer data)
{
	auto lc = static_cast<LayoutConfig *>(data);
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		return;
		}
	lc->options.startup_path = STARTUP_PATH_CURRENT;
}

static void startup_path_set_last_cb(GtkWidget *widget, gpointer data)
{
	auto lc = static_cast<LayoutConfig *>(data);
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		return;
		}
	lc->options.startup_path = STARTUP_PATH_LAST;
}

static void startup_path_set_home_cb(GtkWidget *widget, gpointer data)
{
	auto lc = static_cast<LayoutConfig *>(data);
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		{
		return;
		}
	lc->options.startup_path = STARTUP_PATH_HOME;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void layout_config_save_cb_unused(GtkWidget *, gpointer)
{
	//layout_config_apply();
	save_options(options);
}
#pragma GCC diagnostic pop

void layout_show_config_window(LayoutWindow *lw)
{
	LayoutConfig *lc;
	GtkWidget *win_vbox;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *group;
	GtkWidget *frame;
	GtkWidget *tabcomp;

	lc = g_new0(LayoutConfig, 1);
	lc->lw = lw;
	layout_sync_options_with_current_state(lw);
	copy_layout_options(&lc->options, &lw->options);

	lc->configwindow = window_new(GTK_WINDOW_TOPLEVEL, "Layout", PIXBUF_INLINE_ICON_CONFIG, nullptr, _("Window options and layout"));
	DEBUG_NAME(lc->configwindow);
	gtk_window_set_type_hint(GTK_WINDOW(lc->configwindow), GDK_WINDOW_TYPE_HINT_DIALOG);

	g_signal_connect(G_OBJECT(lc->configwindow), "delete_event",
			 G_CALLBACK(layout_config_delete_cb), lc);

	gtk_window_set_default_size(GTK_WINDOW(lc->configwindow), CONFIG_WINDOW_DEF_WIDTH, CONFIG_WINDOW_DEF_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(lc->configwindow), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(lc->configwindow), PREF_PAD_BORDER);

	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	DEBUG_NAME(win_vbox);
	gtk_container_add(GTK_CONTAINER(lc->configwindow), win_vbox);
	gtk_widget_show(win_vbox);

	hbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_BUTTON_GAP);
	gtk_box_pack_end(GTK_BOX(win_vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = pref_button_new(nullptr, GQ_ICON_OK, "OK",
				 G_CALLBACK(layout_config_ok_cb), lc);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);

	ct_button = button;
/*
	button = pref_button_new(NULL, GQ_ICON_SAVE, _("Save"), FALSE,
				 G_CALLBACK(layout_config_save_cb), NULL);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_widget_show(button);
*/
	button = pref_button_new(nullptr, GQ_ICON_HELP, _("Help"),
				 G_CALLBACK(layout_config_help_cb), lc);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_APPLY, _("Apply"),
				 G_CALLBACK(layout_config_apply_cb), lc);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_CANCEL, _("Cancel"),
				 G_CALLBACK(layout_config_close_cb), lc);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	if (!generic_dialog_get_alternative_button_order(lc->configwindow))
		{
		gtk_box_reorder_child(GTK_BOX(hbox), ct_button, -1);
		}

	frame = pref_frame_new(win_vbox, TRUE, nullptr, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	DEBUG_NAME(frame);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	DEBUG_NAME(vbox);
	gtk_container_add(GTK_CONTAINER(frame), vbox);
	gtk_widget_show(vbox);


	group = pref_group_new(vbox, FALSE, _("General options"), GTK_ORIENTATION_VERTICAL);

	pref_label_new(group, _("Home path (empty to use your home directory)"));
	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	tabcomp = tab_completion_new(&lc->home_path_entry, lc->options.home_path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(lc->home_path_entry, nullptr, TRUE);
	gtk_box_pack_start(GTK_BOX(hbox), tabcomp, TRUE, TRUE, 0);
	gtk_widget_show(tabcomp);

	button = pref_button_new(hbox, nullptr, _("Use current"),
				 G_CALLBACK(home_path_set_current_cb), lc);

	pref_checkbox_new_int(group, _("Show date in directories list view"),
			      lc->options.show_directory_date, &lc->options.show_directory_date);

	group = pref_group_new(vbox, FALSE, _("Start-up directory:"), GTK_ORIENTATION_VERTICAL);

	button = pref_radiobutton_new(group, nullptr, _("No change"),
				      (lc->options.startup_path == STARTUP_PATH_CURRENT),
				      G_CALLBACK(startup_path_set_current_cb), lc);
	button = pref_radiobutton_new(group, button, _("Restore last path"),
				      (lc->options.startup_path == STARTUP_PATH_LAST),
				      G_CALLBACK(startup_path_set_last_cb), lc);
	button = pref_radiobutton_new(group, button, _("Home path"),
				      (lc->options.startup_path == STARTUP_PATH_HOME),
				      G_CALLBACK(startup_path_set_home_cb), lc);

	group = pref_group_new(vbox, FALSE, _("Layout"), GTK_ORIENTATION_VERTICAL);

	lc->layout_widget = layout_config_new();
	DEBUG_NAME(lc->layout_widget);
	layout_config_set(lc->layout_widget, lw->options.style, lw->options.order);
	gtk_box_pack_start(GTK_BOX(group), lc->layout_widget, TRUE, TRUE, 0);

	gtk_widget_show(lc->layout_widget);
	gtk_widget_show(lc->configwindow);
}

/*
 *-----------------------------------------------------------------------------
 * base
 *-----------------------------------------------------------------------------
 */

void layout_sync_options_with_current_state(LayoutWindow *lw)
{
	Histogram *histogram;
#ifdef GDK_WINDOWING_X11
	GdkWindow *window;
#endif

	if (!layout_valid(&lw)) return;

	lw->options.main_window.maximized =  window_maximized(lw->window);
	if (!lw->options.main_window.maximized)
		{
		layout_geometry_get(lw, &lw->options.main_window.x, &lw->options.main_window.y,
				    &lw->options.main_window.w, &lw->options.main_window.h);
		}

	layout_geometry_get_dividers(lw, &lw->options.main_window.hdivider_pos, &lw->options.main_window.vdivider_pos);

	layout_geometry_get_tools(lw, &lw->options.float_window.x, &lw->options.float_window.y,
				  &lw->options.float_window.w, &lw->options.float_window.h, &lw->options.float_window.vdivider_pos);

	lw->options.image_overlay.state = image_osd_get(lw->image);
	histogram = image_osd_get_histogram(lw->image);

	lw->options.image_overlay.histogram_channel = histogram->histogram_channel;
	lw->options.image_overlay.histogram_mode = histogram->histogram_mode;

	g_free(lw->options.last_path);
	lw->options.last_path = g_strdup(layout_get_path(lw));

	layout_geometry_get_log_window(lw, &lw->options.log_window.x, &lw->options.log_window.y,
	                                 &lw->options.log_window.w, &lw->options.log_window.h);

#ifdef GDK_WINDOWING_X11
	GdkDisplay *display;

	if (options->save_window_workspace)
		{
		display = gdk_display_get_default();

		if (GDK_IS_X11_DISPLAY(display))
			{
			window = gtk_widget_get_window(GTK_WIDGET(lw->window));
			lw->options.workspace = gdk_x11_window_get_desktop(window);
			}
		}
#endif
}

void layout_apply_options(LayoutWindow *lw, LayoutOptions *lop)
{
	gboolean refresh_style;
	gboolean refresh_lists;

	if (!layout_valid(&lw)) return;
/** @FIXME add other options too */

	refresh_style = (lop->style != lw->options.style || strcmp(lop->order, lw->options.order) != 0);
	refresh_lists = (lop->show_directory_date != lw->options.show_directory_date);

	copy_layout_options(&lw->options, lop);

	if (refresh_style) layout_style_set(lw, lw->options.style, lw->options.order);
	if (refresh_lists) layout_refresh(lw);
}

void save_layout(LayoutWindow *lw)
{
	gchar *path;
	gchar *xml_name;

	if (!g_str_has_prefix(lw->options.id, "lw"))
		{
		xml_name = g_strdup_printf("%s.xml", lw->options.id);
		path = g_build_filename(get_window_layouts_dir(), xml_name, NULL);
		save_config_to_file(path, options, lw);

		g_free(xml_name);
		g_free(path);
		}
}

void layout_close(LayoutWindow *lw)
{
	if (layout_window_list && layout_window_list->next)
		{
		save_layout(lw);
		layout_free(lw);
		}
	else
		{
		exit_program();
		}
}

void layout_free(LayoutWindow *lw)
{
	gint i;
	if (!lw) return;

	layout_window_list = g_list_remove(layout_window_list, lw);
	if (current_lw == lw) current_lw = nullptr;

	if (lw->exif_window) g_signal_handlers_disconnect_matched(G_OBJECT(lw->exif_window), G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, lw);

	layout_bars_close(lw);

	g_object_unref(lw->menu_bar);
	g_object_unref(lw->utility_box);

	for (i = 0; i < TOOLBAR_COUNT; i++)
		{
		if (lw->toolbar[i]) g_object_unref(lw->toolbar[i]);
		}

	gtk_widget_destroy(lw->window);

	if (lw->split_image_sizegroup) g_object_unref(lw->split_image_sizegroup);

	file_data_unregister_notify_func(layout_image_notify_cb, lw);

	if (lw->dir_fd)
		{
		file_data_unregister_real_time_monitor(lw->dir_fd);
		file_data_unref(lw->dir_fd);
		}

	free_layout_options_content(&lw->options);
	g_free(lw);
}

static gboolean layout_delete_cb(GtkWidget *, GdkEventAny *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_close(lw);
	return TRUE;
}

LayoutWindow *layout_new(FileData *dir_fd, LayoutOptions *lop)
{
	return layout_new_with_geometry(dir_fd, lop, nullptr);
}

gboolean release_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	return defined_mouse_buttons(widget, event, data);
}

static gboolean move_window_to_workspace_cb(gpointer data)
{
#ifdef GDK_WINDOWING_X11
	auto lw = static_cast<LayoutWindow *>(data);
	GdkWindow *window;
	GdkDisplay *display;

	if (options->save_window_workspace)
		{
		display = gdk_display_get_default();

		if (GDK_IS_X11_DISPLAY(display))
			{
			if (lw->options.workspace != -1)
				{
				window = gtk_widget_get_window(GTK_WIDGET(lw->window));
				gdk_x11_window_move_to_desktop(window, lw->options.workspace);
				}
			}
		}
#endif
	return G_SOURCE_REMOVE;
}

LayoutWindow *layout_new_with_geometry(FileData *dir_fd, LayoutOptions *lop,
				       const gchar *geometry)
{
	LayoutWindow *lw;
	GdkGeometry hint;
	GdkWindowHints hint_mask;
	Histogram *histogram;
	gchar *default_path;

	DEBUG_1("%s layout_new: start", get_exec_time());
	lw = g_new0(LayoutWindow, 1);

	if (lop)
		copy_layout_options(&lw->options, lop);
	else
		init_layout_options(&lw->options);

	lw->sort_method = SORT_NAME;
	lw->sort_ascend = TRUE;

	layout_set_unique_id(lw);

	/* default layout */

	layout_config_parse(lw->options.style, lw->options.order,
			    &lw->dir_location,  &lw->file_location, &lw->image_location);
	if (lw->options.dir_view_type > DIRVIEW_LAST) lw->options.dir_view_type = DIRVIEW_LIST;
	if (lw->options.file_view_type > FILEVIEW_LAST) lw->options.file_view_type = FILEVIEW_LIST;
	/* divider positions */

	default_path = g_build_filename(get_rc_dir(), DEFAULT_WINDOW_LAYOUT, NULL);

	if (!options->save_window_positions)
		{
		if (!isfile(default_path))
			{
			lw->options.main_window.hdivider_pos = MAIN_WINDOW_DIV_HPOS;
			lw->options.main_window.vdivider_pos = MAIN_WINDOW_DIV_VPOS;
			lw->options.float_window.vdivider_pos = MAIN_WINDOW_DIV_VPOS;
			}
		}

	/* window */

	lw->window = window_new(GTK_WINDOW_TOPLEVEL, GQ_APPNAME_LC, nullptr, nullptr, nullptr);
	DEBUG_NAME(lw->window);
	gtk_window_set_resizable(GTK_WINDOW(lw->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(lw->window), 0);

	g_signal_connect(G_OBJECT(lw->window), "button_release_event", G_CALLBACK(release_cb), lw);

	if (options->save_window_positions)
		{
		hint_mask = GDK_HINT_USER_POS;
		}
	else
		{
		hint_mask = static_cast<GdkWindowHints>(0);
		}

	hint.min_width = 32;
	hint.min_height = 32;
	hint.base_width = 0;
	hint.base_height = 0;
	gtk_window_set_geometry_hints(GTK_WINDOW(lw->window), nullptr, &hint,
				      static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE | hint_mask));

	if (options->save_window_positions || isfile(default_path))
		{
		gtk_window_set_default_size(GTK_WINDOW(lw->window), lw->options.main_window.w, lw->options.main_window.h);
		gtk_window_move(GTK_WINDOW(lw->window), lw->options.main_window.x, lw->options.main_window.y);
		if (lw->options.main_window.maximized) gtk_window_maximize(GTK_WINDOW(lw->window));

		g_idle_add(move_window_to_workspace_cb, lw);
		}
	else
		{
		gtk_window_set_default_size(GTK_WINDOW(lw->window), MAINWINDOW_DEF_WIDTH, MAINWINDOW_DEF_HEIGHT);
		}

	g_free(default_path);
	g_signal_connect(G_OBJECT(lw->window), "delete_event",
			 G_CALLBACK(layout_delete_cb), lw);

	g_signal_connect(G_OBJECT(lw->window), "focus-in-event",
			 G_CALLBACK(layout_set_current_cb), lw);

	layout_keyboard_init(lw, lw->window);

	lw->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(lw->main_box);
	gtk_container_add(GTK_CONTAINER(lw->window), lw->main_box);
	gtk_widget_show(lw->main_box);

	layout_grid_setup(lw);
	image_top_window_set_sync(lw->image, (lw->options.tools_float || lw->options.tools_hidden));

	layout_util_sync(lw);
	layout_status_update_all(lw);

	if (dir_fd)
		{
		layout_set_fd(lw, dir_fd);
		}
	else
		{
		GdkPixbuf *pixbuf;

		pixbuf = pixbuf_inline(PIXBUF_INLINE_LOGO);

		/** @FIXME the zoom value set here is the value, which is then copied again and again
		   in "Leave Zoom at previous setting" mode. This is not ideal.  */
		image_change_pixbuf(lw->image, pixbuf, 0.0, FALSE);
		g_object_unref(pixbuf);
		}

	if (geometry)
		{
		if (!gtk_window_parse_geometry(GTK_WINDOW(lw->window), geometry))
			{
			log_printf("%s", _("Invalid geometry\n"));
			}
		}

	gtk_widget_show(lw->window);
	layout_tools_hide(lw, lw->options.tools_hidden);

	image_osd_set(lw->image, static_cast<OsdShowFlags>(lw->options.image_overlay.state));
	histogram = image_osd_get_histogram(lw->image);

	histogram->histogram_channel = lw->options.image_overlay.histogram_channel;
	histogram->histogram_mode = lw->options.image_overlay.histogram_mode;

	layout_window_list = g_list_append(layout_window_list, lw);

	file_data_register_notify_func(layout_image_notify_cb, lw, NOTIFY_PRIORITY_LOW);

	DEBUG_1("%s layout_new: end", get_exec_time());

	return lw;
}

void layout_write_attributes(LayoutOptions *layout, GString *outstr, gint indent)
{
	WRITE_NL(); WRITE_CHAR(*layout, id);

	WRITE_NL(); WRITE_INT(*layout, style);
	WRITE_NL(); WRITE_CHAR(*layout, order);
	WRITE_NL(); WRITE_UINT(*layout, dir_view_type);
	WRITE_NL(); WRITE_UINT(*layout, file_view_type);
	WRITE_NL(); WRITE_UINT(*layout, dir_view_list_sort.method);
	WRITE_NL(); WRITE_BOOL(*layout, dir_view_list_sort.ascend);
	WRITE_NL(); WRITE_BOOL(*layout, show_marks);
	WRITE_NL(); WRITE_BOOL(*layout, show_file_filter);
	WRITE_NL(); WRITE_BOOL(*layout, show_thumbnails);
	WRITE_NL(); WRITE_BOOL(*layout, show_directory_date);
	WRITE_NL(); WRITE_CHAR(*layout, home_path);
	WRITE_NL(); WRITE_UINT(*layout, startup_path);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_INT(*layout, main_window.x);
	WRITE_NL(); WRITE_INT(*layout, main_window.y);
	WRITE_NL(); WRITE_INT(*layout, main_window.w);
	WRITE_NL(); WRITE_INT(*layout, main_window.h);
	WRITE_NL(); WRITE_BOOL(*layout, main_window.maximized);
	WRITE_NL(); WRITE_INT(*layout, main_window.hdivider_pos);
	WRITE_NL(); WRITE_INT(*layout, main_window.vdivider_pos);
	WRITE_NL(); WRITE_INT(*layout, workspace);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_INT(*layout, folder_window.vdivider_pos);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_INT(*layout, float_window.x);
	WRITE_NL(); WRITE_INT(*layout, float_window.y);
	WRITE_NL(); WRITE_INT(*layout, float_window.w);
	WRITE_NL(); WRITE_INT(*layout, float_window.h);
	WRITE_NL(); WRITE_INT(*layout, float_window.vdivider_pos);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_INT(*layout, properties_window.w);
	WRITE_NL(); WRITE_INT(*layout, properties_window.h);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(*layout, tools_float);
	WRITE_NL(); WRITE_BOOL(*layout, tools_hidden);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(*layout, toolbar_hidden);
	WRITE_NL(); WRITE_BOOL(*layout, show_info_pixel);
	WRITE_NL(); WRITE_BOOL(*layout, ignore_alpha);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(*layout, bars_state.info);
	WRITE_NL(); WRITE_BOOL(*layout, bars_state.sort);
	WRITE_NL(); WRITE_BOOL(*layout, bars_state.tools_float);
	WRITE_NL(); WRITE_BOOL(*layout, bars_state.tools_hidden);
	WRITE_NL(); WRITE_BOOL(*layout, bars_state.hidden);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_UINT(*layout, image_overlay.state);
	WRITE_NL(); WRITE_INT(*layout, image_overlay.histogram_channel);
	WRITE_NL(); WRITE_INT(*layout, image_overlay.histogram_mode);

	WRITE_NL(); WRITE_INT(*layout, log_window.x);
	WRITE_NL(); WRITE_INT(*layout, log_window.y);
	WRITE_NL(); WRITE_INT(*layout, log_window.w);
	WRITE_NL(); WRITE_INT(*layout, log_window.h);

	WRITE_NL(); WRITE_INT(*layout, preferences_window.x);
	WRITE_NL(); WRITE_INT(*layout, preferences_window.y);
	WRITE_NL(); WRITE_INT(*layout, preferences_window.w);
	WRITE_NL(); WRITE_INT(*layout, preferences_window.h);
	WRITE_NL(); WRITE_INT(*layout, preferences_window.page_number);

	WRITE_NL(); WRITE_INT(*layout, search_window.x);
	WRITE_NL(); WRITE_INT(*layout, search_window.y);
	WRITE_NL(); WRITE_INT(*layout, search_window.w);
	WRITE_NL(); WRITE_INT(*layout, search_window.h);

	WRITE_NL(); WRITE_INT(*layout, dupe_window.x);
	WRITE_NL(); WRITE_INT(*layout, dupe_window.y);
	WRITE_NL(); WRITE_INT(*layout, dupe_window.w);
	WRITE_NL(); WRITE_INT(*layout, dupe_window.h);

	WRITE_NL(); WRITE_INT(*layout, advanced_exif_window.x);
	WRITE_NL(); WRITE_INT(*layout, advanced_exif_window.y);
	WRITE_NL(); WRITE_INT(*layout, advanced_exif_window.w);
	WRITE_NL(); WRITE_INT(*layout, advanced_exif_window.h);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(*layout, animate);
}


void layout_write_config(LayoutWindow *lw, GString *outstr, gint indent)
{
	layout_sync_options_with_current_state(lw);
	WRITE_NL(); WRITE_STRING("<layout");
	layout_write_attributes(&lw->options, outstr, indent + 1);
	WRITE_STRING(">");

	bar_sort_write_config(lw->bar_sort, outstr, indent + 1);
	bar_write_config(lw->bar, outstr, indent + 1);

	WRITE_SEPARATOR();
	generic_dialog_windows_write_config(outstr, indent + 1);

	WRITE_SEPARATOR();
	layout_toolbar_write_config(lw, TOOLBAR_MAIN, outstr, indent + 1);
	layout_toolbar_write_config(lw, TOOLBAR_STATUS, outstr, indent + 1);

	WRITE_NL(); WRITE_STRING("</layout>");
}

void layout_load_attributes(LayoutOptions *layout, const gchar **attribute_names, const gchar **attribute_values)
{
	gchar *id = nullptr;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		/* layout options */
		if (READ_CHAR_FULL("id", id)) continue;

		if (READ_INT(*layout, style)) continue;
		if (READ_CHAR(*layout, order)) continue;

		if (READ_UINT_ENUM(*layout, dir_view_type)) continue;
		if (READ_UINT_ENUM(*layout, file_view_type)) continue;
		if (READ_UINT_ENUM(*layout, dir_view_list_sort.method)) continue;
		if (READ_BOOL(*layout, dir_view_list_sort.ascend)) continue;
		if (READ_BOOL(*layout, show_marks)) continue;
		if (READ_BOOL(*layout, show_file_filter)) continue;
		if (READ_BOOL(*layout, show_thumbnails)) continue;
		if (READ_BOOL(*layout, show_directory_date)) continue;
		if (READ_CHAR(*layout, home_path)) continue;
		if (READ_UINT_ENUM_CLAMP(*layout, startup_path, 0, STARTUP_PATH_HOME)) continue;

		/* window positions */

		if (READ_INT(*layout, main_window.x)) continue;
		if (READ_INT(*layout, main_window.y)) continue;
		if (READ_INT(*layout, main_window.w)) continue;
		if (READ_INT(*layout, main_window.h)) continue;
		if (READ_BOOL(*layout, main_window.maximized)) continue;
		if (READ_INT(*layout, main_window.hdivider_pos)) continue;
		if (READ_INT(*layout, main_window.vdivider_pos)) continue;

		if (READ_INT_CLAMP(*layout, folder_window.vdivider_pos, 1, 1000)) continue;

		if (READ_INT(*layout, float_window.x)) continue;
		if (READ_INT(*layout, float_window.y)) continue;
		if (READ_INT(*layout, float_window.w)) continue;
		if (READ_INT(*layout, float_window.h)) continue;
		if (READ_INT(*layout, float_window.vdivider_pos)) continue;

		if (READ_INT(*layout, properties_window.w)) continue;
		if (READ_INT(*layout, properties_window.h)) continue;

		if (READ_BOOL(*layout, tools_float)) continue;
		if (READ_BOOL(*layout, tools_hidden)) continue;
		if (READ_BOOL(*layout, toolbar_hidden)) continue;
		if (READ_BOOL(*layout, show_info_pixel)) continue;
		if (READ_BOOL(*layout, ignore_alpha)) continue;

		if (READ_BOOL(*layout, bars_state.info)) continue;
		if (READ_BOOL(*layout, bars_state.sort)) continue;
		if (READ_BOOL(*layout, bars_state.tools_float)) continue;
		if (READ_BOOL(*layout, bars_state.tools_hidden)) continue;
		if (READ_BOOL(*layout, bars_state.hidden)) continue;

		if (READ_UINT(*layout, image_overlay.state)) continue;
		if (READ_INT(*layout, image_overlay.histogram_channel)) continue;
		if (READ_INT(*layout, image_overlay.histogram_mode)) continue;

		if (READ_INT(*layout, log_window.x)) continue;
		if (READ_INT(*layout, log_window.y)) continue;
		if (READ_INT(*layout, log_window.w)) continue;
		if (READ_INT(*layout, log_window.h)) continue;

		if (READ_INT(*layout, preferences_window.x)) continue;
		if (READ_INT(*layout, preferences_window.y)) continue;
		if (READ_INT(*layout, preferences_window.w)) continue;
		if (READ_INT(*layout, preferences_window.h)) continue;
		if (READ_INT(*layout, preferences_window.page_number)) continue;

		if (READ_INT(*layout, search_window.x)) continue;
		if (READ_INT(*layout, search_window.y)) continue;
		if (READ_INT(*layout, search_window.w)) continue;
		if (READ_INT(*layout, search_window.h)) continue;

		if (READ_INT(*layout, dupe_window.x)) continue;
		if (READ_INT(*layout, dupe_window.y)) continue;
		if (READ_INT(*layout, dupe_window.w)) continue;
		if (READ_INT(*layout, dupe_window.h)) continue;

		if (READ_INT(*layout, advanced_exif_window.x)) continue;
		if (READ_INT(*layout, advanced_exif_window.y)) continue;
		if (READ_INT(*layout, advanced_exif_window.w)) continue;
		if (READ_INT(*layout, advanced_exif_window.h)) continue;

		if (READ_BOOL(*layout, animate)) continue;
		if (READ_INT(*layout, workspace)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}
	if (id && strcmp(id, LAYOUT_ID_CURRENT) != 0)
		{
		g_free(layout->id);
		layout->id = id;
		}
	else
		{
		g_free(id);
		}
}

static void layout_config_startup_path(LayoutOptions *lop, gchar **path)
{
	switch (lop->startup_path)
		{
		case STARTUP_PATH_LAST:
			*path = (history_list_find_last_path_by_key("path_list") && isdir(history_list_find_last_path_by_key("path_list"))) ? g_strdup(history_list_find_last_path_by_key("path_list")) : get_current_dir();
			break;
		case STARTUP_PATH_HOME:
			*path = (lop->home_path && isdir(lop->home_path)) ? g_strdup(lop->home_path) : g_strdup(homedir());
			break;
		default:
			*path = get_current_dir();
			break;
		}
}


static void layout_config_commandline(LayoutOptions *lop, gchar **path)
{
	gchar *last_image;

	if (command_line->startup_blank)
		{
		*path = nullptr;
		}
	else if (command_line->file)
		{
		*path = g_strdup(command_line->file);
		}
	else if (command_line->path)
		{
		*path = g_strdup(command_line->path);
		}
	else layout_config_startup_path(lop, path);

	if (isdir(*path))
		{
		last_image = get_recent_viewed_folder_image(*path);
		if (last_image)
			{
			g_free(*path);
			*path = last_image;
			}
		}

	if (command_line->tools_show)
		{
		lop->tools_float = FALSE;
		lop->tools_hidden = FALSE;
		}
	else if (command_line->tools_hide)
		{
		lop->tools_hidden = TRUE;
		}
}

static gboolean first_found = FALSE;

LayoutWindow *layout_new_from_config(const gchar **attribute_names, const gchar **attribute_values, gboolean use_commandline)
{
	LayoutOptions lop;
	LayoutWindow *lw;
	gchar *path = nullptr;

	init_layout_options(&lop);

	if (attribute_names) layout_load_attributes(&lop, attribute_names, attribute_values);

	/* If multiple windows are specified in the config. file,
	 * use the command line options only in the main window.
	 */
	if (use_commandline && !first_found)
		{
		first_found = TRUE;
		layout_config_commandline(&lop, &path);
		}
	else
		{
		layout_config_startup_path(&lop, &path);
		}

	lw = layout_new_with_geometry(nullptr, &lop, use_commandline ? command_line->geometry : nullptr);
	layout_sort_set(lw, options->file_sort.method, options->file_sort.ascending);
	layout_set_path(lw, path);

	if (use_commandline && command_line->startup_full_screen) layout_image_full_screen_start(lw);
	if (use_commandline && command_line->startup_in_slideshow) layout_image_slideshow_start(lw);
	if (use_commandline && command_line->log_window_show) log_window_new(lw);

	g_free(path);
	free_layout_options_content(&lop);
	return lw;
}

void layout_update_from_config(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values)
{
	LayoutOptions lop;

	init_layout_options(&lop);

	if (attribute_names) layout_load_attributes(&lop, attribute_names, attribute_values);

	layout_apply_options(lw, &lop);

	free_layout_options_content(&lop);
}

LayoutWindow *layout_new_from_default()
{
	LayoutWindow *lw;
	GList *work;
	gboolean success;
	gchar *default_path;

	default_path = g_build_filename(get_rc_dir(), DEFAULT_WINDOW_LAYOUT, NULL);
	success = load_config_from_file(default_path, TRUE);
	g_free(default_path);

	if (success)
		{
		work = g_list_last(layout_window_list);
		lw = static_cast<LayoutWindow *>(work->data);
		g_free(lw->options.id);
		lw->options.id = g_strdup(layout_get_unique_id());
		}
	else
		{
		lw = layout_new_from_config(nullptr, nullptr, TRUE);
		}
	return lw;
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
