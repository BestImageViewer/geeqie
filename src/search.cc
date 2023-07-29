/*
 * Copyright (C) 2005 John Ellis
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
#include "search.h"

#include "bar-keywords.h"
#include "cache.h"
#include "collect-table.h"
#include "dnd.h"
#include "dupe.h"
#include "editors.h"
#include "filedata.h"
#include "image-load.h"
#include "img-view.h"
#include "layout-util.h"
#include "menu.h"
#include "metadata.h"
#include "misc.h"
#include "pixbuf-util.h"
#include "print.h"
#include "thumb.h"
#include "ui-bookmark.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-spinner.h"
#include "ui-tabcomp.h"
#include "ui-tree-edit.h"
#include "uri-utils.h"
#include "utilops.h"
#include "window.h"
#include <cmath>

#define DEF_SEARCH_WIDTH  700
#define DEF_SEARCH_HEIGHT 650

#define SEARCH_BUFFER_MATCH_LOAD 20
#define SEARCH_BUFFER_MATCH_HIT  5
#define SEARCH_BUFFER_MATCH_MISS 1
#define SEARCH_BUFFER_FLUSH_SIZE 99

#define FORMAT_CLASS_BROKEN static_cast<FileFormatClass>(FILE_FORMAT_CLASSES + 1)

enum MatchType {
	SEARCH_MATCH_NONE,
	SEARCH_MATCH_EQUAL,
	SEARCH_MATCH_CONTAINS,
	SEARCH_MATCH_NAME_EQUAL,
	SEARCH_MATCH_NAME_CONTAINS,
	SEARCH_MATCH_PATH_CONTAINS,
	SEARCH_MATCH_UNDER,
	SEARCH_MATCH_OVER,
	SEARCH_MATCH_BETWEEN,
	SEARCH_MATCH_ALL,
	SEARCH_MATCH_ANY,
	SEARCH_MATCH_COLLECTION
};

enum {
	SEARCH_COLUMN_POINTER = 0,
	SEARCH_COLUMN_RANK,
	SEARCH_COLUMN_THUMB,
	SEARCH_COLUMN_NAME,
	SEARCH_COLUMN_SIZE,
	SEARCH_COLUMN_DATE,
	SEARCH_COLUMN_DIMENSIONS,
	SEARCH_COLUMN_PATH,
	SEARCH_COLUMN_COUNT	/* total columns */
};

struct SearchData
{
	GtkWidget *window;

	GtkWidget *button_thumbs;
	GtkWidget *label_status;
	GtkWidget *label_progress;
	GtkWidget *button_start;
	GtkWidget *button_stop;
	GtkWidget *button_close;
	GtkWidget *button_help;
	GtkWidget *spinner;

	GtkWidget *collection;
	GtkWidget *fd_button;
	GtkWidget *collection_entry;

	GtkWidget *box_search;

	GtkWidget *menu_path;
	GtkWidget *path_entry;
	GtkWidget *check_recurse;

	GtkWidget *result_view;

	GtkWidget *check_name;
	GtkWidget *menu_name;
	GtkWidget *entry_name;
	GtkWidget *check_name_match_case;

	GtkWidget *check_size;
	GtkWidget *menu_size;
	GtkWidget *spin_size;
	GtkWidget *spin_size_end;

	GtkWidget *check_date;
	GtkWidget *menu_date;
	GtkWidget *date_sel;
	GtkWidget *date_sel_end;
	GtkWidget *date_type;

	GtkWidget *check_dimensions;
	GtkWidget *menu_dimensions;
	GtkWidget *spin_width;
	GtkWidget *spin_height;
	GtkWidget *spin_width_end;
	GtkWidget *spin_height_end;

	GtkWidget *check_similarity;
	GtkWidget *spin_similarity;
	GtkWidget *entry_similarity;

	GtkWidget *check_keywords;
	GtkWidget *menu_keywords;
	GtkWidget *entry_keywords;

	GtkWidget *check_comment;
	GtkWidget *menu_comment;
	GtkWidget *entry_comment;

	GtkWidget *check_rating;
	GtkWidget *menu_rating;
	GtkWidget *spin_rating;
	GtkWidget *spin_rating_end;

	GtkWidget *check_class;
	GtkWidget *menu_class;
	GtkWidget *class_type;
	GtkWidget *marks_type;
	GtkWidget *menu_marks;

	FileData *search_dir_fd;
	gboolean   search_path_recurse;
	gchar *search_name;
	GRegex *search_name_regex;
	gboolean   search_name_match_case;
	gint64 search_size;
	gint64 search_size_end;
	gint   search_date_y;
	gint   search_date_m;
	gint   search_date_d;
	gint   search_date_end_y;
	gint   search_date_end_m;
	gint   search_date_end_d;
	gint   search_width;
	gint   search_height;
	gint   search_width_end;
	gint   search_height_end;
	gint   search_similarity;
	gchar *search_similarity_path;
	CacheData *search_similarity_cd;
	GList *search_keyword_list;
	gchar *search_comment;
	GRegex *search_comment_regex;
	gint   search_rating;
	gint   search_rating_end;
	gboolean   search_comment_match_case;

	MatchType search_type;

	MatchType match_name;
	MatchType match_size;
	MatchType match_date;
	MatchType match_dimensions;
	MatchType match_keywords;
	MatchType match_comment;
	MatchType match_rating;
	MatchType match_gps;
	MatchType match_class;
	MatchType match_marks;

	gboolean match_name_enable;
	gboolean match_size_enable;
	gboolean match_date_enable;
	gboolean match_dimensions_enable;
	gboolean match_similarity_enable;
	gboolean match_keywords_enable;
	gboolean match_comment_enable;
	gboolean match_rating_enable;
	gboolean match_class_enable;
	gboolean match_marks_enable;
	gboolean match_broken_enable;

	GList *search_folder_list;
	GList *search_done_list;
	GList *search_file_list;
	GList *search_buffer_list;

	gint search_count;
	gint search_total;
	gint search_buffer_count;

	guint search_idle_id; /* event source id */
	guint update_idle_id; /* event source id */

	ImageLoader *img_loader;
	CacheData   *img_cd;

	FileData *click_fd;

	ThumbLoader *thumb_loader;
	gboolean thumb_enable;
	FileData *thumb_fd;

	/* Used for lat/long coordinate search
	*/
	gint search_gps;
	gdouble search_lat, search_lon;
	GtkWidget *entry_gps_coord;
	GtkWidget *check_gps;
	GtkWidget *spin_gps;
	GtkWidget *units_gps;
	GtkWidget *menu_gps;
	gboolean match_gps_enable;

};

struct MatchFileData
{
	FileData *fd;
	gint width;
	gint height;
	gint rank;
};

struct MatchList
{
	const gchar *text;
	MatchType type;
};

static const MatchList text_search_menu_path[] = {
	{ N_("folder"),		SEARCH_MATCH_NONE },
	{ N_("comments"),	SEARCH_MATCH_ALL },
	{ N_("results"),	SEARCH_MATCH_CONTAINS },
	{ N_("collection"),	SEARCH_MATCH_COLLECTION }
};

static const MatchList text_search_menu_name[] = {
	{ N_("name contains"),	SEARCH_MATCH_NAME_CONTAINS },
	{ N_("name is"),	SEARCH_MATCH_NAME_EQUAL },
	{ N_("path contains"),	SEARCH_MATCH_PATH_CONTAINS }
};

static const MatchList text_search_menu_size[] = {
	{ N_("equal to"),	SEARCH_MATCH_EQUAL },
	{ N_("less than"),	SEARCH_MATCH_UNDER },
	{ N_("greater than"),	SEARCH_MATCH_OVER },
	{ N_("between"),	SEARCH_MATCH_BETWEEN }
};

static const MatchList text_search_menu_date[] = {
	{ N_("equal to"),	SEARCH_MATCH_EQUAL },
	{ N_("before"),		SEARCH_MATCH_UNDER },
	{ N_("after"),		SEARCH_MATCH_OVER },
	{ N_("between"),	SEARCH_MATCH_BETWEEN }
};

static const MatchList text_search_menu_keyword[] = {
	{ N_("match all"),	SEARCH_MATCH_ALL },
	{ N_("match any"),	SEARCH_MATCH_ANY },
	{ N_("exclude"),	SEARCH_MATCH_NONE }
};

static const MatchList text_search_menu_comment[] = {
	{ N_("contains"),	SEARCH_MATCH_CONTAINS },
	{ N_("miss"),		SEARCH_MATCH_NONE }
};


static const MatchList text_search_menu_rating[] = {
	{ N_("equal to"),	SEARCH_MATCH_EQUAL },
	{ N_("less than"),	SEARCH_MATCH_UNDER },
	{ N_("greater than"),	SEARCH_MATCH_OVER },
	{ N_("between"),	SEARCH_MATCH_BETWEEN }
};

static const MatchList text_search_menu_gps[] = {
	{ N_("not geocoded"),	SEARCH_MATCH_NONE },
	{ N_("less than"),	SEARCH_MATCH_UNDER },
	{ N_("greater than"),	SEARCH_MATCH_OVER }
};

static const MatchList text_search_menu_class[] = {
	{ N_("is"),	SEARCH_MATCH_EQUAL },
	{ N_("is not"),	SEARCH_MATCH_NONE }
};

static const MatchList text_search_menu_marks[] = {
	{ N_("is"),	SEARCH_MATCH_EQUAL },
	{ N_("is not"),	SEARCH_MATCH_NONE }
};

static GList *search_window_list = nullptr;


static gint search_result_selection_count(SearchData *sd, gint64 *bytes);
static gint search_result_count(SearchData *sd, gint64 *bytes);

static void search_window_close(SearchData *sd);

static void search_notify_cb(FileData *fd, NotifyType type, gpointer data);
static void search_start_cb(GtkWidget *widget, gpointer data);
void mfd_list_free(GList *list);


/**
 * This array must be kept in sync with the contents of:\n
 * @link search_result_press_cb @endlink \n
 * @link search_window_keypress_cb @endlink \n
 * @link search_result_menu @endlink
 *
 * See also @link hard_coded_window_keys @endlink
 **/

hard_coded_window_keys search_window_keys[] = {
	{GDK_CONTROL_MASK, 'C', N_("Copy")},
	{GDK_CONTROL_MASK, 'M', N_("Move")},
	{GDK_CONTROL_MASK, 'R', N_("Rename")},
	{GDK_CONTROL_MASK, 'D', N_("Move to Trash")},
	{GDK_SHIFT_MASK, GDK_KEY_Delete, N_("Delete")},
	{static_cast<GdkModifierType>(0), GDK_KEY_Delete, N_("Remove")},
	{GDK_CONTROL_MASK, 'A', N_("Select all")},
	{static_cast<GdkModifierType>(GDK_CONTROL_MASK + GDK_SHIFT_MASK), 'A', N_("Select none")},
	{GDK_CONTROL_MASK, GDK_KEY_Delete, N_("Clear")},
	{GDK_CONTROL_MASK, 'T', N_("Toggle thumbs")},
	{GDK_CONTROL_MASK, 'W', N_("Close window")},
	{static_cast<GdkModifierType>(0), GDK_KEY_Return, N_("View")},
	{static_cast<GdkModifierType>(0), 'V', N_("View in new window")},
	{static_cast<GdkModifierType>(0), 'C', N_("Collection from selection")},
	{GDK_CONTROL_MASK, GDK_KEY_Return, N_("Start/stop search")},
	{static_cast<GdkModifierType>(0), GDK_KEY_F3, N_("Find duplicates")},
	{static_cast<GdkModifierType>(0), 0, nullptr}
};
/*
 *-------------------------------------------------------------------
 * utils
 *-------------------------------------------------------------------
 */

static time_t convert_dmy_to_time(gint day, gint month, gint year)
{
	struct tm lt;

	lt.tm_sec = 0;
	lt.tm_min = 0;
	lt.tm_hour = 0;
	lt.tm_mday = day;
	lt.tm_mon = month - 1;
	lt.tm_year = year - 1900;
	lt.tm_isdst = 0;

	return mktime(&lt);
}

static void search_status_update(SearchData *sd)
{
	gchar *buf;
	gint t;
	gint s;
	gint64 t_bytes;
	gint64 s_bytes;
	gchar *tt;

	t = search_result_count(sd, &t_bytes);
	s = search_result_selection_count(sd, &s_bytes);

	tt = text_from_size_abrev(t_bytes);

	if (s > 0)
		{
		gchar *ts = text_from_size_abrev(s_bytes);
		buf = g_strdup_printf(_("%s, %d files (%s, %d)"), tt, t, ts, s);
		g_free(ts);
		}
	else
		{
		buf = g_strdup_printf(_("%s, %d files"), tt, t);
		}

	g_free(tt);

	gtk_label_set_text(GTK_LABEL(sd->label_status), buf);
	g_free(buf);
}

static void search_progress_update(SearchData *sd, gboolean search, gdouble thumbs)
{

	if (search || thumbs >= 0.0)
		{
		gchar *buf;
		const gchar *message;

		if (search && (sd->search_folder_list || sd->search_file_list))
			message = _("Searching...");
		else if (thumbs >= 0.0)
			message = _("Loading thumbs...");
		else
			message = "";

		buf = g_strdup_printf("%s(%d / %d)", message, sd->search_count, sd->search_total);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(sd->label_progress), buf);
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(sd->label_progress),
					      (thumbs >= 0.0) ? thumbs : 0.0);
		g_free(buf);
		}
	else
		{
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(sd->label_progress), "");
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(sd->label_progress), 0.0);
		}
}

/*
 *-------------------------------------------------------------------
 * result list
 *-------------------------------------------------------------------
 */

static gint search_result_find_row(SearchData *sd, FileData *fd, GtkTreeIter *iter)
{
	GtkTreeModel *store;
	gboolean valid;
	gint n = 0;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view));
	valid = gtk_tree_model_get_iter_first(store, iter);
	while (valid)
		{
		MatchFileData *mfd;
		n++;

		gtk_tree_model_get(store, iter, SEARCH_COLUMN_POINTER, &mfd, -1);
		if (mfd->fd == fd) return n;
		valid = gtk_tree_model_iter_next(store, iter);
		}

	return -1;
}


static gboolean search_result_row_selected(SearchData *sd, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	GList *work;
	gboolean found = FALSE;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sd->result_view));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (!found && work)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		MatchFileData *mfd_n;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd_n, -1);
		if (mfd_n->fd == fd) found = TRUE;
		work = work->next;
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	return found;
}

static gint search_result_selection_util(SearchData *sd, gint64 *bytes, GList **list)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	GList *work;
	gint n = 0;
	gint64 total = 0;
	GList *plist = nullptr;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view));
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sd->result_view));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (work)
		{
		n++;

		if (bytes || list)
			{
			auto tpath = static_cast<GtkTreePath *>(work->data);
			MatchFileData *mfd;
			GtkTreeIter iter;

			gtk_tree_model_get_iter(store, &iter, tpath);
			gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd, -1);
			total += mfd->fd->size;

			if (list) plist = g_list_prepend(plist, file_data_ref(mfd->fd));
			}

		work = work->next;
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	if (bytes) *bytes = total;
	if (list) *list = g_list_reverse(plist);

	return n;
}

static GList *search_result_selection_list(SearchData *sd)
{
	GList *list;

	search_result_selection_util(sd, nullptr, &list);
	return list;
}

static gint search_result_selection_count(SearchData *sd, gint64 *bytes)
{
	return search_result_selection_util(sd, bytes, nullptr);
}

static gint search_result_util(SearchData *sd, gint64 *bytes, GList **list)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	gboolean valid;
	gint n = 0;
	gint64 total = 0;
	GList *plist = nullptr;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view));

	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		n++;
		if (bytes || list)
			{
			MatchFileData *mfd;

			gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd, -1);
			total += mfd->fd->size;

			if (list) plist = g_list_prepend(plist, file_data_ref(mfd->fd));
			}
		valid = gtk_tree_model_iter_next(store, &iter);
		}

	if (bytes) *bytes = total;
	if (list) *list = g_list_reverse(plist);

	return n;
}

static GList *search_result_get_filelist(SearchData *sd)
{
	GList *list = nullptr;

	search_result_util(sd, nullptr, &list);
	return list;
}

static gint search_result_count(SearchData *sd, gint64 *bytes)
{
	return search_result_util(sd, bytes, nullptr);
}

static void search_result_append(SearchData *sd, MatchFileData *mfd)
{
	FileData *fd;
	GtkListStore *store;
	GtkTreeIter iter;
	gchar *text_size;
	gchar *text_dim = nullptr;

	fd = mfd->fd;

	if (!fd) return;

	text_size = text_from_size(fd->size);
	if (mfd->width > 0 && mfd->height > 0) text_dim = g_strdup_printf("%d x %d", mfd->width, mfd->height);

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view)));
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter,
				SEARCH_COLUMN_POINTER, mfd,
				SEARCH_COLUMN_RANK, mfd->rank,
				SEARCH_COLUMN_THUMB, fd->thumb_pixbuf,
				SEARCH_COLUMN_NAME, fd->name,
				SEARCH_COLUMN_SIZE, text_size,
				SEARCH_COLUMN_DATE, text_from_time(fd->date),
				SEARCH_COLUMN_DIMENSIONS, text_dim,
				SEARCH_COLUMN_PATH, fd->path,
				-1);

	g_free(text_size);
	g_free(text_dim);
}

static GList *search_result_refine_list(SearchData *sd)
{
	GList *list = nullptr;
	GtkTreeModel *store;
	GtkTreeIter iter;
	gboolean valid;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view));

	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		MatchFileData *mfd;

		gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd, -1);
		list = g_list_prepend(list, mfd->fd);

		valid = gtk_tree_model_iter_next(store, &iter);
		}

	/* clear it here, so that the FileData in list is not freed */
	gtk_list_store_clear(GTK_LIST_STORE(store));

	return g_list_reverse(list);
}

static gboolean search_result_free_node(GtkTreeModel *store, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	MatchFileData *mfd;

	gtk_tree_model_get(store, iter, SEARCH_COLUMN_POINTER, &mfd, -1);
	file_data_unref(mfd->fd);
	g_free(mfd);

	return FALSE;
}

static void search_result_clear(SearchData *sd)
{
	GtkListStore *store;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view)));

	gtk_tree_model_foreach(GTK_TREE_MODEL(store), search_result_free_node, sd);
	gtk_list_store_clear(store);

	sd->click_fd = nullptr;

	thumb_loader_free(sd->thumb_loader);
	sd->thumb_loader = nullptr;
	sd->thumb_fd = nullptr;

	search_status_update(sd);
}

static void search_result_remove_item(SearchData *sd, MatchFileData *mfd, GtkTreeIter *iter)
{
	GtkTreeModel *store;

	if (!mfd || !iter) return;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view));

	tree_view_move_cursor_away(GTK_TREE_VIEW(sd->result_view), iter, TRUE);

	gtk_list_store_remove(GTK_LIST_STORE(store), iter);
	if (sd->click_fd == mfd->fd) sd->click_fd = nullptr;
	if (sd->thumb_fd == mfd->fd) sd->thumb_fd = nullptr;
	file_data_unref(mfd->fd);
	g_free(mfd);
}

static void search_result_remove(SearchData *sd, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	gboolean valid;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		MatchFileData *mfd;

		gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd, -1);
		if (mfd->fd == fd)
			{
			search_result_remove_item(sd, mfd, &iter);
			return;
			}

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		}
}

static void search_result_remove_selection(SearchData *sd)
{
	GtkTreeSelection *selection;
	GtkTreeModel *store;
	GList *slist;
	GList *flist = nullptr;
	GList *work;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sd->result_view));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (work)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		GtkTreeIter iter;
		MatchFileData *mfd;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd, -1);
		flist = g_list_prepend(flist, mfd->fd);
		work = work->next;
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	work = flist;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		search_result_remove(sd, fd);
		}
	g_list_free(flist);

	search_status_update(sd);
}

static void search_result_edit_selected(SearchData *sd, const gchar *key)
{
	file_util_start_editor_from_filelist(key, search_result_selection_list(sd), nullptr, sd->window);
}

static void search_result_collection_from_selection(SearchData *sd)
{
	CollectWindow *w;
	GList *list;

	list = search_result_selection_list(sd);
	w = collection_window_new(nullptr);
	collection_table_add_filelist(w->table, list);
	filelist_free(list);
}

static gboolean search_result_update_idle_cb(gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_status_update(sd);

	sd->update_idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void search_result_update_idle_cancel(SearchData *sd)
{
	if (sd->update_idle_id)
		{
		g_source_remove(sd->update_idle_id);
		sd->update_idle_id = 0;
		}
}

static gboolean search_result_select_cb(GtkTreeSelection *, GtkTreeModel *, GtkTreePath *, gboolean, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!sd->update_idle_id)
		{
		sd->update_idle_id = g_idle_add(search_result_update_idle_cb, sd);
		}

	return TRUE;
}

/*
 *-------------------------------------------------------------------
 * result list thumbs
 *-------------------------------------------------------------------
 */

static void search_result_thumb_step(SearchData *sd);


static void search_result_thumb_set(SearchData *sd, FileData *fd, GtkTreeIter *iter)
{
	GtkListStore *store;
	GtkTreeIter iter_n;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view)));
	if (!iter)
		{
		if (search_result_find_row(sd, fd, &iter_n) >= 0) iter = &iter_n;
		}

	if (iter) gtk_list_store_set(store, iter, SEARCH_COLUMN_THUMB, fd->thumb_pixbuf, -1);
}

static void search_result_thumb_do(SearchData *sd)
{
	FileData *fd;

	if (!sd->thumb_loader || !sd->thumb_fd) return;
	fd = sd->thumb_fd;

	search_result_thumb_set(sd, fd, nullptr);
}

static void search_result_thumb_done_cb(ThumbLoader *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_result_thumb_do(sd);
	search_result_thumb_step(sd);
}

static void search_result_thumb_step(SearchData *sd)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	MatchFileData *mfd = nullptr;
	gboolean valid;
	gint row = 0;
	gint length = 0;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	if (!sd->thumb_enable)
		{
		while (valid)
			{
			gtk_list_store_set(GTK_LIST_STORE(store), &iter, SEARCH_COLUMN_THUMB, NULL, -1);
			valid = gtk_tree_model_iter_next(store, &iter);
			}
		return;
		}

	while (!mfd && valid)
		{
		GdkPixbuf *pixbuf;

		length++;
		gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd, SEARCH_COLUMN_THUMB, &pixbuf, -1);
		if (pixbuf || mfd->fd->thumb_pixbuf)
			{
			if (!pixbuf) gtk_list_store_set(GTK_LIST_STORE(store), &iter, SEARCH_COLUMN_THUMB, mfd->fd->thumb_pixbuf, -1);
			row++;
			mfd = nullptr;
			}
		valid = gtk_tree_model_iter_next(store, &iter);
		}
	if (valid)
		{
		while (gtk_tree_model_iter_next(store, &iter)) length++;
		}

	if (!mfd)
		{
		sd->thumb_fd = nullptr;
		thumb_loader_free(sd->thumb_loader);
		sd->thumb_loader = nullptr;

		search_progress_update(sd, TRUE, -1.0);
		return;
		}

	search_progress_update(sd, FALSE, static_cast<gdouble>(row)/length);

	sd->thumb_fd = mfd->fd;
	thumb_loader_free(sd->thumb_loader);
	sd->thumb_loader = thumb_loader_new(options->thumbnails.max_width, options->thumbnails.max_height);

	thumb_loader_set_callbacks(sd->thumb_loader,
				   search_result_thumb_done_cb,
				   search_result_thumb_done_cb,
				   nullptr,
				   sd);
	if (!thumb_loader_start(sd->thumb_loader, mfd->fd))
		{
		search_result_thumb_do(sd);
		search_result_thumb_step(sd);
		}
}

static void search_result_thumb_height(SearchData *sd)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;
	GList *list;

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(sd->result_view), SEARCH_COLUMN_THUMB - 1);
	if (!column) return;

	gtk_tree_view_column_set_fixed_width(column, (sd->thumb_enable) ? options->thumbnails.max_width : 4);

	list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
	if (!list) return;
	cell = static_cast<GtkCellRenderer *>(list->data);
	g_list_free(list);

	g_object_set(G_OBJECT(cell), "height", (sd->thumb_enable) ? options->thumbnails.max_height : -1, NULL);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(sd->result_view));
}

static void search_result_thumb_enable(SearchData *sd, gboolean enable)
{
	GtkTreeViewColumn *column;

	if (sd->thumb_enable == enable) return;

	if (sd->thumb_enable)
		{
		GtkTreeModel *store;
		GtkTreeIter iter;
		gboolean valid;

		thumb_loader_free(sd->thumb_loader);
		sd->thumb_loader = nullptr;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view));
		valid = gtk_tree_model_get_iter_first(store, &iter);
		while (valid)
			{
			gtk_list_store_set(GTK_LIST_STORE(store), &iter, SEARCH_COLUMN_THUMB, NULL, -1);
			valid = gtk_tree_model_iter_next(store, &iter);
			}
		search_progress_update(sd, TRUE, -1.0);
		}

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(sd->result_view), SEARCH_COLUMN_THUMB - 1);
	if (column)
		{
		gtk_tree_view_column_set_visible(column, enable);
		}

	sd->thumb_enable = enable;

	search_result_thumb_height(sd);
	if (!sd->search_folder_list && !sd->search_file_list) search_result_thumb_step(sd);
}

/*
 *-------------------------------------------------------------------
 * result list menu
 *-------------------------------------------------------------------
 */

static void sr_menu_view_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (sd->click_fd) layout_set_fd(nullptr, sd->click_fd);
}

static void sr_menu_viewnew_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GList *list;

	list = search_result_selection_list(sd);
	view_window_new_from_list(list);
	filelist_free(list);
}

static void sr_menu_select_all_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sd->result_view));
	gtk_tree_selection_select_all(selection);
}

static void sr_menu_select_none_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sd->result_view));
	gtk_tree_selection_unselect_all(selection);
}

static void sr_menu_edit_cb(GtkWidget *widget, gpointer data)
{
	SearchData *sd;
	auto key = static_cast<const gchar *>(data);

	sd = static_cast<SearchData *>(submenu_item_get_data(widget));
	if (!sd) return;

	search_result_edit_selected(sd, key);
}

static void sr_menu_print_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	print_window_new(sd->click_fd, search_result_selection_list(sd),
			 search_result_get_filelist(sd), sd->window);
}

static void sr_menu_copy_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_copy(nullptr, search_result_selection_list(sd), nullptr, sd->window);
}

static void sr_menu_move_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_move(nullptr, search_result_selection_list(sd), nullptr, sd->window);
}

static void sr_menu_rename_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_rename(nullptr, search_result_selection_list(sd), sd->window);
}

static void sr_menu_delete_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	options->file_ops.safe_delete_enable = FALSE;
	file_util_delete(nullptr, search_result_selection_list(sd), sd->window);
}

static void sr_menu_move_to_trash_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	options->file_ops.safe_delete_enable = TRUE;
	file_util_delete(nullptr, search_result_selection_list(sd), sd->window);
}

static void sr_menu_copy_path_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_copy_path_list_to_clipboard(search_result_selection_list(sd), TRUE);
}

static void sr_menu_copy_path_unquoted_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_copy_path_list_to_clipboard(search_result_selection_list(sd), FALSE);
}

static void sr_menu_play_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	start_editor_from_file(options->image_l_click_video_editor, sd->click_fd);
}

static void search_result_menu_destroy_cb(GtkWidget *, gpointer data)
{
	auto editmenu_fd_list = static_cast<GList *>(data);

	filelist_free(editmenu_fd_list);
}

/**
 * @brief Add file selection list to a collection
 * @param[in] widget
 * @param[in] data Index to the collection list menu item selected, or -1 for new collection
 *
 *
 */
static void search_pop_menu_collections_cb(GtkWidget *widget, gpointer data)
{
	SearchData *sd;
	GList *selection_list;

	sd = static_cast<SearchData *>(submenu_item_get_data(widget));
	selection_list = search_result_selection_list(sd);
	pop_menu_collections(selection_list, data);

	filelist_free(selection_list);
}

static GtkWidget *search_result_menu(SearchData *sd, gboolean on_row, gboolean empty)
{
	GtkWidget *menu;
	GtkWidget *item;
	GList *editmenu_fd_list;
	gboolean video;
	GtkAccelGroup *accel_group;

	menu = popup_menu_short_lived();
	accel_group = gtk_accel_group_new();
	gtk_menu_set_accel_group(GTK_MENU(menu), accel_group);

	g_object_set_data(G_OBJECT(menu), "window_keys", search_window_keys);
	g_object_set_data(G_OBJECT(menu), "accel_group", accel_group);

	video = (on_row && sd->click_fd && sd->click_fd->format_class == FORMAT_CLASS_VIDEO);
	menu_item_add_icon_sensitive(menu, _("_Play"), GQ_ICON_PREV_PAGE , video,
			    G_CALLBACK(sr_menu_play_cb), sd);
	menu_item_add_divider(menu);

	menu_item_add_sensitive(menu, _("_View"), on_row,
				G_CALLBACK(sr_menu_view_cb), sd);
	menu_item_add_icon_sensitive(menu, _("View in _new window"), GQ_ICON_NEW, on_row,
				      G_CALLBACK(sr_menu_viewnew_cb), sd);
	menu_item_add_divider(menu);
	menu_item_add_sensitive(menu, _("Select all"), !empty,
				G_CALLBACK(sr_menu_select_all_cb), sd);
	menu_item_add_sensitive(menu, _("Select none"), !empty,
				G_CALLBACK(sr_menu_select_none_cb), sd);
	menu_item_add_divider(menu);

	editmenu_fd_list = search_result_selection_list(sd);
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(search_result_menu_destroy_cb), editmenu_fd_list);
	submenu_add_edit(menu, &item, G_CALLBACK(sr_menu_edit_cb), sd, editmenu_fd_list);
	if (!on_row) gtk_widget_set_sensitive(item, FALSE);

	submenu_add_collections(menu, &item,
				G_CALLBACK(search_pop_menu_collections_cb), sd);
	gtk_widget_set_sensitive(item, on_row);

	menu_item_add_icon_sensitive(menu, _("Print..."), GQ_ICON_PRINT, on_row,
				      G_CALLBACK(sr_menu_print_cb), sd);
	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("_Copy..."), GQ_ICON_COPY, on_row,
				      G_CALLBACK(sr_menu_copy_cb), sd);
	menu_item_add_sensitive(menu, _("_Move..."), on_row,
				G_CALLBACK(sr_menu_move_cb), sd);
	menu_item_add_sensitive(menu, _("_Rename..."), on_row,
				G_CALLBACK(sr_menu_rename_cb), sd);
	menu_item_add_sensitive(menu, _("_Copy path"), on_row,
				G_CALLBACK(sr_menu_copy_path_cb), sd);
	menu_item_add_sensitive(menu, _("_Copy path unquoted"), on_row,
				G_CALLBACK(sr_menu_copy_path_unquoted_cb), sd);

	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu,
				options->file_ops.confirm_move_to_trash ? _("Move to Trash...") :
					_("Move to Trash"), GQ_ICON_DELETE, on_row,
				G_CALLBACK(sr_menu_move_to_trash_cb), sd);
	menu_item_add_icon_sensitive(menu,
				options->file_ops.confirm_delete ? _("_Delete...") :
					_("_Delete"), GQ_ICON_DELETE_SHRED, on_row,
				G_CALLBACK(sr_menu_delete_cb), sd);

	return menu;
}

/*
 *-------------------------------------------------------------------
 * result list input
 *-------------------------------------------------------------------
 */

static gboolean search_result_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	MatchFileData *mfd = nullptr;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, nullptr, nullptr, nullptr))
		{
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd, -1);
		gtk_tree_path_free(tpath);
		}

	sd->click_fd = mfd ? mfd->fd : nullptr;

	if (bevent->button == MOUSE_BUTTON_RIGHT)
		{
		GtkWidget *menu;

		menu = search_result_menu(sd, (mfd != nullptr), (search_result_count(sd, nullptr) == 0));
		gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
		}

	if (!mfd) return FALSE;

	if (bevent->button == MOUSE_BUTTON_LEFT && bevent->type == GDK_2BUTTON_PRESS)
		{
		layout_set_fd(nullptr, mfd->fd);
		}

	if (bevent->button == MOUSE_BUTTON_MIDDLE) return TRUE;

	if (bevent->button == MOUSE_BUTTON_RIGHT)
		{
		if (!search_result_row_selected(sd, mfd->fd))
			{
			GtkTreeSelection *selection;

			selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
			gtk_tree_selection_unselect_all(selection);
			gtk_tree_selection_select_iter(selection, &iter);

			tpath = gtk_tree_model_get_path(store, &iter);
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, nullptr, FALSE);
			gtk_tree_path_free(tpath);
			}
		return TRUE;
		}

	if (bevent->button == MOUSE_BUTTON_LEFT && bevent->type == GDK_BUTTON_PRESS &&
	    !(bevent->state & GDK_SHIFT_MASK ) &&
	    !(bevent->state & GDK_CONTROL_MASK ) &&
	    search_result_row_selected(sd, mfd->fd))
		{
		/* this selection handled on release_cb */
		gtk_widget_grab_focus(widget);
		return TRUE;
		}

	return FALSE;
}

static gboolean search_result_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreeIter iter;

	MatchFileData *mfd = nullptr;

	if (bevent->button != MOUSE_BUTTON_LEFT && bevent->button != MOUSE_BUTTON_MIDDLE) return TRUE;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));

	if ((bevent->x != 0 || bevent->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, nullptr, nullptr, nullptr))
		{
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd, -1);
		gtk_tree_path_free(tpath);
		}

	if (bevent->button == MOUSE_BUTTON_MIDDLE)
		{
		if (mfd && sd->click_fd == mfd->fd)
			{
			GtkTreeSelection *selection;

			selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
			if (search_result_row_selected(sd, mfd->fd))
				{
				gtk_tree_selection_unselect_iter(selection, &iter);
				}
			else
				{
				gtk_tree_selection_select_iter(selection, &iter);
				}
			}
		return TRUE;
		}

	if (mfd && sd->click_fd == mfd->fd &&
	    !(bevent->state & GDK_SHIFT_MASK ) &&
	    !(bevent->state & GDK_CONTROL_MASK ) &&
	    search_result_row_selected(sd, mfd->fd))
		{
		GtkTreeSelection *selection;

		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
		gtk_tree_selection_unselect_all(selection);
		gtk_tree_selection_select_iter(selection, &iter);

		tpath = gtk_tree_model_get_path(store, &iter);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, nullptr, FALSE);
		gtk_tree_path_free(tpath);

		return TRUE;
		}

	return FALSE;
}


static gboolean search_result_keypress_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	gboolean stop_signal = FALSE;
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	MatchFileData *mfd = nullptr;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sd->result_view));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	if (slist)
		{
		GtkTreePath *tpath;
		GtkTreeIter iter;
		GList *last;

		last = g_list_last(slist);
		tpath = static_cast<GtkTreePath *>(last->data);

		/* last is newest selected file */
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, SEARCH_COLUMN_POINTER, &mfd, -1);
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	if (event->state & GDK_CONTROL_MASK)
		{
		stop_signal = TRUE;
		switch (event->keyval)
			{
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
			case '0':
				break;
			case 'C': case 'c':
				file_util_copy(nullptr, search_result_selection_list(sd), nullptr, widget);
				break;
			case 'M': case 'm':
				file_util_move(nullptr, search_result_selection_list(sd), nullptr, widget);
				break;
			case 'R': case 'r':
				file_util_rename(nullptr, search_result_selection_list(sd), widget);
				break;
			case 'D': case 'd':
				options->file_ops.safe_delete_enable = TRUE;
				file_util_delete(nullptr, search_result_selection_list(sd), widget);
				break;
			case 'A': case 'a':
				if (event->state & GDK_SHIFT_MASK)
					{
					gtk_tree_selection_unselect_all(selection);
					}
				else
					{
					gtk_tree_selection_select_all(selection);
					}
				break;
			case GDK_KEY_Delete: case GDK_KEY_KP_Delete:
				search_result_clear(sd);
				break;
			default:
				stop_signal = FALSE;
				break;
			}
		}
	else
		{
		stop_signal = TRUE;
		switch (event->keyval)
			{
			case GDK_KEY_Return: case GDK_KEY_KP_Enter:
				if (mfd) layout_set_fd(nullptr, mfd->fd);
				break;
			case 'V': case 'v':
				{
				GList *list;

				list = search_result_selection_list(sd);
				view_window_new_from_list(list);
				filelist_free(list);
				}
				break;
			case GDK_KEY_Delete: case GDK_KEY_KP_Delete:
				search_result_remove_selection(sd);
				break;
			case 'C': case 'c':
				search_result_collection_from_selection(sd);
				break;
			case GDK_KEY_Menu:
			case GDK_KEY_F10:
				{
				GtkWidget *menu;

				if (mfd)
					sd->click_fd = mfd->fd;
				else
					sd->click_fd = nullptr;

				menu = search_result_menu(sd, (mfd != nullptr), (search_result_count(sd, nullptr) > 0));

				gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_EAST, GDK_GRAVITY_CENTER, nullptr);
				}
				break;
			default:
				stop_signal = FALSE;
				break;
			}
		}

	return stop_signal;
}

static gboolean search_window_keypress_cb(GtkWidget *, GdkEventKey *event, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	gboolean stop_signal = FALSE;

	if (event->state & GDK_CONTROL_MASK)
		{
		stop_signal = TRUE;
		switch (event->keyval)
			{
			case 'T': case 't':
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sd->button_thumbs),
					!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sd->button_thumbs)));
				break;
			case 'W': case 'w':
				search_window_close(sd);
				break;
			case GDK_KEY_Return: case GDK_KEY_KP_Enter:
				search_start_cb(nullptr, sd);
				break;
			default:
				stop_signal = FALSE;
				break;
			}
		}
	if (!stop_signal && is_help_key(event))
		{
		help_window_show("GuideImageSearchSearch.html");
		stop_signal = TRUE;
		}

	return stop_signal;
}

/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */

static GtkTargetEntry result_drag_types[] = {
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
};
static gint n_result_drag_types = 2;

static GtkTargetEntry result_drop_types[] = {
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
};
static gint n_result_drop_types = 2;

static void search_dnd_data_set(GtkWidget *, GdkDragContext *,
				GtkSelectionData *selection_data, guint,
				guint, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GList *list;

	list = search_result_selection_list(sd);
	if (!list) return;

	uri_selection_data_set_uris_from_filelist(selection_data, list);
	filelist_free(list);
}

static void search_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (sd->click_fd && !search_result_row_selected(sd, sd->click_fd))
		{
		GtkListStore *store;
		GtkTreeIter iter;

		store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));
		if (search_result_find_row(sd, sd->click_fd, &iter) >= 0)
			{
			GtkTreeSelection *selection;
			GtkTreePath *tpath;

			selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
			gtk_tree_selection_unselect_all(selection);
			gtk_tree_selection_select_iter(selection, &iter);

			tpath = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, nullptr, FALSE);
			gtk_tree_path_free(tpath);
			}
		}

	if (sd->thumb_enable &&
	    sd->click_fd && sd->click_fd->thumb_pixbuf)
		{
		dnd_set_drag_icon(widget, context, sd->click_fd->thumb_pixbuf, search_result_selection_count(sd, nullptr));
		}
}

static void search_gps_dnd_received_cb(GtkWidget *, GdkDragContext *,
										gint, gint,
										GtkSelectionData *selection_data, guint info,
										guint, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GList *list;
	gdouble latitude, longitude;
	FileData *fd;

	if (info == TARGET_URI_LIST)
		{
		list = uri_filelist_from_gtk_selection_data(selection_data);

		/* If more than one file, use only the first file in a list.
		*/
		if (list != nullptr)
			{
			fd = static_cast<FileData *>(list->data);
			latitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude", 1000);
			longitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude", 1000);
			if (latitude != 1000 && longitude != 1000)
				{
				gtk_entry_set_text(GTK_ENTRY(sd->entry_gps_coord),
							g_strdup_printf("%lf %lf", latitude, longitude));
				}
			else
				{
				gtk_entry_set_text(GTK_ENTRY(sd->entry_gps_coord), "Image is not geocoded");
				}
			}
		}

	if (info == TARGET_TEXT_PLAIN)
		{
		gtk_entry_set_text(GTK_ENTRY(sd->entry_gps_coord),"");
		}
}

static void search_path_entry_dnd_received_cb(GtkWidget *, GdkDragContext *,
										gint, gint,
										GtkSelectionData *selection_data, guint info,
										guint, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GList *list;
	FileData *fd;

	if (info == TARGET_URI_LIST)
		{
		list = uri_filelist_from_gtk_selection_data(selection_data);
		/* If more than one file, use only the first file in a list.
		*/
		if (list != nullptr)
			{
			fd = static_cast<FileData *>(list->data);
			gtk_entry_set_text(GTK_ENTRY(sd->path_entry),
						g_strdup_printf("%s", fd->path));
			gtk_widget_set_tooltip_text(GTK_WIDGET(sd->path_entry),g_strdup_printf("%s", fd->path));
			}
		}

	if (info == TARGET_TEXT_PLAIN)
		{
		gtk_entry_set_text(GTK_ENTRY(sd->path_entry),"");
		}
}

static void search_image_content_dnd_received_cb(GtkWidget *, GdkDragContext *,
										gint, gint,
										GtkSelectionData *selection_data, guint info,
										guint, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GList *list;
	FileData *fd;

	if (info == TARGET_URI_LIST)
		{
		list = uri_filelist_from_gtk_selection_data(selection_data);
		/* If more than one file, use only the first file in a list.
		*/
		if (list != nullptr)
			{
			fd = static_cast<FileData *>(list->data);
			gtk_entry_set_text(GTK_ENTRY(sd->entry_similarity),
						g_strdup_printf("%s", fd->path));
			gtk_widget_set_tooltip_text(GTK_WIDGET(sd->entry_similarity),g_strdup_printf("%s", fd->path));
			}
		}

	if (info == TARGET_TEXT_PLAIN)
		{
		gtk_entry_set_text(GTK_ENTRY(sd->entry_similarity),"");
		}
}

static void search_dnd_init(SearchData *sd)
{
	gtk_drag_source_set(sd->result_view, static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
			    result_drag_types, n_result_drag_types,
			    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	g_signal_connect(G_OBJECT(sd->result_view), "drag_data_get",
			 G_CALLBACK(search_dnd_data_set), sd);
	g_signal_connect(G_OBJECT(sd->result_view), "drag_begin",
			 G_CALLBACK(search_dnd_begin), sd);

	gtk_drag_dest_set(GTK_WIDGET(sd->entry_gps_coord),
					 GTK_DEST_DEFAULT_ALL,
					  result_drop_types, n_result_drop_types,
					 GDK_ACTION_COPY);

	g_signal_connect(G_OBJECT(sd->entry_gps_coord), "drag_data_received",
					G_CALLBACK(search_gps_dnd_received_cb), sd);

	gtk_drag_dest_set(GTK_WIDGET(sd->path_entry),
					GTK_DEST_DEFAULT_ALL,
					result_drop_types, n_result_drop_types,
					GDK_ACTION_COPY);

	g_signal_connect(G_OBJECT(sd->path_entry), "drag_data_received",
					G_CALLBACK(search_path_entry_dnd_received_cb), sd);

	gtk_drag_dest_set(GTK_WIDGET(sd->entry_similarity),
					GTK_DEST_DEFAULT_ALL,
					result_drop_types, n_result_drop_types,
					GDK_ACTION_COPY);

	g_signal_connect(G_OBJECT(sd->entry_similarity), "drag_data_received",
					G_CALLBACK(search_image_content_dnd_received_cb), sd);
}

/*
 *-------------------------------------------------------------------
 * search core
 *-------------------------------------------------------------------
 */

#define MATCH_IS_BETWEEN(val, a, b)  (b > a ? (val >= a && val <= b) : (val >= b && val <= a))

static gboolean search_step_cb(gpointer data);


static void search_buffer_flush(SearchData *sd)
{
	GList *work;

	work = g_list_last(sd->search_buffer_list);
	while (work)
		{
		auto mfd = static_cast<MatchFileData *>(work->data);
		work = work->prev;

		search_result_append(sd, mfd);
		}

	g_list_free(sd->search_buffer_list);
	sd->search_buffer_list = nullptr;
	sd->search_buffer_count = 0;
}

static void search_stop(SearchData *sd)
{
	if (sd->search_idle_id)
		{
		g_source_remove(sd->search_idle_id);
		sd->search_idle_id = 0;
		}

	image_loader_free(sd->img_loader);
	sd->img_loader = nullptr;
	cache_sim_data_free(sd->img_cd);
	sd->img_cd = nullptr;

	cache_sim_data_free(sd->search_similarity_cd);
	sd->search_similarity_cd = nullptr;

	search_buffer_flush(sd);

	filelist_free(sd->search_folder_list);
	sd->search_folder_list = nullptr;

	g_list_free(sd->search_done_list);
	sd->search_done_list = nullptr;

	filelist_free(sd->search_file_list);
	sd->search_file_list = nullptr;

	sd->match_broken_enable = FALSE;

	gtk_widget_set_sensitive(sd->box_search, TRUE);
	spinner_set_interval(sd->spinner, -1);
	gtk_widget_set_sensitive(sd->button_start, TRUE);
	gtk_widget_set_sensitive(sd->button_stop, FALSE);
	search_progress_update(sd, TRUE, -1.0);
	search_status_update(sd);
}

static void search_file_load_process(SearchData *sd, CacheData *cd)
{
	GdkPixbuf *pixbuf;

	pixbuf = image_loader_get_pixbuf(sd->img_loader);

	/* Used to determine if image is broken
	 */
	if (cd && !pixbuf)
		{
		if (!cd->dimensions)
			{
			cache_sim_data_set_dimensions(cd, -1, -1);
			}
		}
	else if (cd && pixbuf)
		{
		if (!cd->dimensions)
			{
			cache_sim_data_set_dimensions(cd, gdk_pixbuf_get_width(pixbuf),
							  gdk_pixbuf_get_height(pixbuf));
			}

		if (sd->match_similarity_enable && !cd->similarity)
			{
			ImageSimilarityData *sim;

			sim = image_sim_new_from_pixbuf(pixbuf);
			cache_sim_data_set_similarity(cd, sim);
			image_sim_free(sim);
			}

		if (options->thumbnails.enable_caching &&
		    sd->img_loader && image_loader_get_fd(sd->img_loader))
			{
			gchar *base;
			const gchar *path;
			mode_t mode = 0755;

			path = image_loader_get_fd(sd->img_loader)->path;
			base = cache_get_location(CACHE_TYPE_SIM, path, FALSE, &mode);
			if (recursive_mkdir_if_not_exists(base, mode))
				{
				g_free(cd->path);
				cd->path = cache_get_location(CACHE_TYPE_SIM, path, TRUE, nullptr);
				if (cache_sim_data_save(cd))
					{
					filetime_set(cd->path, filetime(image_loader_get_fd(sd->img_loader)->path));
					}
				}
			g_free(base);
			}
		}

	image_loader_free(sd->img_loader);
	sd->img_loader = nullptr;

	sd->search_idle_id = g_idle_add(search_step_cb, sd);
}

static void search_file_load_done_cb(ImageLoader *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	search_file_load_process(sd, sd->img_cd);
}

static gboolean search_file_do_extra(SearchData *sd, FileData *fd, gint *match,
				     gint *width, gint *height, gint *simval)
{
	gboolean new_data = FALSE;
	gboolean tmatch = TRUE;
	gboolean tested = FALSE;

	if (!sd->img_cd)
		{
		gchar *cd_path;

		new_data = TRUE;

		cd_path = cache_find_location(CACHE_TYPE_SIM, fd->path);
		if (cd_path && filetime(fd->path) == filetime(cd_path))
			{
			sd->img_cd = cache_sim_data_load(cd_path);
			}
		g_free(cd_path);
		}

	if (!sd->img_cd)
		{
		sd->img_cd = cache_sim_data_new();
		}

	if (new_data)
		{
		if ((sd->match_dimensions_enable && !sd->img_cd->dimensions) || (sd->match_similarity_enable && !sd->img_cd->similarity) || sd->match_broken_enable)
			{
			sd->img_loader = image_loader_new(fd);
			g_signal_connect(G_OBJECT(sd->img_loader), "error", (GCallback)search_file_load_done_cb, sd);
			g_signal_connect(G_OBJECT(sd->img_loader), "done", (GCallback)search_file_load_done_cb, sd);
			if (image_loader_start(sd->img_loader))
				{
				return TRUE;
				}
			else
				{
				image_loader_free(sd->img_loader);
				sd->img_loader = nullptr;
				}
			}
		}

	if (sd->match_broken_enable)
		{
		tested = TRUE;
		tmatch = FALSE;
		if (sd->match_class == SEARCH_MATCH_EQUAL && sd->img_cd->width == -1)
			{
			tmatch = TRUE;
			}
		else if (sd->match_class == SEARCH_MATCH_NONE && sd->img_cd->width != -1)
			{
			tmatch = TRUE;
			}
		}

	if (tmatch && sd->match_dimensions_enable && sd->img_cd->dimensions)
		{
		CacheData *cd = sd->img_cd;

		tmatch = FALSE;
		tested = TRUE;

		if (sd->match_dimensions == SEARCH_MATCH_EQUAL)
			{
			tmatch = (cd->width == sd->search_width && cd->height == sd->search_height);
			}
		else if (sd->match_dimensions == SEARCH_MATCH_UNDER)
			{
			tmatch = (cd->width < sd->search_width && cd->height < sd->search_height);
			}
		else if (sd->match_dimensions == SEARCH_MATCH_OVER)
			{
			tmatch = (cd->width > sd->search_width && cd->height > sd->search_height);
			}
		else if (sd->match_dimensions == SEARCH_MATCH_BETWEEN)
			{
			tmatch = (MATCH_IS_BETWEEN(cd->width, sd->search_width, sd->search_width_end) &&
				  MATCH_IS_BETWEEN(cd->height, sd->search_height, sd->search_height_end));
			}
		}

	if (tmatch && sd->match_similarity_enable && sd->img_cd->similarity)
		{
		gdouble value = 0.0;

		tmatch = FALSE;
		tested = TRUE;

		/** @FIXME implement similarity checking */
		if (sd->search_similarity_cd && sd->search_similarity_cd->similarity)
			{
			gdouble result;

			result = image_sim_compare_fast(sd->search_similarity_cd->sim, sd->img_cd->sim,
							static_cast<gdouble>(sd->search_similarity) / 100.0);
			result *= 100.0;
			if (result >= static_cast<gdouble>(sd->search_similarity))
				{
				tmatch = TRUE;
				value = static_cast<gint>(result);
				}
			}

		if (simval) *simval = value;
		}

	if (sd->img_cd->dimensions)
		{
		if (width) *width = sd->img_cd->width;
		if (height) *height = sd->img_cd->height;
		}

	cache_sim_data_free(sd->img_cd);
	sd->img_cd = nullptr;

	*match = (tmatch && tested);

	return FALSE;
}

static gboolean search_file_next(SearchData *sd)
{
	FileData *fd;
	gboolean match = TRUE;
	gboolean tested = FALSE;
	gboolean extra_only = FALSE;
	gint width = 0;
	gint height = 0;
	gint sim = 0;
	time_t file_date;

	if (!sd->search_file_list) return FALSE;

	if (sd->img_cd)
		{
		/* on end of a CacheData load, skip recomparing non-extra match types */
		extra_only = TRUE;
		match = FALSE;
		}
	else
		{
		sd->search_total++;
		}

	fd = static_cast<FileData *>(sd->search_file_list->data);

	if (match && sd->match_name_enable && sd->search_name)
		{
		tested = TRUE;
		match = FALSE;

		if (sd->match_name == SEARCH_MATCH_NAME_EQUAL)
			{
			if (sd->search_name_match_case)
				{
				match = (strcmp(fd->name, sd->search_name) == 0);
				}
			else
				{
				match = (g_ascii_strcasecmp(fd->name, sd->search_name) == 0);
				}
			}
		else if (sd->match_name == SEARCH_MATCH_NAME_CONTAINS || sd->match_name == SEARCH_MATCH_PATH_CONTAINS)
			{
			const gchar *fd_name_or_path;
			if (sd->match_name == SEARCH_MATCH_NAME_CONTAINS)
				{
				fd_name_or_path = fd->name;
				}
			else
				{
				fd_name_or_path = fd->path;
				}
			if (sd->search_name_match_case)
				{
				match = g_regex_match(sd->search_name_regex, fd_name_or_path, static_cast<GRegexMatchFlags>(0), nullptr);
				}
			else
				{
				/* sd->search_name is converted in search_start() */
				gchar *haystack = g_utf8_strdown(fd_name_or_path, -1);
				match = g_regex_match(sd->search_name_regex, haystack, static_cast<GRegexMatchFlags>(0), nullptr);
				g_free(haystack);
				}
			}
		}

	if (match && sd->match_size_enable)
		{
		tested = TRUE;
		match = FALSE;

		if (sd->match_size == SEARCH_MATCH_EQUAL)
			{
			match = (fd->size == sd->search_size);
			}
		else if (sd->match_size == SEARCH_MATCH_UNDER)
			{
			match = (fd->size < sd->search_size);
			}
		else if (sd->match_size == SEARCH_MATCH_OVER)
			{
			match = (fd->size > sd->search_size);
			}
		else if (sd->match_size == SEARCH_MATCH_BETWEEN)
			{
			match = MATCH_IS_BETWEEN(fd->size, sd->search_size, sd->search_size_end);
			}
		}

	if (match && sd->match_date_enable)
		{
		tested = TRUE;
		match = FALSE;

		if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->date_type)), _("Changed")) == 0)
			{
			file_date = fd->cdate;
			}
		else if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->date_type)), _("Original")) == 0)
			{
			read_exif_time_data(fd);
			file_date = fd->exifdate;
			}
		else if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->date_type)), _("Digitized")) == 0)
			{
			read_exif_time_digitized_data(fd);
			file_date = fd->exifdate_digitized;
			}
		else
			{
			file_date = fd->date;
			}

		if (sd->match_date == SEARCH_MATCH_EQUAL)
			{
			struct tm *lt;

			lt = localtime(&file_date);
			match = (lt &&
				 lt->tm_year == sd->search_date_y - 1900 &&
				 lt->tm_mon == sd->search_date_m - 1 &&
				 lt->tm_mday == sd->search_date_d);
			}
		else if (sd->match_date == SEARCH_MATCH_UNDER)
			{
			match = (file_date < convert_dmy_to_time(sd->search_date_d, sd->search_date_m, sd->search_date_y));
			}
		else if (sd->match_date == SEARCH_MATCH_OVER)
			{
			match = (file_date > convert_dmy_to_time(sd->search_date_d, sd->search_date_m, sd->search_date_y) + 60 * 60 * 24 - 1);
			}
		else if (sd->match_date == SEARCH_MATCH_BETWEEN)
			{
			time_t a = convert_dmy_to_time(sd->search_date_d, sd->search_date_m, sd->search_date_y);
			time_t b = convert_dmy_to_time(sd->search_date_end_d, sd->search_date_end_m, sd->search_date_end_y);

			if (b >= a)
				{
				b += 60 * 60 * 24 - 1;
				}
			else
				{
				a += 60 * 60 * 24 - 1;
				}
			match = MATCH_IS_BETWEEN(file_date, a, b);
			}
		}

	if (match && sd->match_keywords_enable && sd->search_keyword_list)
		{
		GList *list;

		tested = TRUE;
		match = FALSE;

		list = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);

		if (list)
			{
			GList *needle;
			GList *haystack;

			if (sd->match_keywords == SEARCH_MATCH_ALL)
				{
				gboolean found = TRUE;

				needle = sd->search_keyword_list;
				while (needle && found)
					{
					found = FALSE;
					haystack = list;
					while (haystack && !found)
						{
						found = (g_ascii_strcasecmp(static_cast<gchar *>(needle->data),
								    static_cast<gchar *>(haystack->data)) == 0);
						haystack = haystack->next;
						}
					needle = needle->next;
					}

				match = found;
				}
			else if (sd->match_keywords == SEARCH_MATCH_ANY)
				{
				gboolean found = FALSE;

				needle = sd->search_keyword_list;
				while (needle && !found)
					{
					haystack = list;
					while (haystack && !found)
						{
						found = (g_ascii_strcasecmp(static_cast<gchar *>(needle->data),
								    static_cast<gchar *>(haystack->data)) == 0);
						haystack = haystack->next;
						}
					needle = needle->next;
					}

				match = found;
				}
			else if (sd->match_keywords == SEARCH_MATCH_NONE)
				{
				gboolean found = FALSE;

				needle = sd->search_keyword_list;
				while (needle && !found)
					{
					haystack = list;
					while (haystack && !found)
						{
						found = (g_ascii_strcasecmp(static_cast<gchar *>(needle->data),
								    static_cast<gchar *>(haystack->data)) == 0);
						haystack = haystack->next;
						}
					needle = needle->next;
					}

				match = !found;
				}
			g_list_free_full(list, g_free);
			}
		else
			{
			match = (sd->match_keywords == SEARCH_MATCH_NONE);
			}
		}

	if (match && sd->match_comment_enable && sd->search_comment && strlen(sd->search_comment))
		{
		gchar *comment;

		tested = TRUE;
		match = FALSE;

		comment = metadata_read_string(fd, COMMENT_KEY, METADATA_PLAIN);

		if (comment)
			{
			if (!sd->search_comment_match_case)
				{
				gchar *tmp = g_utf8_strdown(comment, -1);
				g_free(comment);
				comment = tmp;
				}

			if (sd->match_comment == SEARCH_MATCH_CONTAINS)
				{
				match = g_regex_match(sd->search_comment_regex, comment, static_cast<GRegexMatchFlags>(0), nullptr);
				}
			else if (sd->match_comment == SEARCH_MATCH_NONE)
				{
				match = !g_regex_match(sd->search_comment_regex, comment, static_cast<GRegexMatchFlags>(0), nullptr);
				}
			g_free(comment);
			}
		else
			{
			match = (sd->match_comment == SEARCH_MATCH_NONE);
			}
		}

	if (match && sd->match_rating_enable)
		{
		tested = TRUE;
		match = FALSE;
		gint rating;

		rating = metadata_read_int(fd, RATING_KEY, 0);
		if (sd->match_rating == SEARCH_MATCH_EQUAL)
			{
			match = (rating == sd->search_rating);
			}
		else if (sd->match_rating == SEARCH_MATCH_UNDER)
			{
			match = (rating < sd->search_rating);
			}
		else if (sd->match_rating == SEARCH_MATCH_OVER)
			{
			match = (rating > sd->search_rating);
			}
		else if (sd->match_rating == SEARCH_MATCH_BETWEEN)
			{
			match = MATCH_IS_BETWEEN(rating, sd->search_rating, sd->search_rating_end);
			}
		}

	if (match && sd->match_class_enable)
		{
		tested = TRUE;
		match = FALSE;
		FileFormatClass format_class;
		FileFormatClass search_class;

		if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->class_type)), _("Image")) == 0)
			{
			search_class = FORMAT_CLASS_IMAGE;
			}
		else if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->class_type)), _("Raw Image")) == 0)
			{
			search_class = FORMAT_CLASS_RAWIMAGE;
			}
		else if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->class_type)), _("Video")) == 0)
			{
			search_class = FORMAT_CLASS_VIDEO;
			}
		else if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->class_type)), _("Document")) == 0)
			{
			search_class = FORMAT_CLASS_DOCUMENT;
			}
		else if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->class_type)), _("Metadata")) == 0)
			{
			search_class = FORMAT_CLASS_META;
			}
		else if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->class_type)), _("Unknown")) == 0)
			{
			search_class = FORMAT_CLASS_UNKNOWN;
			}
		else
			{
			search_class = FORMAT_CLASS_BROKEN;
			}

		if (search_class != FORMAT_CLASS_BROKEN)
			{
			format_class = fd->format_class;
			if (sd->match_class == SEARCH_MATCH_EQUAL)
				{
				match = (format_class == search_class);
				}
			else if (sd->match_class == SEARCH_MATCH_NONE)
				{
				match = (format_class != search_class);
				}
			}
		else
			{
			if (fd->format_class == FORMAT_CLASS_IMAGE || fd->format_class == FORMAT_CLASS_RAWIMAGE || fd->format_class == FORMAT_CLASS_VIDEO || fd->format_class == FORMAT_CLASS_DOCUMENT)
				{
				sd->match_broken_enable = TRUE;
				match = TRUE;
				}
			else
				{
				sd->match_broken_enable = FALSE;
				}
			}
		}

	if (match && sd->match_marks_enable)
		{
		tested = TRUE;
		match = FALSE;
		gint search_marks = -1;
		gint i = 0;
		gchar *marks_string = nullptr;

		if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->marks_type)), _("Any mark")) == 0)
			{
			search_marks = -1;
			}
		else
			{
			for (i = 0; i < FILEDATA_MARKS_SIZE; i++)
				{
				marks_string = g_strdup_printf("%s%d", _("Mark "), i + 1);
				if (g_strcmp0(marks_string, options->marks_tooltips[i]) != 0)
					{
					g_free(marks_string);
					marks_string = g_strdup_printf("%s%d %s", _("Mark "), i + 1,
													options->marks_tooltips[i]);
					}

				if (g_strcmp0(gtk_combo_box_text_get_active_text(
								GTK_COMBO_BOX_TEXT(sd->marks_type)),
								marks_string) == 0)
					{
					search_marks = 1 << i;
					}
				g_free(marks_string);
				marks_string = nullptr;
				}
			}

		if (sd->match_marks == SEARCH_MATCH_EQUAL)
			{
			match = (fd->marks & search_marks);
			}
		else
			{
			if (search_marks == -1)
				{
				match = fd->marks ? FALSE : TRUE;
				}
			else
				{
				match = (fd->marks & search_marks) ? FALSE : TRUE;
				}
			}
		}

	if (match && sd->match_gps_enable)
		{
		/* Calculate the distance the image is from the specified origin.
		* This is a standard algorithm. A simplified one may be faster.
		*/
		#define RADIANS  0.0174532925
		#define KM_EARTH_RADIUS 6371
		#define MILES_EARTH_RADIUS 3959
		#define NAUTICAL_MILES_EARTH_RADIUS 3440

		gdouble latitude, longitude, range, conversion;

		if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->units_gps)), _("km")) == 0)
			{
			conversion = KM_EARTH_RADIUS;
			}
		else if (g_strcmp0(gtk_combo_box_text_get_active_text(
						GTK_COMBO_BOX_TEXT(sd->units_gps)), _("miles")) == 0)
			{
			conversion = MILES_EARTH_RADIUS;
			}
		else
			{
			conversion = NAUTICAL_MILES_EARTH_RADIUS;
			}

		tested = TRUE;
		match = FALSE;

		latitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude", 1000);
		longitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude", 1000);
		if (latitude != 1000 && longitude != 1000)
			{
			range = conversion * acos(sin(latitude * RADIANS) *
						sin(sd->search_lat * RADIANS) + cos(latitude * RADIANS) *
						cos(sd->search_lat * RADIANS) * cos((sd->search_lon -
						longitude) * RADIANS));
			if (sd->match_gps == SEARCH_MATCH_UNDER)
				{
				if (sd->search_gps >= range)
					match = TRUE;
				}
			else if (sd->match_gps == SEARCH_MATCH_OVER)
				{
				if (sd->search_gps < range)
					match = TRUE;
				}
			}
		else if (sd->match_gps == SEARCH_MATCH_NONE)
			{
			match = TRUE;
			}
		}

	if ((match || extra_only) && (sd->match_dimensions_enable || sd->match_similarity_enable || sd->match_broken_enable))
		{
		tested = TRUE;

		if (search_file_do_extra(sd, fd, &match, &width, &height, &sim))
			{
			sd->search_buffer_count += SEARCH_BUFFER_MATCH_LOAD;
			return TRUE;
			}
		}

	sd->search_file_list = g_list_remove(sd->search_file_list, fd);

	if (tested && match)
		{
		auto mfd = g_new(MatchFileData, 1);
		mfd->fd = fd;

		mfd->width = width;
		mfd->height = height;
		mfd->rank = sim;

		sd->search_buffer_list = g_list_prepend(sd->search_buffer_list, mfd);
		sd->search_buffer_count += SEARCH_BUFFER_MATCH_HIT;
		sd->search_count++;
		search_progress_update(sd, TRUE, -1.0);
		}
	else
		{
		file_data_unref(fd);
		sd->search_buffer_count += SEARCH_BUFFER_MATCH_MISS;
		}

	return FALSE;
}

static gboolean search_step_cb(gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	FileData *fd;

	if (sd->search_buffer_count > SEARCH_BUFFER_FLUSH_SIZE)
		{
		search_buffer_flush(sd);
		search_progress_update(sd, TRUE, -1.0);
		}

	if (sd->search_file_list)
		{
		if (search_file_next(sd))
			{
			sd->search_idle_id = 0;
			return G_SOURCE_REMOVE;
			}
		return G_SOURCE_CONTINUE;
		}

	if (!sd->search_file_list && !sd->search_folder_list)
		{
		sd->search_idle_id = 0;

		search_stop(sd);
		search_result_thumb_step(sd);

		return G_SOURCE_REMOVE;
		}

	fd = static_cast<FileData *>(sd->search_folder_list->data);

	if (g_list_find(sd->search_done_list, fd) == nullptr)
		{
		GList *list = nullptr;
		GList *dlist = nullptr;
		gboolean success = FALSE;

		sd->search_done_list = g_list_prepend(sd->search_done_list, fd);

		if (sd->search_type == SEARCH_MATCH_NONE)
			{
			success = filelist_read(fd, &list, &dlist);
			}
		else if (sd->search_type == SEARCH_MATCH_ALL &&
			 sd->search_dir_fd &&
			 strlen(fd->path) >= strlen(sd->search_dir_fd->path))
			{
			const gchar *path;

			path = fd->path + strlen(sd->search_dir_fd->path);
			if (path != fd->path)
				{
				FileData *dir_fd = file_data_new_dir(path);
				success = filelist_read(dir_fd, &list, nullptr);
				file_data_unref(dir_fd);
				}
			success |= filelist_read(fd, nullptr, &dlist);
			if (success)
				{
				GList *work;

				work = list;
				while (work)
					{
					FileData *fdp;
					GList *link;
					gchar *meta_path;

					fdp = static_cast<FileData *>(work->data);
					link = work;
					work = work->next;

					meta_path = cache_find_location(CACHE_TYPE_METADATA, fdp->path);
					if (!meta_path)
						{
						list = g_list_delete_link(list, link);
						file_data_unref(fdp);
						}
					g_free(meta_path);
					}
				}
			}

		if (success)
			{
			list = filelist_sort(list, SORT_NAME, TRUE);
			sd->search_file_list = list;

			if (sd->search_path_recurse)
				{
				dlist = filelist_sort(dlist, SORT_NAME, TRUE);
				sd->search_folder_list = g_list_concat(dlist, sd->search_folder_list);
				}
			else
				{
				filelist_free(dlist);
				}
			}
		}
	else
		{
		sd->search_folder_list = g_list_remove(sd->search_folder_list, fd);
		sd->search_done_list = g_list_remove(sd->search_done_list, fd);
		file_data_unref(fd);
		}

	return G_SOURCE_CONTINUE;
}

static void search_similarity_load_done_cb(ImageLoader *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	search_file_load_process(sd, sd->search_similarity_cd);
}

static void search_start(SearchData *sd)
{
	GError *error = nullptr;

	search_stop(sd);
	search_result_clear(sd);

	if (sd->search_dir_fd)
		{
		sd->search_folder_list = g_list_prepend(sd->search_folder_list, file_data_ref(sd->search_dir_fd));
		}

	if (!sd->search_name_match_case)
		{
		/* convert to lowercase here, so that this is only done once per search */
		gchar *tmp = g_utf8_strdown(sd->search_name, -1);
		g_free(sd->search_name);
		sd->search_name = tmp;
		}

	if(sd->search_name_regex)
		{
		g_regex_unref(sd->search_name_regex);
		}

	sd->search_name_regex = g_regex_new(sd->search_name, static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), &error);
	if (error)
		{
		log_printf("Error: could not compile regular expression %s\n%s\n", sd->search_name, error->message);
		g_error_free(error);
		error = nullptr;
		sd->search_name_regex = g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
		}

	if (!sd->search_comment_match_case)
		{
		/* convert to lowercase here, so that this is only done once per search */
		gchar *tmp = g_utf8_strdown(sd->search_comment, -1);
		g_free(sd->search_comment);
		sd->search_comment = tmp;
		}

	if(sd->search_comment_regex)
		{
		g_regex_unref(sd->search_comment_regex);
		}

	sd->search_comment_regex = g_regex_new(sd->search_comment, static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), &error);
	if (error)
		{
		log_printf("Error: could not compile regular expression %s\n%s\n", sd->search_comment, error->message);
		g_error_free(error);
		error = nullptr;
		sd->search_comment_regex = g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
		}

	sd->search_count = 0;
	sd->search_total = 0;

	gtk_widget_set_sensitive(sd->box_search, FALSE);
	spinner_set_interval(sd->spinner, SPINNER_SPEED);
	gtk_widget_set_sensitive(sd->button_start, FALSE);
	gtk_widget_set_sensitive(sd->button_stop, TRUE);
	search_progress_update(sd, TRUE, -1.0);

	if (sd->match_similarity_enable &&
	    !sd->search_similarity_cd &&
	    isfile(sd->search_similarity_path))
		{
		gchar *cd_path;

		cd_path = cache_find_location(CACHE_TYPE_SIM, sd->search_similarity_path);
		if (cd_path && filetime(sd->search_similarity_path) == filetime(cd_path))
			{
			sd->search_similarity_cd = cache_sim_data_load(cd_path);
			}
		g_free(cd_path);

		if (!sd->search_similarity_cd || !sd->search_similarity_cd->similarity)
			{
			if (!sd->search_similarity_cd)
				{
				sd->search_similarity_cd = cache_sim_data_new();
				}

			sd->img_loader = image_loader_new(file_data_new_group(sd->search_similarity_path));
			g_signal_connect(G_OBJECT(sd->img_loader), "error", (GCallback)search_similarity_load_done_cb, sd);
			g_signal_connect(G_OBJECT(sd->img_loader), "done", (GCallback)search_similarity_load_done_cb, sd);
			if (image_loader_start(sd->img_loader))
				{
				return;
				}
			image_loader_free(sd->img_loader);
			sd->img_loader = nullptr;
			}

		}

	sd->search_idle_id = g_idle_add(search_step_cb, sd);
}

static void search_start_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	GtkTreeViewColumn *column;
	gchar *path;
	gchar *entry_text;
	gchar *collection;

	if (sd->search_folder_list)
		{
		search_stop(sd);
		search_result_thumb_step(sd);
		return;
		}

	if (sd->match_name_enable) history_combo_append_history(sd->entry_name, nullptr);
	g_free(sd->search_name);
	sd->search_name = g_strdup(gtk_entry_get_text(GTK_ENTRY(sd->entry_name)));

	/* XXX */
	g_free(sd->search_comment);
	sd->search_comment = g_strdup(gtk_entry_get_text(GTK_ENTRY(sd->entry_comment)));

	g_free(sd->search_similarity_path);
	sd->search_similarity_path = g_strdup(gtk_entry_get_text(GTK_ENTRY(sd->entry_similarity)));
	if (sd->match_similarity_enable)
		{
		if (!isfile(sd->search_similarity_path))
			{
			file_util_warning_dialog(_("File not found"),
						 _("Please enter an existing file for image content."),
						 GQ_ICON_DIALOG_WARNING, sd->window);
			return;
			}
		tab_completion_append_to_history(sd->entry_similarity, sd->search_similarity_path);
		}

	/* Check the coordinate entry.
	* If the result is not sensible, it should get blocked.
	*/
	if (sd->match_gps_enable)
		{
		if (sd->match_gps != SEARCH_MATCH_NONE)
			{
			entry_text = decode_geo_parameters(gtk_entry_get_text(
										GTK_ENTRY(sd->entry_gps_coord)));

			sd->search_lat = 1000;
			sd->search_lon = 1000;
			sscanf(entry_text," %lf  %lf ", &sd->search_lat, &sd->search_lon );
			if (entry_text == nullptr || g_strstr_len(entry_text, -1, "Error") ||
						sd->search_lat < -90 || sd->search_lat > 90 ||
						sd->search_lon < -180 || sd->search_lon > 180)
				{
				file_util_warning_dialog(_(
						"Entry does not contain a valid lat/long value"),
							entry_text, GQ_ICON_DIALOG_WARNING, sd->window);
				return;
				}
			g_free(entry_text);
			}
		}

	g_list_free_full(sd->search_keyword_list, g_free);
	sd->search_keyword_list = keyword_list_pull(sd->entry_keywords);

	date_selection_get(sd->date_sel, &sd->search_date_d, &sd->search_date_m, &sd->search_date_y);
	date_selection_get(sd->date_sel_end, &sd->search_date_end_d, &sd->search_date_end_m, &sd->search_date_end_y);

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(sd->result_view), SEARCH_COLUMN_DIMENSIONS - 1);
	gtk_tree_view_column_set_visible(column, sd->match_dimensions_enable);

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(sd->result_view), SEARCH_COLUMN_RANK - 1);
	gtk_tree_view_column_set_visible(column, sd->match_similarity_enable);
	if (!sd->match_similarity_enable)
		{
		GtkTreeSortable *sortable;
		gint id;
		GtkSortType order;

		sortable = GTK_TREE_SORTABLE(gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view)));
		if (gtk_tree_sortable_get_sort_column_id(sortable, &id, &order) &&
		    id == SEARCH_COLUMN_RANK)
			{
			gtk_tree_sortable_set_sort_column_id(sortable, SEARCH_COLUMN_PATH, GTK_SORT_ASCENDING);
			}
		}

	if (sd->search_type == SEARCH_MATCH_NONE)
		{
		/* search path */

		path = remove_trailing_slash(gtk_entry_get_text(GTK_ENTRY(sd->path_entry)));
		if (isdir(path))
			{
			file_data_unref(sd->search_dir_fd);
			sd->search_dir_fd = file_data_new_dir(path);

			tab_completion_append_to_history(sd->path_entry, sd->search_dir_fd->path);

			search_start(sd);
			}
		else
			{
			file_util_warning_dialog(_("Folder not found"),
						 _("Please enter an existing folder to search."),
						 GQ_ICON_DIALOG_WARNING, sd->window);
			}

		g_free(path);
		}
	else if (sd->search_type == SEARCH_MATCH_ALL)
		{
		/* search metadata */
		file_data_unref(sd->search_dir_fd);
		sd->search_dir_fd = file_data_new_dir(get_metadata_cache_dir());
		search_start(sd);
		}
	else if (sd->search_type == SEARCH_MATCH_CONTAINS)
		{
		/* search current result list */
		GList *list;

		list = search_result_refine_list(sd);

		file_data_unref(sd->search_dir_fd);
		sd->search_dir_fd = nullptr;

		search_start(sd);

		sd->search_file_list = g_list_concat(sd->search_file_list, list);
		}
	else if (sd->search_type == SEARCH_MATCH_COLLECTION)
		{
		collection = g_strdup(gtk_entry_get_text(GTK_ENTRY(sd->collection_entry)));

		if (is_collection(collection))
			{
			GList *list = nullptr;

			list = collection_contents_fd(collection);

			file_data_unref(sd->search_dir_fd);
			sd->search_dir_fd = nullptr;

			search_start(sd);

			sd->search_file_list = g_list_concat(sd->search_file_list, list);
			}
		else
			{
			file_util_warning_dialog(_("Collection not found"), _("Please enter an existing collection name."), GQ_ICON_DIALOG_WARNING, sd->window);
			}
		g_free(collection);
		}
}

/*
 *-------------------------------------------------------------------
 * window construct
 *-------------------------------------------------------------------
 */

enum {
	MENU_CHOICE_COLUMN_NAME = 0,
	MENU_CHOICE_COLUMN_VALUE
};

static void search_thumb_toggle_cb(GtkWidget *button, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_result_thumb_enable(sd, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)));
}

static gint sort_matchdata_dimensions(MatchFileData *a, MatchFileData *b)
{
	gint sa;
	gint sb;

	sa = a->width * a->height;
	sb = b->width * b->height;

	if (sa > sb) return 1;
	if (sa < sb) return -1;
	return 0;
}

static gint search_result_sort_cb(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	gint n = GPOINTER_TO_INT(data);
	MatchFileData *fda;
	MatchFileData *fdb;

	gtk_tree_model_get(model, a, SEARCH_COLUMN_POINTER, &fda, -1);
	gtk_tree_model_get(model, b, SEARCH_COLUMN_POINTER, &fdb, -1);

	if (!fda || !fdb) return 0;

	switch (n)
		{
		case SEARCH_COLUMN_RANK:
			if ((fda)->rank > (fdb)->rank) return 1;
			if ((fda)->rank < (fdb)->rank) return -1;
			return 0;
			break;
		case SEARCH_COLUMN_NAME:
			if (options->file_sort.case_sensitive)
				return strcmp(fda->fd->collate_key_name, fdb->fd->collate_key_name);
			else
				return strcmp(fda->fd->collate_key_name_nocase, fdb->fd->collate_key_name_nocase);
			break;
		case SEARCH_COLUMN_SIZE:
			if (fda->fd->size > fdb->fd->size) return 1;
			if (fda->fd->size < fdb->fd->size) return -1;
			return 0;
			break;
		case SEARCH_COLUMN_DATE:
			if (fda->fd->date > fdb->fd->date) return 1;
			if (fda->fd->date < fdb->fd->date) return -1;
			return 0;
			break;
		case SEARCH_COLUMN_DIMENSIONS:
			return sort_matchdata_dimensions(fda, fdb);
			break;
		case SEARCH_COLUMN_PATH:
			return utf8_compare(fda->fd->path, fdb->fd->path, options->file_sort.case_sensitive);
			break;
		default:
			break;
		}

	return 0;
}

static void search_result_add_column(SearchData * sd, gint n, const gchar *title, gboolean image, gboolean right_justify)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, title);
	gtk_tree_view_column_set_min_width(column, 4);

	if (n != SEARCH_COLUMN_THUMB) gtk_tree_view_column_set_resizable(column, TRUE);

	if (!image)
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
		renderer = gtk_cell_renderer_text_new();
		if (right_justify) g_object_set(G_OBJECT(renderer), "xalign", 1.0, NULL);
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_add_attribute(column, renderer, "text", n);

		gtk_tree_view_column_set_sort_column_id(column, n);
		}
	else
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
		renderer = gtk_cell_renderer_pixbuf_new();
		cell_renderer_height_override(renderer);
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_add_attribute(column, renderer, "pixbuf", n);
		}

	gtk_tree_view_append_column(GTK_TREE_VIEW(sd->result_view), column);
}

static void menu_choice_set_visible(GtkWidget *widget, gboolean visible)
{
	if (visible)
		{
		if (!gtk_widget_get_visible(widget)) gtk_widget_show(widget);
		}
	else
		{
		if (gtk_widget_get_visible(widget)) gtk_widget_hide(widget);
		}
}

static gboolean menu_choice_get_match_type(GtkWidget *combo, MatchType *type)
{
	GtkTreeModel *store;
	GtkTreeIter iter;

	store = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter)) return FALSE;
	gtk_tree_model_get(store, &iter, MENU_CHOICE_COLUMN_VALUE, type, -1);
	return TRUE;
}

static void menu_choice_path_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->search_type)) return;

	menu_choice_set_visible(gtk_widget_get_parent(sd->check_recurse),
				(sd->search_type == SEARCH_MATCH_NONE));
	menu_choice_set_visible(sd->collection, (sd->search_type == SEARCH_MATCH_COLLECTION));
}

static void menu_choice_name_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_name)) return;
}

static void menu_choice_size_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_size)) return;

	menu_choice_set_visible(gtk_widget_get_parent(sd->spin_size_end),
				(sd->match_size == SEARCH_MATCH_BETWEEN));
}

static void menu_choice_rating_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_rating)) return;

	menu_choice_set_visible(gtk_widget_get_parent(sd->spin_rating_end),
				(sd->match_rating == SEARCH_MATCH_BETWEEN));
}

static void menu_choice_class_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_class)) return;
}

static void menu_choice_marks_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_marks)) return;
}

static void menu_choice_date_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_date)) return;

	menu_choice_set_visible(gtk_widget_get_parent(sd->date_sel_end),
				(sd->match_date == SEARCH_MATCH_BETWEEN));
}

static void menu_choice_dimensions_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_dimensions)) return;

	menu_choice_set_visible(gtk_widget_get_parent(sd->spin_width_end),
				(sd->match_dimensions == SEARCH_MATCH_BETWEEN));
}

static void menu_choice_keyword_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_keywords)) return;
}

static void menu_choice_comment_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_comment)) return;
}

static void menu_choice_spin_cb(GtkAdjustment *adjustment, gpointer data)
{
	auto value = static_cast<gint *>(data);

	*value = static_cast<gint>(gtk_adjustment_get_value(adjustment));
}

static void menu_choice_gps_cb(GtkWidget *combo, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(combo, &sd->match_gps)) return;

	menu_choice_set_visible(gtk_widget_get_parent(sd->spin_gps),
					(sd->match_gps != SEARCH_MATCH_NONE));
}

static GtkWidget *menu_spin(GtkWidget *box, gdouble min, gdouble max, gint value,
			    GCallback func, gpointer data)
{
	GtkWidget *spin;
	GtkAdjustment *adj;

	spin = gtk_spin_button_new_with_range(min, max, 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), static_cast<gdouble>(value));
	adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin));
	if (func) g_signal_connect(G_OBJECT(adj), "value_changed",
				   G_CALLBACK(func), data);
	gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 0);
	gtk_widget_show(spin);

	return spin;
}

static void menu_choice_check_cb(GtkWidget *button, gpointer data)
{
	auto widget = static_cast<GtkWidget *>(data);
	gboolean active;
	gboolean *value;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
	gtk_widget_set_sensitive(widget, active);

	value = static_cast<gboolean *>(g_object_get_data(G_OBJECT(button), "check_var"));
	if (value) *value = active;
}

static GtkWidget *menu_choice_menu(const MatchList *items, gint item_count,
				   GCallback func, gpointer data)
{
	GtkWidget *combo;
	GtkCellRenderer *renderer;
	GtkListStore *store;
	gint i;

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), renderer,
				       "text", MENU_CHOICE_COLUMN_NAME, NULL);

	for (i = 0; i < item_count; i++)
		{
		GtkTreeIter iter;

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, MENU_CHOICE_COLUMN_NAME, _(items[i].text),
						 MENU_CHOICE_COLUMN_VALUE, items[i].type, -1);
		}

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

	if (func) g_signal_connect(G_OBJECT(combo), "changed",
				   G_CALLBACK(func), data);

	return combo;
}

static GtkWidget *menu_choice(GtkWidget *box, GtkWidget **check, GtkWidget **menu,
			      const gchar *text, gboolean *value,
			      const MatchList *items, gint item_count,
			      GCallback func, gpointer data)
{
	GtkWidget *base_box;
	GtkWidget *hbox;
	GtkWidget *button;
	GtkWidget *option;

	base_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	gtk_box_pack_start(GTK_BOX(box), base_box, FALSE, FALSE, 0);
	gtk_widget_show(base_box);

	button = gtk_check_button_new();
	if (value) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), *value);
	gtk_box_pack_start(GTK_BOX(base_box), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	if (check) *check = button;
	if (value) g_object_set_data(G_OBJECT(button), "check_var", value);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_pack_start(GTK_BOX(base_box), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);

	g_signal_connect(G_OBJECT(button), "toggled",
			 G_CALLBACK(menu_choice_check_cb), hbox);
	gtk_widget_set_sensitive(hbox, (value) ? *value : FALSE);

	pref_label_new(hbox, text);

	if (!items && !menu) return hbox;

	option = menu_choice_menu(items, item_count, func, data);
	gtk_box_pack_start(GTK_BOX(hbox), option, FALSE, FALSE, 0);
	gtk_widget_show(option);
	if (menu) *menu = option;

	return hbox;
}

static void search_window_get_geometry(SearchData *sd)
{
	GdkWindow *window;
	LayoutWindow *lw = nullptr;

	layout_valid(&lw);

	if (!sd || !lw) return;

	window = gtk_widget_get_window(sd->window);
	gdk_window_get_position(window, &lw->options.search_window.x, &lw->options.search_window.y);
	lw->options.search_window.w = gdk_window_get_width(window);
	lw->options.search_window.h = gdk_window_get_height(window);
}

static void search_window_close(SearchData *sd)
{

	search_window_get_geometry(sd);

	gtk_widget_destroy(sd->window);
}

static void search_window_close_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_window_close(sd);
}

static void search_window_help_cb(GtkWidget *, gpointer)
{
	help_window_show("GuideImageSearchSearch.html");
}

static gboolean search_window_delete_cb(GtkWidget *, GdkEventAny *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_window_close(sd);
	return TRUE;
}

static void search_window_destroy_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_window_list = g_list_remove(search_window_list, sd);

	search_result_update_idle_cancel(sd);

	mfd_list_free(sd->search_buffer_list);
	sd->search_buffer_list = nullptr;

	search_stop(sd);
	search_result_clear(sd);

	file_data_unref(sd->search_dir_fd);

	g_free(sd->search_name);
	if(sd->search_name_regex)
		{
		g_regex_unref(sd->search_name_regex);
		}
	g_free(sd->search_comment);
	if(sd->search_comment_regex)
		{
		g_regex_unref(sd->search_comment_regex);
		}
	g_free(sd->search_similarity_path);
	g_list_free_full(sd->search_keyword_list, g_free);

	file_data_unregister_notify_func(search_notify_cb, sd);

	g_free(sd);
}

static void select_collection_dialog_close_cb(FileDialog *fdlg, gpointer)
{
	file_dialog_close(fdlg);
}

static void select_collection_dialog_ok_cb(FileDialog *fdlg, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	gchar *path;
	gchar *path_noext;
	gchar *collection;

	path = g_strdup(gtk_entry_get_text(GTK_ENTRY(fdlg->entry)));
	path_noext = remove_extension_from_path(path);
	collection = g_path_get_basename(path_noext);

	gtk_entry_set_text(GTK_ENTRY(sd->collection_entry), collection);
	file_dialog_close(fdlg);

	g_free(path);
	g_free(path_noext);
	g_free(collection);
}

static void select_collection_clicked_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	FileDialog *fdlg;
	const gchar *title;
	const gchar *btntext;
	gpointer btnfunc;
	const gchar *icon_name;

	title = _("Select collection");
	btntext = nullptr;
	btnfunc = reinterpret_cast<void *>(select_collection_dialog_ok_cb);
	icon_name = GQ_ICON_OK;

	fdlg = file_util_file_dlg(title, "dlg_collection", sd->window, select_collection_dialog_close_cb, sd);

	generic_dialog_add_message(GENERIC_DIALOG(fdlg), nullptr, title, nullptr, FALSE);
	file_dialog_add_button(fdlg, icon_name, btntext, reinterpret_cast<void(*)(FileDialog *, gpointer)>(btnfunc), TRUE);

	file_dialog_add_path_widgets(fdlg, get_collections_dir(), nullptr, "search_collection", GQ_COLLECTION_EXT, _("Collection Files"));

	gtk_widget_show(GENERIC_DIALOG(fdlg)->dialog);
}

void search_new(FileData *dir_fd, FileData *example_file)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *hbox2;
	GtkWidget *pad_box;
	GtkWidget *frame;
	GtkWidget *scrolled;
	GtkListStore *store;
	GtkTreeSortable *sortable;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkWidget *combo;
	GdkGeometry geometry;
	gint i;
	gchar *marks_string;
	LayoutWindow *lw = nullptr;

	layout_valid(&lw);

	auto sd = g_new0(SearchData, 1);

	sd->search_dir_fd = file_data_ref(dir_fd);
	sd->search_path_recurse = TRUE;
	sd->search_size = 0;
	sd->search_width = 640;
	sd->search_height = 480;
	sd->search_width_end = 1024;
	sd->search_height_end = 768;

	sd->search_type = SEARCH_MATCH_NONE;

	sd->match_name = SEARCH_MATCH_NAME_CONTAINS;
	sd->match_size = SEARCH_MATCH_EQUAL;
	sd->match_date = SEARCH_MATCH_EQUAL;
	sd->match_dimensions = SEARCH_MATCH_EQUAL;
	sd->match_keywords = SEARCH_MATCH_ALL;
	sd->match_comment = SEARCH_MATCH_CONTAINS;
	sd->match_rating = SEARCH_MATCH_EQUAL;
	sd->match_class = SEARCH_MATCH_EQUAL;
	sd->match_marks = SEARCH_MATCH_EQUAL;

	sd->match_name_enable = TRUE;

	sd->search_similarity = 95;

	sd->search_gps = 1;
	sd->match_gps = SEARCH_MATCH_NONE;

	if (example_file)
		{
		sd->search_similarity_path = g_strdup(example_file->path);
		}

	sd->window = window_new(GTK_WINDOW_TOPLEVEL, "search", nullptr, nullptr, _("Image search"));
	DEBUG_NAME(sd->window);

	gtk_window_set_resizable(GTK_WINDOW(sd->window), TRUE);

	geometry.min_width = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.min_height = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.base_width = DEF_SEARCH_WIDTH;
	geometry.base_height = DEF_SEARCH_HEIGHT;
	gtk_window_set_geometry_hints(GTK_WINDOW(sd->window), nullptr, &geometry,
				      static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE));

	if (lw && options->save_window_positions)
		{
		gtk_window_set_default_size(GTK_WINDOW(sd->window), lw->options.search_window.w, lw->options.search_window.h);
		gtk_window_move(GTK_WINDOW(sd->window), lw->options.search_window.x, lw->options.search_window.y);
		}
	else
		{
		gtk_window_set_default_size(GTK_WINDOW(sd->window), DEF_SEARCH_WIDTH, DEF_SEARCH_HEIGHT);
		}

	g_signal_connect(G_OBJECT(sd->window), "delete_event",
			 G_CALLBACK(search_window_delete_cb), sd);
	g_signal_connect(G_OBJECT(sd->window), "destroy",
			 G_CALLBACK(search_window_destroy_cb), sd);

	g_signal_connect(G_OBJECT(sd->window), "key_press_event",
			 G_CALLBACK(search_window_keypress_cb), sd);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), PREF_PAD_GAP);
	gtk_container_add(GTK_CONTAINER(sd->window), vbox);
	gtk_widget_show(vbox);

	sd->box_search = pref_box_new(vbox, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	hbox = pref_box_new(sd->box_search, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	pref_label_new(hbox, _("Search:"));

	sd->menu_path = menu_choice_menu(text_search_menu_path, sizeof(text_search_menu_path) / sizeof(MatchList),
					 G_CALLBACK(menu_choice_path_cb), sd);
	gtk_box_pack_start(GTK_BOX(hbox), sd->menu_path, FALSE, FALSE, 0);
	gtk_widget_show(sd->menu_path);

	hbox2 = pref_box_new(hbox, TRUE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	combo = tab_completion_new_with_history(&sd->path_entry, sd->search_dir_fd->path,
						"search_path", -1,
						nullptr, nullptr);
	tab_completion_add_select_button(sd->path_entry, nullptr, TRUE);
	gtk_box_pack_start(GTK_BOX(hbox2), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);
	sd->check_recurse = pref_checkbox_new_int(hbox2, _("Recurse"),
						  sd->search_path_recurse, &sd->search_path_recurse);

	sd->collection = pref_box_new(hbox, TRUE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	sd->collection_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(sd->collection_entry), "");
	gtk_box_pack_start(GTK_BOX(sd->collection), sd->collection_entry, TRUE, TRUE, 0);
	gtk_widget_show(sd->collection_entry);

	sd->fd_button = gtk_button_new_with_label("...");
	g_signal_connect(G_OBJECT(sd->fd_button), "clicked", G_CALLBACK(select_collection_clicked_cb), sd);
	gtk_box_pack_start(GTK_BOX(sd->collection), sd->fd_button, FALSE, FALSE, 0);
	gtk_widget_show(sd->fd_button);

	gtk_widget_hide(sd->collection);

	/* Search for file name */
	hbox = menu_choice(sd->box_search, &sd->check_name, &sd->menu_name,
			   _("File"), &sd->match_name_enable,
			   text_search_menu_name, sizeof(text_search_menu_name) / sizeof(MatchList),
			   G_CALLBACK(menu_choice_name_cb), sd);
	combo = history_combo_new(&sd->entry_name, "", "search_name", -1);
	gtk_box_pack_start(GTK_BOX(hbox), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);
	pref_checkbox_new_int(hbox, _("Match case"),
			      sd->search_name_match_case, &sd->search_name_match_case);
	gtk_widget_set_tooltip_text(GTK_WIDGET(combo), "When set to \"contains\" or \"path contains\", this field uses Perl Compatible Regular Expressions.\ne.g. use \n.*\\.jpg\n and not \n*.jpg\n\nSee the Help file.");

	/* Search for file size */
	hbox = menu_choice(sd->box_search, &sd->check_size, &sd->menu_size,
			   _("File size is"), &sd->match_size_enable,
			   text_search_menu_size, sizeof(text_search_menu_size) / sizeof(MatchList),
			   G_CALLBACK(menu_choice_size_cb), sd);
	sd->spin_size = menu_spin(hbox, 0, 1024*1024*1024, sd->search_size,
				  G_CALLBACK(menu_choice_spin_cb), &sd->search_size);
	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_pack_start(GTK_BOX(hbox), hbox2, FALSE, FALSE, 0);
	pref_label_new(hbox2, _("and"));
	sd->spin_size_end = menu_spin(hbox2, 0, 1024*1024*1024, sd->search_size_end,
				      G_CALLBACK(menu_choice_spin_cb), &sd->search_size_end);

	/* Search for file date */
	hbox = menu_choice(sd->box_search, &sd->check_date, &sd->menu_date,
			   _("File date is"), &sd->match_date_enable,
			   text_search_menu_date, sizeof(text_search_menu_date) / sizeof(MatchList),
			   G_CALLBACK(menu_choice_date_cb), sd);

	sd->date_sel = date_selection_new();
	date_selection_time_set(sd->date_sel, time(nullptr));
	gtk_box_pack_start(GTK_BOX(hbox), sd->date_sel, FALSE, FALSE, 0);
	gtk_widget_show(sd->date_sel);

	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_pack_start(GTK_BOX(hbox), hbox2, FALSE, FALSE, 0);
	pref_label_new(hbox2, _("and"));
	sd->date_sel_end = date_selection_new();
	date_selection_time_set(sd->date_sel_end, time(nullptr));
	gtk_box_pack_start(GTK_BOX(hbox2), sd->date_sel_end, FALSE, FALSE, 0);
	gtk_widget_show(sd->date_sel_end);

	sd->date_type = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->date_type), _("Modified"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->date_type), _("Status Changed"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->date_type), _("Original"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->date_type), _("Digitized"));
	gtk_box_pack_start(GTK_BOX(hbox), sd->date_type, FALSE, FALSE, 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(sd->date_type), 0);
	gtk_widget_set_tooltip_text(sd->date_type, "Modified (mtime)\nStatus Changed (ctime)\nOriginal (Exif.Photo.DateTimeOriginal)\nDigitized (Exif.Photo.DateTimeDigitized)");
	gtk_widget_show(sd->date_type);

	/* Search for image dimensions */
	hbox = menu_choice(sd->box_search, &sd->check_dimensions, &sd->menu_dimensions,
			   _("Image dimensions are"), &sd->match_dimensions_enable,
			   text_search_menu_size, sizeof(text_search_menu_size) / sizeof(MatchList),
			   G_CALLBACK(menu_choice_dimensions_cb), sd);
	pad_box = pref_box_new(hbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 2);
	sd->spin_width = menu_spin(pad_box, 0, 1000000, sd->search_width,
				   G_CALLBACK(menu_choice_spin_cb), &sd->search_width);
	pref_label_new(pad_box, "x");
	sd->spin_height = menu_spin(pad_box, 0, 1000000, sd->search_height,
				    G_CALLBACK(menu_choice_spin_cb), &sd->search_height);
	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_box_pack_start(GTK_BOX(hbox), hbox2, FALSE, FALSE, 0);
	pref_label_new(hbox2, _("and"));
	pref_spacer(hbox2, PREF_PAD_SPACE - 2*2);
	sd->spin_width_end = menu_spin(hbox2, 0, 1000000, sd->search_width_end,
				       G_CALLBACK(menu_choice_spin_cb), &sd->search_width_end);
	pref_label_new(hbox2, "x");
	sd->spin_height_end = menu_spin(hbox2, 0, 1000000, sd->search_height_end,
					G_CALLBACK(menu_choice_spin_cb), &sd->search_height_end);

	/* Search for image similarity */
	hbox = menu_choice(sd->box_search, &sd->check_similarity, nullptr,
			   _("Image content is"), &sd->match_similarity_enable,
			   nullptr, 0, nullptr, sd);
	sd->spin_similarity = menu_spin(hbox, 80, 100, sd->search_similarity,
					G_CALLBACK(menu_choice_spin_cb), &sd->search_similarity);

	/* xgettext:no-c-format */
	pref_label_new(hbox, _("% similar to"));

	combo = tab_completion_new_with_history(&sd->entry_similarity,
						(sd->search_similarity_path) ? sd->search_similarity_path : "",
						"search_similarity_path", -1, nullptr, nullptr);
	tab_completion_add_select_button(sd->entry_similarity, nullptr, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);
	pref_checkbox_new_int(hbox, _("Ignore rotation"),
				options->rot_invariant_sim, &options->rot_invariant_sim);

	/* Search for image keywords */
	hbox = menu_choice(sd->box_search, &sd->check_keywords, &sd->menu_keywords,
			   _("Keywords"), &sd->match_keywords_enable,
			   text_search_menu_keyword, sizeof(text_search_menu_keyword) / sizeof(MatchList),
			   G_CALLBACK(menu_choice_keyword_cb), sd);
	sd->entry_keywords = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), sd->entry_keywords, TRUE, TRUE, 0);
	gtk_widget_set_sensitive(sd->entry_keywords, sd->match_keywords_enable);
	g_signal_connect(G_OBJECT(sd->check_keywords), "toggled",
			 G_CALLBACK(menu_choice_check_cb), sd->entry_keywords);
	gtk_widget_show(sd->entry_keywords);

	/* Search for image comment */
	hbox = menu_choice(sd->box_search, &sd->check_comment, &sd->menu_comment,
			_("Comment"), &sd->match_comment_enable,
			text_search_menu_comment, sizeof(text_search_menu_comment) / sizeof(MatchList),
			G_CALLBACK(menu_choice_comment_cb), sd);
	sd->entry_comment = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), sd->entry_comment, TRUE, TRUE, 0);
	gtk_widget_set_sensitive(sd->entry_comment, sd->match_comment_enable);
	g_signal_connect(G_OBJECT(sd->check_comment), "toggled",
			G_CALLBACK(menu_choice_check_cb), sd->entry_comment);
	gtk_widget_show(sd->entry_comment);
	pref_checkbox_new_int(hbox, _("Match case"),
			      sd->search_comment_match_case, &sd->search_comment_match_case);
	gtk_widget_set_tooltip_text(GTK_WIDGET(sd->entry_comment), "This field uses Perl Compatible Regular Expressions.\ne.g. use \nabc.*ghk\n and not \nabc*ghk\n\nSee the Help file.");

	/* Search for image rating */
	hbox = menu_choice(sd->box_search, &sd->check_rating, &sd->menu_rating,
			   _("Image rating is"), &sd->match_rating_enable,
			   text_search_menu_rating, sizeof(text_search_menu_rating) / sizeof(MatchList),
			   G_CALLBACK(menu_choice_rating_cb), sd);
	sd->spin_size = menu_spin(hbox, -1, 5, sd->search_rating,
				  G_CALLBACK(menu_choice_spin_cb), &sd->search_rating);
	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_pack_start(GTK_BOX(hbox), hbox2, FALSE, FALSE, 0);
	pref_label_new(hbox2, _("and"));
	sd->spin_rating_end = menu_spin(hbox2, -1, 5, sd->search_rating_end,
				      G_CALLBACK(menu_choice_spin_cb), &sd->search_rating_end);

	/* Search for images within a specified range of a lat/long coordinate
	*/
	hbox = menu_choice(sd->box_search, &sd->check_gps, &sd->menu_gps,
			   _("Image is"), &sd->match_gps_enable,
			   text_search_menu_gps, sizeof(text_search_menu_gps) / sizeof(MatchList),
			   G_CALLBACK(menu_choice_gps_cb), sd);

	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_pack_start(GTK_BOX(hbox), hbox2, FALSE, FALSE, 0);
	sd->spin_gps = menu_spin(hbox2, 1, 9999, sd->search_gps,
								   G_CALLBACK(menu_choice_spin_cb), &sd->search_gps);

	sd->units_gps = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->units_gps), _("km"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->units_gps), _("miles"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->units_gps), _("n.m."));
	gtk_box_pack_start(GTK_BOX(hbox2), sd->units_gps, FALSE, FALSE, 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(sd->units_gps), 0);
	gtk_widget_set_tooltip_text(sd->units_gps, "kilometres, miles or nautical miles");
	gtk_widget_show(sd->units_gps);

	pref_label_new(hbox2, _("from"));

	sd->entry_gps_coord = gtk_entry_new();
	gtk_editable_set_editable(GTK_EDITABLE(sd->entry_gps_coord), TRUE);
	gtk_widget_set_has_tooltip(sd->entry_gps_coord, TRUE);
	gtk_widget_set_tooltip_text(sd->entry_gps_coord, _("Enter a coordinate in the form:\n89.123 179.456\nor drag-and-drop a geo-coded image\nor left-click on the map and paste\nor cut-and-paste or drag-and-drop\nan internet search URL\nSee the Help file"));
	gtk_box_pack_start(GTK_BOX(hbox2), sd->entry_gps_coord, TRUE, TRUE, 0);
	gtk_widget_set_sensitive(sd->entry_gps_coord, TRUE);

	gtk_widget_show(sd->entry_gps_coord);

	/* Search for image class */
	hbox = menu_choice(sd->box_search, &sd->check_class, &sd->menu_class,
			   _("Image class"), &sd->match_class_enable,
			   text_search_menu_class, sizeof(text_search_menu_class) / sizeof(MatchList),
			   G_CALLBACK(menu_choice_class_cb), sd);

	sd->class_type = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->class_type), _("Image"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->class_type), _("Raw Image"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->class_type), _("Video"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->class_type), _("Document"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->class_type), _("Metadata"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->class_type), _("Unknown"));
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->class_type), _("Broken"));
	gtk_box_pack_start(GTK_BOX(hbox), sd->class_type, FALSE, FALSE, 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(sd->class_type), 0);
	gtk_widget_show(sd->class_type);

	/* Search for image marks */
	hbox = menu_choice(sd->box_search, &sd->check_class, &sd->menu_marks,
			   _("Marks"), &sd->match_marks_enable,
			   text_search_menu_marks, sizeof(text_search_menu_marks) / sizeof(MatchList),
			   G_CALLBACK(menu_choice_marks_cb), sd);

	sd->marks_type = gtk_combo_box_text_new();
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->marks_type), _("Any mark"));
	for (i = 0; i < FILEDATA_MARKS_SIZE; i++)
		{
		marks_string = g_strdup_printf("%s%d", _("Mark "), i + 1);
		if (g_strcmp0(marks_string, options->marks_tooltips[i]) != 0)
			{
			g_free(marks_string);
			marks_string = g_strdup_printf("%s%d %s", _("Mark "), i + 1,
											options->marks_tooltips[i]);
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->marks_type), marks_string);
			}
		else
			{
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->marks_type), marks_string);
			}
		g_free(marks_string);
		}
	gtk_box_pack_start(GTK_BOX(hbox), sd->marks_type, FALSE, FALSE, 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(sd->marks_type), 0);
	gtk_widget_show(sd->marks_type);

	/* Done the types of searches */

	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	store = gtk_list_store_new(8, G_TYPE_POINTER, G_TYPE_INT, GDK_TYPE_PIXBUF,
				   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				   G_TYPE_STRING, G_TYPE_STRING);

	/* set up sorting */
	sortable = GTK_TREE_SORTABLE(store);
	gtk_tree_sortable_set_sort_func(sortable, SEARCH_COLUMN_RANK, search_result_sort_cb,
				  GINT_TO_POINTER(SEARCH_COLUMN_RANK), nullptr);
	gtk_tree_sortable_set_sort_func(sortable, SEARCH_COLUMN_NAME, search_result_sort_cb,
				  GINT_TO_POINTER(SEARCH_COLUMN_NAME), nullptr);
	gtk_tree_sortable_set_sort_func(sortable, SEARCH_COLUMN_SIZE, search_result_sort_cb,
				  GINT_TO_POINTER(SEARCH_COLUMN_SIZE), nullptr);
	gtk_tree_sortable_set_sort_func(sortable, SEARCH_COLUMN_DATE, search_result_sort_cb,
				  GINT_TO_POINTER(SEARCH_COLUMN_DATE), nullptr);
	gtk_tree_sortable_set_sort_func(sortable, SEARCH_COLUMN_DIMENSIONS, search_result_sort_cb,
				  GINT_TO_POINTER(SEARCH_COLUMN_DIMENSIONS), nullptr);
	gtk_tree_sortable_set_sort_func(sortable, SEARCH_COLUMN_PATH, search_result_sort_cb,
				  GINT_TO_POINTER(SEARCH_COLUMN_PATH), nullptr);

#if 0
	/* by default, search results are unsorted until user selects a sort column - for speed,
	 * using sort slows search speed by an order of magnitude with 1000's of results :-/
	 */
	gtk_tree_sortable_set_sort_column_id(sortable, SEARCH_COLUMN_PATH, GTK_SORT_ASCENDING);
#endif

	sd->result_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);
	gtk_container_add(GTK_CONTAINER(scrolled), sd->result_view);
	gtk_widget_show(sd->result_view);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sd->result_view));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_MULTIPLE);
	gtk_tree_selection_set_select_function(selection, search_result_select_cb, sd, nullptr);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(sd->result_view), TRUE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(sd->result_view), FALSE);

	search_result_add_column(sd, SEARCH_COLUMN_RANK, _("Rank"), FALSE, FALSE);
	search_result_add_column(sd, SEARCH_COLUMN_THUMB, _("Thumb"), TRUE, FALSE);
	search_result_add_column(sd, SEARCH_COLUMN_NAME, _("Name"), FALSE, FALSE);
	search_result_add_column(sd, SEARCH_COLUMN_SIZE, _("Size"), FALSE, TRUE);
	search_result_add_column(sd, SEARCH_COLUMN_DATE, _("Date"), FALSE, TRUE);
	search_result_add_column(sd, SEARCH_COLUMN_DIMENSIONS, _("Dimensions"), FALSE, FALSE);
	search_result_add_column(sd, SEARCH_COLUMN_PATH, _("Path"), FALSE, FALSE);

	search_dnd_init(sd);

	g_signal_connect(G_OBJECT(sd->result_view), "button_press_event",
			 G_CALLBACK(search_result_press_cb), sd);
	g_signal_connect(G_OBJECT(sd->result_view), "button_release_event",
			 G_CALLBACK(search_result_release_cb), sd);
	g_signal_connect(G_OBJECT(sd->result_view), "key_press_event",
			 G_CALLBACK(search_result_keypress_cb), sd);

	hbox = pref_box_new(vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);

	sd->button_thumbs = pref_checkbox_new(hbox, _("Thumbnails"), FALSE,
					      G_CALLBACK(search_thumb_toggle_cb), sd);
	gtk_widget_set_tooltip_text(GTK_WIDGET(sd->button_thumbs), "Ctrl-T");

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(hbox), frame, TRUE, TRUE, PREF_PAD_SPACE);
	gtk_widget_show(frame);

	sd->label_status = gtk_label_new("");
	gtk_widget_set_size_request(sd->label_status, 50, -1);
	gtk_container_add(GTK_CONTAINER(frame), sd->label_status);
	gtk_widget_show(sd->label_status);

	sd->label_progress = gtk_progress_bar_new();
	gtk_widget_set_size_request(sd->label_progress, 50, -1);

	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(sd->label_progress), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(sd->label_progress), TRUE);

	gtk_box_pack_start(GTK_BOX(hbox), sd->label_progress, TRUE, TRUE, 0);
	gtk_widget_show(sd->label_progress);

	sd->spinner = spinner_new(nullptr, -1);
	gtk_box_pack_start(GTK_BOX(hbox), sd->spinner, FALSE, FALSE, 0);
	gtk_widget_show(sd->spinner);

	sd->button_help = pref_button_new(hbox, GQ_ICON_HELP, _("Help"), G_CALLBACK(search_window_help_cb), sd);
	gtk_widget_set_tooltip_text(GTK_WIDGET(sd->button_help), "F1");
	gtk_widget_set_sensitive(sd->button_help, TRUE);
	pref_spacer(hbox, PREF_PAD_BUTTON_GAP);
	sd->button_start = pref_button_new(hbox, GQ_ICON_FIND, nullptr,
					   G_CALLBACK(search_start_cb), sd);
	gtk_widget_set_tooltip_text(GTK_WIDGET(sd->button_start), "Ctrl-Return");
	pref_spacer(hbox, PREF_PAD_BUTTON_GAP);
	sd->button_stop = pref_button_new(hbox, GQ_ICON_STOP, _("Stop"),
					  G_CALLBACK(search_start_cb), sd);
	gtk_widget_set_tooltip_text(GTK_WIDGET(sd->button_stop), "Ctrl-Return");
	gtk_widget_set_sensitive(sd->button_stop, FALSE);
	pref_spacer(hbox, PREF_PAD_BUTTON_GAP);
	sd->button_close = pref_button_new(hbox, GQ_ICON_CLOSE, _("Close"), G_CALLBACK(search_window_close_cb), sd);
	gtk_widget_set_tooltip_text(GTK_WIDGET(sd->button_close), "Ctrl-W");
	gtk_widget_set_sensitive(sd->button_close, TRUE);

	search_result_thumb_enable(sd, TRUE);
	search_result_thumb_enable(sd, FALSE);
	column = gtk_tree_view_get_column(GTK_TREE_VIEW(sd->result_view), SEARCH_COLUMN_RANK - 1);
	gtk_tree_view_column_set_visible(column, FALSE);

	search_status_update(sd);
	search_progress_update(sd, FALSE, -1.0);

	search_window_list = g_list_append(search_window_list, sd);

	file_data_register_notify_func(search_notify_cb, sd, NOTIFY_PRIORITY_MEDIUM);

	gtk_widget_show(sd->window);
}

/*
 *-------------------------------------------------------------------
 * maintenance (move, delete, etc.)
 *-------------------------------------------------------------------
 */

static void search_result_change_path(SearchData *sd, FileData *fd)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	gboolean valid;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(sd->result_view));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		GtkTreeIter current;
		MatchFileData *mfd;

		current = iter;
		valid = gtk_tree_model_iter_next(store, &iter);

		gtk_tree_model_get(store, &current, SEARCH_COLUMN_POINTER, &mfd, -1);
		if (mfd->fd == fd)
			{
			if (fd->change && fd->change->dest)
				{
				gtk_list_store_set(GTK_LIST_STORE(store), &current,
						   SEARCH_COLUMN_NAME, mfd->fd->name,
						   SEARCH_COLUMN_PATH, mfd->fd->path, -1);
				}
			else
				{
				search_result_remove_item(sd, mfd, &current);
				}
			}
		}
}

static void search_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!(type & NOTIFY_CHANGE) || !fd->change) return;

	DEBUG_1("Notify search: %s %04x", fd->path, type);

	switch (fd->change->type)
		{
		case FILEDATA_CHANGE_MOVE:
		case FILEDATA_CHANGE_RENAME:
		case FILEDATA_CHANGE_DELETE:
			search_result_change_path(sd, fd);
			break;
		case FILEDATA_CHANGE_COPY:
		case FILEDATA_CHANGE_UNSPECIFIED:
		case FILEDATA_CHANGE_WRITE_METADATA:
			break;
		}
}

void mfd_list_free(GList *list)
{
	GList *work;

	work = list;
	while (work)
		{
		auto mfd = static_cast<MatchFileData *>(work->data);
		file_data_unref((FileData *)mfd->fd);
		work = work->next;
		}

	g_list_free(list);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
