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

#include "layout.h"

#include <unistd.h>

#include <algorithm>
#include <string>
#include <utility>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#  include <gdk/gdkx.h>
#endif
#include <glib-object.h>
#include <pango/pango.h>

#include "bar-sort.h"
#include "bar.h"
#include "compat.h"
#include "filedata.h"
#include "histogram.h"
#include "history-list.h"
#include "image-overlay.h"
#include "image.h"
#include "intl.h"
#include "layout-config.h"
#include "layout-image.h"
#include "layout-util.h"
#include "main-defines.h"
#include "main.h"
#include "menu.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "pixbuf-util.h"
#include "preferences.h"
#include "rcfile.h"
#include "shortcuts.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "ui-utildlg.h"
#include "view-dir.h"
#include "view-file.h"
#include "window.h"

namespace
{

constexpr gint MAINWINDOW_DEF_WIDTH = 700;
constexpr gint MAINWINDOW_DEF_HEIGHT = 500;

constexpr gint MAIN_WINDOW_DIV_HPOS = MAINWINDOW_DEF_WIDTH / 2;
constexpr gint MAIN_WINDOW_DIV_VPOS = MAINWINDOW_DEF_HEIGHT / 2;

constexpr gint TOOLWINDOW_DEF_WIDTH = 260;
constexpr gint TOOLWINDOW_DEF_HEIGHT = 450;

constexpr gint PROGRESS_WIDTH = 150;

constexpr gint ZOOM_LABEL_WIDTH = 120;

constexpr gint CONFIG_WINDOW_DEF_WIDTH = 600;
constexpr gint CONFIG_WINDOW_DEF_HEIGHT = 400;

struct LayoutConfig
{
	LayoutWindow *lw;

	GtkWidget *configwindow;
	GtkWidget *home_path_entry;
	GtkWidget *layout_widget;

	LayoutOptions options;
};

#define LAYOUT_ID_CURRENT "_current_"

std::vector<LayoutWindow *> layout_window_list;
LayoutWindow *current_lw = nullptr;

inline gboolean is_current_layout_id(const gchar *id)
{
	return strcmp(id, LAYOUT_ID_CURRENT) == 0;
}

LayoutWindow *layout_window_find_by_options_id(const gchar *id)
{
	auto it = std::find_if(layout_window_list.begin(), layout_window_list.end(),
	                       [id](LayoutWindow *lw){ return g_strcmp0(lw->options.id, id) == 0; });
	if (it == layout_window_list.end()) return nullptr;

	return *it;
}

void layout_load_attributes(LayoutOptions &lop, const gchar **attribute_names, const gchar **attribute_values)
{
	g_autofree gchar *id = nullptr;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		/* layout options */
		if (READ_CHAR_FULL("id", id)) continue;

		if (READ_INT(lop, style)) continue;
		if (READ_CHAR(lop, order)) continue;

		if (READ_UINT_ENUM(lop, dir_view_type)) continue;
		if (READ_UINT_ENUM(lop, file_view_type)) continue;
		if (READ_UINT_ENUM(lop, file_view_list_sort.method)) continue;
		if (READ_BOOL(lop, file_view_list_sort.ascending)) continue;
		if (READ_BOOL(lop, file_view_list_sort.case_sensitive)) continue;
		if (READ_UINT_ENUM(lop, dir_view_list_sort.method)) continue;
		if (READ_BOOL(lop, dir_view_list_sort.ascending)) continue;
		if (READ_BOOL(lop, dir_view_list_sort.case_sensitive)) continue;
		if (READ_BOOL(lop, show_marks)) continue;
		if (READ_BOOL(lop, show_file_filter)) continue;
		if (READ_BOOL(lop, show_thumbnails)) continue;
		if (READ_BOOL(lop, show_directory_date)) continue;
		if (READ_CHAR(lop, home_path)) continue;
		if (READ_UINT_ENUM_CLAMP(lop, startup_path, 0, STARTUP_PATH_HOME)) continue;

		/* window positions */

		if (READ_INT_FULL("main_window.x", lop.main_window.rect.x)) continue;
		if (READ_INT_FULL("main_window.y", lop.main_window.rect.y)) continue;
		if (READ_INT_FULL("main_window.w", lop.main_window.rect.width)) continue;
		if (READ_INT_FULL("main_window.h", lop.main_window.rect.height)) continue;
		if (READ_BOOL(lop, main_window.maximized)) continue;
		if (READ_INT(lop, main_window.hdivider_pos)) continue;
		if (READ_INT(lop, main_window.vdivider_pos)) continue;

		if (READ_INT_CLAMP(lop, folder_window.vdivider_pos, 1, 1000)) continue;

		if (READ_INT_FULL("float_window.x", lop.float_window.rect.x)) continue;
		if (READ_INT_FULL("float_window.y", lop.float_window.rect.y)) continue;
		if (READ_INT_FULL("float_window.w", lop.float_window.rect.width)) continue;
		if (READ_INT_FULL("float_window.h", lop.float_window.rect.height)) continue;
		if (READ_INT(lop, float_window.vdivider_pos)) continue;

		if (READ_BOOL(lop, tools_float)) continue;
		if (READ_BOOL(lop, tools_hidden)) continue;
		if (READ_BOOL(lop, show_info_pixel)) continue;
		if (READ_BOOL(lop, ignore_alpha)) continue;

		if (READ_BOOL(lop, bars_state.info)) continue;
		if (READ_BOOL(lop, bars_state.sort)) continue;
		if (READ_BOOL(lop, bars_state.tools_float)) continue;
		if (READ_BOOL(lop, bars_state.tools_hidden)) continue;
		if (READ_BOOL(lop, bars_state.hidden)) continue;

		if (READ_UINT(lop, image_overlay.state)) continue;
		if (READ_INT(lop, image_overlay.histogram_channel)) continue;
		if (READ_INT(lop, image_overlay.histogram_mode)) continue;

		if (READ_INT(lop, log_window.x)) continue;
		if (READ_INT(lop, log_window.y)) continue;
		if (READ_INT(lop, log_window.width)) continue;
		if (READ_INT(lop, log_window.height)) continue;

		if (READ_INT_FULL("preferences_window.x", lop.preferences_window.rect.x)) continue;
		if (READ_INT_FULL("preferences_window.y", lop.preferences_window.rect.y)) continue;
		if (READ_INT_FULL("preferences_window.w", lop.preferences_window.rect.width)) continue;
		if (READ_INT_FULL("preferences_window.h", lop.preferences_window.rect.height)) continue;
		if (READ_INT(lop, preferences_window.page_number)) continue;

		if (READ_INT(lop, search_window.x)) continue;
		if (READ_INT(lop, search_window.y)) continue;
		if (READ_INT_FULL("search_window.w", lop.search_window.width)) continue;
		if (READ_INT_FULL("search_window.h", lop.search_window.height)) continue;

		if (READ_INT_FULL("dupe_window.x", lop.dupe_window.rect.x)) continue;
		if (READ_INT_FULL("dupe_window.y", lop.dupe_window.rect.y)) continue;
		if (READ_INT_FULL("dupe_window.w", lop.dupe_window.rect.width)) continue;
		if (READ_INT_FULL("dupe_window.h", lop.dupe_window.rect.height)) continue;
		if (READ_INT(lop, dupe_window.vdivider_pos)) continue;

		if (READ_INT(lop, advanced_exif_window.x)) continue;
		if (READ_INT(lop, advanced_exif_window.y)) continue;
		if (READ_INT_FULL("advanced_exif_window.w", lop.advanced_exif_window.width)) continue;
		if (READ_INT_FULL("advanced_exif_window.h", lop.advanced_exif_window.height)) continue;

		if (READ_BOOL(lop, animate)) continue;
		if (READ_INT(lop, workspace)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	if (id && !is_current_layout_id(id))
		{
		std::swap(lop.id, id);
		}
}

LayoutOptions init_layout_options(const gchar **attribute_names, const gchar **attribute_values)
{
	LayoutOptions lop{};

	lop.dir_view_list_sort = { SORT_NAME, TRUE, TRUE };
	lop.dir_view_type = DIRVIEW_LIST;
	lop.file_view_list_sort = { SORT_NAME, TRUE, TRUE };
	lop.file_view_type = FILEVIEW_LIST;
	lop.float_window.rect = {0, 0, 260, 450};
	lop.float_window.vdivider_pos = -1;
	lop.home_path = nullptr;
	lop.id = g_strdup("null");
	lop.main_window.hdivider_pos = -1;
	lop.main_window.maximized = FALSE;
	lop.main_window.rect = {0, 0, 720, 540};
	lop.main_window.vdivider_pos = 200;
	lop.search_window = {100, 100, 700, 650};
	lop.dupe_window.rect = {100, 100, 800, 400};
	lop.advanced_exif_window = {0, 0, 900, 600};
	lop.folder_window.vdivider_pos = 100;
	lop.order = g_strdup("123");
	lop.show_directory_date = FALSE;
	lop.show_marks = FALSE;
	lop.show_file_filter = FALSE;
	lop.show_thumbnails = FALSE;
	lop.style = 0;
	lop.show_info_pixel = FALSE;
	lop.selectable_toolbars_hidden = FALSE;
	lop.tools_float = FALSE;
	lop.tools_hidden = FALSE;
	lop.image_overlay.histogram_channel = HCHAN_RGB;
	lop.image_overlay.histogram_mode = HMODE_LOG;
	lop.image_overlay.state = OSD_SHOW_NOTHING;
	lop.animate = TRUE;
	lop.bars_state.hidden = FALSE;
	lop.log_window = {0, 0, 520, 400};
	lop.preferences_window.rect = {0, 0, 700, 600};
	lop.split_pane_sync = FALSE;
	lop.workspace = -1;

	if (attribute_names) layout_load_attributes(lop, attribute_names, attribute_values);

	return lop;
}

void free_layout_options_content(LayoutOptions &dest)
{
	g_free(dest.id);
	g_free(dest.order);
	g_free(dest.home_path);
}

void copy_layout_options(LayoutOptions &dest, const LayoutOptions &src)
{
	free_layout_options_content(dest);

	dest = src;
	dest.id = g_strdup(src.id);
	dest.order = g_strdup(src.order);
	dest.home_path = g_strdup(src.home_path);
}

void layout_options_set_unique_id(LayoutOptions &options)
{
	if (options.id && options.id[0]) return; /* id is already set */

	g_free(options.id);
	options.id = layout_get_unique_id();
}

void layout_apply_options(LayoutWindow *lw, const LayoutOptions &lop)
{
	if (!layout_valid(&lw)) return;

	/** @FIXME add other options too */

	gboolean refresh_style = (lop.style != lw->options.style || strcmp(lop.order, lw->options.order) != 0);
	gboolean refresh_lists = (lop.show_directory_date != lw->options.show_directory_date);

	copy_layout_options(lw->options, lop);

	if (refresh_style) layout_style_set(lw, lw->options.style, lw->options.order);
	if (refresh_lists) layout_refresh(lw);
}

} // namespace

static void layout_list_scroll_to_subpart(LayoutWindow *lw, const gchar *needle);


/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

LayoutWindow *get_current_layout()
{
	if (current_lw) return current_lw;

	if (!layout_window_list.empty()) return layout_window_list.front();

	return nullptr;
}

gboolean layout_valid(LayoutWindow **lw)
{
	if (*lw == nullptr)
		{
		*lw = get_current_layout();
		return *lw != nullptr;
		}

	return std::find(layout_window_list.cbegin(), layout_window_list.cend(), *lw) != layout_window_list.cend();
}

LayoutWindow *layout_find_by_image(ImageWindow *imd)
{
	auto it = std::find_if(layout_window_list.begin(), layout_window_list.end(),
	                       [imd](LayoutWindow *lw){ return lw->image == imd; });
	if (it == layout_window_list.end()) return nullptr;

	return *it;
}

LayoutWindow *layout_find_by_image_fd(ImageWindow *imd)
{
	auto it = std::find_if(layout_window_list.begin(), layout_window_list.end(),
	                       [imd](LayoutWindow *lw){ return lw->image->image_fd == imd->image_fd; });
	if (it == layout_window_list.end()) return nullptr;

	return *it;
}

LayoutWindow *layout_find_by_layout_id(const gchar *id)
{
	if (!id || !id[0]) return nullptr;

	if (is_current_layout_id(id))
		{
		return get_current_layout();
		}

	return layout_window_find_by_options_id(id);
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

static gboolean layout_set_current_cb(GtkWidget *, GdkEventFocus *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	current_lw = lw;
	return FALSE;
}

static void layout_box_folders_changed_cb(GtkWidget *widget, gpointer)
{
	/** @FIXME this is probably not the correct way to implement this */
	for (LayoutWindow *lw : layout_window_list)
		{
		lw->options.folder_window.vdivider_pos = gtk_paned_get_position(GTK_PANED(widget));
		}
}

gchar *layout_get_window_list()
{
	GString *ret = g_string_new(nullptr);

	for (const LayoutWindow *lw : layout_window_list)
		{
		if (ret->len > 0)
			g_string_append_c(ret, '\n');

		g_string_append(ret, lw->options.id);
		}

	return g_string_free(ret, FALSE);
}

/*
 *-----------------------------------------------------------------------------
 * menu, toolbar, and dir view
 *-----------------------------------------------------------------------------
 */

static void layout_path_entry_changed_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(widget)) < 0) return;

	const gchar *buf = gq_gtk_entry_get_text(GTK_ENTRY(lw->path_entry));
	if (!lw->dir_fd || strcmp(buf, lw->dir_fd->path) != 0)
		{
		layout_set_path(lw, buf);
		}
}

static void layout_path_entry_tab_cb(LayoutWindow *lw, const gchar *path)
{
	g_autofree gchar *buf = g_strdup(path);
	parse_out_relatives(buf);

	if (isdir(buf))
		{
		if ((!lw->dir_fd || strcmp(lw->dir_fd->path, buf) != 0) && layout_set_path(lw, buf))
			{
			gtk_widget_grab_focus(lw->path_entry);
			gint pos = -1;
			/* put the G_DIR_SEPARATOR back, if we are in tab completion for a dir and result was path change */
			gtk_editable_insert_text(GTK_EDITABLE(lw->path_entry), G_DIR_SEPARATOR_S, -1, &pos);
			gtk_editable_set_position(GTK_EDITABLE(lw->path_entry), -1);
			}
		}
	else if (lw->dir_fd)
		{
		g_autofree gchar *base = remove_level_from_path(buf);

		if (strcmp(lw->dir_fd->path, base) == 0)
			{
			layout_list_scroll_to_subpart(lw, filename_from_path(buf));
			}
		}
}

static void layout_path_entry_cb(LayoutWindow *lw, const gchar *path)
{
	if (download_web_file(path, FALSE, lw)) return;

	g_autofree gchar *buf = g_strdup(path);
	parse_out_relatives(buf);

	layout_set_path(lw, buf);
}

static void layout_vd_select_cb(ViewDir *, FileData *fd, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_set_fd(lw, fd);
}

static void layout_path_entry_tab_append_cb(LayoutWindow *lw, gint n)
{
	if (!lw || !lw->back_button) return;
	if (!layout_valid(&lw)) return;

	/* Enable back button if it makes sense */
	gtk_widget_set_sensitive(lw->back_button, (n > 1));
}

static gboolean path_entry_tooltip_cb(GtkWidget *widget, gpointer)
{
	g_autoptr(GList) box_child_list = gq_gtk_widget_get_children(GTK_WIDGET(widget));
	g_autofree gchar *current_path = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(box_child_list->data));

	gtk_widget_set_tooltip_text(widget, current_path);

	return FALSE;
}

static GtkWidget *layout_tool_setup(LayoutWindow *lw)
{
	GtkWidget *box;
	GtkWidget *menu_bar;
	GtkWidget *menu_tool_bar;
	GtkWidget *menu_toolbar_box;
	GtkWidget *scd;
	GtkWidget *toolbar;

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	if (!options->expand_menu_toolbar)
		{
		menu_toolbar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget *scroll_window = gq_gtk_scrolled_window_new(nullptr, nullptr);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);

		if (!options->hamburger_menu)
			{
			menu_bar = layout_actions_menu_bar(lw);
			gq_gtk_box_pack_start(GTK_BOX(menu_toolbar_box), menu_bar, FALSE, FALSE, 0);
			}

		toolbar = layout_actions_toolbar(lw, TOOLBAR_MAIN);

		gq_gtk_box_pack_start(GTK_BOX(menu_toolbar_box), toolbar, FALSE, FALSE, 0);
		gq_gtk_container_add(scroll_window, menu_toolbar_box);
		gq_gtk_box_pack_start(GTK_BOX(box), scroll_window, FALSE, FALSE, 0);

		gq_gtk_widget_show_all(scroll_window);
		}
	else
		{
		menu_tool_bar = layout_actions_menu_tool_bar(lw);
		DEBUG_NAME(menu_tool_bar);
		gtk_widget_show(menu_tool_bar);
		gq_gtk_box_pack_start(GTK_BOX(lw->main_box), lw->menu_tool_bar, FALSE, FALSE, 0);
		}

	lw->path_entry = tab_completion_new_with_history(nullptr, nullptr, "path_list", -1);
	GtkWidget *tabcomp = tab_completion_get_box(lw->path_entry);
	DEBUG_NAME(tabcomp);
	tab_completion_set_enter_func(lw->path_entry,
	                              [lw](const gchar *text){ layout_path_entry_cb(lw, text); });
	tab_completion_set_tab_func(lw->path_entry,
	                            [lw](const gchar *text){ layout_path_entry_tab_cb(lw, text); });
	tab_completion_set_tab_append_func(lw->path_entry,
	                                   [lw](const gchar *, gint n){ layout_path_entry_tab_append_cb(lw, n); });

	if (options->hamburger_menu)
		{
		GtkWidget *open_menu = layout_actions_menu_bar(lw);
		gtk_widget_set_tooltip_text(open_menu, _("Open application menu"));

		GtkWidget *box_menu_tabcomp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		gq_gtk_box_pack_start(GTK_BOX(box_menu_tabcomp), open_menu, FALSE, FALSE, 0);
		gq_gtk_box_pack_start(GTK_BOX(box_menu_tabcomp), tabcomp, TRUE, TRUE, 0);
		gtk_widget_show(box_menu_tabcomp);

		gq_gtk_box_pack_start(GTK_BOX(box), box_menu_tabcomp, FALSE, FALSE, 0);
		}
	else
		{
		gq_gtk_box_pack_start(GTK_BOX(box), tabcomp, FALSE, FALSE, 0);
		}

	gtk_widget_set_has_tooltip(tabcomp, TRUE);
	g_signal_connect(G_OBJECT(tabcomp), "query_tooltip", G_CALLBACK(path_entry_tooltip_cb), lw);

	g_signal_connect(G_OBJECT(gtk_widget_get_parent(gtk_widget_get_parent(lw->path_entry))), "changed", G_CALLBACK(layout_path_entry_changed_cb), lw);

	GtkWidget *box_folders = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(box_folders);
	gq_gtk_box_pack_start(GTK_BOX(box), box_folders, TRUE, TRUE, 0);

	lw->vd = vd_new(lw);

	vd_set_select_func(lw->vd, layout_vd_select_cb, lw);

	lw->dir_view = lw->vd->widget;
	DEBUG_NAME(lw->dir_view);
	gtk_paned_add2(GTK_PANED(box_folders), lw->dir_view);
	gtk_widget_show(lw->dir_view);

	scd = shortcuts_new(lw);
	DEBUG_NAME(scd);
	gtk_paned_add1(GTK_PANED(box_folders), scd);
	gtk_paned_set_position(GTK_PANED(box_folders), lw->options.folder_window.vdivider_pos);

	gtk_widget_show(box_folders);

	g_signal_connect(G_OBJECT(box_folders), "notify::position", G_CALLBACK(layout_box_folders_changed_cb), lw);

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
	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	auto *lw = static_cast<LayoutWindow *>(data);
	if (!lw) return;

	auto sort = lw->options.file_view_list_sort;
	sort.method = static_cast<SortType>(GPOINTER_TO_INT(menu_item_radio_get_data(widget)));

	if (sort_type_requires_metadata(sort.method))
		{
		vf_read_metadata_in_idle(lw->vf);
		}
	layout_sort_set_files(lw, sort);
}

static void layout_sort_menu_ascend_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	auto sort = lw->options.file_view_list_sort;
	sort.ascending = !sort.ascending;

	layout_sort_set_files(lw, sort);
}

static void layout_sort_menu_case_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	auto sort = lw->options.file_view_list_sort;
	sort.case_sensitive = !sort.case_sensitive;

	layout_sort_set_files(lw, sort);
}

static void layout_sort_button_press_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	GtkWidget *menu = submenu_add_sort(nullptr, G_CALLBACK(layout_sort_menu_cb), lw, TRUE, lw->options.file_view_list_sort.method);

	/* ascending option */
	menu_item_add_divider(menu);
	menu_item_add_check(menu, _("Ascending"), lw->options.file_view_list_sort.ascending, G_CALLBACK(layout_sort_menu_ascend_cb), lw);
	menu_item_add_check(menu, _("Case"), lw->options.file_view_list_sort.case_sensitive, G_CALLBACK(layout_sort_menu_case_cb), lw);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static GtkWidget *layout_sort_button(LayoutWindow *lw, GtkWidget *box)
{
	GtkWidget *button;

	GtkWidget *frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	gq_gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gq_gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);
	gtk_widget_show(frame);

	button = gtk_button_new_with_label(sort_type_get_text(lw->options.file_view_list_sort.method));
#if HAVE_GTK4
	gtk_button_set_icon_name(GTK_BUTTON(button), GQ_ICON_PAN_DOWN);
#else
	GtkWidget *image = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
	gtk_button_set_image(GTK_BUTTON(button), image);
#endif
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(layout_sort_button_press_cb), lw);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_button_set_image_position(GTK_BUTTON(button), GTK_POS_RIGHT);

	gq_gtk_container_add(frame, button);

	gtk_widget_show(button);

	return button;
}

template<ZoomMode mode>
static void layout_zoom_menu_cb(GtkWidget *widget, gpointer)
{
	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	options->image.zoom_mode = mode;
}

template<ScrollReset scroll_reset_method>
static void layout_scroll_menu_cb(GtkWidget *widget, gpointer)
{
	if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) return;

	options->image.scroll_reset_method = scroll_reset_method;
	image_options_sync();
}

static void layout_zoom_button_press_cb(GtkWidget *, gpointer)
{
	GtkWidget *menu = popup_menu_short_lived();

	menu_item_add_radio(menu, _("Zoom to original size"), nullptr,
	                    options->image.zoom_mode == ZOOM_RESET_ORIGINAL,
	                    G_CALLBACK(layout_zoom_menu_cb<ZOOM_RESET_ORIGINAL>), nullptr);
	menu_item_add_radio(menu, _("Fit image to window"), nullptr,
	                    options->image.zoom_mode == ZOOM_RESET_FIT_WINDOW,
	                    G_CALLBACK(layout_zoom_menu_cb<ZOOM_RESET_FIT_WINDOW>), nullptr);
	menu_item_add_radio(menu, _("Leave Zoom at previous setting"), nullptr,
	                    options->image.zoom_mode == ZOOM_RESET_NONE,
	                    G_CALLBACK(layout_zoom_menu_cb<ZOOM_RESET_NONE>), nullptr);

	menu_item_add_divider(menu);

	menu_item_add_radio(menu, _("Scroll to top left corner"), nullptr,
	                    options->image.scroll_reset_method == ScrollReset::TOPLEFT,
	                    G_CALLBACK(layout_scroll_menu_cb<ScrollReset::TOPLEFT>), nullptr);
	menu_item_add_radio(menu, _("Scroll to image center"), nullptr,
	                    options->image.scroll_reset_method == ScrollReset::CENTER,
	                    G_CALLBACK(layout_scroll_menu_cb<ScrollReset::CENTER>), nullptr);
	menu_item_add_radio(menu, _("Keep the region from previous image"), nullptr,
	                    options->image.scroll_reset_method == ScrollReset::NOCHANGE,
	                    G_CALLBACK(layout_scroll_menu_cb<ScrollReset::NOCHANGE>), nullptr);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static GtkWidget *layout_zoom_button(LayoutWindow *lw, GtkWidget *box, gint size, gboolean)
{
	GtkWidget *button;

	GtkWidget *frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	if (size) gtk_widget_set_size_request(frame, size, -1);
	gq_gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);

	gq_gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);

	gtk_widget_show(frame);

	button = gtk_button_new_with_label("1:1");
#if HAVE_GTK4
	gtk_button_set_icon_name(GTK_BUTTON(button), GQ_ICON_PAN_DOWN);
#else
	GtkWidget *image = gtk_image_new_from_icon_name(GQ_ICON_PAN_DOWN, GTK_ICON_SIZE_BUTTON);
	gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
	gtk_button_set_image(GTK_BUTTON(button), image);
#endif
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(layout_zoom_button_press_cb), lw);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_button_set_image_position(GTK_BUTTON(button), GTK_POS_RIGHT);

	gq_gtk_container_add(frame, button);
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
	g_autofree gchar *buf = nullptr;
	gint hrs;
	gint min;
	gdouble sec;

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
			g_autofree gchar *ss = nullptr;

			if (layout_image_slideshow_active(lw))
				{
				GString *delay;

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

				ss = g_string_free(delay, FALSE);
				}
			else
				{
				ss = g_strdup("");
				}

			s = layout_selection_count(lw, &s_bytes);

			layout_bars_new_selection(lw, s);

			g_autofree gchar *b = text_from_size_abrev(n_bytes);

			if (s > 0)
				{
				g_autofree gchar *sb = text_from_size_abrev(s_bytes);
				buf = g_strdup_printf(_("%s, %d files (%s, %d)%s"), b, n, sb, s, ss);
				}
			else
				{
				buf = g_strdup_printf(_("%s, %d files%s"), b, n, ss);
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
}

void layout_status_update_image(LayoutWindow *lw)
{
	FileData *fd;
	gint page_total;
	gint page_num;

	if (!layout_valid(&lw) || !lw->image) return;
	if (!lw->info_zoom || !lw->info_details) return; /*called from layout_style_set */

	if (!lw->image->image_fd)
		{
		gtk_button_set_label(GTK_BUTTON(lw->info_zoom), "");
		gtk_label_set_text(GTK_LABEL(lw->info_details), "");
		}
	else
		{
		g_autofree gchar *zoom_text = image_zoom_get_as_text(lw->image);
		gtk_button_set_label(GTK_BUTTON(lw->info_zoom), zoom_text);

		g_autofree gchar *b = image_get_fd(lw->image) ? text_from_size(image_get_fd(lw->image)->size) : g_strdup("0");

		g_autofree gchar *details_text = nullptr;
		if (lw->image->unknown)
			{
			const gchar *filename = image_get_path(lw->image);
			if (filename && !access_file(filename, R_OK))
				{
				details_text = g_strdup_printf(_("(no read permission) %s bytes"), b);
				}
			else
				{
				details_text = g_strdup_printf(_("( ? x ? ) %s bytes"), b);
				}
			}
		else
			{
			gint width;
			gint height;
			fd = image_get_fd(lw->image);
			page_total = fd->page_total;
			page_num = fd->page_num + 1;
			image_get_image_size(lw->image, &width, &height);

			if (page_total > 1)
				{
				details_text = g_strdup_printf(_("( %d x %d ) %s bytes [%d/%d]"), width, height, b, page_num, page_total);
				}
			else
				{
				details_text = g_strdup_printf(_("( %d x %d ) %s bytes"), width, height, b);
				}
			}

		g_signal_emit_by_name (lw->image->pr, "update-pixel");

		gtk_label_set_text(GTK_LABEL(lw->info_details), details_text);
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

static GtkWidget *layout_status_label(const gchar *text, GtkWidget *box, gboolean start, gint size, gboolean expand)
{
	GtkWidget *label;

	GtkWidget *frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	if (size) gtk_widget_set_size_request(frame, size, -1);
	gq_gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	if (start)
		{
		gq_gtk_box_pack_start(GTK_BOX(box), frame, expand, expand, 0);
		}
	else
		{
		gq_gtk_box_pack_end(GTK_BOX(box), frame, expand, expand, 0);
		}
	gtk_widget_show(frame);

	label = gtk_label_new(text ? text : "");
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gq_gtk_container_add(frame, label);
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
	gq_gtk_box_pack_end(GTK_BOX(box), lw->info_box, FALSE, FALSE, 0);
	gtk_widget_show(lw->info_box);

	if (small_format)
		{
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(hbox);
		gq_gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
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

	gq_gtk_box_pack_start(GTK_BOX(hbox), lw->info_progress_bar, FALSE, FALSE, 0);
	gtk_widget_show(lw->info_progress_bar);

	lw->info_sort = layout_sort_button(lw, hbox);
	gtk_widget_set_tooltip_text(lw->info_sort, _("Select sort order"));
	gtk_widget_show(lw->info_sort);

	lw->info_status = layout_status_label(nullptr, lw->info_box, TRUE, 0, (!small_format));
	DEBUG_NAME(lw->info_status);
	gtk_widget_set_tooltip_text(lw->info_status, _("Folder contents (files selected)\nSlideshow [time interval]"));

	if (small_format)
		{
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(hbox);
		gq_gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
	lw->info_details = layout_status_label(nullptr, hbox, TRUE, 0, TRUE);
	DEBUG_NAME(lw->info_details);
	gtk_widget_set_tooltip_text(lw->info_details, _("(Image dimensions) Image size [page n of m]"));
	toolbar = layout_actions_toolbar(lw, TOOLBAR_STATUS);

	toolbar_frame = gtk_frame_new(nullptr);
	DEBUG_NAME(toolbar_frame);
	gq_gtk_frame_set_shadow_type(GTK_FRAME(toolbar_frame), GTK_SHADOW_IN);
	gq_gtk_container_add(toolbar_frame, toolbar);
	gtk_widget_show(toolbar_frame);
	gtk_widget_show(toolbar);
	gq_gtk_box_pack_end(GTK_BOX(hbox), toolbar_frame, FALSE, FALSE, 0);
	lw->info_zoom = layout_zoom_button(lw, hbox, ZOOM_LABEL_WIDTH, TRUE);
	gtk_widget_set_tooltip_text(lw->info_zoom, _("Select zoom and scroll mode"));
	gtk_widget_show(lw->info_zoom);

	if (small_format)
		{
		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		DEBUG_NAME(hbox);
		gq_gtk_box_pack_start(GTK_BOX(lw->info_box), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);
		}
	lw->info_pixel = layout_status_label(nullptr, hbox, FALSE, 0, small_format); /* expand only in small format */
	DEBUG_NAME(lw->info_pixel);
	gtk_widget_set_tooltip_text(lw->info_pixel, _("[Pixel x,y coord]: (Pixel R,G,B value)"));
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

	if (lw->vf) vf_sort_set(lw->vf, lw->options.file_view_list_sort);
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

std::vector<int> layout_selection_list_by_index(LayoutWindow *lw)
{
	if (!layout_valid(&lw) || !lw->vf) return {};

	return vf_selection_get_list_by_index(lw->vf);
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

guint layout_window_count()
{
	return layout_window_list.size();
}

LayoutWindow *layout_window_first()
{
	if (layout_window_list.empty()) return nullptr;

	return layout_window_list.front();
}

void layout_window_foreach(const LayoutWindowCallback &lw_cb)
{
	for (LayoutWindow *lw : layout_window_list)
		{
		lw_cb(lw);
		}
}

gboolean layout_window_is_displayed(const gchar *id)
{
	return layout_window_find_by_options_id(id) != nullptr;
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

	if (lw->path_entry) gq_gtk_entry_set_text(GTK_ENTRY(lw->path_entry), lw->dir_fd->path);

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

		g_autofree gchar *last_image = get_recent_viewed_folder_image(fd->path);
		if (last_image)
			{
			fd = file_data_new_group(last_image);

			if (isfile(fd->path)) have_file = TRUE;
			}

		}
	else
		{
		g_autofree gchar *base = remove_level_from_path(fd->path);

		if (lw->dir_fd && strcmp(lw->dir_fd->path, base) == 0)
			{
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
			}
		else
			{
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
		metadata_write_queue_confirm(FALSE, nullptr);

	if (lw->vf && (options->read_metadata_in_idle || sort_type_requires_metadata(lw->options.file_view_list_sort.method)))
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

void layout_sort_set_files(LayoutWindow *lw, FileData::FileList::SortSettings settings)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.file_view_list_sort == settings) return;

	lw->options.file_view_list_sort = settings;

	if (lw->info_sort) gtk_button_set_label(GTK_BUTTON(lw->info_sort), sort_type_get_text(settings.method));
	layout_list_sync_sort(lw);
}

gboolean layout_sort_get(LayoutWindow *lw, FileData::FileList::SortSettings &settings)
{
	if (!layout_valid(&lw)) return FALSE;

	settings = lw->options.file_view_list_sort;

	return TRUE;
}

static gboolean layout_geometry_get(LayoutWindow *lw, GdkRectangle &rect)
{
	if (!layout_valid(&lw)) return FALSE;

	rect = widget_get_root_origin_geometry(lw->window);
	return TRUE;

	return FALSE;
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

void layout_views_set_sort_dir(LayoutWindow *lw, FileData::FileList::SortSettings settings)
{
	if (!layout_valid(&lw)) return;

	if (lw->options.dir_view_list_sort == settings) return;

	lw->options.dir_view_list_sort = settings;

	layout_style_set(lw, -1, nullptr);
}

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

static gboolean layout_geometry_get_tools(LayoutWindow *lw, GdkRectangle &rect, gint &divider_pos)
{
	GtkAllocation allocation;
	if (!layout_valid(&lw)) return FALSE;

	if (!lw->tools || !gtk_widget_get_visible(lw->tools))
		{
		/* use the stored values (sort of breaks success return value) */

		divider_pos = lw->options.float_window.vdivider_pos;

		return FALSE;
		}

	rect = widget_get_root_origin_geometry(lw->tools);
	gtk_widget_get_allocation(gtk_paned_get_child1(GTK_PANED(lw->tools_pane)), &allocation);

	if (gtk_orientable_get_orientation(GTK_ORIENTABLE(lw->tools_pane)) == GTK_ORIENTATION_VERTICAL)
		{
		divider_pos = allocation.height;
		}
	else
		{
		divider_pos = allocation.width;
		}

	return TRUE;
}

static gboolean layout_geometry_get_log_window(LayoutWindow *lw, GdkRectangle &log_window)
{
	if (!layout_valid(&lw)) return FALSE;

	if (!lw->log_window)
		{
		return FALSE;
		}

	log_window = widget_get_root_origin_geometry(lw->log_window);

	return TRUE;
}

static void layout_tools_geometry_sync(LayoutWindow *lw)
{
	layout_geometry_get_tools(lw, lw->options.float_window.rect, lw->options.float_window.vdivider_pos);
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
	GtkWidget *w1;
	GtkWidget *w2;
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

		lw->tools = window_new("tools", PIXBUF_INLINE_ICON_TOOLS, _("Tools"));
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
		gq_gtk_widget_destroy(gq_gtk_bin_get_child(GTK_WIDGET(lw->tools)));
		}

	layout_actions_add_window(lw, lw->tools);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(vbox);
	gq_gtk_container_add(lw->tools, vbox);
	if (options->expand_menu_toolbar) gq_gtk_box_pack_start(GTK_BOX(vbox), lw->menu_tool_bar, FALSE, FALSE, 0);
	gtk_widget_show(vbox);

	layout_status_setup(lw, vbox, TRUE);

	lw->tools_pane = gtk_paned_new(vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(lw->tools_pane);
	gq_gtk_box_pack_start(GTK_BOX(vbox), lw->tools_pane, TRUE, TRUE, 0);
	gtk_widget_show(lw->tools_pane);

	gtk_paned_pack1(GTK_PANED(lw->tools_pane), w1, FALSE, TRUE);
	gtk_paned_pack2(GTK_PANED(lw->tools_pane), w2, TRUE, TRUE);

	gtk_widget_show(tools);
	gtk_widget_show(files);

	if (new_window)
		{
		if (options->save_window_positions)
			{
			gtk_window_set_default_size(GTK_WINDOW(lw->tools), lw->options.float_window.rect.width, lw->options.float_window.rect.height);
			gq_gtk_window_move(GTK_WINDOW(lw->tools), lw->options.float_window.rect.x, lw->options.float_window.rect.y);
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
				widget_remove_from_parent(lw->split_images[i]->widget);
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
	GtkWidget *w1;
	GtkWidget *w2;
	GtkWidget *w3;

	GtkWidget *image_sb; /* image together with sidebars in utility box */
	GtkWidget *tools;
	GtkWidget *files;

	layout_actions_setup(lw);
	create_toolbars(lw);

	lw->group_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(lw->group_box);
	if (options->expand_menu_toolbar)
		{
		gq_gtk_box_pack_end(GTK_BOX(lw->main_box), lw->group_box, TRUE, TRUE, 0);
		}
	else
		{
		gq_gtk_box_pack_start(GTK_BOX(lw->main_box), lw->group_box, TRUE, TRUE, 0);
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
		gq_gtk_box_pack_start(GTK_BOX(lw->group_box), image_sb, TRUE, TRUE, 0);
		gtk_widget_show(image_sb);

		layout_tools_setup(lw, tools, files);

		image_grab_focus(lw->image);

		return;
		}

	if (lw->tools)
		{
		layout_tools_geometry_sync(lw);
		gq_gtk_widget_destroy(lw->tools);
		lw->tools = nullptr;
		lw->tools_pane = nullptr;
		}

	layout_status_setup(lw, lw->group_box, FALSE);

	layout_grid_compute(lw, image_sb, tools, files, &w1, &w2, &w3);

	v = lw->v_pane = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	DEBUG_NAME(v);

	h = lw->h_pane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(h);

	if (!layout_location_vertical(static_cast<LayoutLocation>(priority_location)))
		{
		std::swap(v, h);
		}

	gq_gtk_box_pack_start(GTK_BOX(lw->group_box), v, TRUE, TRUE, 0);

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
		LayoutLocation d;
		LayoutLocation f;
		LayoutLocation i;

		layout_config_parse(style, order, d, f, i);

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
	if (lw->utility_box) widget_remove_from_parent(lw->utility_box);

	if (options->expand_menu_toolbar)
		{
		if (lw->toolbar[TOOLBAR_STATUS]) widget_remove_from_parent(lw->toolbar[TOOLBAR_STATUS]);

		if (lw->menu_tool_bar) widget_remove_from_parent(lw->menu_tool_bar);
		}
	else
		{
		if (lw->menu_bar) widget_remove_from_parent(lw->menu_bar);
		for (i = 0; i < TOOLBAR_COUNT; i++)
			if (lw->toolbar[i]) widget_remove_from_parent(lw->toolbar[i]);
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
	for (LayoutWindow *lw : layout_window_list)
		{
		if (!lw->image) continue;

		for (ImageWindow *split_image : lw->split_images)
			{
			if (!split_image) continue;

			image_background_set_color_from_options(split_image, !!lw->full_screen);
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

void current_layout_selectable_toolbars_toggle()
{
	if (!layout_valid(&current_lw)) return;
	if (!current_lw->toolbar[TOOLBAR_MAIN]) return;
	if (!current_lw->menu_bar) return;
	if (!current_lw->info_box) return;

	current_lw->options.selectable_toolbars_hidden = !current_lw->options.selectable_toolbars_hidden;

	gtk_widget_set_visible(current_lw->toolbar[TOOLBAR_MAIN],
	                       !options->selectable_bars.tool_bar || !current_lw->options.selectable_toolbars_hidden);

	gtk_widget_set_visible(current_lw->menu_bar,
	                       !options->selectable_bars.menu_bar || !current_lw->options.selectable_toolbars_hidden);

	gtk_widget_set_visible(current_lw->info_box,
	                       !options->selectable_bars.status_bar || !current_lw->options.selectable_toolbars_hidden);
}

void layout_info_pixel_set(LayoutWindow *lw, gboolean show)
{
	GtkWidget *frame;

	if (!layout_valid(&lw)) return;
	if (!lw->info_pixel) return;

	lw->options.show_info_pixel = show;

	frame = gtk_widget_get_parent(lw->info_pixel);
	gtk_widget_set_visible(frame, lw->options.show_info_pixel);

	g_signal_emit_by_name (lw->image->pr, "update-pixel");
}

/*
 *-----------------------------------------------------------------------------
 * configuration
 *-----------------------------------------------------------------------------
 */

static gint layout_config_delete_cb(GtkWidget *w, GdkEventAny *event, gpointer data);

static void layout_config_close_cb(GtkWidget *, gpointer data)
{
	auto lc = static_cast<LayoutConfig *>(data);

	gq_gtk_widget_destroy(lc->configwindow);
	free_layout_options_content(lc->options);
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

	layout_apply_options(lc->lw, lc->options);
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
	gq_gtk_entry_set_text(GTK_ENTRY(lc->home_path_entry), layout_get_path(lc->lw));
}

template<StartUpPath startup_path>
static void startup_path_set_cb(GtkWidget *widget, gpointer data)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;

	auto *lc = static_cast<LayoutConfig *>(data);
	lc->options.startup_path = startup_path;
}

void layout_show_config_window(LayoutWindow *lw)
{
	LayoutConfig *lc;
	GtkWidget *win_vbox;
	GtkWidget *vbox;
	GtkWidget *button;
	GtkWidget *ct_button;
	GtkWidget *group;
	GtkWidget *frame;

	lc = g_new0(LayoutConfig, 1);
	lc->lw = lw;
	layout_sync_options_with_current_state(lw);
	copy_layout_options(lc->options, lw->options);

	lc->configwindow = window_new("Layout", PIXBUF_INLINE_ICON_CONFIG, _("Window options and layout"));
	DEBUG_NAME(lc->configwindow);
	gtk_window_set_type_hint(GTK_WINDOW(lc->configwindow), GDK_WINDOW_TYPE_HINT_DIALOG);

	g_signal_connect(G_OBJECT(lc->configwindow), "delete_event",
			 G_CALLBACK(layout_config_delete_cb), lc);

	gtk_window_set_default_size(GTK_WINDOW(lc->configwindow), CONFIG_WINDOW_DEF_WIDTH, CONFIG_WINDOW_DEF_HEIGHT);
	gtk_window_set_resizable(GTK_WINDOW(lc->configwindow), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(lc->configwindow), PREF_PAD_BORDER);

	win_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_SPACE);
	DEBUG_NAME(win_vbox);
	gq_gtk_container_add(lc->configwindow, win_vbox);
	gtk_widget_show(win_vbox);

	GtkWidget *hbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_BUTTON_GAP);
	gq_gtk_box_pack_end(GTK_BOX(win_vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = pref_button_new(nullptr, GQ_ICON_OK, "OK",
				 G_CALLBACK(layout_config_ok_cb), lc);
	gq_gtk_container_add(hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);

	ct_button = button;

	button = pref_button_new(nullptr, GQ_ICON_HELP, _("Help"),
				 G_CALLBACK(layout_config_help_cb), lc);
	gq_gtk_container_add(hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_APPLY, _("Apply"),
				 G_CALLBACK(layout_config_apply_cb), lc);
	gq_gtk_container_add(hbox, button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_CANCEL, _("Cancel"),
				 G_CALLBACK(layout_config_close_cb), lc);
	gq_gtk_container_add(hbox, button);
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
	gq_gtk_container_add(frame, vbox);
	gtk_widget_show(vbox);


	group = pref_group_new(vbox, FALSE, _("General options"), GTK_ORIENTATION_VERTICAL);

	pref_label_new(group, _("Home path (empty to use your home directory)"));
	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	lc->home_path_entry = tab_completion_new(hbox, lc->options.home_path);
	tab_completion_add_select_button(lc->home_path_entry, nullptr, TRUE, nullptr, nullptr, nullptr);

	button = pref_button_new(hbox, nullptr, _("Use current"),
				 G_CALLBACK(home_path_set_current_cb), lc);

	pref_checkbox_new_int(group, _("Show date in directories list view"),
			      lc->options.show_directory_date, &lc->options.show_directory_date);

	group = pref_group_new(vbox, FALSE, _("Start-up directory:"), GTK_ORIENTATION_VERTICAL);

	button = pref_radiobutton_new(group, nullptr, _("No change"),
	                              lc->options.startup_path == STARTUP_PATH_CURRENT,
	                              G_CALLBACK(startup_path_set_cb<STARTUP_PATH_CURRENT>), lc);
	button = pref_radiobutton_new(group, button, _("Restore last path"),
	                              lc->options.startup_path == STARTUP_PATH_LAST,
	                              G_CALLBACK(startup_path_set_cb<STARTUP_PATH_LAST>), lc);
	button = pref_radiobutton_new(group, button, _("Home path"),
	                              lc->options.startup_path == STARTUP_PATH_HOME,
	                              G_CALLBACK(startup_path_set_cb<STARTUP_PATH_HOME>), lc);

	group = pref_group_new(vbox, FALSE, _("Layout"), GTK_ORIENTATION_VERTICAL);

	lc->layout_widget = layout_config_new();
	DEBUG_NAME(lc->layout_widget);
	layout_config_set(lc->layout_widget, lw->options.style, lw->options.order);
	gq_gtk_box_pack_start(GTK_BOX(group), lc->layout_widget, TRUE, TRUE, 0);

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
	if (!layout_valid(&lw)) return;

	lw->options.main_window.maximized =  window_maximized(lw->window);
	if (!lw->options.main_window.maximized)
		{
		layout_geometry_get(lw, lw->options.main_window.rect);
		}

	layout_geometry_get_dividers(lw, &lw->options.main_window.hdivider_pos, &lw->options.main_window.vdivider_pos);

	layout_geometry_get_tools(lw, lw->options.float_window.rect, lw->options.float_window.vdivider_pos);

	lw->options.image_overlay.state = image_osd_get(lw->image);

	Histogram *histogram = image_osd_get_histogram(lw->image);
	if (histogram)
		{
		lw->options.image_overlay.histogram_channel = histogram->get_channel();
		lw->options.image_overlay.histogram_mode = histogram->get_mode();
		}

	layout_geometry_get_log_window(lw, lw->options.log_window);

#ifdef GDK_WINDOWING_X11
	if (options->save_window_workspace)
		{
		GdkDisplay *display = gdk_display_get_default();

		if (GDK_IS_X11_DISPLAY(display))
			{
			GdkWindow *window = gtk_widget_get_window(lw->window);
			lw->options.workspace = gdk_x11_window_get_desktop(window);
			}
		}
#endif
}

void save_layout(LayoutWindow *lw)
{
	if (g_str_has_prefix(lw->options.id, "lw")) return;

	g_autofree gchar *xml_name = g_strdup_printf("%s.xml", lw->options.id);
	g_autofree gchar *path = g_build_filename(get_window_layouts_dir(), xml_name, NULL);

	save_config_to_file(path, options, lw);
}

void layout_close(LayoutWindow *lw)
{
	if (layout_window_count() > 1)
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

	layout_window_list.erase(std::remove(layout_window_list.begin(), layout_window_list.end(), lw),
	                         layout_window_list.end());
	if (current_lw == lw) current_lw = nullptr;

	if (lw->exif_window) g_signal_handlers_disconnect_matched(G_OBJECT(lw->exif_window), G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, lw);

	layout_bars_close(lw);

	g_object_unref(lw->menu_bar);
	g_object_unref(lw->utility_box);

	for (i = 0; i < TOOLBAR_COUNT; i++)
		{
		if (lw->toolbar[i]) g_object_unref(lw->toolbar[i]);
		}

	gq_gtk_widget_destroy(lw->window);

	if (lw->split_image_sizegroup) g_object_unref(lw->split_image_sizegroup);

	file_data_unregister_notify_func(layout_image_notify_cb, lw);

	if (lw->dir_fd)
		{
		file_data_unregister_real_time_monitor(lw->dir_fd);
		file_data_unref(lw->dir_fd);
		}

	free_layout_options_content(lw->options);
	g_free(lw);
}

static gboolean layout_delete_cb(GtkWidget *, GdkEventAny *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_close(lw);
	return TRUE;
}

static gboolean move_window_to_workspace_cb(gpointer data)
{
#ifdef GDK_WINDOWING_X11
	auto lw = static_cast<LayoutWindow *>(data);

	if (options->save_window_workspace)
		{
		GdkDisplay *display = gdk_display_get_default();
		if (GDK_IS_X11_DISPLAY(display))
			{
			if (lw->options.workspace != -1)
				{
				GdkWindow *window = gtk_widget_get_window(lw->window);
				gdk_x11_window_move_to_desktop(window, lw->options.workspace);
				}
			}
		}
#endif
	return G_SOURCE_REMOVE;
}

static LayoutWindow *layout_new(const LayoutOptions &lop)
{
	LayoutWindow *lw;
	GdkGeometry hint;
	GdkWindowHints hint_mask;

	DEBUG_1("%s layout_new: start", get_exec_time());
	lw = g_new0(LayoutWindow, 1);

	copy_layout_options(lw->options, lop);

	layout_options_set_unique_id(lw->options);

	/* default layout */

	layout_config_parse(lw->options.style, lw->options.order,
	                    lw->dir_location, lw->file_location, lw->image_location);
	if (lw->options.dir_view_type > DIRVIEW_LAST) lw->options.dir_view_type = DIRVIEW_LIST;
	if (lw->options.file_view_type > FILEVIEW_LAST) lw->options.file_view_type = FILEVIEW_LIST;
	/* divider positions */

	g_autofree gchar *default_path = g_build_filename(get_rc_dir(), DEFAULT_WINDOW_LAYOUT, NULL);

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

	lw->window = window_new(GQ_APPNAME_LC, nullptr, nullptr);
	DEBUG_NAME(lw->window);
	gtk_window_set_resizable(GTK_WINDOW(lw->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(lw->window), 0);

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
		gtk_window_set_default_size(GTK_WINDOW(lw->window), lw->options.main_window.rect.width, lw->options.main_window.rect.height);
		gq_gtk_window_move(GTK_WINDOW(lw->window), lw->options.main_window.rect.x, lw->options.main_window.rect.y);
		if (lw->options.main_window.maximized) gtk_window_maximize(GTK_WINDOW(lw->window));

		g_idle_add(move_window_to_workspace_cb, lw);
		}
	else
		{
		gtk_window_set_default_size(GTK_WINDOW(lw->window), MAINWINDOW_DEF_WIDTH, MAINWINDOW_DEF_HEIGHT);
		}

	g_signal_connect(G_OBJECT(lw->window), "delete_event",
			 G_CALLBACK(layout_delete_cb), lw);

	g_signal_connect(G_OBJECT(lw->window), "focus-in-event",
			 G_CALLBACK(layout_set_current_cb), lw);

	layout_keyboard_init(lw, lw->window);

	lw->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	DEBUG_NAME(lw->main_box);
	gq_gtk_container_add(lw->window, lw->main_box);
	gtk_widget_show(lw->main_box);

	layout_grid_setup(lw);
	image_top_window_set_sync(lw->image, (lw->options.tools_float || lw->options.tools_hidden));

	layout_util_sync(lw);
	layout_status_update_all(lw);

	g_autoptr(GdkPixbuf) pixbuf = pixbuf_inline(PIXBUF_INLINE_LOGO);

	/** @FIXME the zoom value set here is the value, which is then copied again and again
	   in 'Leave Zoom at previous setting' mode. This is not ideal.  */
	image_change_pixbuf(lw->image, pixbuf, 0.0, FALSE);

	layout_tools_hide(lw, lw->options.tools_hidden);

	image_osd_set(lw->image, static_cast<OsdShowFlags>(lw->options.image_overlay.state));

	Histogram *histogram = image_osd_get_histogram(lw->image);
	if (histogram)
		{
		histogram->set_channel(lw->options.image_overlay.histogram_channel);
		histogram->set_mode(lw->options.image_overlay.histogram_mode);
		}

	layout_window_list.push_back(lw);

	/* Refer to the activate signal in main */
#if HAVE_GTK4
	if (layout_window_count() == 1)
		{
		gtk_widget_hide(lw->window);
		}
#else
	if (layout_window_count() > 1)
		{
		gtk_widget_show(lw->window);
		}
#endif

	file_data_register_notify_func(layout_image_notify_cb, lw, NOTIFY_PRIORITY_LOW);

	DEBUG_1("%s layout_new: end", get_exec_time());

	return lw;
}

static void layout_write_attributes(const LayoutOptions &lop, GString *outstr, gint indent)
{
	WRITE_NL(); WRITE_CHAR(lop, id);

	WRITE_NL(); WRITE_INT(lop, style);
	WRITE_NL(); WRITE_CHAR(lop, order);
	WRITE_NL(); WRITE_UINT(lop, dir_view_type);
	WRITE_NL(); WRITE_UINT(lop, file_view_type);

	WRITE_NL(); WRITE_UINT(lop, file_view_list_sort.method);
	WRITE_NL(); WRITE_BOOL(lop, file_view_list_sort.ascending);
	WRITE_NL(); WRITE_BOOL(lop, file_view_list_sort.case_sensitive);

	WRITE_NL(); WRITE_UINT(lop, dir_view_list_sort.method);
	WRITE_NL(); WRITE_BOOL(lop, dir_view_list_sort.ascending);
	WRITE_NL(); WRITE_BOOL(lop, dir_view_list_sort.case_sensitive);
	WRITE_NL(); WRITE_BOOL(lop, show_marks);
	WRITE_NL(); WRITE_BOOL(lop, show_file_filter);
	WRITE_NL(); WRITE_BOOL(lop, show_thumbnails);
	WRITE_NL(); WRITE_BOOL(lop, show_directory_date);
	WRITE_NL(); WRITE_CHAR(lop, home_path);
	WRITE_NL(); WRITE_UINT(lop, startup_path);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_INT_FULL("main_window.x", lop.main_window.rect.x);
	WRITE_NL(); WRITE_INT_FULL("main_window.y", lop.main_window.rect.y);
	WRITE_NL(); WRITE_INT_FULL("main_window.w", lop.main_window.rect.width);
	WRITE_NL(); WRITE_INT_FULL("main_window.h", lop.main_window.rect.height);
	WRITE_NL(); WRITE_BOOL(lop, main_window.maximized);
	WRITE_NL(); WRITE_INT(lop, main_window.hdivider_pos);
	WRITE_NL(); WRITE_INT(lop, main_window.vdivider_pos);
	WRITE_NL(); WRITE_INT(lop, workspace);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_INT(lop, folder_window.vdivider_pos);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_INT_FULL("float_window.x", lop.float_window.rect.x);
	WRITE_NL(); WRITE_INT_FULL("float_window.y", lop.float_window.rect.y);
	WRITE_NL(); WRITE_INT_FULL("float_window.w", lop.float_window.rect.width);
	WRITE_NL(); WRITE_INT_FULL("float_window.h", lop.float_window.rect.height);
	WRITE_NL(); WRITE_INT(lop, float_window.vdivider_pos);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(lop, tools_float);
	WRITE_NL(); WRITE_BOOL(lop, tools_hidden);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(lop, show_info_pixel);
	WRITE_NL(); WRITE_BOOL(lop, ignore_alpha);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(lop, bars_state.info);
	WRITE_NL(); WRITE_BOOL(lop, bars_state.sort);
	WRITE_NL(); WRITE_BOOL(lop, bars_state.tools_float);
	WRITE_NL(); WRITE_BOOL(lop, bars_state.tools_hidden);
	WRITE_NL(); WRITE_BOOL(lop, bars_state.hidden);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_UINT(lop, image_overlay.state);
	WRITE_NL(); WRITE_INT(lop, image_overlay.histogram_channel);
	WRITE_NL(); WRITE_INT(lop, image_overlay.histogram_mode);

	WRITE_NL(); WRITE_INT(lop, log_window.x);
	WRITE_NL(); WRITE_INT(lop, log_window.y);
	WRITE_NL(); WRITE_INT(lop, log_window.width);
	WRITE_NL(); WRITE_INT(lop, log_window.height);

	WRITE_NL(); WRITE_INT_FULL("preferences_window.x", lop.preferences_window.rect.x);
	WRITE_NL(); WRITE_INT_FULL("preferences_window.y", lop.preferences_window.rect.y);
	WRITE_NL(); WRITE_INT_FULL("preferences_window.w", lop.preferences_window.rect.width);
	WRITE_NL(); WRITE_INT_FULL("preferences_window.h", lop.preferences_window.rect.height);
	WRITE_NL(); WRITE_INT(lop, preferences_window.page_number);

	WRITE_NL(); WRITE_INT(lop, search_window.x);
	WRITE_NL(); WRITE_INT(lop, search_window.y);
	WRITE_NL(); WRITE_INT_FULL("search_window.w", lop.search_window.width);
	WRITE_NL(); WRITE_INT_FULL("search_window.h", lop.search_window.height);

	WRITE_NL(); WRITE_INT_FULL("dupe_window.x", lop.dupe_window.rect.x);
	WRITE_NL(); WRITE_INT_FULL("dupe_window.y", lop.dupe_window.rect.y);
	WRITE_NL(); WRITE_INT_FULL("dupe_window.w", lop.dupe_window.rect.width);
	WRITE_NL(); WRITE_INT_FULL("dupe_window.h", lop.dupe_window.rect.height);
	WRITE_NL(); WRITE_INT(lop, dupe_window.vdivider_pos);

	WRITE_NL(); WRITE_INT(lop, advanced_exif_window.x);
	WRITE_NL(); WRITE_INT(lop, advanced_exif_window.y);
	WRITE_NL(); WRITE_INT_FULL("advanced_exif_window.w", lop.advanced_exif_window.width);
	WRITE_NL(); WRITE_INT_FULL("advanced_exif_window.h", lop.advanced_exif_window.height);
	WRITE_SEPARATOR();

	WRITE_NL(); WRITE_BOOL(lop, animate);
}


void layout_write_config(LayoutWindow *lw, GString *outstr, gint indent)
{
	layout_sync_options_with_current_state(lw);
	WRITE_NL(); WRITE_STRING("<layout");
	layout_write_attributes(lw->options, outstr, indent + 1);
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

static gchar *layout_config_startup_path(const LayoutOptions &lop)
{
	switch (lop.startup_path)
		{
		case STARTUP_PATH_LAST:
			{
			const gchar *path = history_list_find_last_path_by_key("path_list");
			return (path && isdir(path)) ? g_strdup(path) : get_current_dir();
			}
		case STARTUP_PATH_HOME:
			return (lop.home_path && isdir(lop.home_path)) ? g_strdup(lop.home_path) : g_strdup(homedir());
		default:
			return get_current_dir();
		}
}

LayoutWindow *layout_new_from_config(const gchar **attribute_names, const gchar **attribute_values, gboolean use_commandline)
{
	LayoutOptions lop = init_layout_options(attribute_names, attribute_values);

	g_autofree gchar *path = layout_config_startup_path(lop);

	/* If multiple windows are specified in the config. file,
	 * use the command line options only in the main window.
	 */
	static bool first_found = false;
	if (use_commandline && !first_found)
		{
		first_found = true;

		if (isdir(path))
			{
			g_autofree gchar *last_image = get_recent_viewed_folder_image(path);
			if (last_image)
				{
				std::swap(path, last_image);
				}
			}
		}

	LayoutWindow *lw = layout_new(lop);
	layout_sort_set_files(lw, lw->options.file_view_list_sort);

	layout_set_path(lw, path);

	free_layout_options_content(lop);
	return lw;
}

void layout_update_from_config(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values)
{
	LayoutOptions lop = init_layout_options(attribute_names, attribute_values);

	layout_apply_options(lw, lop);

	free_layout_options_content(lop);
}

LayoutWindow *layout_new_from_default()
{
	LayoutWindow *lw;

	g_autofree gchar *default_path = g_build_filename(get_rc_dir(), DEFAULT_WINDOW_LAYOUT, NULL);
	if (load_config_from_file(default_path, TRUE))
		{
		lw = layout_window_list.back();
		}
	else
		{
		lw = layout_new_from_config(nullptr, nullptr, TRUE);
		}

	g_autofree gchar *id_tmp = layout_get_unique_id();
	std::swap(lw->options.id, id_tmp);

	return lw;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
