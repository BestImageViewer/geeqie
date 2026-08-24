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

#include "view-dir-list.h"

#include <unistd.h>

#include <cstdlib>
#include <cstring>

#include <glib-object.h>

#include "filedata.h"
#include "layout.h"
#include "options.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-tree-edit.h"
#include "utilops.h"
#include "view-dir.h"

struct ViewDirInfoList
{
	GList *list;
	GtkWidget *box;
	GHashTable *labels;
	GHashTable *buttons;
	FileData *selected_fd;
	FileData *last_press_fd;
	gint64 last_press_time;
};

#define VDLIST(_vd_) ((ViewDirInfoList *)((_vd_)->info))

namespace
{

constexpr gchar VDLIST_FD_DATA[] = "vdlist-fd";

} // namespace

static void vdlist_editing_changed(GtkEditableLabel *label, GParamSpec *, gpointer data);
static void vdlist_button_state_changed(GtkWidget *button, GtkStateFlags previous_flags, gpointer data);

static GtkWidget *vdlist_icon_widget_new(const gchar *icon_name, const gchar *emblem_name)
{
	GtkWidget *image = gtk_image_new_from_icon_name(icon_name);
	gtk_image_set_pixel_size(GTK_IMAGE(image), 16);
	GtkWidget *content = image;
	if (emblem_name)
		{
		GtkWidget *overlay = gtk_overlay_new();
		GtkWidget *emblem = gtk_image_new_from_icon_name(emblem_name);
		gtk_image_set_pixel_size(GTK_IMAGE(emblem), 10);
		gtk_widget_set_halign(emblem, GTK_ALIGN_END);
		gtk_widget_set_valign(emblem, GTK_ALIGN_END);
		gtk_overlay_set_child(GTK_OVERLAY(overlay), image);
		gtk_overlay_add_overlay(GTK_OVERLAY(overlay), emblem);
		content = overlay;
		}

	GtkWidget *hit_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(hit_box, 16, 16);
	gtk_box_append(GTK_BOX(hit_box), content);
	return hit_box;
}

/*
 *-----------------------------------------------------------------------------
 * misc
 *-----------------------------------------------------------------------------
 */

FileData *vdlist_row_by_path(ViewDir *vd, const gchar *path, gint *row)
{
	GList *work;
	gint n;

	if (!path)
		{
		if (row) *row = -1;
		return nullptr;
		}

	n = 0;
	work = VDLIST(vd)->list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		if (strcmp(fd->path, path) == 0)
			{
			if (row) *row = n;
			return fd;
			}
		work = work->next;
		n++;
		}

	if (row) *row = -1;
	return nullptr;
}

/*
 *-----------------------------------------------------------------------------
 * dnd
 *-----------------------------------------------------------------------------
 */

void vdlist_scroll_to_fd(ViewDir *vd, FileData *fd, gfloat)
{
	if (!gtk_widget_get_realized(vd->view)) return;

	vdlist_color_set(vd, fd, TRUE);

	auto *button = static_cast<GtkWidget *>(g_hash_table_lookup(VDLIST(vd)->buttons, fd));
	if (button && !gtk_widget_has_focus(button)) gtk_widget_grab_focus(button);
}

/*
 *-----------------------------------------------------------------------------
 * main
 *-----------------------------------------------------------------------------
 */

static gboolean vdlist_populate(ViewDir *vd, gboolean clear)
{
	(void)clear;
	GList *work;
	GList *old_list;
	gboolean ret;
	FileData *fd;
	const auto settings = vd->layout ? vd->layout->options.dir_view_list_sort : FileData::FileList::SortSettings{ SORT_NAME, TRUE, TRUE };

	old_list = VDLIST(vd)->list;

	ret = filelist_read(vd->dir_fd, nullptr, &VDLIST(vd)->list);
	VDLIST(vd)->list = filelist_sort(VDLIST(vd)->list, settings);

	/* add . and .. */

	if (options->file_filter.show_parent_directory && strcmp(vd->dir_fd->path, G_DIR_SEPARATOR_S) != 0)
		{
		g_autofree gchar *filepath = g_build_filename(vd->dir_fd->path, "..", NULL);
		fd = file_data_new_dir(filepath);
		VDLIST(vd)->list = g_list_prepend(VDLIST(vd)->list, fd);
		}

	if (options->file_filter.show_dot_directory)
		{
		g_autofree gchar *filepath = g_build_filename(vd->dir_fd->path, ".", NULL);
		fd = file_data_new_dir(filepath);
		VDLIST(vd)->list = g_list_prepend(VDLIST(vd)->list, fd);
		}

	while (GtkWidget *child = gtk_widget_get_first_child(VDLIST(vd)->box))
		{
		gtk_box_remove(GTK_BOX(VDLIST(vd)->box), child);
		}

	g_hash_table_remove_all(VDLIST(vd)->labels);
	g_hash_table_remove_all(VDLIST(vd)->buttons);

	work = VDLIST(vd)->list;
	while (work)
		{
		const gchar *icon_name;
		const gchar *emblem_name = nullptr;
		const gchar *date = "";

		fd = static_cast<FileData *>(work->data);

		if (access_file(fd->path, R_OK | X_OK) && fd->name)
			{
			if (islink(fd->path))
				{
				icon_name = GQ_ICON_DIRECTORY;
				emblem_name = GQ_ICON_LINK;
				}
			else if (fd->name[0] == '.' && fd->name[1] == '\0')
				{
				icon_name = GQ_ICON_OPEN;
				}
			else if (fd->name[0] == '.' && fd->name[1] == '.' && fd->name[2] == '\0')
				{
				icon_name = GQ_ICON_GO_UP;
				}
			else if (!access_file(fd->path, W_OK) )
				{
				icon_name = GQ_ICON_DIRECTORY;
				emblem_name = GQ_ICON_READONLY;
				}
			else
				{
				icon_name = GQ_ICON_DIRECTORY;
				if (vd->layout && vd->layout->options.show_directory_date)
					date = text_from_time(fd->date);
				}
			}
		else
			{
			icon_name = GQ_ICON_DIRECTORY;
			emblem_name = GQ_ICON_UNREADABLE;
			}

		g_autofree gchar *link = islink(fd->path) ? realpath(fd->path, nullptr) : nullptr;
		GtkWidget *button = gtk_button_new();
		GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
		GtkWidget *icon_widget = vdlist_icon_widget_new(icon_name, emblem_name);
		GtkWidget *name = gtk_editable_label_new(fd->name);
		GtkWidget *date_label = gtk_label_new(date);

		gtk_widget_set_hexpand(button, TRUE);
		gtk_widget_add_css_class(button, "flat");
		gtk_widget_add_css_class(button, "vdlist-row-button");
		gtk_widget_set_focusable(button, TRUE);
		gtk_widget_set_hexpand(name, TRUE);
		gtk_widget_set_can_target(name, FALSE);
		gtk_label_set_xalign(GTK_LABEL(date_label), 0.0);
		gtk_widget_set_tooltip_text(button, link);

		gtk_box_append(GTK_BOX(row), icon_widget);
		gtk_box_append(GTK_BOX(row), name);
		gtk_box_append(GTK_BOX(row), date_label);
		gtk_button_set_child(GTK_BUTTON(button), row);

		g_object_set_data(G_OBJECT(button), VDLIST_FD_DATA, fd);
		g_object_set_data(G_OBJECT(row), VDLIST_FD_DATA, fd);
		g_object_set_data(G_OBJECT(icon_widget), VDLIST_FD_DATA, fd);
		g_object_set_data(G_OBJECT(name), VDLIST_FD_DATA, fd);
		g_object_set_data(G_OBJECT(date_label), VDLIST_FD_DATA, fd);

		g_hash_table_insert(VDLIST(vd)->buttons, fd, button);
		g_hash_table_insert(VDLIST(vd)->labels, fd, name);

		g_signal_connect(button, "state-flags-changed", G_CALLBACK(vdlist_button_state_changed), vd);
		g_signal_connect(name, "notify::editing", G_CALLBACK(vdlist_editing_changed), vd);
		gtk_box_append(GTK_BOX(VDLIST(vd)->box), button);
		work = work->next;
		}


	vd->click_fd = nullptr;
	vd->drop_fd = nullptr;
	VDLIST(vd)->selected_fd = nullptr;
	VDLIST(vd)->last_press_fd = nullptr;
	VDLIST(vd)->last_press_time = 0;

	file_data_list_free(old_list);
	return ret;
}

gboolean vdlist_set_fd(ViewDir *vd, FileData *dir_fd)
{
	gboolean ret;

	if (!dir_fd) return FALSE;
	if (vd->dir_fd == dir_fd) return TRUE;

	g_autofree gchar *old_path = nullptr; /* Used to store directory for walking up */
	if (vd->dir_fd)
		{
		g_autofree gchar *base = remove_level_from_path(vd->dir_fd->path);
		if (strcmp(base, dir_fd->path) == 0)
			{
			old_path = g_strdup(filename_from_path(vd->dir_fd->path));
			}
		}

	vd->dir_fd.reset(dir_fd);

	ret = vdlist_populate(vd, TRUE);

	/* scroll to make last path visible */
	FileData *found = nullptr;
	GList *work;

	work = VDLIST(vd)->list;
	while (work && !found)
		{
		auto fd = static_cast<FileData *>(work->data);
		if (!old_path || strcmp(old_path, fd->name) == 0) found = fd;
		work = work->next;
		}

	if (found) vdlist_scroll_to_fd(vd, found, 0.5);

	return ret;
}

void vdlist_refresh(ViewDir *vd)
{
	vdlist_populate(vd, FALSE);
}

gboolean vdlist_press_key_cb(GtkWidget *widget, guint keyval, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	if (keyval != GDK_KEY_Menu) return FALSE;

	(void)widget;
	vd->click_fd = VDLIST(vd)->selected_fd;

	vd_color_set(vd, vd->click_fd, TRUE);

	vd_pop_menu(vd, vd->click_fd);

	return TRUE;
}

void vdlist_press_cb(ViewDir *vd, gdouble x, gdouble y)
{
	vd->click_fd = vdlist_fd_at_point(vd, x, y);
}

void vdlist_destroy_cb(GtkWidget *widget, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);

	vd_dnd_drop_scroll_cancel(vd);
	widget_auto_scroll_stop(widget);

	g_clear_pointer(&VDLIST(vd)->labels, g_hash_table_unref);
	g_clear_pointer(&VDLIST(vd)->buttons, g_hash_table_unref);
	file_data_list_free(VDLIST(vd)->list);
}

FileData *vdlist_fd_at_point(ViewDir *vd, gdouble x, gdouble y)
{
	GtkWidget *picked = gtk_widget_pick(vd->view, x, y, GTK_PICK_NON_TARGETABLE);
	while (picked && picked != vd->view)
		{
		auto *fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(picked), VDLIST_FD_DATA));
		if (fd) return fd;
		picked = gtk_widget_get_parent(picked);
		}
	return nullptr;
}

void vdlist_color_set(ViewDir *vd, FileData *fd, gboolean set)
{
	auto *button = static_cast<GtkWidget *>(g_hash_table_lookup(VDLIST(vd)->buttons, fd));
	if (!button) return;

	if (set)
		{
		if (VDLIST(vd)->selected_fd && VDLIST(vd)->selected_fd != fd)
			{
			auto *old_button = static_cast<GtkWidget *>(g_hash_table_lookup(VDLIST(vd)->buttons, VDLIST(vd)->selected_fd));
			if (old_button) gtk_widget_remove_css_class(old_button, "vdlist-selected");
			}

		gtk_widget_add_css_class(button, "vdlist-selected");
		VDLIST(vd)->selected_fd = fd;
		}
	else
		{
		gtk_widget_remove_css_class(button, "vdlist-selected");
		if (VDLIST(vd)->selected_fd == fd) VDLIST(vd)->selected_fd = nullptr;
		}
}

static void vdlist_editing_changed(GtkEditableLabel *label, GParamSpec *, gpointer data)
{
	if (gtk_editable_label_get_editing(label)) return;
	gtk_widget_set_can_target(GTK_WIDGET(label), FALSE);
	auto *vd = static_cast<ViewDir *>(data);
	auto *fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(label), VDLIST_FD_DATA));
	if (!fd) return;

	const gchar *new_name = gtk_editable_get_text(GTK_EDITABLE(label));
	if (g_strcmp0(new_name, fd->name) == 0) return;
	g_autofree gchar *base = remove_level_from_path(fd->path);
	g_autofree gchar *new_path = g_build_filename(base, new_name, NULL);
	file_util_rename_dir(fd, new_path, vd->view, [vd](gboolean success, const gchar *path)
	{
		if (!success) return;
		vd_refresh(vd);
		FileData *fd = vdlist_row_by_path(vd, path, nullptr);
		if (fd) vdlist_scroll_to_fd(vd, fd, 0.5);
	});
}

static void vdlist_button_state_changed(GtkWidget *button, GtkStateFlags previous_flags, gpointer data)
{
	const GtkStateFlags flags = gtk_widget_get_state_flags(button);
	if ((previous_flags & GTK_STATE_FLAG_ACTIVE) || !(flags & GTK_STATE_FLAG_ACTIVE)) return;

	auto *vd = static_cast<ViewDir *>(data);
	auto *fd = static_cast<FileData *>(g_object_get_data(G_OBJECT(button), VDLIST_FD_DATA));
	if (!fd) return;

	vdlist_color_set(vd, fd, TRUE);

	gint double_click_time = 400;
	g_object_get(gtk_settings_get_default(), "gtk-double-click-time", &double_click_time, nullptr);
	const gint64 press_time = g_get_monotonic_time();
	const gboolean double_click = VDLIST(vd)->last_press_fd == fd &&
	                              press_time - VDLIST(vd)->last_press_time <= double_click_time * 1000;
	VDLIST(vd)->last_press_fd = fd;
	VDLIST(vd)->last_press_time = press_time;

	if ((options->view_dir_list_single_click_enter || double_click) && vd->select_func)
		{
		vd->select_func(vd, fd, vd->select_data);
		}
}

void vdlist_rename_by_data(ViewDir *vd, FileData *fd)
{
	auto *label = static_cast<GtkWidget *>(g_hash_table_lookup(VDLIST(vd)->labels, fd));
	if (!label) return;
	gtk_widget_set_can_target(label, TRUE);
	gtk_editable_label_start_editing(GTK_EDITABLE_LABEL(label));
}

ViewDir *vdlist_new(ViewDir *vd)
{
	vd->info = g_new0(ViewDirInfoList, 1);

	vd->type = DIRVIEW_LIST;
	VDLIST(vd)->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	VDLIST(vd)->labels = g_hash_table_new(g_direct_hash, g_direct_equal);
	VDLIST(vd)->buttons = g_hash_table_new(g_direct_hash, g_direct_equal);
	vd->view = VDLIST(vd)->box;
	gtk_widget_add_css_class(vd->view, "view-dir-list");

	return vd;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
