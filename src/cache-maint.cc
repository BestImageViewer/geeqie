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

#include "cache-maint.h"

#include <dirent.h>

#include <cstdlib>
#include <cstring>

#include <glib-object.h>
#include <gtk/gtk.h>

#include "cache-loader.h"
#include "cache.h"
#include "compat.h"
#include "filedata.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "main.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "thumb-standard.h"
#include "thumb.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "ui-utildlg.h"
#include "window.h"

namespace
{

struct CMData
{
	GList *list;
	GList *done_list;
	guint idle_id; /* event source id */
	GenericDialog *gd;
	GtkWidget *entry;
	GtkWidget *spinner;
	GtkWidget *button_stop;
	GtkWidget *button_close;
	gboolean clear;
	gboolean metadata;
	gboolean remote;
	GtkApplication *app;
};

struct CacheManager
{
	GenericDialog *dialog;
	GtkWidget *folder_entry;
	GtkWidget *progress;

	GList *list_todo;

	gint count_total;
	gint count_done;
};

struct CacheOpsData
{
	GenericDialog *gd;
	ThumbLoaderStd *tl;
	CacheLoader *cl;
	GSourceFunc destroy_func; /* Used by the command line prog. functions */
	GtkApplication *app;

	GList *list;
	GList *list_dir;

	gint days;
	gboolean clear;

	GtkWidget *button_close;
	GtkWidget *button_stop;
	GtkWidget *button_start;
	GtkWidget *progress;
	GtkWidget *progress_bar;
	GtkWidget *spinner;

	GtkWidget *group;
	GtkWidget *entry;

	gint count_total;
	gint count_done;

	gboolean local;
	gboolean recurse;

	gboolean remote;

	guint idle_id; /* event source id */
};

constexpr gint PURGE_DIALOG_WIDTH = 400;

/* sorry for complexity (cm->done_list), but need it to remove empty dirs */
CMData *cache_maintain_data_new(gboolean clear, gboolean metadata, gboolean remote)
{
	const gchar *cache_folder = metadata ? get_metadata_cache_dir() : get_thumbnails_cache_dir();
	FileData *dir_fd = file_data_new_dir(cache_folder);

	GList *dlist;
	if (!filelist_read(dir_fd, nullptr, &dlist))
		{
		file_data_unref(dir_fd);
		return nullptr;
		}

	dlist = g_list_append(dlist, dir_fd);

	auto *cm = g_new0(CMData, 1);
	cm->list = dlist;
	cm->done_list = nullptr;
	cm->clear = clear;
	cm->metadata = metadata;
	cm->remote = remote;

	return cm;
}

void cache_maintain_home_close(CMData *cm)
{
	if (cm->idle_id) g_source_remove(cm->idle_id);
	if (cm->gd) generic_dialog_close(cm->gd);
	file_data_list_free(cm->list);
	g_list_free(cm->done_list);
	g_free(cm);
}

} // namespace

/*
 *-----------------------------------------------------------------------------
 * Command line cache maintenance program functions
 *-----------------------------------------------------------------------------
 */
static gchar *cache_maintenance_path = nullptr;

static void cache_manager_sim_remote(GtkApplication *app, const gchar *path, gboolean recurse, GSourceFunc destroy_func);

static gboolean cache_maintenance_sim_stop_cb(gpointer data)
{
	auto *cd = static_cast<CacheOpsData *>(data);

	g_application_withdraw_notification(G_APPLICATION(cd->app), "cache_maintenance");

	g_free(data);

	exit(EXIT_SUCCESS);
}

static gboolean cache_maintenance_render_stop_cb(gpointer data)
{
	auto *cd = static_cast<CacheOpsData *>(data);

	cache_maintenance_notification(cd->app, _("Creating sim data..."), TRUE);

	cache_manager_sim_remote(cd->app, cache_maintenance_path, TRUE, cache_maintenance_sim_stop_cb);

	return G_SOURCE_REMOVE;
}

static void cache_maintenance_clean_stop_cb(gpointer data)
{
	auto *cm = static_cast<CMData *>(data);


	cache_maintenance_notification(cm->app,  _("Creating thumbs..."), TRUE);
	cache_manager_render_remote(cm->app, cache_maintenance_path, TRUE, options->thumbnails.cache_into_dirs, cache_maintenance_render_stop_cb);
}

void cache_maintenance(GtkApplication *app, const gchar *path)
{
	cache_maintenance_path = g_strdup(path);

	cache_maintenance_notification(app, _("Cleaning thumbs and sims..."), TRUE);

	cache_maintain_home_remote(app, FALSE, FALSE, cache_maintenance_clean_stop_cb);
}

/*
 *-------------------------------------------------------------------
 * cache maintenance
 *-------------------------------------------------------------------
 */

static gboolean isempty(const gchar *path)
{
	DIR *dp;
	struct dirent *dir;

	g_autofree gchar *pathl = path_from_utf8(path);
	dp = opendir(pathl);
	if (!dp) return FALSE;

	while ((dir = readdir(dp)) != nullptr)
		{
		gchar *name = dir->d_name;

		if (name[0] != '.' || (name[1] != '\0' && (name[1] != '.' || name[2] != '\0')) )
			{
			closedir(dp);
			return FALSE;
			}
		}

	closedir(dp);
	return TRUE;
}

static void cache_maintain_home_stop(CMData *cm)
{
	if (cm->idle_id)
		{
		g_source_remove(cm->idle_id);
		cm->idle_id = 0;
		}

	if (!cm->remote)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(cm->entry), _("done"));
		gtk_spinner_stop(GTK_SPINNER(cm->spinner));

		gtk_widget_set_sensitive(cm->button_stop, FALSE);
		gtk_widget_set_sensitive(cm->button_close, TRUE);
		}
}

static gboolean cache_maintain_home_cb(gpointer data)
{
	auto cm = static_cast<CMData *>(data);
	GList *dlist = nullptr;
	GList *list = nullptr;
	FileData *fd;
	gboolean just_done = FALSE;
	gboolean still_have_a_file = TRUE;
	gsize base_length;
	const gchar *cache_folder;
	gboolean filter_disable;

	if (cm->metadata)
		{
		cache_folder = get_metadata_cache_dir();
		}
	else
		{
		cache_folder = get_thumbnails_cache_dir();
		}

	base_length = strlen(cache_folder);

	if (!cm->list)
		{
		DEBUG_1("purge chk done.");
		cm->idle_id = 0;
		cache_maintain_home_stop(cm);
		return G_SOURCE_REMOVE;
		}

	fd = static_cast<FileData *>(cm->list->data);

	DEBUG_1("purge chk (%d) \"%s\"", (cm->clear && !cm->metadata), fd->path);

/**
 * It is necessary to disable the file filter when clearing the cache,
 * otherwise the .sim (file similarity) files are not deleted.
 */
	filter_disable = options->file_filter.disable;
	options->file_filter.disable = TRUE;

	if (g_list_find(cm->done_list, fd) == nullptr)
		{
		cm->done_list = g_list_prepend(cm->done_list, fd);

		if (filelist_read(fd, &list, &dlist))
			{
			GList *work;

			just_done = TRUE;
			still_have_a_file = FALSE;

			work = list;
			while (work)
				{
				auto fd_list = static_cast<FileData *>(work->data);
				g_autofree gchar *path_buf = g_strdup(fd_list->path);

				gchar *dot = strrchr(path_buf, '.');

				if (dot) *dot = '\0';
				if ((!cm->metadata && cm->clear) ||
				    (strlen(path_buf) > base_length && !isfile(path_buf + base_length)) )
					{
					if (dot) *dot = '.';
					if (!unlink_file(path_buf)) log_printf("failed to delete:%s\n", path_buf);
					}
				else
					{
					still_have_a_file = TRUE;
					}

				work = work->next;
				}
			}
		}
	options->file_filter.disable = filter_disable;

	file_data_list_free(list);

	cm->list = g_list_concat(dlist, cm->list);

	if (cm->list && g_list_find(cm->done_list, cm->list->data) != nullptr)
		{
		/* check if the dir is empty */

		if (cm->list->data == fd && just_done)
			{
			if (!still_have_a_file && !dlist && cm->list->next && !rmdir_utf8(fd->path))
				{
				log_printf("Unable to delete dir: %s\n", fd->path);
				}
			}
		else
			{
			/* must re-check for an empty dir */
			if (isempty(fd->path) && cm->list->next && !rmdir_utf8(fd->path))
				{
				log_printf("Unable to delete dir: %s\n", fd->path);
				}
			}

		fd = static_cast<FileData *>(cm->list->data);
		cm->done_list = g_list_remove(cm->done_list, fd);
		cm->list = g_list_remove(cm->list, fd);
		file_data_unref(fd);
		}

	if (cm->list && !cm->remote)
		{
		const gchar *buf;

		fd = static_cast<FileData *>(cm->list->data);
		if (strlen(fd->path) > base_length)
			{
			buf = fd->path + base_length;
			}
		else
			{
			buf = "...";
			}
		gq_gtk_entry_set_text(GTK_ENTRY(cm->entry), buf);
		}

	return G_SOURCE_CONTINUE;
}

static void cache_maintain_home_close_cb(GenericDialog *, gpointer data)
{
	auto cm = static_cast<CMData *>(data);

	if (!gtk_widget_get_sensitive(cm->button_close)) return;

	cache_maintain_home_close(cm);
}

static void cache_maintain_home_stop_cb(GenericDialog *, gpointer data)
{
	auto cm = static_cast<CMData *>(data);

	cache_maintain_home_stop(cm);
}

static void cache_maintain_home(gboolean metadata, gboolean clear, GtkWidget *parent)
{
	CMData *cm = cache_maintain_data_new(clear, metadata, FALSE);
	if (!cm) return;

	const gchar *msg;
	GtkWidget *hbox;

	if (metadata)
		{
		msg = _("Removing old metadata...");
		}
	else if (clear)
		{
		msg = _("Clearing cached thumbnails...");
		}
	else
		{
		msg = _("Removing old thumbnails...");
		}

	cm->gd = generic_dialog_new(_("Maintenance"),
				    "main_maintenance",
				    parent, FALSE,
				    nullptr, cm);
	cm->gd->cancel_cb = cache_maintain_home_close_cb;
	cm->button_close = generic_dialog_add_button(cm->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_maintain_home_close_cb, FALSE);
	gtk_widget_set_sensitive(cm->button_close, FALSE);
	cm->button_stop = generic_dialog_add_button(cm->gd, GQ_ICON_STOP, _("Stop"),
						    cache_maintain_home_stop_cb, FALSE);

	generic_dialog_add_message(cm->gd, nullptr, msg, nullptr, FALSE);
	gtk_window_set_default_size(GTK_WINDOW(cm->gd->dialog), PURGE_DIALOG_WIDTH, -1);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(cm->gd->vbox), hbox, FALSE, FALSE, 5);
	gtk_widget_show(hbox);

	cm->entry = gtk_entry_new();
	gtk_widget_set_can_focus(cm->entry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(cm->entry), FALSE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), cm->entry, TRUE, TRUE, 0);
	gtk_widget_show(cm->entry);

	cm->spinner = gtk_spinner_new();
	gtk_spinner_start(GTK_SPINNER(cm->spinner));
	gq_gtk_box_pack_start(GTK_BOX(hbox), cm->spinner, FALSE, FALSE, 0);
	gtk_widget_show(cm->spinner);

	gtk_widget_show(cm->gd->dialog);

	cm->idle_id = g_idle_add(cache_maintain_home_cb, cm);
}

/**
 * @brief Clears or culls cached data
 * @param metadata TRUE - work on metadata cache, FALSE - work on thumbnail cache
 * @param clear TRUE - clear cache, FALSE - delete orphaned cached items
 * @param func Function called when idle loop function terminates
 *
 *
 */
void cache_maintain_home_remote(GtkApplication *app, gboolean metadata, gboolean clear, GDestroyNotify func)
{
	CMData *cm = cache_maintain_data_new(clear, metadata, TRUE);
	if (!cm) return;

	cm->app = app;

	cm->idle_id = g_idle_add_full(G_PRIORITY_LOW, cache_maintain_home_cb, cm, func);
}

static void cache_maint_moved(FileData *fd)
{
	const gchar *src = fd->change->source;
	const gchar *dest = fd->change->dest;

	if (!src || !dest) return;

	const auto cache_move = [src, dest](CacheType cache_type)
	{
		g_autofree gchar *src_path = cache_find_location(cache_type, src);
		if (!src_path || !isfile(src_path)) return;

		g_autofree gchar *dest_base = cache_create_location(cache_type, dest);
		if (!dest_base) return;

		g_autofree gchar *dest_path = cache_get_location(cache_type, dest);
		if (!dest_path) return;

		if (!move_file(src_path, dest_path))
			{
			DEBUG_1("Failed to move cache file \"%s\" to \"%s\"", src_path, dest_path);
			/* we remove it anyway - it's stale */
			unlink_file(src_path);
			}
	};

	cache_move(CACHE_TYPE_THUMB);
	cache_move(CACHE_TYPE_SIM);
	cache_move(CACHE_TYPE_METADATA);

	if (options->thumbnails.enable_caching && options->thumbnails.spec_standard)
		thumb_std_maint_moved(src, dest);
}

static void cache_maint_removed(FileData *fd)
{
	const auto cache_remove = [fd](CacheType cache_type)
	{
		g_autofree gchar *path = cache_find_location(cache_type, fd->path);
		if (!path || !isfile(path)) return;

		if (!unlink_file(path))
			{
			DEBUG_1("Failed to remove cache file %s", path);
			}
	};

	cache_remove(CACHE_TYPE_THUMB);
	cache_remove(CACHE_TYPE_SIM);
	cache_remove(CACHE_TYPE_METADATA);

	if (options->thumbnails.enable_caching && options->thumbnails.spec_standard)
		thumb_std_maint_removed(fd->path);
}

static void cache_maint_copied(FileData *fd)
{
	g_autofree gchar *src_path = cache_find_location(CACHE_TYPE_METADATA, fd->change->source);
	if (!src_path) return;

	g_autofree gchar *dest_base = cache_create_location(CACHE_TYPE_METADATA, fd->change->dest);
	if (!dest_base) return;

	g_autofree gchar *dest_path = cache_get_location(CACHE_TYPE_METADATA, fd->change->dest);
	if (!copy_file(src_path, dest_path))
		{
		DEBUG_1("failed to copy metadata %s to %s", src_path, dest_path);
		}
}

void cache_notify_cb(FileData *fd, NotifyType type, gpointer)
{
	if (!(type & NOTIFY_CHANGE) || !fd->change) return;

	DEBUG_1("Notify cache_maint: %s %04x", fd->path, type);
	switch (fd->change->type)
		{
		case FILEDATA_CHANGE_MOVE:
		case FILEDATA_CHANGE_RENAME:
			cache_maint_moved(fd);
			break;
		case FILEDATA_CHANGE_COPY:
			cache_maint_copied(fd);
			break;
		case FILEDATA_CHANGE_DELETE:
			cache_maint_removed(fd);
			break;
		case FILEDATA_CHANGE_UNSPECIFIED:
		case FILEDATA_CHANGE_WRITE_METADATA:
			break;
		}
}


/*
 *-------------------------------------------------------------------
 * new cache maintenance utilities
 *-------------------------------------------------------------------
 */

static void cache_manager_render_reset(CacheOpsData *cd)
{
	file_data_list_free(cd->list);
	cd->list = nullptr;

	file_data_list_free(cd->list_dir);
	cd->list_dir = nullptr;

	thumb_loader_free(reinterpret_cast<ThumbLoader *>(cd->tl));
	cd->tl = nullptr;
}

static void cache_manager_render_close_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!gtk_widget_get_sensitive(cd->button_close)) return;

	cache_manager_render_reset(cd);
	generic_dialog_close(cd->gd);
	g_free(cd);
}

static void cache_manager_render_finish(CacheOpsData *cd)
{
	cache_manager_render_reset(cd);
	if (!cd->remote)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("done"));
		gtk_spinner_stop(GTK_SPINNER(cd->spinner));

		gtk_widget_set_sensitive(cd->group, TRUE);
		gtk_widget_set_sensitive(cd->button_start, TRUE);
		gtk_widget_set_sensitive(cd->button_stop, FALSE);
		gtk_widget_set_sensitive(cd->button_close, TRUE);
		}
}

static void cache_manager_render_stop_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("stopped"));
	cache_manager_render_finish(cd);

	if (cd->destroy_func)
		{
		g_idle_add(cd->destroy_func, nullptr);
		}
}

static void cache_manager_render_folder(CacheOpsData *cd, FileData *dir_fd)
{
	GList *list_d = nullptr;
	GList *list_f = nullptr;

	if (cd->recurse)
		{
		filelist_read(dir_fd, &list_f, &list_d);
		}
	else
		{
		filelist_read(dir_fd, &list_f, nullptr);
		}

	list_f = filelist_filter(list_f, FALSE);
	list_d = filelist_filter(list_d, TRUE);

	cd->list = g_list_concat(list_f, cd->list);
	cd->list_dir = g_list_concat(list_d, cd->list_dir);
}

static gboolean cache_manager_render_file(CacheOpsData *cd);

static void cache_manager_render_thumb_done_cb(ThumbLoader *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	thumb_loader_free(reinterpret_cast<ThumbLoader *>(cd->tl));
	cd->tl = nullptr;

	while (cache_manager_render_file(cd));
}

static gboolean cache_manager_render_file(CacheOpsData *cd)
{
	if (cd->list)
		{
		FileData *fd;
		gint success;

		fd = static_cast<FileData *>(cd->list->data);
		cd->list = g_list_remove(cd->list, fd);

		cd->tl = reinterpret_cast<ThumbLoaderStd *>(thumb_loader_new(options->thumbnails.max_width, options->thumbnails.max_height));
		thumb_loader_set_callbacks(reinterpret_cast<ThumbLoader *>(cd->tl),
					   cache_manager_render_thumb_done_cb,
					   cache_manager_render_thumb_done_cb,
					   nullptr, cd);
		thumb_loader_set_cache(reinterpret_cast<ThumbLoader *>(cd->tl), TRUE, cd->local, TRUE);
		success = thumb_loader_start(reinterpret_cast<ThumbLoader *>(cd->tl), fd);
		if (success)
			{
			if (!cd->remote)
				{
				gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), fd->path);
				cd->count_done = cd->count_done + 1;
				gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->progress_bar), static_cast<gdouble>(cd->count_done) / cd->count_total);
				}
			}
		else
			{
			thumb_loader_free(reinterpret_cast<ThumbLoader *>(cd->tl));
			cd->tl = nullptr;
			}

		file_data_unref(fd);

		return (!success);
		}
	if (cd->list_dir)
		{
		FileData *fd;

		fd = static_cast<FileData *>(cd->list_dir->data);
		cd->list_dir = g_list_remove(cd->list_dir, fd);

		cache_manager_render_folder(cd, fd);

		file_data_unref(fd);

		return TRUE;
		}

	if (!cd->remote)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("done"));
		}
	cache_manager_render_finish(cd);

	if (cd->destroy_func)
		{
		g_idle_add(cd->destroy_func, cd);
		}

	return FALSE;
}

static void cache_manager_render_start_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);
	GList *list_total = nullptr;

	if(!cd->remote)
		{
		if (cd->list || !gtk_widget_get_sensitive(cd->button_start)) return;
		}

	g_autofree gchar *path = remove_trailing_slash((gq_gtk_entry_get_text(GTK_ENTRY(cd->entry))));
	parse_out_relatives(path);

	if (!isdir(path))
		{
		if (!cd->remote)
			{
			warning_dialog(_("Invalid folder"),
			_("The specified folder can not be found."),
			GQ_ICON_DIALOG_WARNING, cd->gd->dialog);
			}
		else
			{
			log_printf("The specified folder can not be found: %s\n", path);
			}
		}
	else
		{
		FileData *dir_fd;
		if(!cd->remote)
			{
			gtk_widget_set_sensitive(cd->group, FALSE);
			gtk_widget_set_sensitive(cd->button_start, FALSE);
			gtk_widget_set_sensitive(cd->button_stop, TRUE);
			gtk_widget_set_sensitive(cd->button_close, FALSE);

			gtk_spinner_start(GTK_SPINNER(cd->spinner));
			}
		dir_fd = file_data_new_dir(path);
		cache_manager_render_folder(cd, dir_fd);
		list_total = filelist_recursive(dir_fd);
		cd->count_total = g_list_length(list_total);
		file_data_unref(dir_fd);
		g_list_free(list_total);
		cd->count_done = 0;

		while (cache_manager_render_file(cd));
		}
}

static void cache_manager_render_start_render_remote(CacheOpsData *cd, const gchar *user_path)
{
	g_autofree gchar *path = remove_trailing_slash(user_path);
	parse_out_relatives(path);

	if (!isdir(path))
		{
		log_printf("The specified folder can not be found: %s\n", path);
		}
	else
		{
		FileData *dir_fd;

		dir_fd = file_data_new_dir(path);
		cache_manager_render_folder(cd, dir_fd);
		file_data_unref(dir_fd);
		while (cache_manager_render_file(cd))
			;
		}
}

static void cache_manager_render_dialog(GtkWidget *widget, const gchar *path)
{
	CacheOpsData *cd;
	GtkWidget *hbox;
	GtkWidget *label;
	GtkWidget *button;

	cd = g_new0(CacheOpsData, 1);
	cd->remote = FALSE;

	cd->gd = generic_dialog_new(_("Create thumbnails"),
				    "create_thumbnails",
				    widget, FALSE,
				    nullptr, cd);
	gtk_window_set_default_size(GTK_WINDOW(cd->gd->dialog), PURGE_DIALOG_WIDTH, -1);
	cd->gd->cancel_cb = cache_manager_render_close_cb;
	cd->button_close = generic_dialog_add_button(cd->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_manager_render_close_cb, FALSE);
	cd->button_start = generic_dialog_add_button(cd->gd, GQ_ICON_OK, _("S_tart"),
						     cache_manager_render_start_cb, FALSE);
	cd->button_stop = generic_dialog_add_button(cd->gd, GQ_ICON_STOP, _("Stop"),
						    cache_manager_render_stop_cb, FALSE);
	gtk_widget_set_sensitive(cd->button_stop, FALSE);

	generic_dialog_add_message(cd->gd, nullptr, _("Create thumbnails"), nullptr, FALSE);

	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);
	cd->group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(cd->group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	label = tab_completion_new(&cd->entry, path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(cd->entry,_("Select folder") , TRUE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);

	pref_checkbox_new_int(cd->group, _("Include subfolders"), FALSE, &cd->recurse);
	button = pref_checkbox_new_int(cd->group, _("Store thumbnails local to source images"), FALSE, &cd->local);
	gtk_widget_set_sensitive(button, options->thumbnails.spec_standard);

	pref_line(cd->gd->vbox, PREF_PAD_SPACE);
	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	cd->progress = gtk_entry_new();
	gtk_widget_set_can_focus(cd->progress, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(cd->progress), FALSE);
	gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("click start to begin"));
	gq_gtk_box_pack_start(GTK_BOX(hbox), cd->progress, TRUE, TRUE, 0);
	gtk_widget_show(cd->progress);

	cd->progress_bar = gtk_progress_bar_new();
	gq_gtk_box_pack_start(GTK_BOX(cd->gd->vbox), cd->progress_bar, TRUE, TRUE, 0);
	gtk_widget_show(cd->progress_bar);

	cd->spinner = gtk_spinner_new();
	gq_gtk_box_pack_start(GTK_BOX(hbox), cd->spinner, FALSE, FALSE, 0);
	gtk_widget_show(cd->spinner);

	cd->list = nullptr;

	gtk_widget_show(cd->gd->dialog);
}

/**
 * @brief Create thumbnails
 * @param path Path to image folder
 * @param recurse
 * @param local Create thumbnails in same folder as images
 * @param destroy_func Function called when idle loop function terminates
 *
 *
 */
void cache_manager_render_remote(GtkApplication *app, const gchar *path, gboolean recurse, gboolean local, GSourceFunc destroy_func)
{
	CacheOpsData *cd;

	cd = g_new0(CacheOpsData, 1);
	cd->recurse = recurse;
	cd->local = local;
	cd->remote = TRUE;
	cd->destroy_func = destroy_func;
	cd->app = app;

	cache_manager_render_start_render_remote(cd, path);
}

static void cache_manager_standard_clean_close_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!gtk_widget_get_sensitive(cd->button_close)) return;

	generic_dialog_close(cd->gd);

	thumb_loader_std_thumb_file_validate_cancel(cd->tl);
	file_data_list_free(cd->list);
	g_free(cd);
}

static void cache_manager_standard_clean_done(CacheOpsData *cd)
{
	if (!cd->remote)
		{
		gtk_widget_set_sensitive(cd->button_stop, FALSE);
		gtk_widget_set_sensitive(cd->button_close, TRUE);

		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->progress), 1.0);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cd->progress), _("done"));
		}
	if (cd->idle_id)
		{
		g_source_remove(cd->idle_id);
		cd->idle_id = 0;
		}

	thumb_loader_std_thumb_file_validate_cancel(cd->tl);
	cd->tl = nullptr;

	file_data_list_free(cd->list);
	cd->list = nullptr;
}

static void cache_manager_standard_clean_stop_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	cache_manager_standard_clean_done(cd);
}

static gboolean cache_manager_standard_clean_clear_cb(gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (cd->list)
		{
		FileData *next_fd;

		next_fd = static_cast<FileData *>(cd->list->data);
		cd->list = g_list_remove(cd->list, next_fd);

		DEBUG_1("thumb removed: %s", next_fd->path);

		unlink_file(next_fd->path);
		file_data_unref(next_fd);

		cd->count_done++;
		if (!cd->remote)
			{
			if (cd->count_total != 0)
				{
				gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->progress),
							      static_cast<gdouble>(cd->count_done) / cd->count_total);
				}
			}

		return G_SOURCE_CONTINUE;
		}

	cd->idle_id = 0;
	cache_manager_standard_clean_done(cd);
	return G_SOURCE_REMOVE;
}

static void cache_manager_standard_clean_valid_cb(const gchar *path, gboolean valid, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (path)
		{
		if (!valid)
			{
			DEBUG_1("thumb cleaned: %s", path);
			unlink_file(path);
			}

		cd->count_done++;
		if (!cd->remote)
			{
			if (cd->count_total != 0)
				{
				gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->progress),
							      static_cast<gdouble>(cd->count_done) / cd->count_total);
				}
			}
		}

	cd->tl = nullptr;
	if (cd->list)
		{
		FileData *next_fd;

		next_fd = static_cast<FileData *>(cd->list->data);
		cd->list = g_list_remove(cd->list, next_fd);

		cd->tl = thumb_loader_std_thumb_file_validate(next_fd->path, cd->days,
							      cache_manager_standard_clean_valid_cb, cd);
		file_data_unref(next_fd);
		}
	else
		{
		cache_manager_standard_clean_done(cd);
		}
}

static void cache_manager_standard_clean_start(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!cd->remote)
	{
		if (cd->list || !gtk_widget_get_sensitive(cd->button_start)) return;

		gtk_widget_set_sensitive(cd->button_start, FALSE);
		gtk_widget_set_sensitive(cd->button_stop, TRUE);
		gtk_widget_set_sensitive(cd->button_close, FALSE);

		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cd->progress), _("running..."));
	}

	const auto get_thumbnails_folder_files = [](const gchar *thumb_folder)
	{
		g_autofree gchar *path = g_build_filename(get_thumbnails_standard_cache_dir(), thumb_folder, NULL);
		FileData *dir_fd = file_data_new_dir(path);

		GList *list = nullptr;
		filelist_read(dir_fd, &list, nullptr);
		file_data_unref(dir_fd);

		return list;
	};

	cd->list = get_thumbnails_folder_files(THUMB_FOLDER_NORMAL);
	cd->list = g_list_concat(cd->list, get_thumbnails_folder_files(THUMB_FOLDER_LARGE));
	cd->list = g_list_concat(cd->list, get_thumbnails_folder_files(THUMB_FOLDER_FAIL));

	cd->count_total = g_list_length(cd->list);
	cd->count_done = 0;

	/* start iterating */
	if (cd->clear)
		{
		cd->idle_id = g_idle_add(cache_manager_standard_clean_clear_cb, cd);
		}
	else
		{
		cache_manager_standard_clean_valid_cb(nullptr, TRUE, cd);
		}
}

static void cache_manager_standard_clean_start_cb(GenericDialog *gd, gpointer data)
{
	cache_manager_standard_clean_start(gd, data);
}

static void cache_manager_standard_process(GtkWidget *widget, gboolean clear)
{
	CacheOpsData *cd;
	const gchar *icon_name;
	const gchar *msg;

	cd = g_new0(CacheOpsData, 1);
	cd->clear = clear;
	cd->remote = FALSE;

	if (clear)
		{
		icon_name = GQ_ICON_DELETE;
		msg = _("Clearing thumbnails...");
		}
	else
		{
		icon_name = GQ_ICON_CLEAR;
		msg = _("Removing old thumbnails...");
		}

	cd->gd = generic_dialog_new(_("Maintenance"),
				    "standard_maintenance",
				    widget, FALSE,
				    nullptr, cd);
	cd->gd->cancel_cb = cache_manager_standard_clean_close_cb;
	cd->button_close = generic_dialog_add_button(cd->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_manager_standard_clean_close_cb, FALSE);
	cd->button_start = generic_dialog_add_button(cd->gd, GQ_ICON_OK, _("S_tart"),
						     cache_manager_standard_clean_start_cb, FALSE);
	cd->button_stop = generic_dialog_add_button(cd->gd, GQ_ICON_STOP, _("Stop"),
						    cache_manager_standard_clean_stop_cb, FALSE);
	gtk_widget_set_sensitive(cd->button_stop, FALSE);

	generic_dialog_add_message(cd->gd, icon_name, msg, nullptr, FALSE);

	cd->progress = gtk_progress_bar_new();
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(cd->progress), _("click start to begin"));
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(cd->progress), TRUE);
	gq_gtk_box_pack_start(GTK_BOX(cd->gd->vbox), cd->progress, FALSE, FALSE, 0);
	gtk_widget_show(cd->progress);

	cd->days = 30;
	cd->tl = nullptr;
	cd->idle_id = 0;

	gtk_widget_show(cd->gd->dialog);
}

void cache_manager_standard_process_remote(gboolean clear)
{
	CacheOpsData *cd;

	cd = g_new0(CacheOpsData, 1);
	cd->clear = clear;
	cd->days = 30;
	cd->tl = nullptr;
	cd->idle_id = 0;
	cd->remote = TRUE;

	cache_manager_standard_clean_start(nullptr, cd);
}

static void cache_manager_standard_clean_cb(GtkWidget *widget, gpointer)
{
	cache_manager_standard_process(widget, FALSE);
}

static void cache_manager_standard_clear_cb(GtkWidget *widget, gpointer)
{
	cache_manager_standard_process(widget, TRUE);
}


static void cache_manager_main_clean_cb(GtkWidget *widget, gpointer)
{
	cache_maintain_home(FALSE, FALSE, widget);
}


static void dummy_cancel_cb(GenericDialog *, gpointer)
{
	/* no op, only so cancel button appears */
}

static void cache_manager_main_clear_ok_cb(GenericDialog *, gpointer)
{
	cache_maintain_home(FALSE, TRUE, nullptr);
}

static void cache_manager_main_clear_confirm(GtkWidget *parent)
{
	GenericDialog *gd;

	gd = generic_dialog_new(_("Clear cache"),
				"clear_cache", parent, TRUE,
				dummy_cancel_cb, nullptr);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_QUESTION, _("Clear cache"),
				   _("This will remove all thumbnails and sim. files\nthat have been saved to disk, continue?"), TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", cache_manager_main_clear_ok_cb, TRUE);

	gtk_widget_show(gd->dialog);
}

static void cache_manager_main_clear_cb(GtkWidget *widget, gpointer)
{
	cache_manager_main_clear_confirm(widget);
}

static void cache_manager_render_cb(GtkWidget *widget, gpointer)
{
	const gchar *path = layout_get_path(nullptr);

	if (!path || !*path) path = homedir();
	cache_manager_render_dialog(widget, path);
}

static void cache_manager_metadata_clean_cb(GtkWidget *widget, gpointer)
{
	cache_maintain_home(TRUE, FALSE, widget);
}


static CacheManager *cache_manager = nullptr;

static void cache_manager_close_cb(GenericDialog *gd, gpointer)
{
	generic_dialog_close(gd);

	g_free(cache_manager);
	cache_manager = nullptr;
}

static void cache_manager_help_cb(GenericDialog *, gpointer)
{
	help_window_show("GuideReferenceManagement.html");
}

static GtkWidget *cache_manager_location_label(GtkWidget *group, const gchar *subdir)
{
	GtkWidget *label;

	g_autofree gchar *buf = g_strdup_printf(_("Location: %s"), subdir);
	label = pref_label_new(group, buf);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_yalign(GTK_LABEL(label), 0.5);

	return label;
}

static gboolean cache_manager_sim_file(CacheOpsData *cd);

static void cache_manager_sim_reset(CacheOpsData *cd)
{
	file_data_list_free(cd->list);
	cd->list = nullptr;

	file_data_list_free(cd->list_dir);
	cd->list_dir = nullptr;

	cache_loader_free(cd->cl);
	cd->cl = nullptr;
}

static void cache_manager_sim_close_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!gtk_widget_get_sensitive(cd->button_close)) return;

	cache_manager_sim_reset(cd);
	generic_dialog_close(cd->gd);
	g_free(cd);
}

static void cache_manager_sim_finish(CacheOpsData *cd)
{
	cache_manager_sim_reset(cd);
	if (!cd->remote)
		{
		gtk_spinner_stop(GTK_SPINNER(cd->spinner));

		gtk_widget_set_sensitive(cd->group, TRUE);
		gtk_widget_set_sensitive(cd->button_start, TRUE);
		gtk_widget_set_sensitive(cd->button_stop, FALSE);
		gtk_widget_set_sensitive(cd->button_close, TRUE);
		}
}

static void cache_manager_sim_stop_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("stopped"));
	cache_manager_sim_finish(cd);
}

static void cache_manager_sim_folder(CacheOpsData *cd, FileData *dir_fd)
{
	GList *list_d = nullptr;
	GList *list_f = nullptr;

	if (cd->recurse)
		{
		filelist_read(dir_fd, &list_f, &list_d);
		}
	else
		{
		filelist_read(dir_fd, &list_f, nullptr);
		}

	list_f = filelist_filter(list_f, FALSE);
	list_d = filelist_filter(list_d, TRUE);

	cd->list = g_list_concat(list_f, cd->list);
	cd->list_dir = g_list_concat(list_d, cd->list_dir);
}

static void cache_manager_sim_file_done_cb(CacheLoader *, gint, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	cache_loader_free(cd->cl);
	cd->cl = nullptr;

	while (cache_manager_sim_file(cd));
}

static void cache_manager_sim_start_sim_remote(GtkApplication *, CacheOpsData *cd, const gchar *user_path)
{
	g_autofree gchar *path = remove_trailing_slash(user_path);
	parse_out_relatives(path);

	if (!isdir(path))
		{
		log_printf("The specified folder can not be found: %s\n", path);
		}
	else
		{
		FileData *dir_fd;

		dir_fd = file_data_new_dir(path);
		cache_manager_sim_folder(cd, dir_fd);
		file_data_unref(dir_fd);
		while (cache_manager_sim_file(cd))
			;
		}
}

/**
 * @brief Generate .sim files
 * @param path Path to image folder
 * @param recurse
 * @param destroy_func Function called when idle loop function terminates
 *
 *
 */
static void cache_manager_sim_remote(GtkApplication *app, const gchar *path, gboolean recurse, GSourceFunc destroy_func)
{
	CacheOpsData *cd;

	cd = g_new0(CacheOpsData, 1);
	cd->recurse = recurse;
	cd->remote = TRUE;
	cd->destroy_func = destroy_func;
	cd->app = app;

	cache_manager_sim_start_sim_remote(app, cd, path);
}

static gboolean cache_manager_sim_file(CacheOpsData *cd)
{
	CacheDataType load_mask;

	if (cd->list)
		{
		FileData *fd;
		fd = static_cast<FileData *>(cd->list->data);
		cd->list = g_list_remove(cd->list, fd);

		load_mask = static_cast<CacheDataType>(CACHE_LOADER_DIMENSIONS | CACHE_LOADER_DATE | CACHE_LOADER_MD5SUM | CACHE_LOADER_SIMILARITY);
		cd->cl = cache_loader_new(fd, load_mask, (cache_manager_sim_file_done_cb), cd);

		if (!cd->remote)
			{
			gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), fd->path);
			}

		file_data_unref(fd);
		cd->count_done = cd->count_done + 1;
		if (!cd->remote)
			{
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->progress_bar), static_cast<gdouble>(cd->count_done) / cd->count_total);
			}

		return FALSE;
		}
	if (cd->list_dir)
		{
		FileData *fd;

		fd = static_cast<FileData *>(cd->list_dir->data);
		cd->list_dir = g_list_remove(cd->list_dir, fd);

		cache_manager_sim_folder(cd, fd);
		file_data_unref(fd);

		return TRUE;
		}

	if (!cd->remote)
		{
		gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("done"));
		}

	cache_manager_sim_finish(cd);

	if (cd->destroy_func)
		{
		g_idle_add(cd->destroy_func, cd);
		}

	return FALSE;
}

static void cache_manager_sim_start_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);
	GList *list_total = nullptr;

	if (!cd->remote)
		{
		if (cd->list || !gtk_widget_get_sensitive(cd->button_start)) return;
		}

	g_autofree gchar *path = remove_trailing_slash((gq_gtk_entry_get_text(GTK_ENTRY(cd->entry))));
	parse_out_relatives(path);

	if (!isdir(path))
		{
		if (!cd->remote)
			{
			warning_dialog(_("Invalid folder"),
			_("The specified folder can not be found."),
			GQ_ICON_DIALOG_WARNING, cd->gd->dialog);
			}
		else
			{
			log_printf("The specified folder can not be found: %s\n", path);
			}
		}
	else
		{
		FileData *dir_fd;
		if(!cd->remote)
			{
			gtk_widget_set_sensitive(cd->group, FALSE);
			gtk_widget_set_sensitive(cd->button_start, FALSE);
			gtk_widget_set_sensitive(cd->button_stop, TRUE);
			gtk_widget_set_sensitive(cd->button_close, FALSE);

			gtk_spinner_start(GTK_SPINNER(cd->spinner));
			}
		dir_fd = file_data_new_dir(path);
		cache_manager_sim_folder(cd, dir_fd);
		list_total = filelist_recursive(dir_fd);
		cd->count_total = g_list_length(list_total);
		file_data_unref(dir_fd);
		g_list_free(list_total);
		cd->count_done = 0;

		while (cache_manager_sim_file(cd))
			;
		}
}

static void cache_manager_sim_load_dialog(GtkWidget *widget, const gchar *path)
{
	CacheOpsData *cd;
	GtkWidget *hbox;
	GtkWidget *label;

	cd = g_new0(CacheOpsData, 1);
	cd->remote = FALSE;
	cd->recurse = TRUE;

	cd->gd = generic_dialog_new(_("Create sim. files"), "create_sim_files", widget, FALSE, nullptr, cd);
	gtk_window_set_default_size(GTK_WINDOW(cd->gd->dialog), PURGE_DIALOG_WIDTH, -1);
	cd->gd->cancel_cb = cache_manager_sim_close_cb;
	cd->button_close = generic_dialog_add_button(cd->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_manager_sim_close_cb, FALSE);
	cd->button_start = generic_dialog_add_button(cd->gd, GQ_ICON_OK, _("S_tart"),
						     cache_manager_sim_start_cb, FALSE);
	cd->button_stop = generic_dialog_add_button(cd->gd, GQ_ICON_STOP, _("Stop"),
						    cache_manager_sim_stop_cb, FALSE);
	gtk_widget_set_sensitive(cd->button_stop, FALSE);

	generic_dialog_add_message(cd->gd, nullptr, _("Create sim. files recursively"), nullptr, FALSE);

	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);
	cd->group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(cd->group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	label = tab_completion_new(&cd->entry, path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(cd->entry,_("Select folder") , TRUE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);

	pref_line(cd->gd->vbox, PREF_PAD_SPACE);
	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	cd->progress = gtk_entry_new();
	gtk_widget_set_can_focus(cd->progress, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(cd->progress), FALSE);
	gq_gtk_entry_set_text(GTK_ENTRY(cd->progress), _("click start to begin"));
	gq_gtk_box_pack_start(GTK_BOX(hbox), cd->progress, TRUE, TRUE, 0);
	gtk_widget_show(cd->progress);

	cd->progress_bar = gtk_progress_bar_new();
	gq_gtk_box_pack_start(GTK_BOX(cd->gd->vbox), cd->progress_bar, TRUE, TRUE, 0);
	gtk_widget_show(cd->progress_bar);

	cd->spinner = gtk_spinner_new();
	gq_gtk_box_pack_start(GTK_BOX(hbox), cd->spinner, FALSE, FALSE, 0);
	gtk_widget_show(cd->spinner);

	cd->list = nullptr;

	gtk_widget_show(cd->gd->dialog);
}

static void cache_manager_sim_load_cb(GtkWidget *widget, gpointer)
{
	const gchar *path = layout_get_path(nullptr);

	if (!path || !*path) path = homedir();
	cache_manager_sim_load_dialog(widget, path);
}

static void cache_manager_cache_maintenance_close_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!gtk_widget_get_sensitive(cd->button_close)) return;

	cache_manager_sim_reset(cd);
	generic_dialog_close(cd->gd);
	g_free(cd);
}

static void cache_manager_cache_maintenance_start_cb(GenericDialog *, gpointer data)
{
	auto cd = static_cast<CacheOpsData *>(data);

	if (!cd->remote)
		{
		if (cd->list || !gtk_widget_get_sensitive(cd->button_start)) return;
		}

	g_autofree gchar *path = remove_trailing_slash((gq_gtk_entry_get_text(GTK_ENTRY(cd->entry))));
	parse_out_relatives(path);

	if (!isdir(path))
		{
		if (!cd->remote)
			{
			warning_dialog(_("Invalid folder"),
			_("The specified folder can not be found."),
			GQ_ICON_DIALOG_WARNING, cd->gd->dialog);
			}
		else
			{
			log_printf("The specified folder can not be found: \"%s\"\n", path);
			}
		}
	else
		{
		g_autofree gchar *cmd_line = g_strdup_printf("%s --cache-maintenance=\"%s\"", gq_executable_path, path);

		g_spawn_command_line_async(cmd_line, nullptr);

		generic_dialog_close(cd->gd);
		cache_manager_sim_reset(cd);
		g_free(cd);
		}
}

static void cache_manager_cache_maintenance_load_dialog(GtkWidget *widget, const gchar *path)
{
	CacheOpsData *cd;
	GtkWidget *hbox;
	GtkWidget *label;

	cd = g_new0(CacheOpsData, 1);
	cd->remote = FALSE;
	cd->recurse = TRUE;

	cd->gd = generic_dialog_new(_("Background cache maintenance"), "background_cache_maintenance", widget, FALSE, nullptr, cd);
	gtk_window_set_default_size(GTK_WINDOW(cd->gd->dialog), PURGE_DIALOG_WIDTH, -1);
	cd->gd->cancel_cb = cache_manager_cache_maintenance_close_cb;
	cd->button_close = generic_dialog_add_button(cd->gd, GQ_ICON_CLOSE, _("Close"),
						     cache_manager_cache_maintenance_close_cb, FALSE);
	cd->button_start = generic_dialog_add_button(cd->gd, GQ_ICON_OK, _("S_tart"),
						     cache_manager_cache_maintenance_start_cb, FALSE);

	generic_dialog_add_message(cd->gd, nullptr, _("Recursively delete orphaned thumbnails\nand .sim files, and create new\nthumbnails and .sim files"), nullptr, FALSE);

	hbox = pref_box_new(cd->gd->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	pref_spacer(hbox, PREF_PAD_INDENT);
	cd->group = pref_box_new(hbox, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(cd->group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(hbox, _("Folder:"));

	label = tab_completion_new(&cd->entry, path, nullptr, nullptr, nullptr, nullptr);
	tab_completion_add_select_button(cd->entry,_("Select folder") , TRUE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
	gtk_widget_show(label);

	cd->list = nullptr;

	gtk_widget_show(cd->gd->dialog);
}

static void cache_manager_cache_maintenance_load_cb(GtkWidget *widget, gpointer)
{
	const gchar *path = layout_get_path(nullptr);

	if (!path || !*path) path = homedir();
	cache_manager_cache_maintenance_load_dialog(widget, path);
}

void cache_manager_show()
{
	GenericDialog *gd;
	GtkWidget *group;
	GtkWidget *button;
	GtkWidget *table;
	GtkSizeGroup *sizegroup;

	if (cache_manager)
		{
		gtk_window_present(GTK_WINDOW(cache_manager->dialog->dialog));
		return;
		}

	cache_manager = g_new0(CacheManager, 1);

	cache_manager->dialog = generic_dialog_new(_("Cache Maintenance"),
						   "cache_manager",
						   nullptr, FALSE,
						   nullptr, cache_manager);
	gd = cache_manager->dialog;

	gd->cancel_cb = cache_manager_close_cb;
	generic_dialog_add_button(gd, GQ_ICON_CLOSE, _("Close"),
				  cache_manager_close_cb, FALSE);
	generic_dialog_add_button(gd, GQ_ICON_HELP, _("Help"),
				  cache_manager_help_cb, FALSE);

	generic_dialog_add_message(gd, nullptr, _("Cache and Data Maintenance"), nullptr, FALSE);

	sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	group = pref_group_new(gd->vbox, FALSE, _("Geeqie thumbnail and sim. cache"), GTK_ORIENTATION_VERTICAL);

	cache_manager_location_label(group, get_thumbnails_cache_dir());

	table = pref_table_new(group, 2, 2, FALSE, FALSE);

	button = pref_table_button(table, 0, 0, GQ_ICON_CLEAR, _("Clean up"),
				   G_CALLBACK(cache_manager_main_clean_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 0, _("Remove orphaned or outdated thumbnails and sim. files."), GTK_ALIGN_START);

	button = pref_table_button(table, 0, 1, GQ_ICON_DELETE, _("Clear cache"),
				   G_CALLBACK(cache_manager_main_clear_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 1, _("Delete all cached data."), GTK_ALIGN_START);


	group = pref_group_new(gd->vbox, FALSE, _("Shared thumbnail cache"), GTK_ORIENTATION_VERTICAL);

	g_autofree gchar *path = g_build_filename(get_thumbnails_standard_cache_dir(), NULL);
	cache_manager_location_label(group, path);

	table = pref_table_new(group, 2, 2, FALSE, FALSE);

	button = pref_table_button(table, 0, 0, GQ_ICON_CLEAR, _("Clean up"),
				   G_CALLBACK(cache_manager_standard_clean_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 0, _("Remove orphaned or outdated thumbnails."), GTK_ALIGN_START);

	button = pref_table_button(table, 0, 1, GQ_ICON_DELETE, _("Clear cache"),
				   G_CALLBACK(cache_manager_standard_clear_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 1, _("Delete all cached thumbnails."), GTK_ALIGN_START);

	group = pref_group_new(gd->vbox, FALSE, _("Create thumbnails"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 2, 1, FALSE, FALSE);

	button = pref_table_button(table, 0, 1, GQ_ICON_RUN, _("Render"),
				   G_CALLBACK(cache_manager_render_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 1, _("Render thumbnails for a specific folder."), GTK_ALIGN_START);
	gtk_widget_set_sensitive(group, options->thumbnails.enable_caching);

	group = pref_group_new(gd->vbox, FALSE, _("File similarity cache"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 3, 2, FALSE, FALSE);

	button = pref_table_button(table, 0, 0, GQ_ICON_RUN, _("Create"),
				   G_CALLBACK(cache_manager_sim_load_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 0, _("Create sim. files recursively."), GTK_ALIGN_START);
	gtk_widget_set_sensitive(group, options->thumbnails.enable_caching);

	group = pref_group_new(gd->vbox, FALSE, _("Metadata"), GTK_ORIENTATION_VERTICAL);

	cache_manager_location_label(group, get_metadata_cache_dir());

	table = pref_table_new(group, 2, 1, FALSE, FALSE);

	button = pref_table_button(table, 0, 0, GQ_ICON_CLEAR, _("Clean up"),
				   G_CALLBACK(cache_manager_metadata_clean_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 0, _("Remove orphaned keywords and comments."), GTK_ALIGN_START);

	group = pref_group_new(gd->vbox, FALSE, _("Background cache maintenance"), GTK_ORIENTATION_VERTICAL);

	table = pref_table_new(group, 3, 2, FALSE, FALSE);

	button = pref_table_button(table, 0, 0, GQ_ICON_RUN, _("Select"),
				   G_CALLBACK(cache_manager_cache_maintenance_load_cb), cache_manager);
	gtk_size_group_add_widget(sizegroup, button);
	pref_table_label(table, 1, 0, _("Run cache maintenance as a background job."), GTK_ALIGN_START);
	gtk_widget_set_sensitive(group, options->thumbnails.enable_caching);

	/* @FIXME This feature does not work. The command line option must be used */
	gtk_widget_set_sensitive(group, FALSE);
	gtk_widget_set_tooltip_text(button, _("Feature disabled in this version.\nUse command line:\nGQ_CACHE_MAINTENANCE=  geeqie --cache-maintenance=<FOLDER>"));

	gtk_widget_show(cache_manager->dialog->dialog);
}

void cache_maintenance_notification(GtkApplication *app, const gchar *message, gboolean show_quit_button)
{
	GIcon *geeqie_icon;
	GNotification *notification;

	notification = g_notification_new("Geeqie");
	geeqie_icon = g_themed_icon_new(PIXBUF_INLINE_ICON);

	g_notification_set_body(notification, message);
	g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_NORMAL);
	g_notification_set_title(notification, _("Cache Maintenance"));

	if (show_quit_button)
		{
		g_notification_add_button(notification, _("Quit"), "app.quit");
		}

	g_application_send_notification(G_APPLICATION(app), "cache_maintenance", notification);

	g_object_unref(geeqie_icon);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
