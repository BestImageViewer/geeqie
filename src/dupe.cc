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

#include <cinttypes>

#include "main.h"
#include "dupe.h"

#include "cache.h"
#include "collect-table.h"
#include "dnd.h"
#include "filedata.h"
#include "history-list.h"
#include "image-load.h"
#include "img-view.h"
#include "layout-image.h"
#include "layout-util.h"
#include "md5-util.h"
#include "menu.h"
#include "misc.h"
#include "pixbuf-util.h"
#include "print.h"
#include "thumb.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tree-edit.h"
#include "uri-utils.h"
#include "utilops.h"
#include "window.h"

#include <cmath>


#define DUPE_DEF_WIDTH 800
#define DUPE_DEF_HEIGHT 400
#define DUPE_PROGRESS_PULSE_STEP 0.0001

/** column assignment order (simply change them here)
 */
enum {
	DUPE_COLUMN_POINTER = 0,
	DUPE_COLUMN_RANK,
	DUPE_COLUMN_THUMB,
	DUPE_COLUMN_NAME,
	DUPE_COLUMN_SIZE,
	DUPE_COLUMN_DATE,
	DUPE_COLUMN_DIMENSIONS,
	DUPE_COLUMN_PATH,
	DUPE_COLUMN_COLOR,
	DUPE_COLUMN_SET,
	DUPE_COLUMN_COUNT	/**< total columns */
};

enum DUPE_CHECK_RESULT {
	DUPE_MATCH = 0,
	DUPE_NO_MATCH,
	DUPE_NAME_MATCH
};

/** Used for similarity checks. One for each item pushed
 * onto the thread pool.
 */
struct DupeQueueItem
{
	DupeItem *needle;
	DupeWindow *dw;
	GList *work; /**< pointer into \a dw->list or \a dw->second_list (#DupeItem) */
	gint index; /**< The order items pushed onto thread pool. Used to sort returned matches */
};

/** Used for similarity checks thread. One for each pair match found.
 */
struct DupeSearchMatch
{
	DupeItem *a; /**< \a a / \a b matched pair found */
	DupeItem *b; /**< \a a / \a b matched pair found */
	gdouble rank;
	gint index; /**< The order items pushed onto thread pool. Used to sort returned matches */
};

static DupeMatchType param_match_mask;
static GList *dupe_window_list = nullptr;	/**< list of open DupeWindow *s */

/*
 * Well, after adding the 'compare two sets' option things got a little sloppy in here
 * because we have to account for two 'modes' everywhere. (be careful).
 */

static void dupe_match_unlink(DupeItem *a, DupeItem *b);
static DupeItem *dupe_match_find_parent(DupeWindow *dw, DupeItem *child);

static gint dupe_match(DupeItem *a, DupeItem *b, DupeMatchType mask, gdouble *rank, gint fast);

static void dupe_thumb_step(DupeWindow *dw);
static gint dupe_check_cb(gpointer data);

static void dupe_second_add(DupeWindow *dw, DupeItem *di);
static void dupe_second_remove(DupeWindow *dw, DupeItem *di);
static GtkWidget *dupe_menu_popup_second(DupeWindow *dw, DupeItem *di);

static void dupe_dnd_init(DupeWindow *dw);

static void dupe_notify_cb(FileData *fd, NotifyType type, gpointer data);
static void delete_finished_cb(gboolean success, const gchar *dest_path, gpointer data);

static GtkWidget *submenu_add_export(GtkWidget *menu, GtkWidget **menu_item, GCallback func, gpointer data);
static void dupe_pop_menu_export_cb(GtkWidget *widget, gpointer data);

static void dupe_init_list_cache(DupeWindow *dw);
static void dupe_destroy_list_cache(DupeWindow *dw);
static gboolean dupe_insert_in_list_cache(DupeWindow *dw, FileData *fd);

static void dupe_match_link(DupeItem *a, DupeItem *b, gdouble rank);
static gint dupe_match_link_exists(DupeItem *child, DupeItem *parent);

/**
 * This array must be kept in sync with the contents of:\n
 *  @link dupe_window_keypress_cb() @endlink \n
 *  @link dupe_menu_popup_main() @endlink
 *
 * See also @link hard_coded_window_keys @endlink
 **/
hard_coded_window_keys dupe_window_keys[] = {
	{GDK_CONTROL_MASK, 'C', N_("Copy")},
	{GDK_CONTROL_MASK, 'M', N_("Move")},
	{GDK_CONTROL_MASK, 'R', N_("Rename")},
	{GDK_CONTROL_MASK, 'D', N_("Move to Trash")},
	{GDK_SHIFT_MASK, GDK_KEY_Delete, N_("Delete")},
	{static_cast<GdkModifierType>(0), GDK_KEY_Delete, N_("Remove")},
	{GDK_CONTROL_MASK, GDK_KEY_Delete, N_("Clear")},
	{GDK_CONTROL_MASK, 'A', N_("Select all")},
	{static_cast<GdkModifierType>(GDK_CONTROL_MASK + GDK_SHIFT_MASK), 'A', N_("Select none")},
	{GDK_CONTROL_MASK, 'T', N_("Toggle thumbs")},
	{GDK_CONTROL_MASK, 'W', N_("Close window")},
	{static_cast<GdkModifierType>(0), GDK_KEY_Return, N_("View")},
	{static_cast<GdkModifierType>(0), 'V', N_("View in new window")},
	{static_cast<GdkModifierType>(0), 'C', N_("Collection from selection")},
	{GDK_CONTROL_MASK, 'L', N_("Append list")},
	{static_cast<GdkModifierType>(0), '0', N_("Select none")},
	{static_cast<GdkModifierType>(0), '1', N_("Select group 1 duplicates")},
	{static_cast<GdkModifierType>(0), '2', N_("Select group 2 duplicates")},
	{static_cast<GdkModifierType>(0), 0, nullptr}
};

/**
 * @brief The function run in threads for similarity checks
 * @param d1 #DupeQueueItem
 * @param d2 #DupeWindow
 *
 * Used only for similarity checks.\n
 * Search \a dqi->list for \a dqi->needle and if a match is
 * found, create a #DupeSearchMatch and add to \a dw->search_matches list\n
 * If \a dw->abort is set, just increment \a dw->thread_count
 */
static void dupe_comparison_func(gpointer d1, gpointer d2)
{
	auto dqi = static_cast<DupeQueueItem *>(d1);
	auto dw = static_cast<DupeWindow *>(d2);
	DupeSearchMatch *dsm;
	DupeItem *di;
	GList *matches = nullptr;
	gdouble rank = 0;

	if (!dw->abort)
		{
		GList *work = dqi->work;
		while (work)
			{
			di = static_cast<DupeItem *>(work->data);

			/* forward for second set, back for simple compare */
			if (dw->second_set)
				{
				work = work->next;
				}
			else
				{
				work = work->prev;
				}

			if (dupe_match(di, dqi->needle, dqi->dw->match_mask, &rank, TRUE))
				{
				dsm = g_new0(DupeSearchMatch, 1);
				dsm->a = di;
				dsm->b = dqi->needle;
				dsm->rank = rank;
				matches = g_list_prepend(matches, dsm);
				dsm->index = dqi->index;
				}

			if (dw->abort)
				{
				break;
				}
			}

		matches = g_list_reverse(matches);
		g_mutex_lock(&dw->search_matches_mutex);
		dw->search_matches = g_list_concat(dw->search_matches, matches);
		g_mutex_unlock(&dw->search_matches_mutex);
		}

	g_mutex_lock(&dw->thread_count_mutex);
	dw->thread_count++;
	g_mutex_unlock(&dw->thread_count_mutex);
	g_free(dqi);
}

/*
 * ------------------------------------------------------------------
 * Window updates
 * ------------------------------------------------------------------
 */

/**
 * @brief Update display of status label
 * @param dw
 * @param count_only
 *
 *
 */
static void dupe_window_update_count(DupeWindow *dw, gboolean count_only)
{
	gchar *text;

	if (!dw->list)
		{
		text = g_strdup(_("Drop files to compare them."));
		}
	else if (count_only)
		{
		text = g_strdup_printf(_("%d files"), g_list_length(dw->list));
		}
	else
		{
		text = g_strdup_printf(_("%d matches found in %d files"), g_list_length(dw->dupes), g_list_length(dw->list));
		}

	if (dw->second_set)
		{
		gchar *buf = g_strconcat(text, " ", _("[set 1]"), NULL);
		g_free(text);
		text = buf;
		}
	gtk_label_set_text(GTK_LABEL(dw->status_label), text);

	g_free(text);
}

/**
 * @brief Returns time in µsec since Epoch
 * @returns
 *
 *
 */
static guint64 msec_time()
{
	struct timeval tv;

	if (gettimeofday(&tv, nullptr) == -1) return 0;

	return static_cast<guint64>(tv.tv_sec) * 1000000 + static_cast<guint64>(tv.tv_usec);
}

static gint dupe_iterations(gint n)
{
	return (n * ((n + 1) / 2));
}

/**
 * @brief
 * @param dw
 * @param status
 * @param value
 * @param force
 *
 * If \a status is blank, clear status bar text and set progress to zero. \n
 * If \a force is not set, after 2 secs has elapsed, update time-to-go every 250 ms.
 */
static void dupe_window_update_progress(DupeWindow *dw, const gchar *status, gdouble value, gboolean force)
{
	const gchar *status_text;

	if (status)
		{
		guint64 new_time = 0;

		if (dw->setup_n % 10 == 0)
			{
			new_time = msec_time() - dw->setup_time;
			}

		if (!force &&
		    value != 0.0 &&
		    dw->setup_count > 0 &&
		    new_time > 2000000)
			{
			gchar *buf;
			gint t;
			gint d;
			guint32 rem;

			if (new_time - dw->setup_time_count < 250000) return;
			dw->setup_time_count = new_time;

			if (dw->setup_done)
				{
				if (dw->second_set)
					{
					t = dw->setup_count;
					d = dw->setup_count - dw->setup_n;
					}
				else
					{
					t = dupe_iterations(dw->setup_count);
					d = dupe_iterations(dw->setup_count - dw->setup_n);
					}
				}
			else
				{
				t = dw->setup_count;
				d = dw->setup_count - dw->setup_n;
				}

			rem = (t - d) ? (static_cast<gdouble>(dw->setup_time_count / 1000000) / (t - d)) * d : 0;

			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dw->extra_label), value);

			buf = g_strdup_printf("%s %d:%02d ", status, rem / 60, rem % 60);
			gtk_progress_bar_set_text(GTK_PROGRESS_BAR(dw->extra_label), buf);
			g_free(buf);

			return;
			}
		else if (force ||
			 value == 0.0 ||
			 dw->setup_count == 0 ||
			 dw->setup_time_count == 0 ||
			 (new_time > 0 && new_time - dw->setup_time_count >= 250000))
			{
			if (dw->setup_time_count == 0) dw->setup_time_count = 1;
			if (new_time > 0) dw->setup_time_count = new_time;
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dw->extra_label), value);
			status_text = status;
			}
		else
			{
			status_text = nullptr;
			}
		}
	else
		{
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dw->extra_label), 0.0);
		status_text = " ";
		}

	if (status_text) gtk_progress_bar_set_text(GTK_PROGRESS_BAR(dw->extra_label), status_text);
}

static void widget_set_cursor(GtkWidget *widget, gint icon)
{
	GdkCursor *cursor;
	GdkDisplay *display;

	if (!gtk_widget_get_window(widget)) return;

	if (icon == -1)
		{
		cursor = nullptr;
		}
	else
		{
		display = gdk_display_get_default();
		cursor = gdk_cursor_new_for_display(display, static_cast<GdkCursorType>(icon));
		}

	gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);

	if (cursor) g_object_unref(G_OBJECT(cursor));
}

/*
 * ------------------------------------------------------------------
 * row color utils
 * ------------------------------------------------------------------
 */

static void dupe_listview_realign_colors(DupeWindow *dw)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	gboolean color_set = TRUE;
	DupeItem *parent = nullptr;
	gboolean valid;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		DupeItem *child;
		DupeItem *child_parent;

		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &child, -1);
		child_parent = dupe_match_find_parent(dw, child);
		if (!parent || parent != child_parent)
			{
			if (!parent)
				{
				/* keep the first row as it is */
				gtk_tree_model_get(store, &iter, DUPE_COLUMN_COLOR, &color_set, -1);
				}
			else
				{
				color_set = !color_set;
				}
			parent = dupe_match_find_parent(dw, child);
			}
		gtk_list_store_set(GTK_LIST_STORE(store), &iter, DUPE_COLUMN_COLOR, color_set, -1);

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		}
}

/*
 * ------------------------------------------------------------------
 * Dupe item utils
 * ------------------------------------------------------------------
 */

static DupeItem *dupe_item_new(FileData *fd)
{
	DupeItem *di;

	di = g_new0(DupeItem, 1);

	di->fd = file_data_ref(fd);
	di->group_rank = 0.0;

	return di;
}

static void dupe_item_free(DupeItem *di)
{
	file_data_unref(di->fd);
	image_sim_free(di->simd);
	g_free(di->md5sum);
	if (di->pixbuf) g_object_unref(di->pixbuf);

	g_free(di);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static DupeItem *dupe_item_find_fd_by_list_unused(FileData *fd, GList *work)
{
	while (work)
		{
		DupeItem *di = static_cast<DupeItem *>(work->data);

		if (di->fd == fd) return di;

		work = work->next;
		}

	return NULL;
}

static DupeItem *dupe_item_find_fd_unused(DupeWindow *dw, FileData *fd)
{
	DupeItem *di;

	di = dupe_item_find_fd_by_list_unused(fd, dw->list);
	if (!di && dw->second_set) di = dupe_item_find_fd_by_list_unused(fd, dw->second_list);

	return di;
}

static DupeItem *dupe_item_find_path_by_list_unused(const gchar *path, GList *work)
{
	while (work)
		{
		DupeItem *di = static_cast<DupeItem *>(work->data);

		if (strcmp(di->fd->path, path) == 0) return di;

		work = work->next;
		}

	return NULL;
}

static DupeItem *dupe_item_find_path_unused(DupeWindow *dw, const gchar *path)
{
	DupeItem *di;

	di = dupe_item_find_path_by_list_unused(path, dw->list);
	if (!di && dw->second_set) di = dupe_item_find_path_by_list_unused(path, dw->second_list);

	return di;
}
#pragma GCC diagnostic pop

/*
 * ------------------------------------------------------------------
 * Image property cache
 * ------------------------------------------------------------------
 */

static void dupe_item_read_cache(DupeItem *di)
{
	gchar *path;
	CacheData *cd;

	if (!di) return;

	path = cache_find_location(CACHE_TYPE_SIM, di->fd->path);
	if (!path) return;

	if (filetime(di->fd->path) != filetime(path))
		{
		g_free(path);
		return;
		}

	cd = cache_sim_data_load(path);
	g_free(path);

	if (cd)
		{
		if (!di->simd && cd->sim)
			{
			di->simd = cd->sim;
			cd->sim = nullptr;
			}
		if (di->width == 0 && di->height == 0 && cd->dimensions)
			{
			di->width = cd->width;
			di->height = cd->height;
			di->dimensions = (di->width << 16) + di->height;
			}
		if (!di->md5sum && cd->have_md5sum)
			{
			di->md5sum = md5_digest_to_text(cd->md5sum);
			}
		cache_sim_data_free(cd);
		}
}

static void dupe_item_write_cache(DupeItem *di)
{
	gchar *base;
	mode_t mode = 0755;

	if (!di) return;

	base = cache_get_location(CACHE_TYPE_SIM, di->fd->path, FALSE, &mode);
	if (recursive_mkdir_if_not_exists(base, mode))
		{
		CacheData *cd;

		cd = cache_sim_data_new();
		cd->path = cache_get_location(CACHE_TYPE_SIM, di->fd->path, TRUE, nullptr);

		if (di->width != 0) cache_sim_data_set_dimensions(cd, di->width, di->height);
		if (di->md5sum)
			{
			guchar digest[16];
			if (md5_digest_from_text(di->md5sum, digest)) cache_sim_data_set_md5sum(cd, digest);
			}
		if (di->simd) cache_sim_data_set_similarity(cd, di->simd);

		if (cache_sim_data_save(cd))
			{
			filetime_set(cd->path, filetime(di->fd->path));
			}
		cache_sim_data_free(cd);
		}
	g_free(base);
}

/*
 * ------------------------------------------------------------------
 * Window list utils
 * ------------------------------------------------------------------
 */

static gint dupe_listview_find_item(GtkListStore *store, DupeItem *item, GtkTreeIter *iter)
{
	gboolean valid;
	gint row = 0;

	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), iter);
	while (valid)
		{
		DupeItem *item_n;
		gtk_tree_model_get(GTK_TREE_MODEL(store), iter, DUPE_COLUMN_POINTER, &item_n, -1);
		if (item_n == item) return row;

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), iter);
		row++;
		}

	return -1;
}

static void dupe_listview_add(DupeWindow *dw, DupeItem *parent, DupeItem *child)
{
	DupeItem *di;
	gint row;
	gchar *text[DUPE_COLUMN_COUNT];
	GtkListStore *store;
	GtkTreeIter iter;
	gboolean color_set = FALSE;
	gint rank;

	if (!parent) return;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview)));

	if (child)
		{
		DupeMatch *dm;

		row = dupe_listview_find_item(store, parent, &iter);
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, DUPE_COLUMN_COLOR, &color_set, -1);

		row++;

		if (child->group)
			{
			dm = static_cast<DupeMatch *>(child->group->data);
			rank = static_cast<gint>(floor(dm->rank));
			}
		else
			{
			rank = 1;
			log_printf("NULL group in item!\n");
			}
		}
	else
		{
		if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter))
			{
			gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, DUPE_COLUMN_COLOR, &color_set, -1);
			color_set = !color_set;
			dw->set_count++;
			}
		else
			{
			color_set = FALSE;
			}
		row = 0;
		rank = 0;
		}

	di = (child) ? child : parent;

	if (!child && dw->second_set)
		{
		text[DUPE_COLUMN_RANK] = g_strdup("[1]");
		}
	else if (rank == 0)
		{
		text[DUPE_COLUMN_RANK] = g_strdup((di->second) ? "(2)" : "");
		}
	else
		{
		text[DUPE_COLUMN_RANK] = g_strdup_printf("%d%s", rank, (di->second) ? " (2)" : "");
		}

	text[DUPE_COLUMN_THUMB] = nullptr;
	text[DUPE_COLUMN_NAME] = const_cast<gchar *>(di->fd->name);
	text[DUPE_COLUMN_SIZE] = text_from_size(di->fd->size);
	text[DUPE_COLUMN_DATE] = const_cast<gchar *>(text_from_time(di->fd->date));
	if (di->width > 0 && di->height > 0)
		{
		text[DUPE_COLUMN_DIMENSIONS] = g_strdup_printf("%d x %d", di->width, di->height);
		}
	else
		{
		text[DUPE_COLUMN_DIMENSIONS] = g_strdup("");
		}
	text[DUPE_COLUMN_PATH] = di->fd->path;
	text[DUPE_COLUMN_COLOR] = nullptr;

	gtk_list_store_insert(store, &iter, row);
	gtk_list_store_set(store, &iter,
				DUPE_COLUMN_POINTER, di,
				DUPE_COLUMN_RANK, text[DUPE_COLUMN_RANK],
				DUPE_COLUMN_THUMB, NULL,
				DUPE_COLUMN_NAME, text[DUPE_COLUMN_NAME],
				DUPE_COLUMN_SIZE, text[DUPE_COLUMN_SIZE],
				DUPE_COLUMN_DATE, text[DUPE_COLUMN_DATE],
				DUPE_COLUMN_DIMENSIONS, text[DUPE_COLUMN_DIMENSIONS],
				DUPE_COLUMN_PATH, text[DUPE_COLUMN_PATH],
				DUPE_COLUMN_COLOR, color_set,
				DUPE_COLUMN_SET, dw->set_count,
				-1);

	g_free(text[DUPE_COLUMN_RANK]);
	g_free(text[DUPE_COLUMN_SIZE]);
	g_free(text[DUPE_COLUMN_DIMENSIONS]);
}

static void dupe_listview_select_dupes(DupeWindow *dw, DupeSelectType parents);

static void dupe_listview_populate(DupeWindow *dw)
{
	GtkListStore *store;
	GList *work;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview)));
	gtk_list_store_clear(store);

	work = g_list_last(dw->dupes);
	while (work)
		{
		auto parent = static_cast<DupeItem *>(work->data);
		GList *temp;

		dupe_listview_add(dw, parent, nullptr);

		temp = g_list_last(parent->group);
		while (temp)
			{
			auto dm = static_cast<DupeMatch *>(temp->data);
			DupeItem *child;

			child = dm->di;

			dupe_listview_add(dw, parent, child);

			temp = temp->prev;
			}

		work = work->prev;
		}

	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(dw->listview));

	if (options->duplicates_select_type == DUPE_SELECT_GROUP1)
		{
		dupe_listview_select_dupes(dw, DUPE_SELECT_GROUP1);
		}
	else if (options->duplicates_select_type == DUPE_SELECT_GROUP2)
		{
		dupe_listview_select_dupes(dw, DUPE_SELECT_GROUP2);
		}

}

static void dupe_listview_remove(DupeWindow *dw, DupeItem *di)
{
	GtkListStore *store;
	GtkTreeIter iter;
	gint row;

	if (!di) return;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview)));
	row = dupe_listview_find_item(store, di, &iter);
	if (row < 0) return;

	tree_view_move_cursor_away(GTK_TREE_VIEW(dw->listview), &iter, TRUE);
	gtk_list_store_remove(store, &iter);

	if (g_list_find(dw->dupes, di) != nullptr)
		{
		if (!dw->color_frozen) dupe_listview_realign_colors(dw);
		}
}


static GList *dupe_listview_get_filelist(DupeWindow *, GtkWidget *listview)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	gboolean valid;
	GList *list = nullptr;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(listview));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		DupeItem *di;
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		list = g_list_prepend(list, file_data_ref(di->fd));

		valid = gtk_tree_model_iter_next(store, &iter);
		}

	return g_list_reverse(list);
}


static GList *dupe_listview_get_selection(DupeWindow *, GtkWidget *listview)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	GList *list = nullptr;
	GList *work;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (work)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		DupeItem *di = nullptr;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		if (di)
			{
			list = g_list_prepend(list, file_data_ref(di->fd));
			}
		work = work->next;
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	return g_list_reverse(list);
}

static gboolean dupe_listview_item_is_selected(DupeWindow *, DupeItem *di, GtkWidget *listview)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	GList *work;
	gboolean found = FALSE;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (!found && work)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		DupeItem *di_n;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di_n, -1);
		if (di_n == di) found = TRUE;
		work = work->next;
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	return found;
}

static void dupe_listview_select_dupes(DupeWindow *dw, DupeSelectType parents)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	gboolean valid;
	gint set_count = 0;
	gint set_count_last = -1;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	gtk_tree_selection_unselect_all(selection);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		DupeItem *di;

		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, DUPE_COLUMN_SET, &set_count, -1);
		if (set_count != set_count_last)
			{
			set_count_last = set_count;
			if (parents == DUPE_SELECT_GROUP1)
				{
				gtk_tree_selection_select_iter(selection, &iter);
				}
			}
		else
			{
			if (parents == DUPE_SELECT_GROUP2)
				{
				gtk_tree_selection_select_iter(selection, &iter);
				}
			}
		valid = gtk_tree_model_iter_next(store, &iter);
		}
}

/*
 * ------------------------------------------------------------------
 * Match group manipulation
 * ------------------------------------------------------------------
 */

/**
 * @brief Search \a parent->group for \a child (#DupeItem)
 * @param child
 * @param parent
 * @returns
 *
 */
static DupeMatch *dupe_match_find_match(DupeItem *child, DupeItem *parent)
{
	GList *work;

	work = parent->group;
	while (work)
		{
		auto dm = static_cast<DupeMatch *>(work->data);
		if (dm->di == child) return dm;
		work = work->next;
		}
	return nullptr;
}

/**
 * @brief Create #DupeMatch structure for \a child, and insert into \a parent->group list.
 * @param child
 * @param parent
 * @param rank
 *
 */
static void dupe_match_link_child(DupeItem *child, DupeItem *parent, gdouble rank)
{
	DupeMatch *dm;

	dm = g_new0(DupeMatch, 1);
	dm->di = child;
	dm->rank = rank;
	parent->group = g_list_append(parent->group, dm);
}

/**
 * @brief Link \a a & \a b as both parent and child
 * @param a
 * @param b
 * @param rank
 *
 * Link \a a as child of \a b, and \a b as child of \a a
 */
static void dupe_match_link(DupeItem *a, DupeItem *b, gdouble rank)
{
	dupe_match_link_child(a, b, rank);
	dupe_match_link_child(b, a, rank);
}

/**
 * @brief Remove \a child #DupeMatch from \a parent->group list.
 * @param child
 * @param parent
 *
 */
static void dupe_match_unlink_child(DupeItem *child, DupeItem *parent)
{
	DupeMatch *dm;

	dm = dupe_match_find_match(child, parent);
	if (dm)
		{
		parent->group = g_list_remove(parent->group, dm);
		g_free(dm);
		}
}

/**
 * @brief  Unlink \a a from \a b, and \a b from \a a
 * @param a
 * @param b
 *
 * Free the relevant #DupeMatch items from the #DupeItem group lists
 */
static void dupe_match_unlink(DupeItem *a, DupeItem *b)
{
	dupe_match_unlink_child(a, b);
	dupe_match_unlink_child(b, a);
}

/**
 * @brief
 * @param parent
 * @param unlink_children
 *
 * If \a unlink_children is set, unlink all entries in \a parent->group list. \n
 * Free the \a parent->group list and set group_rank to zero;
 */
static void dupe_match_link_clear(DupeItem *parent, gboolean unlink_children)
{
	if (unlink_children)
		{
		GList *work;

		work = parent->group;
		while (work)
			{
			auto dm = static_cast<DupeMatch *>(work->data);
			work = work->next;

			dupe_match_unlink_child(parent, dm->di);
			}
		}

	g_list_free_full(parent->group, g_free);
	parent->group = nullptr;
	parent->group_rank = 0.0;
}

/**
 * @brief Search \a parent->group list for \a child
 * @param child
 * @param parent
 * @returns boolean TRUE/FALSE found/not found
 *
 */
static gint dupe_match_link_exists(DupeItem *child, DupeItem *parent)
{
	return (dupe_match_find_match(child, parent) != nullptr);
}

/**
 * @brief  Search \a parent->group for \a child, and return \a child->rank
 * @param child
 * @param parent
 * @returns \a dm->di->rank
 *
 */
static gdouble dupe_match_link_rank(DupeItem *child, DupeItem *parent)
{
	DupeMatch *dm;

	dm = dupe_match_find_match(child, parent);
	if (dm) return dm->rank;

	return 0.0;
}

/**
 * @brief Find highest rank in \a child->group
 * @param child
 * @returns
 *
 * Search the #DupeMatch entries in the \a child->group list.
 * Return the #DupeItem with the highest rank. If more than one have
 * the same rank, the first encountered is used.
 */
static DupeItem *dupe_match_highest_rank(DupeItem *child)
{
	DupeMatch *dr;
	GList *work;

	dr = nullptr;
	work = child->group;
	while (work)
		{
		auto dm = static_cast<DupeMatch *>(work->data);
		if (!dr || dm->rank > dr->rank)
			{
			dr = dm;
			}
		work = work->next;
		}

	return (dr) ? dr->di : nullptr;
}

/**
 * @brief Compute and store \a parent->group_rank
 * @param parent
 *
 * Group_rank = (sum of all child ranks) / n
 */
static void dupe_match_rank_update(DupeItem *parent)
{
	GList *work;
	gdouble rank = 0.0;
	gint c = 0;

	work = parent->group;
	while (work)
		{
		auto dm = static_cast<DupeMatch *>(work->data);
		work = work->next;
		rank += dm->rank;
		c++;
		}

	if (c > 0)
		{
		parent->group_rank = rank / c;
		}
	else
		{
		parent->group_rank = 0.0;
		}
}

static DupeItem *dupe_match_find_parent(DupeWindow *dw, DupeItem *child)
{
	GList *work;

	if (g_list_find(dw->dupes, child)) return child;

	work = child->group;
	while (work)
		{
		auto dm = static_cast<DupeMatch *>(work->data);
		if (g_list_find(dw->dupes, dm->di)) return dm->di;
		work = work->next;
		}

	return nullptr;
}

/**
 * @brief
 * @param work (#DupeItem) dw->list or dw->second_list
 *
 * Unlink all #DupeItem-s in \a work.
 * Do not unlink children.
 */
static void dupe_match_reset_list(GList *work)
{
	while (work)
		{
		auto di = static_cast<DupeItem *>(work->data);
		work = work->next;

		dupe_match_link_clear(di, FALSE);
		}
}

static void dupe_match_reparent(DupeWindow *dw, DupeItem *old_parent, DupeItem *new_parent)
{
	GList *work;

	if (!old_parent || !new_parent || !dupe_match_link_exists(old_parent, new_parent)) return;

	dupe_match_link_clear(new_parent, TRUE);
	work = old_parent->group;
	while (work)
		{
		auto dm = static_cast<DupeMatch *>(work->data);
		dupe_match_unlink_child(old_parent, dm->di);
		dupe_match_link_child(new_parent, dm->di, dm->rank);
		work = work->next;
		}

	new_parent->group = old_parent->group;
	old_parent->group = nullptr;

	work = g_list_find(dw->dupes, old_parent);
	if (work) work->data = new_parent;
}

static void dupe_match_print_group(DupeItem *di)
{
	GList *work;

	log_printf("+ %f %s\n", di->group_rank, di->fd->name);

	work = di->group;
	while (work)
		{
		auto dm = static_cast<DupeMatch *>(work->data);
		work = work->next;

		log_printf("  %f %s\n", dm->rank, dm->di->fd->name);
		}

	log_printf("\n");
}

static void dupe_match_print_list(GList *list)
{
	GList *work;

	work = list;
	while (work)
		{
		auto di = static_cast<DupeItem *>(work->data);
		dupe_match_print_group(di);
		work = work->next;
		}
}

/* level 3, unlinking and orphan handling */
/**
 * @brief
 * @param child
 * @param parent \a di from \a child->group
 * @param[inout] list \a dw->list sorted by rank (#DupeItem)
 * @param dw
 * @returns modified \a list
 *
 * Called for each entry in \a child->group (#DupeMatch) with \a parent set to \a dm->di. \n
 * Find the highest rank #DupeItem of the \a parent's children. \n
 * If that is == \a child OR
 * highest rank #DupeItem of \a child == \a parent then FIXME:
 *
 */
static GList *dupe_match_unlink_by_rank(DupeItem *child, DupeItem *parent, GList *list, DupeWindow *dw)
{
	DupeItem *best = nullptr;

	best = dupe_match_highest_rank(parent); // highest rank in parent->group
	if (best == child || dupe_match_highest_rank(child) == parent)
		{
		GList *work;
		gdouble rank;

		DEBUG_2("link found %s to %s [%d]", child->fd->name, parent->fd->name, g_list_length(parent->group));

		work = parent->group;
		while (work)
			{
			auto dm = static_cast<DupeMatch *>(work->data);
			DupeItem *orphan;

			work = work->next;
			orphan = dm->di;
			if (orphan != child && g_list_length(orphan->group) < 2)
				{
				dupe_match_link_clear(orphan, TRUE);
				if (!dw->second_set || orphan->second)
					{
					dupe_match(orphan, child, dw->match_mask, &rank, FALSE);
					dupe_match_link(orphan, child, rank);
					}
				list = g_list_remove(list, orphan);
				}
			}

		rank = dupe_match_link_rank(child, parent); // child->rank
		dupe_match_link_clear(parent, TRUE);
		dupe_match_link(child, parent, rank);
		list = g_list_remove(list, parent);
		}
	else
		{
		DEBUG_2("unlinking %s and %s", child->fd->name, parent->fd->name);

		dupe_match_unlink(child, parent);
		}

	return list;
}

/* level 2 */
/**
 * @brief
 * @param[inout] list \a dw->list sorted by rank (#DupeItem)
 * @param di
 * @param dw
 * @returns modified \a list
 *
 * Called for each entry in \a list.
 * Call unlink for each child in \a di->group
 */
static GList *dupe_match_group_filter(GList *list, DupeItem *di, DupeWindow *dw)
{
	GList *work;

	work = g_list_last(di->group);
	while (work)
		{
		auto dm = static_cast<DupeMatch *>(work->data);
		work = work->prev;
		list = dupe_match_unlink_by_rank(di, dm->di, list, dw);
		}

	return list;
}

/* level 1 (top) */
/**
 * @brief
 * @param[inout] list \a dw->list sorted by rank (#DupeItem)
 * @param dw
 * @returns Filtered \a list
 *
 * Called once.
 * Call group filter for each \a di in \a list
 */
static GList *dupe_match_group_trim(GList *list, DupeWindow *dw)
{
	GList *work;

	work = list;
	while (work)
		{
		auto di = static_cast<DupeItem *>(work->data);
		if (!di->second) list = dupe_match_group_filter(list, di, dw);
		work = work->next;
		if (di->second) list = g_list_remove(list, di);
		}

	return list;
}

static gint dupe_match_sort_groups_cb(gconstpointer a, gconstpointer b)
{
	auto da = static_cast<const DupeMatch *>(a);
	auto db = static_cast<const DupeMatch *>(b);

	if (da->rank > db->rank) return -1;
	if (da->rank < db->rank) return 1;
	return 0;
}

/**
 * @brief Sorts the children of each #DupeItem in \a list
 * @param list #DupeItem
 *
 * Sorts the #DupeItem->group children on rank
 */
static void dupe_match_sort_groups(GList *list)
{
	GList *work;

	work = list;
	while (work)
		{
		auto di = static_cast<DupeItem *>(work->data);
		di->group = g_list_sort(di->group, dupe_match_sort_groups_cb);
		work = work->next;
		}
}

static gint dupe_match_totals_sort_cb(gconstpointer a, gconstpointer b)
{
	auto da = static_cast<const DupeItem *>(a);
	auto db = static_cast<const DupeItem *>(b);

	if (g_list_length(da->group) > g_list_length(db->group)) return -1;
	if (g_list_length(da->group) < g_list_length(db->group)) return 1;

	if (da->group_rank < db->group_rank) return -1;
	if (da->group_rank > db->group_rank) return 1;

	return 0;
}

/**
 * @brief Callback for group_rank sort
 * @param a
 * @param b
 * @returns
 *
 *
 */
static gint dupe_match_rank_sort_cb(gconstpointer a, gconstpointer b)
{
	auto da = static_cast<const DupeItem *>(a);
	auto db = static_cast<const DupeItem *>(b);

	if (da->group_rank > db->group_rank) return -1;
	if (da->group_rank < db->group_rank) return 1;
	return 0;
}

/**
 * @brief Sorts \a source_list by group-rank
 * @param source_list #DupeItem
 * @returns
 *
 * Computes group_rank for each #DupeItem. \n
 * Items with no group list are ignored.
 * Returns allocated GList of #DupeItem-s sorted by group_rank
 */
static GList *dupe_match_rank_sort(GList *source_list)
{
	GList *list = nullptr;
	GList *work;

	work = source_list;
	while (work)
		{
		auto di = static_cast<DupeItem *>(work->data);

		if (di->group)
			{
			dupe_match_rank_update(di); // Compute and store group_rank for di
			list = g_list_prepend(list, di);
			}

		work = work->next;
		}

	return g_list_sort(list, dupe_match_rank_sort_cb);
}

/**
 * @brief Returns allocated GList of dupes sorted by totals
 * @param source_list
 * @returns
 *
 *
 */
static GList *dupe_match_totals_sort(GList *source_list)
{
	source_list = g_list_sort(source_list, dupe_match_totals_sort_cb);

	source_list = g_list_first(source_list);
	return g_list_reverse(source_list);
}

/**
 * @brief
 * @param dw
 *
 * Called once.
 */
static void dupe_match_rank(DupeWindow *dw)
{
	GList *list;

	list = dupe_match_rank_sort(dw->list); // sorted by group_rank, no-matches filtered out

	if (required_debug_level(2)) dupe_match_print_list(list);

	DEBUG_1("Similar items: %d", g_list_length(list));
	list = dupe_match_group_trim(list, dw);
	DEBUG_1("Unique groups: %d", g_list_length(list));

	dupe_match_sort_groups(list);

	if (required_debug_level(2)) dupe_match_print_list(list);

	list = dupe_match_rank_sort(list);
	if (options->sort_totals)
		{
		list = dupe_match_totals_sort(list);
		}
	if (required_debug_level(2)) dupe_match_print_list(list);

	g_list_free(dw->dupes);
	dw->dupes = list;
}

/*
 * ------------------------------------------------------------------
 * Match group tests
 * ------------------------------------------------------------------
 */

/**
 * @brief
 * @param[in] a
 * @param[in] b
 * @param[in] mask
 * @param[out] rank
 * @param[in] fast
 * @returns
 *
 * For similarity checks, compute rank - (similarity factor between a and b). \n
 * If rank < user-set sim value, returns FALSE.
 */
static gboolean dupe_match(DupeItem *a, DupeItem *b, DupeMatchType mask, gdouble *rank, gint fast)
{
	*rank = 0.0;

	if (a->fd->path == b->fd->path) return FALSE;

	if (mask & DUPE_MATCH_ALL)
		{
		return TRUE;
		}
	if (mask & DUPE_MATCH_PATH)
		{
		if (utf8_compare(a->fd->path, b->fd->path, TRUE) != 0) return FALSE;
		}
	if (mask & DUPE_MATCH_NAME)
		{
		if (strcmp(a->fd->collate_key_name, b->fd->collate_key_name) != 0) return FALSE;
		}
	if (mask & DUPE_MATCH_NAME_CI)
		{
		if (strcmp(a->fd->collate_key_name_nocase, b->fd->collate_key_name_nocase) != 0) return FALSE;
		}
	if (mask & DUPE_MATCH_NAME_CONTENT)
		{
		if (strcmp(a->fd->collate_key_name, b->fd->collate_key_name) == 0)
			{
			if (!a->md5sum) a->md5sum = md5_text_from_file_utf8(a->fd->path, "");
			if (!b->md5sum) b->md5sum = md5_text_from_file_utf8(b->fd->path, "");
			if (a->md5sum[0] == '\0' ||
			    b->md5sum[0] == '\0' ||
			    strcmp(a->md5sum, b->md5sum) != 0)
				{
				return TRUE;
				}
			else
				{
				return FALSE;
				}
			}
		else
			{
			return FALSE;
			}
		}
	if (mask & DUPE_MATCH_NAME_CI_CONTENT)
		{
		if (strcmp(a->fd->collate_key_name_nocase, b->fd->collate_key_name_nocase) == 0)
			{
			if (!a->md5sum) a->md5sum = md5_text_from_file_utf8(a->fd->path, "");
			if (!b->md5sum) b->md5sum = md5_text_from_file_utf8(b->fd->path, "");
			if (a->md5sum[0] == '\0' ||
			    b->md5sum[0] == '\0' ||
			    strcmp(a->md5sum, b->md5sum) != 0)
				{
				return TRUE;
				}
			else
				{
				return FALSE;
				}
			}
		else
			{
			return FALSE;
			}
		}
	if (mask & DUPE_MATCH_SIZE)
		{
		if (a->fd->size != b->fd->size) return FALSE;
		}
	if (mask & DUPE_MATCH_DATE)
		{
		if (a->fd->date != b->fd->date) return FALSE;
		}
	if (mask & DUPE_MATCH_SUM)
		{
		if (!a->md5sum) a->md5sum = md5_text_from_file_utf8(a->fd->path, "");
		if (!b->md5sum) b->md5sum = md5_text_from_file_utf8(b->fd->path, "");
		if (a->md5sum[0] == '\0' ||
		    b->md5sum[0] == '\0' ||
		    strcmp(a->md5sum, b->md5sum) != 0) return FALSE;
		}
	if (mask & DUPE_MATCH_DIM)
		{
		if (a->width == 0) image_load_dimensions(a->fd, &a->width, &a->height);
		if (b->width == 0) image_load_dimensions(b->fd, &b->width, &b->height);
		if (a->width != b->width || a->height != b->height) return FALSE;
		}
	if (mask & DUPE_MATCH_SIM_HIGH ||
	    mask & DUPE_MATCH_SIM_MED ||
	    mask & DUPE_MATCH_SIM_LOW ||
	    mask & DUPE_MATCH_SIM_CUSTOM)
		{
		gdouble f;
		gdouble m;

		if (mask & DUPE_MATCH_SIM_HIGH) m = 0.95;
		else if (mask & DUPE_MATCH_SIM_MED) m = 0.90;
		else if (mask & DUPE_MATCH_SIM_CUSTOM) m = static_cast<gdouble>(options->duplicates_similarity_threshold) / 100.0;
		else m = 0.85;

		if (fast)
			{
			f = image_sim_compare_fast(a->simd, b->simd, m);
			}
		else
			{
			f = image_sim_compare(a->simd, b->simd);
			}

		*rank = f * 100.0;

		if (f < m) return FALSE;

		DEBUG_3("similar: %32s %32s = %f", a->fd->name, b->fd->name, f);
		}

	return TRUE;
}

/**
 * @brief  Determine if there is a match
 * @param di1
 * @param di2
 * @param data
 * @returns DUPE_MATCH/DUPE_NO_MATCH/DUPE_NAME_MATCH
 * 			DUPE_NAME_MATCH is used for name != contents searches:
 * 							the name and content match i.e.
 * 							no match, but keep searching
 *
 * Called when stepping down the array looking for adjacent matches,
 * and from the 2nd set search.
 *
 * Is not used for similarity checks.
 */
static DUPE_CHECK_RESULT dupe_match_check(DupeItem *di1, DupeItem *di2, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	DupeMatchType mask = dw->match_mask;

	if (mask & DUPE_MATCH_ALL)
		{
		return DUPE_MATCH;
		}
	if (mask & DUPE_MATCH_PATH)
		{
		if (utf8_compare(di1->fd->path, di2->fd->path, TRUE) != 0)
			{
			return DUPE_NO_MATCH;
			}
		}
	if (mask & DUPE_MATCH_NAME)
		{
		if (g_strcmp0(di1->fd->collate_key_name, di2->fd->collate_key_name) != 0)
			{
			return DUPE_NO_MATCH;
			}
		}
	if (mask & DUPE_MATCH_NAME_CI)
		{
		if (g_strcmp0(di1->fd->collate_key_name_nocase, di2->fd->collate_key_name_nocase) != 0 )
			{
			return DUPE_NO_MATCH;
			}
		}
	if (mask & DUPE_MATCH_NAME_CONTENT)
		{
		if (g_strcmp0(di1->fd->collate_key_name, di2->fd->collate_key_name) == 0)
			{
			if (g_strcmp0(di1->md5sum, di2->md5sum) == 0)
				{
				return DUPE_NAME_MATCH;
				}
			}
		else
			{
			return DUPE_NO_MATCH;
			}
		}
	if (mask & DUPE_MATCH_NAME_CI_CONTENT)
		{
		if (strcmp(di1->fd->collate_key_name_nocase, di2->fd->collate_key_name_nocase) == 0)
			{
			if (g_strcmp0(di1->md5sum, di2->md5sum) == 0)
				{
				return DUPE_NAME_MATCH;
				}
			}
		else
			{
			return DUPE_NO_MATCH;
			}
		}
	if (mask & DUPE_MATCH_SIZE)
		{
		if (di1->fd->size != di2->fd->size)
			{
			return DUPE_NO_MATCH;
			}
		}
	if (mask & DUPE_MATCH_DATE)
		{
		if (di1->fd->date != di2->fd->date)
			{
			return DUPE_NO_MATCH;
			}
		}
	if (mask & DUPE_MATCH_SUM)
		{
		if (g_strcmp0(di1->md5sum, di2->md5sum) != 0)
			{
			return DUPE_NO_MATCH;
			}
		}
	if (mask & DUPE_MATCH_DIM)
		{
		if (di1->dimensions != di2->dimensions)
			{
			return DUPE_NO_MATCH;
			}
		}

	return DUPE_MATCH;
}

/**
 * @brief The callback for the binary search
 * @param a
 * @param b
 * @param param_match_mask
 * @returns negative/0/positive
 *
 * Is not used for similarity checks.
 *
 * Used only when two file sets are used.
 * Requires use of a global for param_match_mask because there is no
 * g_array_binary_search_with_data() function in glib.
 */
static gint dupe_match_binary_search_cb(gconstpointer a, gconstpointer b)
{
	auto di1 = *(static_cast<const DupeItem *const *>(a));
	auto di2 = static_cast<const DupeItem *>(b);
	DupeMatchType mask = param_match_mask;

	if (mask & DUPE_MATCH_ALL)
		{
		return 0;
		}
	if (mask & DUPE_MATCH_PATH)
		{
		return utf8_compare(di1->fd->path, di2->fd->path, TRUE);
		}
	if (mask & DUPE_MATCH_NAME)
		{
		return g_strcmp0(di1->fd->collate_key_name, di2->fd->collate_key_name);
		}
	if (mask & DUPE_MATCH_NAME_CI)
		{
		return strcmp(di1->fd->collate_key_name_nocase, di2->fd->collate_key_name_nocase);
		}
	if (mask & DUPE_MATCH_NAME_CONTENT)
		{
		return g_strcmp0(di1->fd->collate_key_name, di2->fd->collate_key_name);
		}
	if (mask & DUPE_MATCH_NAME_CI_CONTENT)
		{
		return strcmp(di1->fd->collate_key_name_nocase, di2->fd->collate_key_name_nocase);
		}
	if (mask & DUPE_MATCH_SIZE)
		{
		return (di1->fd->size - di2->fd->size);
		}
	if (mask & DUPE_MATCH_DATE)
		{
		return (di1->fd->date - di2->fd->date);
		}
	if (mask & DUPE_MATCH_SUM)
		{
		return g_strcmp0(di1->md5sum, di2->md5sum);
		}
	if (mask & DUPE_MATCH_DIM)
		{
		return (di1->dimensions - di2->dimensions);
		}

	return 0;
}

/**
 * @brief The callback for the array sort
 * @param a
 * @param b
 * @param data
 * @returns negative/0/positive
 *
 * Is not used for similarity checks.
*/
static gint dupe_match_sort_cb(gconstpointer a, gconstpointer b, gpointer data)
{
	auto di1 = *(static_cast<const DupeItem *const *>(a));
	auto di2 = *(static_cast<const DupeItem *const *>(b));
	auto dw = static_cast<DupeWindow *>(data);
	DupeMatchType mask = dw->match_mask;

	if (mask & DUPE_MATCH_ALL)
		{
		return 0;
		}
	if (mask & DUPE_MATCH_PATH)
		{
		return utf8_compare(di1->fd->path, di2->fd->path, TRUE);
		}
	if (mask & DUPE_MATCH_NAME)
		{
		return g_strcmp0(di1->fd->collate_key_name, di2->fd->collate_key_name);
		}
	if (mask & DUPE_MATCH_NAME_CI)
		{
		return strcmp(di1->fd->collate_key_name_nocase, di2->fd->collate_key_name_nocase);
		}
	if (mask & DUPE_MATCH_NAME_CONTENT)
		{
		return g_strcmp0(di1->fd->collate_key_name, di2->fd->collate_key_name);
		}
	if (mask & DUPE_MATCH_NAME_CI_CONTENT)
		{
		return strcmp(di1->fd->collate_key_name_nocase, di2->fd->collate_key_name_nocase);
		}
	if (mask & DUPE_MATCH_SIZE)
		{
		return (di1->fd->size - di2->fd->size);
		}
	if (mask & DUPE_MATCH_DATE)
		{
		return (di1->fd->date - di2->fd->date);
		}
	if (mask & DUPE_MATCH_SUM)
		{
		if (di1->md5sum[0] == '\0' || di2->md5sum[0] == '\0')
		    {
			return -1;
			}
		else
			{
			return strcmp(di1->md5sum, di2->md5sum);
			}
		}
	if (mask & DUPE_MATCH_DIM)
		{
		if (!di1 || !di2 || !di1->width || !di1->height || !di2->width || !di2->height)
			{
			return -1;
			}
		return (di1->dimensions - di2->dimensions);
		}

	return 0; // should not execute
}

/**
 * @brief Check for duplicate matches
 * @param dw
 *
 * Is not used for similarity checks.
 *
 * Loads the file sets into an array and sorts on the searched
 * for parameter.
 *
 * If one file set, steps down the array looking for adjacent equal values.
 *
 * If two file sets, steps down the first set and for each value
 * does a binary search for matches in the second set.
 */
static void dupe_array_check(DupeWindow *dw )
{
	GArray *array_set1;
	GArray *array_set2;
	GList *work;
	gint i_set1;
	gint i_set2;
	DUPE_CHECK_RESULT check_result;
	param_match_mask = dw->match_mask;
	guint out_match_index;
	gboolean match_found = FALSE;;

	if (!dw->list) return;

	array_set1 = g_array_new(TRUE, TRUE, sizeof(gpointer));
	array_set2 = g_array_new(TRUE, TRUE, sizeof(gpointer));
	dupe_match_reset_list(dw->list);

	work = dw->list;
	while (work)
		{
		auto di = static_cast<DupeItem *>(work->data);
		g_array_append_val(array_set1, di);
		work = work->next;
		}

	g_array_sort_with_data(array_set1, dupe_match_sort_cb, dw);

	if (dw->second_set)
		{
		/* Two sets - nothing can be done until a second set is loaded */
		if (dw->second_list)
			{
			work = dw->second_list;
			while (work)
				{
				g_array_append_val(array_set2, (work->data));
				work = work->next;
				}
			g_array_sort_with_data(array_set2, dupe_match_sort_cb, dw);

			for (i_set1 = 0; i_set1 <= static_cast<gint>(array_set1->len) - 1; i_set1++)
				{
				auto di1 = static_cast<DupeItem *>(g_array_index(array_set1, gpointer, i_set1));
				DupeItem *di2 = nullptr;
				/* If multiple identical entries in set 1, use the last one */
				if (i_set1 < static_cast<gint>(array_set1->len) - 2)
					{
					di2 = static_cast<DupeItem *>(g_array_index(array_set1, gpointer, i_set1 + 1));
					check_result = dupe_match_check(di1, di2, dw);
					if (check_result == DUPE_MATCH || check_result == DUPE_NAME_MATCH)
						{
						continue;
						}
					}

#if ((GLIB_MAJOR_VERSION == 2) && (GLIB_MINOR_VERSION >= 62))
				match_found = g_array_binary_search(array_set2, di1, dupe_match_binary_search_cb, &out_match_index);
#else
				gint i;

				match_found = FALSE;
				for(i=0; i < array_set2->len; i++)
					{
					di2 = static_cast<DupeItem *>(g_array_index(array_set2,  gpointer, i));
					check_result = dupe_match_check(di1, di2, dw);
					if (check_result == DUPE_MATCH)
						{
						match_found = TRUE;
						out_match_index = i;
						break;
						}
					}
#endif

				if (match_found)
					{
					di2 = static_cast<DupeItem *>(g_array_index(array_set2, gpointer, out_match_index));

					check_result = dupe_match_check(di1, di2, dw);
					if (check_result == DUPE_MATCH || check_result == DUPE_NAME_MATCH)
						{
						if (check_result == DUPE_MATCH)
							{
							dupe_match_link(di2, di1, 0.0);
							}
						i_set2 = out_match_index + 1;

						if (i_set2 > static_cast<gint>(array_set2->len) - 1)
							{
							break;
							}
						/* Look for multiple matches in set 2 for item di1 */
						di2 = static_cast<DupeItem *>(g_array_index(array_set2, gpointer, i_set2));
						check_result = dupe_match_check(di1, di2, dw);
						while (check_result == DUPE_MATCH || check_result == DUPE_NAME_MATCH)
							{
							if (check_result == DUPE_MATCH)
								{
								dupe_match_link(di2, di1, 0.0);
								}
							i_set2++;
							if (i_set2 > static_cast<gint>(array_set2->len) - 1)
								{
								break;
								}
							di2 = static_cast<DupeItem *>(g_array_index(array_set2, gpointer, i_set2));
							check_result = dupe_match_check(di1, di2, dw);
							}
						}
					}
				}
			}
		}
	else
		{
		/* File set 1 only */
		g_list_free(dw->dupes);
		dw->dupes = nullptr;

		if (static_cast<gint>(array_set1->len) > 1)
			{
			for (i_set1 = 0; i_set1 <= static_cast<gint>(array_set1->len) - 2; i_set1++)
				{
				auto di1 = static_cast<DupeItem *>(g_array_index(array_set1, gpointer, i_set1));
				auto di2 = static_cast<DupeItem *>(g_array_index(array_set1, gpointer, i_set1 + 1));

				check_result = dupe_match_check(di1, di2, dw);
				if (check_result == DUPE_MATCH || check_result == DUPE_NAME_MATCH)
					{
					if (check_result == DUPE_MATCH)
						{
						dupe_match_link(di2, di1, 0.0);
						}
					i_set1++;

					if ( i_set1 + 1 > static_cast<gint>(array_set1->len) - 1)
						{
						break;
						}
					/* Look for multiple matches for item di1 */
					di2 = static_cast<DupeItem *>(g_array_index(array_set1, gpointer, i_set1 + 1));
					check_result = dupe_match_check(di1, di2, dw);
					while (check_result == DUPE_MATCH || check_result == DUPE_NAME_MATCH)
						{
						if (check_result == DUPE_MATCH)
							{
							dupe_match_link(di2, di1, 0.0);
							}
						i_set1++;

						if (i_set1 + 1 > static_cast<gint>(array_set1->len) - 1)
							{
							break;
							}
						di2 = static_cast<DupeItem *>(g_array_index(array_set1, gpointer, i_set1 + 1));
						check_result = dupe_match_check(di1, di2, dw);
						}
					}
				}
			}
		}
	g_array_free(array_set1, TRUE);
	g_array_free(array_set2, TRUE);
}

/**
 * @brief Look for similarity match
 * @param dw
 * @param needle
 * @param start
 *
 * Only used for similarity checks.\n
 * Called from dupe_check_cb.
 * Called for each entry in the list.
 * Steps through the list looking for matches against needle.
 * Pushes a #DupeQueueItem onto thread pool queue.
 */
static void dupe_list_check_match(DupeWindow *dw, DupeItem *needle, GList *start)
{
	GList *work;
	DupeQueueItem *dqi;

	if (dw->second_set)
		{
		work = dw->second_list;
		}
	else if (start)
		{
		work = start;
		}
	else
		{
		work = g_list_last(dw->list);
		}

	dqi = g_new0(DupeQueueItem, 1);
	dqi->needle = needle;
	dqi->dw = dw;
	dqi->work = work;
	dqi->index = dw->queue_count;
	g_thread_pool_push(dw->dupe_comparison_thread_pool, dqi, nullptr);
}

/*
 * ------------------------------------------------------------------
 * Thumbnail handling
 * ------------------------------------------------------------------
 */

static void dupe_listview_set_thumb(DupeWindow *dw, DupeItem *di, GtkTreeIter *iter)
{
	GtkListStore *store;
	GtkTreeIter iter_n;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview)));
	if (!iter)
		{
		if (dupe_listview_find_item(store, di, &iter_n) >= 0)
			{
			iter = &iter_n;
			}
		}

	if (iter) gtk_list_store_set(store, iter, DUPE_COLUMN_THUMB, di->pixbuf, -1);
}

static void dupe_thumb_do(DupeWindow *dw)
{
	DupeItem *di;

	if (!dw->thumb_loader || !dw->thumb_item) return;
	di = dw->thumb_item;

	if (di->pixbuf) g_object_unref(di->pixbuf);
	di->pixbuf = thumb_loader_get_pixbuf(dw->thumb_loader);

	dupe_listview_set_thumb(dw, di, nullptr);
}

static void dupe_thumb_error_cb(ThumbLoader *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_thumb_do(dw);
	dupe_thumb_step(dw);
}

static void dupe_thumb_done_cb(ThumbLoader *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_thumb_do(dw);
	dupe_thumb_step(dw);
}

static void dupe_thumb_step(DupeWindow *dw)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	DupeItem *di = nullptr;
	gboolean valid;
	gint row = 0;
	gint length = 0;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview));
	valid = gtk_tree_model_get_iter_first(store, &iter);

	while (!di && valid)
		{
		GdkPixbuf *pixbuf;

		length++;
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, DUPE_COLUMN_THUMB, &pixbuf, -1);
		if (pixbuf || di->pixbuf)
			{
			if (!pixbuf) gtk_list_store_set(GTK_LIST_STORE(store), &iter, DUPE_COLUMN_THUMB, di->pixbuf, -1);
			row++;
			di = nullptr;
			}
		valid = gtk_tree_model_iter_next(store, &iter);
		}
	if (valid)
		{
		while (gtk_tree_model_iter_next(store, &iter)) length++;
		}

	if (!di)
		{
		dw->thumb_item = nullptr;
		thumb_loader_free(dw->thumb_loader);
		dw->thumb_loader = nullptr;

		dupe_window_update_progress(dw, nullptr, 0.0, FALSE);
		return;
		}

	dupe_window_update_progress(dw, _("Loading thumbs..."),
				    length == 0 ? 0.0 : static_cast<gdouble>(row) / length, FALSE);

	dw->thumb_item = di;
	thumb_loader_free(dw->thumb_loader);
	dw->thumb_loader = thumb_loader_new(options->thumbnails.max_width, options->thumbnails.max_height);

	thumb_loader_set_callbacks(dw->thumb_loader,
				   dupe_thumb_done_cb,
				   dupe_thumb_error_cb,
				   nullptr,
				   dw);

	/* start it */
	if (!thumb_loader_start(dw->thumb_loader, di->fd))
		{
		/* error, handle it, do next */
		DEBUG_1("error loading thumb for %s", di->fd->path);
		dupe_thumb_do(dw);
		dupe_thumb_step(dw);
		}
}

/*
 * ------------------------------------------------------------------
 * Dupe checking loop
 * ------------------------------------------------------------------
 */

static void dupe_check_stop(DupeWindow *dw)
{
	if (dw->idle_id > 0)
		{
		g_source_remove(dw->idle_id);
		dw->idle_id = 0;
		}

	dw->abort = TRUE;

	while (dw->thread_count < dw->queue_count) // Wait for the queue to empty
		{
		dupe_window_update_progress(dw, nullptr, 0.0, FALSE);
		widget_set_cursor(dw->listview, -1);
		}

	g_list_free(dw->search_matches);
	dw->search_matches = nullptr;

	if (dw->idle_id || dw->img_loader || dw->thumb_loader)
		{
		if (dw->idle_id > 0)
			{
			g_source_remove(dw->idle_id);
			dw->idle_id = 0;
			}
		dupe_window_update_progress(dw, nullptr, 0.0, FALSE);
		widget_set_cursor(dw->listview, -1);
		}

	if (dw->add_files_queue_id)
		{
		g_source_remove(dw->add_files_queue_id);
		dw->add_files_queue_id = 0;
		dupe_destroy_list_cache(dw);
		gtk_widget_set_sensitive(dw->controls_box, TRUE);
		if (g_list_length(dw->add_files_queue) > 0)
			{
			filelist_free(dw->add_files_queue);
			}
		dw->add_files_queue = nullptr;
		dupe_window_update_progress(dw, nullptr, 0.0, FALSE);
		widget_set_cursor(dw->listview, -1);
		}

	thumb_loader_free(dw->thumb_loader);
	dw->thumb_loader = nullptr;

	image_loader_free(dw->img_loader);
	dw->img_loader = nullptr;
}

static void dupe_check_stop_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_check_stop(dw);
}

static void dupe_loader_done_cb(ImageLoader *il, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	GdkPixbuf *pixbuf;

	pixbuf = image_loader_get_pixbuf(il);

	if (dw->setup_point)
		{
		auto di = static_cast<DupeItem *>(dw->setup_point->data);

		if (!di->simd)
			{
			di->simd = image_sim_new_from_pixbuf(pixbuf);
			}
		else
			{
			image_sim_fill_data(di->simd, pixbuf);
			}

		if (di->width == 0 && di->height == 0)
			{
			di->width = gdk_pixbuf_get_width(pixbuf);
			di->height = gdk_pixbuf_get_height(pixbuf);
			}
		if (options->thumbnails.enable_caching)
			{
			dupe_item_write_cache(di);
			}

		image_sim_alternate_processing(di->simd);
		}

	image_loader_free(dw->img_loader);
	dw->img_loader = nullptr;

	dw->idle_id = g_idle_add(dupe_check_cb, dw);
}

static void dupe_setup_reset(DupeWindow *dw)
{
	dw->setup_point = nullptr;
	dw->setup_n = 0;
	dw->setup_time = msec_time();
	dw->setup_time_count = 0;
}

static GList *dupe_setup_point_step(DupeWindow *dw, GList *p)
{
	if (!p) return nullptr;

	if (p->next) return p->next;

	if (dw->second_set && g_list_first(p) == dw->list) return dw->second_list;

	return nullptr;
}

/**
 * @brief Generates the sumcheck or dimensions
 * @param list Set1 or set2
 * @returns TRUE/FALSE = not completed/completed
 *
 * Ensures that the DIs contain the MD5SUM or dimensions for all items in
 * the list. One item at a time. Re-enters if not completed.
 */
static gboolean create_checksums_dimensions(DupeWindow *dw, GList *list)
{
		if ((dw->match_mask & DUPE_MATCH_SUM) ||
			(dw->match_mask & DUPE_MATCH_NAME_CONTENT) ||
			(dw->match_mask & DUPE_MATCH_NAME_CI_CONTENT))
			{
			/* MD5SUM only */
			if (!dw->setup_point) dw->setup_point = list; // setup_point clear on 1st entry

			while (dw->setup_point)
				{
				auto di = static_cast<DupeItem *>(dw->setup_point->data);

				dw->setup_point = dupe_setup_point_step(dw, dw->setup_point);
				dw->setup_n++;

				if (!di->md5sum)
					{
					dupe_window_update_progress(dw, _("Reading checksums..."),
						dw->setup_count == 0 ? 0.0 : static_cast<gdouble>(dw->setup_n - 1) / dw->setup_count, FALSE);

					if (options->thumbnails.enable_caching)
						{
						dupe_item_read_cache(di);
						if (di->md5sum)
							{
							return TRUE;
							}
						}

					di->md5sum = md5_text_from_file_utf8(di->fd->path, "");
					if (options->thumbnails.enable_caching)
						{
						dupe_item_write_cache(di);
						}
					return TRUE;
					}
				}
			dupe_setup_reset(dw);
			}

		if ((dw->match_mask & DUPE_MATCH_DIM)  )
			{
			/* Dimensions only */
			if (!dw->setup_point) dw->setup_point = list;

			while (dw->setup_point)
				{
				auto di = static_cast<DupeItem *>(dw->setup_point->data);

				dw->setup_point = dupe_setup_point_step(dw, dw->setup_point);
				dw->setup_n++;
				if (di->width == 0 && di->height == 0)
					{
					dupe_window_update_progress(dw, _("Reading dimensions..."),
						dw->setup_count == 0 ? 0.0 : static_cast<gdouble>(dw->setup_n - 1) / dw->setup_count, FALSE);

					if (options->thumbnails.enable_caching)
						{
						dupe_item_read_cache(di);
						if (di->width != 0 || di->height != 0)
							{
							return TRUE;
							}
						}

					image_load_dimensions(di->fd, &di->width, &di->height);
					di->dimensions = (di->width << 16) + di->height;
					if (options->thumbnails.enable_caching)
						{
						dupe_item_write_cache(di);
						}
					return TRUE;
					}
				}
			dupe_setup_reset(dw);
			}

	return FALSE;
}

/**
 * @brief Compare func. for sorting search matches
 * @param a #DupeSearchMatch
 * @param b #DupeSearchMatch
 * @returns
 *
 * Used only for similarity checks\n
 * Sorts search matches on order they were inserted into the pool queue
 */
static gint sort_func(gconstpointer a, gconstpointer b)
{
	return static_cast<const DupeSearchMatch *>(a)->index - static_cast<const DupeSearchMatch *>(b)->index;
}

/**
 * @brief Check set 1 (and set 2) for matches
 * @param data DupeWindow
 * @returns TRUE/FALSE = not completed/completed
 *
 * Initiated from start, loader done and item remove
 *
 * On first entry generates di->MD5SUM, di->dimensions and sim data,
 * and updates the cache.
 */
static gboolean dupe_check_cb(gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	DupeSearchMatch *search_match_list_item;

	if (!dw->idle_id)
		{
		return G_SOURCE_REMOVE;
		}

	if (!dw->setup_done) /* Clear on 1st entry */
		{
		if (dw->list)
			{
			if (create_checksums_dimensions(dw, dw->list))
				{
				return G_SOURCE_CONTINUE;
				}
			}
		if (dw->second_list)
			{
			if (create_checksums_dimensions(dw, dw->second_list))
				{
				return G_SOURCE_CONTINUE;
				}
			}
		if ((dw->match_mask & DUPE_MATCH_SIM_HIGH ||
		     dw->match_mask & DUPE_MATCH_SIM_MED ||
		     dw->match_mask & DUPE_MATCH_SIM_LOW ||
		     dw->match_mask & DUPE_MATCH_SIM_CUSTOM) &&
		    !(dw->setup_mask & DUPE_MATCH_SIM_MED) )
			{
			/* Similarity only */
			if (!dw->setup_point) dw->setup_point = dw->list;

			while (dw->setup_point)
				{
				auto di = static_cast<DupeItem *>(dw->setup_point->data);

				if (!di->simd)
					{
					dupe_window_update_progress(dw, _("Reading similarity data..."),
						dw->setup_count == 0 ? 0.0 : static_cast<gdouble>(dw->setup_n) / dw->setup_count, FALSE);

					if (options->thumbnails.enable_caching)
						{
						dupe_item_read_cache(di);
						if (cache_sim_data_filled(di->simd))
							{
							image_sim_alternate_processing(di->simd);
							return G_SOURCE_CONTINUE;
							}
						}

					dw->img_loader = image_loader_new(di->fd);
					image_loader_set_buffer_size(dw->img_loader, 8);
					g_signal_connect(G_OBJECT(dw->img_loader), "error", (GCallback)dupe_loader_done_cb, dw);
					g_signal_connect(G_OBJECT(dw->img_loader), "done", (GCallback)dupe_loader_done_cb, dw);

					if (!image_loader_start(dw->img_loader))
						{
						image_sim_free(di->simd);
						di->simd = image_sim_new();
						image_loader_free(dw->img_loader);
						dw->img_loader = nullptr;
						return G_SOURCE_CONTINUE;
						}
					dw->idle_id = 0;
					return G_SOURCE_REMOVE;
					}

				dw->setup_point = dupe_setup_point_step(dw, dw->setup_point);
				dw->setup_n++;
				}
			dw->setup_mask = static_cast<DupeMatchType>(dw->setup_mask | DUPE_MATCH_SIM_MED);
			dupe_setup_reset(dw);
			}

		/* End of setup not done */
		dupe_window_update_progress(dw, _("Comparing..."), 0.0, FALSE);
		dw->setup_done = TRUE;
		dupe_setup_reset(dw);
		dw->setup_count = g_list_length(dw->list);
		}

	/* Setup done - dw->working set to NULL below
	 * Set before 1st entry: dw->working = g_list_last(dw->list)
	 * Set before 1st entry: dw->setup_count = g_list_length(dw->list)
	 */
	if (!dw->working)
		{
		/* Similarity check threads may still be running */
		if (dw->setup_count > 0 && (dw->match_mask == DUPE_MATCH_SIM_HIGH ||
			dw->match_mask == DUPE_MATCH_SIM_MED ||
			dw->match_mask == DUPE_MATCH_SIM_LOW ||
			dw->match_mask == DUPE_MATCH_SIM_CUSTOM))
			{
			if( dw->thread_count < dw->queue_count)
				{
				dupe_window_update_progress(dw, _("Comparing..."), 0.0, FALSE);

				return G_SOURCE_CONTINUE;
				}

			if (dw->search_matches_sorted == nullptr)
				{
				dw->search_matches_sorted = g_list_sort(dw->search_matches, sort_func);
				dupe_setup_reset(dw);
				}

			while (dw->search_matches_sorted)
				{
				dw->setup_n++;
				dupe_window_update_progress(dw, _("Sorting..."), 0.0, FALSE);
				search_match_list_item = static_cast<DupeSearchMatch *>(dw->search_matches_sorted->data);

				if (!dupe_match_link_exists(search_match_list_item->a, search_match_list_item->b))
					{
					dupe_match_link(search_match_list_item->a, search_match_list_item->b, search_match_list_item->rank);
					}

				dw->search_matches_sorted = dw->search_matches_sorted->next;

				if (dw->search_matches_sorted != nullptr)
					{
					return G_SOURCE_CONTINUE;
					}
				}
			g_list_free(dw->search_matches);
			dw->search_matches = nullptr;
			g_list_free(dw->search_matches_sorted);
			dw->search_matches_sorted = nullptr;
			dw->setup_count = 0;
			}
		else
			{
			if (dw->setup_count > 0)
				{
				dw->setup_count = 0;
				dupe_window_update_progress(dw, _("Sorting..."), 1.0, TRUE);
				return G_SOURCE_CONTINUE;
				}
			}

		dw->idle_id = 0;
		dupe_window_update_progress(dw, nullptr, 0.0, FALSE);

		dupe_match_rank(dw);
		dupe_window_update_count(dw, FALSE);

		dupe_listview_populate(dw);

		/* check thumbs */
		if (dw->show_thumbs) dupe_thumb_step(dw);

		widget_set_cursor(dw->listview, -1);

		return G_SOURCE_REMOVE;
		/* The end */
		}

	/* Setup done - working */
	if (dw->match_mask == DUPE_MATCH_SIM_HIGH ||
		dw->match_mask == DUPE_MATCH_SIM_MED ||
		dw->match_mask == DUPE_MATCH_SIM_LOW ||
		dw->match_mask == DUPE_MATCH_SIM_CUSTOM)
		{
		/* This is the similarity comparison */
		dupe_list_check_match(dw, static_cast<DupeItem *>(dw->working->data), dw->working);
		dupe_window_update_progress(dw, _("Queuing..."), dw->setup_count == 0 ? 0.0 : static_cast<gdouble>(dw->setup_n) / dw->setup_count, FALSE);
		dw->setup_n++;
		dw->queue_count++;

		dw->working = dw->working->prev; /* Is NULL when complete */
		}
	else
		{
		/* This is the comparison for all other parameters.
		 * dupe_array_check() processes the entire list in one go
		*/
		dw->working = nullptr;
		dupe_window_update_progress(dw, _("Comparing..."), 0.0, FALSE);
		dupe_array_check(dw);
		}

	return G_SOURCE_CONTINUE;
}

static void dupe_check_start(DupeWindow *dw)
{
	dw->setup_done = FALSE;

	dw->setup_count = g_list_length(dw->list);
	if (dw->second_set) dw->setup_count += g_list_length(dw->second_list);

	dw->setup_mask = DUPE_MATCH_NONE;
	dupe_setup_reset(dw);

	dw->working = g_list_last(dw->list);

	dupe_window_update_count(dw, TRUE);
	widget_set_cursor(dw->listview, GDK_WATCH);
	dw->queue_count = 0;
	dw->thread_count = 0;
	dw->search_matches_sorted = nullptr;
	dw->abort = FALSE;

	if (dw->idle_id) return;

	dw->idle_id = g_idle_add(dupe_check_cb, dw);
}

static gboolean dupe_check_start_cb(gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_check_start(dw);

	return FALSE;
}

/*
 * ------------------------------------------------------------------
 * Item addition, removal
 * ------------------------------------------------------------------
 */

static void dupe_item_remove(DupeWindow *dw, DupeItem *di)
{
	if (!di) return;

	/* handle things that may be in progress... */
	if (dw->working && dw->working->data == di)
		{
		dw->working = dw->working->prev;
		}
	if (dw->thumb_loader && dw->thumb_item == di)
		{
		dupe_thumb_step(dw);
		}
	if (dw->setup_point && dw->setup_point->data == di)
		{
		dw->setup_point = dupe_setup_point_step(dw, dw->setup_point);
		if (dw->img_loader)
			{
			image_loader_free(dw->img_loader);
			dw->img_loader = nullptr;
			dw->idle_id = g_idle_add(dupe_check_cb, dw);
			}
		}

	if (di->group && dw->dupes)
		{
		/* is a dupe, must remove from group/reset children if a parent */
		DupeItem *parent;

		parent = dupe_match_find_parent(dw, di);
		if (di == parent)
			{
			if (g_list_length(parent->group) < 2)
				{
				DupeItem *child;

				child = dupe_match_highest_rank(parent);
				dupe_match_link_clear(child, TRUE);
				dupe_listview_remove(dw, child);

				dupe_match_link_clear(parent, TRUE);
				dupe_listview_remove(dw, parent);
				dw->dupes = g_list_remove(dw->dupes, parent);
				}
			else
				{
				DupeItem *new_parent;
				DupeMatch *dm;

				dm = static_cast<DupeMatch *>(parent->group->data);
				new_parent = dm->di;
				dupe_match_reparent(dw, parent, new_parent);
				dupe_listview_remove(dw, parent);
				}
			}
		else
			{
			if (g_list_length(parent->group) < 2)
				{
				dupe_match_link_clear(parent, TRUE);
				dupe_listview_remove(dw, parent);
				dw->dupes = g_list_remove(dw->dupes, parent);
				}
			dupe_match_link_clear(di, TRUE);
			dupe_listview_remove(dw, di);
			}
		}
	else
		{
		/* not a dupe, or not sorted yet, simply reset */
		dupe_match_link_clear(di, TRUE);
		}

	if (dw->second_list && g_list_find(dw->second_list, di))
		{
		dupe_second_remove(dw, di);
		}
	else
		{
		dw->list = g_list_remove(dw->list, di);
		}
	dupe_item_free(di);

	dupe_window_update_count(dw, FALSE);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static gboolean dupe_item_remove_by_path_unused(DupeWindow *dw, const gchar *path)
{
	DupeItem *di;

	di = dupe_item_find_path_unused(dw, path);
	if (!di) return FALSE;

	dupe_item_remove(dw, di);

	return TRUE;
}
#pragma GCC diagnostic pop

static gboolean dupe_files_add_queue_cb(gpointer data)
{
	DupeItem *di = nullptr;
	auto dw = static_cast<DupeWindow *>(data);
	FileData *fd;
	GList *queue = dw->add_files_queue;

	gtk_progress_bar_pulse(GTK_PROGRESS_BAR(dw->extra_label));

	if (queue == nullptr)
		{
		dw->add_files_queue_id = 0;
		dupe_destroy_list_cache(dw);
		g_idle_add(dupe_check_start_cb, dw);
		gtk_widget_set_sensitive(dw->controls_box, TRUE);
		return FALSE;
		}

	fd = static_cast<FileData *>(queue->data);
	if (fd)
		{
		if (isfile(fd->path))
			{
			di = dupe_item_new(fd);
			}
		else if (isdir(fd->path))
			{
			GList *f, *d;
			dw->add_files_queue = g_list_remove(dw->add_files_queue, g_list_first(dw->add_files_queue)->data);

			if (filelist_read(fd, &f, &d))
				{
				f = filelist_filter(f, FALSE);
				d = filelist_filter(d, TRUE);

				dw->add_files_queue = g_list_concat(f, dw->add_files_queue);
				dw->add_files_queue = g_list_concat(d, dw->add_files_queue);
				}
			}
		else
			{
			/* Not a file and not a dir */
			dw->add_files_queue = g_list_remove(dw->add_files_queue, g_list_first(dw->add_files_queue)->data);
			}
		}

	if (!di)
		{
		/* A dir was found. Process the contents on next entry */
		return TRUE;
		}

	dw->add_files_queue = g_list_remove(dw->add_files_queue, g_list_first(dw->add_files_queue)->data);

	dupe_item_read_cache(di);

	/* Ensure images in the lists have unique FileDatas */
	if (!dupe_insert_in_list_cache(dw, di->fd))
		{
		dupe_item_free(di);
		return TRUE;
		}

	if (dw->second_drop)
		{
		dupe_second_add(dw, di);
		}
	else
		{
		dw->list = g_list_prepend(dw->list, di);
		}

	if (dw->add_files_queue != nullptr)
		{
		return TRUE;
		}
	else
		{
		dw->add_files_queue_id = 0;
		dupe_destroy_list_cache(dw);
		g_idle_add(dupe_check_start_cb, dw);
		gtk_widget_set_sensitive(dw->controls_box, TRUE);
		return FALSE;
		}
}

static void dupe_files_add(DupeWindow *dw, CollectionData *, CollectInfo *info,
			   FileData *fd, gboolean recurse)
{
	DupeItem *di = nullptr;

	if (info)
		{
		di = dupe_item_new(info->fd);
		}
	else if (fd)
		{
		if (isfile(fd->path) && !g_file_test(fd->path, G_FILE_TEST_IS_SYMLINK))
			{
			di = dupe_item_new(fd);
			}
		else if (isdir(fd->path) && recurse)
			{
			GList *f, *d;
			if (filelist_read(fd, &f, &d))
				{
				GList *work;

				f = filelist_filter(f, FALSE);
				d = filelist_filter(d, TRUE);

				work = f;
				while (work)
					{
					dupe_files_add(dw, nullptr, nullptr, static_cast<FileData *>(work->data), TRUE);
					work = work->next;
					}
				filelist_free(f);
				work = d;
				while (work)
					{
					dupe_files_add(dw, nullptr, nullptr, static_cast<FileData *>(work->data), TRUE);
					work = work->next;
					}
				filelist_free(d);
				}
			}
		}

	if (!di) return;

	dupe_item_read_cache(di);

	/* Ensure images in the lists have unique FileDatas */
	GList *work;
	DupeItem *di_list;
	work = g_list_first(dw->list);
	while (work)
		{
		di_list = static_cast<DupeItem *>(work->data);
		if (di_list->fd == di->fd)
			{
			return;
			}
		else
			{
			work = work->next;
			}
		}

	if (dw->second_list)
		{
		work = g_list_first(dw->second_list);
		while (work)
			{
			di_list = static_cast<DupeItem *>(work->data);
			if (di_list->fd == di->fd)
				{
				return;
				}
			else
				{
				work = work->next;
				}
			}
		}

	if (dw->second_drop)
		{
		dupe_second_add(dw, di);
		}
	else
		{
		dw->list = g_list_prepend(dw->list, di);
		}
}

static void dupe_init_list_cache(DupeWindow *dw)
{
	dw->list_cache = g_hash_table_new(g_direct_hash, g_direct_equal);
	dw->second_list_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

	for (GList *i = dw->list; i != nullptr; i = i->next)
		{
			auto di = static_cast<DupeItem *>(i->data);

			g_hash_table_add(dw->list_cache, di->fd);
		}

	for (GList *i = dw->second_list; i != nullptr; i = i->next)
		{
			auto di = static_cast<DupeItem *>(i->data);

			g_hash_table_add(dw->second_list_cache, di->fd);
		}
}

static void dupe_destroy_list_cache(DupeWindow *dw)
{
	g_hash_table_destroy(dw->list_cache);
	g_hash_table_destroy(dw->second_list_cache);
}

/**
 * @brief Return true if the fd was not in the cache
 * @param dw
 * @param fd
 * @returns
 *
 *
 */
static gboolean dupe_insert_in_list_cache(DupeWindow *dw, FileData *fd)
{
	GHashTable *table =
		dw->second_drop ? dw->second_list_cache : dw->list_cache;
	/* We do this as a lookup + add as we don't want to overwrite
	   items as that would leak the old value. */
	if (g_hash_table_lookup(table, fd) != nullptr)
		return FALSE;
	return g_hash_table_add(table, fd);
}

void dupe_window_add_collection(DupeWindow *dw, CollectionData *collection)
{
	CollectInfo *info;

	info = collection_get_first(collection);
	while (info)
		{
		dupe_files_add(dw, collection, info, nullptr, FALSE);
		info = collection_next_by_info(collection, info);
		}

	dupe_check_start(dw);
}

void dupe_window_add_files(DupeWindow *dw, GList *list, gboolean recurse)
{
	GList *work;

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;
		if (isdir(fd->path) && !recurse)
			{
			GList *f, *d;

			if (filelist_read(fd, &f, &d))
				{
				GList *work_file;
				work_file = f;

				while (work_file)
					{
					/* Add only the files, ignore the dirs when no recurse */
					dw->add_files_queue = g_list_prepend(dw->add_files_queue, work_file->data);
					file_data_ref((FileData *)work_file->data);
					work_file = work_file->next;
					}
				g_list_free(f);
				g_list_free(d);
				}
			}
		else
			{
			dw->add_files_queue = g_list_prepend(dw->add_files_queue, fd);
			file_data_ref(fd);
			}
		}
	if (dw->add_files_queue_id == 0)
		{
		gtk_progress_bar_pulse(GTK_PROGRESS_BAR(dw->extra_label));
		gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(dw->extra_label), DUPE_PROGRESS_PULSE_STEP);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(dw->extra_label), _("Loading file list"));

		dupe_init_list_cache(dw);
		dw->add_files_queue_id = g_idle_add(dupe_files_add_queue_cb, dw);
		gtk_widget_set_sensitive(dw->controls_box, FALSE);
		}
}

static void dupe_item_update(DupeWindow *dw, DupeItem *di)
{
	if ( (dw->match_mask & DUPE_MATCH_NAME) || (dw->match_mask & DUPE_MATCH_PATH || (dw->match_mask & DUPE_MATCH_NAME_CI)) )
		{
		/* only effects matches on name or path */
/*
		FileData *fd = file_data_ref(di->fd);
		gint second;

		second = di->second;
		dupe_item_remove(dw, di);

		dw->second_drop = second;
		dupe_files_add(dw, NULL, NULL, fd, FALSE);
		dw->second_drop = FALSE;

		file_data_unref(fd);
*/
		dupe_check_start(dw);
		}
	else
		{
		GtkListStore *store;
		GtkTreeIter iter;
		gint row;
		/* update the listview(s) */

		store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview)));
		row = dupe_listview_find_item(store, di, &iter);
		if (row >= 0)
			{
			gtk_list_store_set(store, &iter,
					   DUPE_COLUMN_NAME, di->fd->name,
					   DUPE_COLUMN_PATH, di->fd->path, -1);
			}

		if (dw->second_listview)
			{
			store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->second_listview)));
			row = dupe_listview_find_item(store, di, &iter);
			if (row >= 0)
				{
				gtk_list_store_set(store, &iter, 1, di->fd->path, -1);
				}
			}
		}

}

static void dupe_item_update_fd_in_list(DupeWindow *dw, FileData *fd, GList *work)
{
	while (work)
		{
		auto di = static_cast<DupeItem *>(work->data);

		if (di->fd == fd)
			dupe_item_update(dw, di);

		work = work->next;
		}
}

static void dupe_item_update_fd(DupeWindow *dw, FileData *fd)
{
	dupe_item_update_fd_in_list(dw, fd, dw->list);
	if (dw->second_set) dupe_item_update_fd_in_list(dw, fd, dw->second_list);
}


/*
 * ------------------------------------------------------------------
 * Misc.
 * ------------------------------------------------------------------
 */

static GtkWidget *dupe_display_label(GtkWidget *vbox, const gchar *description, const gchar *text)
{
	GtkWidget *hbox;
	GtkWidget *label;

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

	label = gtk_label_new(description);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	label = gtk_label_new(text);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	return label;
}

static void dupe_display_stats(DupeWindow *dw, DupeItem *di)
{
	GenericDialog *gd;
	gchar *buf;

	if (!di) return;

	gd = file_util_gen_dlg("Image thumbprint debug info", "thumbprint",
			       dw->window, TRUE,
			       nullptr, nullptr);
	generic_dialog_add_button(gd, GQ_ICON_CLOSE, _("Close"), nullptr, TRUE);

	dupe_display_label(gd->vbox, "name:", di->fd->name);
	buf = text_from_size(di->fd->size);
	dupe_display_label(gd->vbox, "size:", buf);
	g_free(buf);
	dupe_display_label(gd->vbox, "date:", text_from_time(di->fd->date));
	buf = g_strdup_printf("%d x %d", di->width, di->height);
	dupe_display_label(gd->vbox, "dimensions:", buf);
	g_free(buf);
	dupe_display_label(gd->vbox, "md5sum:", (di->md5sum) ? di->md5sum : "not generated");

	dupe_display_label(gd->vbox, "thumbprint:", (di->simd) ? "" : "not generated");
	if (di->simd)
		{
		GtkWidget *image;
		GdkPixbuf *pixbuf;
		gint x, y;
		guchar *d_pix;
		guchar *dp;
		gint rs;
		gint sp;

		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 32, 32);
		rs = gdk_pixbuf_get_rowstride(pixbuf);
		d_pix = gdk_pixbuf_get_pixels(pixbuf);

		for (y = 0; y < 32; y++)
			{
			dp = d_pix + (y * rs);
			sp = y * 32;
			for (x = 0; x < 32; x++)
				{
				*(dp++) = di->simd->avg_r[sp + x];
				*(dp++) = di->simd->avg_g[sp + x];
				*(dp++) = di->simd->avg_b[sp + x];
				}
			}

		image = gtk_image_new_from_pixbuf(pixbuf);
		gtk_box_pack_start(GTK_BOX(gd->vbox), image, FALSE, FALSE, 0);
		gtk_widget_show(image);

		g_object_unref(pixbuf);
		}

	gtk_widget_show(gd->dialog);
}

static void dupe_window_recompare(DupeWindow *dw)
{
	GtkListStore *store;

	dupe_check_stop(dw);

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview)));
	gtk_list_store_clear(store);

	g_list_free(dw->dupes);
	dw->dupes = nullptr;

	dupe_match_reset_list(dw->list);
	dupe_match_reset_list(dw->second_list);
	dw->set_count = 0;

	dupe_check_start(dw);
}

static void dupe_menu_view(DupeWindow *dw, DupeItem *di, GtkWidget *listview, gint new_window)
{
	if (!di) return;

	if (di->collection && collection_info_valid(di->collection, di->info))
		{
		if (new_window)
			{
			view_window_new_from_collection(di->collection, di->info);
			}
		else
			{
			layout_image_set_collection(nullptr, di->collection, di->info);
			}
		}
	else
		{
		if (new_window)
			{
			GList *list;

			list = dupe_listview_get_selection(dw, listview);
			view_window_new_from_list(list);
			filelist_free(list);
			}
		else
			{
			layout_set_fd(nullptr, di->fd);
			}
		}
}

static void dupe_window_remove_selection(DupeWindow *dw, GtkWidget *listview)
{
	GtkTreeSelection *selection;
	GtkTreeModel *store;
	GtkTreeIter iter;
	GList *slist;
	GList *list = nullptr;
	GList *work;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (work)
		{
		auto tpath = static_cast<GtkTreePath *>(work->data);
		DupeItem *di = nullptr;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		if (di) list = g_list_prepend(list, di);
		work = work->next;
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	dw->color_frozen = TRUE;
	work = list;
	while (work)
		{
		DupeItem *di;

		di = static_cast<DupeItem *>(work->data);
		work = work->next;
		dupe_item_remove(dw, di);
		}
	dw->color_frozen = FALSE;

	g_list_free(list);

	dupe_listview_realign_colors(dw);
}

static void dupe_window_edit_selected(DupeWindow *dw, const gchar *key)
{
	file_util_start_editor_from_filelist(key, dupe_listview_get_selection(dw, dw->listview), nullptr, dw->window);
}

static void dupe_window_collection_from_selection(DupeWindow *dw)
{
	CollectWindow *w;
	GList *list;

	list = dupe_listview_get_selection(dw, dw->listview);
	w = collection_window_new(nullptr);
	collection_table_add_filelist(w->table, list);
	filelist_free(list);
}

static void dupe_window_append_file_list(DupeWindow *dw, gint on_second)
{
	GList *list;

	dw->second_drop = (dw->second_set && on_second);

	list = layout_list(nullptr);
	dupe_window_add_files(dw, list, FALSE);
	filelist_free(list);
}

/*
 *-------------------------------------------------------------------
 * main pop-up menu callbacks
 *-------------------------------------------------------------------
 */

static void dupe_menu_view_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	if (dw->click_item) dupe_menu_view(dw, dw->click_item, dw->listview, FALSE);
}

static void dupe_menu_viewnew_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	if (dw->click_item) dupe_menu_view(dw, dw->click_item, dw->listview, TRUE);
}

static void dupe_menu_select_all_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	GtkTreeSelection *selection;

	options->duplicates_select_type = DUPE_SELECT_NONE;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	gtk_tree_selection_select_all(selection);
}

static void dupe_menu_select_none_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	GtkTreeSelection *selection;

	options->duplicates_select_type = DUPE_SELECT_NONE;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	gtk_tree_selection_unselect_all(selection);
}

static void dupe_menu_select_dupes_set1_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	options->duplicates_select_type = DUPE_SELECT_GROUP1;
	dupe_listview_select_dupes(dw, DUPE_SELECT_GROUP1);
}

static void dupe_menu_select_dupes_set2_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	options->duplicates_select_type = DUPE_SELECT_GROUP2;
	dupe_listview_select_dupes(dw, DUPE_SELECT_GROUP2);
}

static void dupe_menu_edit_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw;
	auto key = static_cast<const gchar *>(data);

	dw = static_cast<DupeWindow *>(submenu_item_get_data(widget));
	if (!dw) return;

	dupe_window_edit_selected(dw, key);
}

static void dupe_menu_print_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	FileData *fd;

	fd = (dw->click_item) ? dw->click_item->fd : nullptr;

	print_window_new(fd,
			 dupe_listview_get_selection(dw, dw->listview),
			 dupe_listview_get_filelist(dw, dw->listview), dw->window);
}

static void dupe_menu_copy_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	file_util_copy(nullptr, dupe_listview_get_selection(dw, dw->listview), nullptr, dw->window);
}

static void dupe_menu_move_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	file_util_move(nullptr, dupe_listview_get_selection(dw, dw->listview), nullptr, dw->window);
}

static void dupe_menu_rename_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	file_util_rename(nullptr, dupe_listview_get_selection(dw, dw->listview), dw->window);
}

static void dupe_menu_delete_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	options->file_ops.safe_delete_enable = FALSE;
	file_util_delete_notify_done(nullptr, dupe_listview_get_selection(dw, dw->listview), dw->window, delete_finished_cb, dw);
}

static void dupe_menu_move_to_trash_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	options->file_ops.safe_delete_enable = TRUE;
	file_util_delete_notify_done(nullptr, dupe_listview_get_selection(dw, dw->listview), dw->window, delete_finished_cb, dw);
}

static void dupe_menu_copy_path_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	file_util_copy_path_list_to_clipboard(dupe_listview_get_selection(dw, dw->listview), TRUE);
}

static void dupe_menu_copy_path_unquoted_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	file_util_copy_path_list_to_clipboard(dupe_listview_get_selection(dw, dw->listview), FALSE);
}

static void dupe_menu_remove_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_window_remove_selection(dw, dw->listview);
}

static void dupe_menu_clear_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_window_clear(dw);
}

static void dupe_menu_close_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_window_close(dw);
}

static void dupe_menu_popup_destroy_cb(GtkWidget *, gpointer data)
{
	auto editmenu_fd_list = static_cast<GList *>(data);

	filelist_free(editmenu_fd_list);
}

static GList *dupe_window_get_fd_list(DupeWindow *dw)
{
	GList *list;

	if (gtk_widget_has_focus(dw->second_listview))
		{
		list = dupe_listview_get_selection(dw, dw->second_listview);
		}
	else
		{
		list = dupe_listview_get_selection(dw, dw->listview);
		}

	return list;
}

/**
 * @brief Add file selection list to a collection
 * @param[in] widget
 * @param[in] data Index to the collection list menu item selected, or -1 for new collection
 *
 *
 */
static void dupe_pop_menu_collections_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw;
	GList *selection_list;

	dw = static_cast<DupeWindow *>(submenu_item_get_data(widget));
	selection_list = dupe_listview_get_selection(dw, dw->listview);
	pop_menu_collections(selection_list, data);

	filelist_free(selection_list);
}

static GtkWidget *dupe_menu_popup_main(DupeWindow *dw, DupeItem *di)
{
	GtkWidget *menu;
	GtkWidget *item;
	gint on_row;
	GList *editmenu_fd_list;
	GtkAccelGroup *accel_group;

	on_row = (di != nullptr);

	menu = popup_menu_short_lived();

	accel_group = gtk_accel_group_new();
	gtk_menu_set_accel_group(GTK_MENU(menu), accel_group);

	g_object_set_data(G_OBJECT(menu), "window_keys", dupe_window_keys);
	g_object_set_data(G_OBJECT(menu), "accel_group", accel_group);

	menu_item_add_sensitive(menu, _("_View"), on_row,
				G_CALLBACK(dupe_menu_view_cb), dw);
	menu_item_add_icon_sensitive(menu, _("View in _new window"), GQ_ICON_NEW, on_row,
				G_CALLBACK(dupe_menu_viewnew_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_sensitive(menu, _("Select all"), (dw->dupes != nullptr),
				G_CALLBACK(dupe_menu_select_all_cb), dw);
	menu_item_add_sensitive(menu, _("Select none"), (dw->dupes != nullptr),
				G_CALLBACK(dupe_menu_select_none_cb), dw);
	menu_item_add_sensitive(menu, _("Select group _1 duplicates"), (dw->dupes != nullptr),
				G_CALLBACK(dupe_menu_select_dupes_set1_cb), dw);
	menu_item_add_sensitive(menu, _("Select group _2 duplicates"), (dw->dupes != nullptr),
				G_CALLBACK(dupe_menu_select_dupes_set2_cb), dw);
	menu_item_add_divider(menu);

	submenu_add_export(menu, &item, G_CALLBACK(dupe_pop_menu_export_cb), dw);
	gtk_widget_set_sensitive(item, on_row);
	menu_item_add_divider(menu);

	editmenu_fd_list = dupe_window_get_fd_list(dw);
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(dupe_menu_popup_destroy_cb), editmenu_fd_list);
	submenu_add_edit(menu, &item, G_CALLBACK(dupe_menu_edit_cb), dw, editmenu_fd_list);
	if (!on_row) gtk_widget_set_sensitive(item, FALSE);

	submenu_add_collections(menu, &item,
								G_CALLBACK(dupe_pop_menu_collections_cb), dw);
	gtk_widget_set_sensitive(item, on_row);

	menu_item_add_icon_sensitive(menu, _("Print..."), GQ_ICON_PRINT, on_row,
				G_CALLBACK(dupe_menu_print_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("_Copy..."), GQ_ICON_COPY, on_row,
				G_CALLBACK(dupe_menu_copy_cb), dw);
	menu_item_add_sensitive(menu, _("_Move..."), on_row,
				G_CALLBACK(dupe_menu_move_cb), dw);
	menu_item_add_sensitive(menu, _("_Rename..."), on_row,
				G_CALLBACK(dupe_menu_rename_cb), dw);
	menu_item_add_sensitive(menu, _("_Copy path"), on_row,
				G_CALLBACK(dupe_menu_copy_path_cb), dw);
	menu_item_add_sensitive(menu, _("_Copy path unquoted"), on_row,
				G_CALLBACK(dupe_menu_copy_path_unquoted_cb), dw);

	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu,
				options->file_ops.confirm_move_to_trash ? _("Move to Trash...") :
					_("Move to Trash"), GQ_ICON_DELETE, on_row,
				G_CALLBACK(dupe_menu_move_to_trash_cb), dw);
	menu_item_add_icon_sensitive(menu,
				options->file_ops.confirm_delete ? _("_Delete...") :
					_("_Delete"), GQ_ICON_DELETE_SHRED, on_row,
				G_CALLBACK(dupe_menu_delete_cb), dw);

	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("Rem_ove"), GQ_ICON_REMOVE, on_row,
				G_CALLBACK(dupe_menu_remove_cb), dw);
	menu_item_add_icon_sensitive(menu, _("C_lear"), GQ_ICON_CLEAR, (dw->list != nullptr),
				G_CALLBACK(dupe_menu_clear_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("Close _window"), GQ_ICON_CLOSE,
			    G_CALLBACK(dupe_menu_close_cb), dw);

	return menu;
}

static gboolean dupe_listview_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	DupeItem *di = nullptr;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, nullptr, nullptr, nullptr))
		{
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		gtk_tree_path_free(tpath);
		}

	dw->click_item = di;

	if (bevent->button == MOUSE_BUTTON_RIGHT)
		{
		/* right click menu */
		GtkWidget *menu;

		if (bevent->state & GDK_CONTROL_MASK && bevent->state & GDK_SHIFT_MASK)
			{
			dupe_display_stats(dw, di);
			return TRUE;
			}
		if (widget == dw->listview)
			{
			menu = dupe_menu_popup_main(dw, di);
			}
		else
			{
			menu = dupe_menu_popup_second(dw, di);
			}
		gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
		}

	if (!di) return FALSE;

	if (bevent->button == MOUSE_BUTTON_LEFT &&
	    bevent->type == GDK_2BUTTON_PRESS)
		{
		dupe_menu_view(dw, di, widget, FALSE);
		}

	if (bevent->button == MOUSE_BUTTON_MIDDLE) return TRUE;

	if (bevent->button == MOUSE_BUTTON_RIGHT)
		{
		if (!dupe_listview_item_is_selected(dw, di, widget))
			{
			GtkTreeSelection *selection;

			selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
			gtk_tree_selection_unselect_all(selection);
			gtk_tree_selection_select_iter(selection, &iter);

			tpath = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &iter);
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, nullptr, FALSE);
			gtk_tree_path_free(tpath);
			}

		return TRUE;
		}

	if (bevent->button == MOUSE_BUTTON_LEFT &&
	    bevent->type == GDK_BUTTON_PRESS &&
	    !(bevent->state & GDK_SHIFT_MASK ) &&
	    !(bevent->state & GDK_CONTROL_MASK ) &&
	    dupe_listview_item_is_selected(dw, di, widget))
		{
		/* this selection handled on release_cb */
		gtk_widget_grab_focus(widget);
		return TRUE;
		}

	return FALSE;
}

static gboolean dupe_listview_release_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	DupeItem *di = nullptr;

	if (bevent->button != MOUSE_BUTTON_LEFT && bevent->button != MOUSE_BUTTON_MIDDLE) return TRUE;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));

	if ((bevent->x != 0 || bevent->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, nullptr, nullptr, nullptr))
		{
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		gtk_tree_path_free(tpath);
		}

	if (bevent->button == MOUSE_BUTTON_MIDDLE)
		{
		if (di && dw->click_item == di)
			{
			GtkTreeSelection *selection;

			selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
			if (dupe_listview_item_is_selected(dw, di, widget))
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

	if (di && dw->click_item == di &&
	    !(bevent->state & GDK_SHIFT_MASK ) &&
	    !(bevent->state & GDK_CONTROL_MASK ) &&
	    dupe_listview_item_is_selected(dw, di, widget))
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

/*
 *-------------------------------------------------------------------
 * second set stuff
 *-------------------------------------------------------------------
 */

static void dupe_second_update_status(DupeWindow *dw)
{
	gchar *buf;

	buf = g_strdup_printf(_("%d files (set 2)"), g_list_length(dw->second_list));
	gtk_label_set_text(GTK_LABEL(dw->second_status_label), buf);
	g_free(buf);
}

static void dupe_second_add(DupeWindow *dw, DupeItem *di)
{
	GtkListStore *store;
	GtkTreeIter iter;

	if (!di) return;

	di->second = TRUE;
	dw->second_list = g_list_prepend(dw->second_list, di);

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->second_listview)));
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, DUPE_COLUMN_POINTER, di, 1, di->fd->path, -1);

	dupe_second_update_status(dw);
}

static void dupe_second_remove(DupeWindow *dw, DupeItem *di)
{
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->second_listview)));
	if (dupe_listview_find_item(store, di, &iter) >= 0)
		{
		tree_view_move_cursor_away(GTK_TREE_VIEW(dw->second_listview), &iter, TRUE);
		gtk_list_store_remove(store, &iter);
		}

	dw->second_list = g_list_remove(dw->second_list, di);

	dupe_second_update_status(dw);
}

static void dupe_second_clear(DupeWindow *dw)
{
	GtkListStore *store;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->second_listview)));
	gtk_list_store_clear(store);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(dw->second_listview));

	g_list_free(dw->dupes);
	dw->dupes = nullptr;

	g_list_free_full(dw->second_list, reinterpret_cast<GDestroyNotify>(dupe_item_free));
	dw->second_list = nullptr;

	dupe_match_reset_list(dw->list);

	dupe_second_update_status(dw);
}

static void dupe_second_menu_view_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	if (dw->click_item) dupe_menu_view(dw, dw->click_item, dw->second_listview, FALSE);
}

static void dupe_second_menu_viewnew_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	if (dw->click_item) dupe_menu_view(dw, dw->click_item, dw->second_listview, TRUE);
}

static void dupe_second_menu_select_all_cb(GtkWidget *, gpointer data)
{
	GtkTreeSelection *selection;
	auto dw = static_cast<DupeWindow *>(data);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->second_listview));
	gtk_tree_selection_select_all(selection);
}

static void dupe_second_menu_select_none_cb(GtkWidget *, gpointer data)
{
	GtkTreeSelection *selection;
	auto dw = static_cast<DupeWindow *>(data);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->second_listview));
	gtk_tree_selection_unselect_all(selection);
}

static void dupe_second_menu_remove_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_window_remove_selection(dw, dw->second_listview);
}

static void dupe_second_menu_clear_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_second_clear(dw);
	dupe_window_recompare(dw);
}

static GtkWidget *dupe_menu_popup_second(DupeWindow *dw, DupeItem *di)
{
	GtkWidget *menu;
	gboolean notempty = (dw->second_list != nullptr);
	gboolean on_row = (di != nullptr);
	GtkAccelGroup *accel_group;

	menu = popup_menu_short_lived();
	accel_group = gtk_accel_group_new();
	gtk_menu_set_accel_group(GTK_MENU(menu), accel_group);

	g_object_set_data(G_OBJECT(menu), "window_keys", dupe_window_keys);
	g_object_set_data(G_OBJECT(menu), "accel_group", accel_group);

	menu_item_add_sensitive(menu, _("_View"), on_row,
				G_CALLBACK(dupe_second_menu_view_cb), dw);
	menu_item_add_icon_sensitive(menu, _("View in _new window"), GQ_ICON_NEW, on_row,
				G_CALLBACK(dupe_second_menu_viewnew_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_sensitive(menu, _("Select all"), notempty,
				G_CALLBACK(dupe_second_menu_select_all_cb), dw);
	menu_item_add_sensitive(menu, _("Select none"), notempty,
				G_CALLBACK(dupe_second_menu_select_none_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_icon_sensitive(menu, _("Rem_ove"), GQ_ICON_REMOVE, on_row,
				      G_CALLBACK(dupe_second_menu_remove_cb), dw);
	menu_item_add_icon_sensitive(menu, _("C_lear"), GQ_ICON_CLEAR, notempty,
				   G_CALLBACK(dupe_second_menu_clear_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("Close _window"), GQ_ICON_CLOSE,
			    G_CALLBACK(dupe_menu_close_cb), dw);

	return menu;
}

static void dupe_second_set_toggle_cb(GtkWidget *widget, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dw->second_set = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

	if (dw->second_set)
		{
		dupe_second_update_status(dw);
		gtk_table_set_col_spacings(GTK_TABLE(dw->table), PREF_PAD_GAP);
		gtk_widget_show(dw->second_vbox);
		}
	else
		{
		gtk_table_set_col_spacings(GTK_TABLE(dw->table), 0);
		gtk_widget_hide(dw->second_vbox);
		dupe_second_clear(dw);
		}

	dupe_window_recompare(dw);
}

static void dupe_sort_totals_toggle_cb(GtkWidget *widget, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	options->sort_totals = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	dupe_window_recompare(dw);

}

/*
 *-------------------------------------------------------------------
 * match type menu
 *-------------------------------------------------------------------
 */

enum {
	DUPE_MENU_COLUMN_NAME = 0,
	DUPE_MENU_COLUMN_MASK
};

static void dupe_listview_show_rank(GtkWidget *listview, gboolean rank);

static void dupe_menu_type_cb(GtkWidget *combo, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	GtkTreeModel *store;
	GtkTreeIter iter;

	store = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter)) return;
	gtk_tree_model_get(store, &iter, DUPE_MENU_COLUMN_MASK, &dw->match_mask, -1);

	options->duplicates_match = dw->match_mask;

	if (dw->match_mask & (DUPE_MATCH_SIM_HIGH | DUPE_MATCH_SIM_MED | DUPE_MATCH_SIM_LOW | DUPE_MATCH_SIM_CUSTOM))
		{
		dupe_listview_show_rank(dw->listview, TRUE);
		}
	else
		{
		dupe_listview_show_rank(dw->listview, FALSE);
		}
	dupe_window_recompare(dw);
}

static void dupe_menu_add_item(GtkListStore *store, const gchar *text, DupeMatchType type, DupeWindow *dw)
{
	GtkTreeIter iter;

	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, DUPE_MENU_COLUMN_NAME, text,
					 DUPE_MENU_COLUMN_MASK, type, -1);

	if (dw->match_mask == type) gtk_combo_box_set_active_iter(GTK_COMBO_BOX(dw->combo), &iter);
}

static void dupe_menu_setup(DupeWindow *dw)
{
	GtkListStore *store;
	GtkCellRenderer *renderer;

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	dw->combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(dw->combo), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(dw->combo), renderer,
				       "text", DUPE_MENU_COLUMN_NAME, NULL);

	dupe_menu_add_item(store, _("Name"), DUPE_MATCH_NAME, dw);
	dupe_menu_add_item(store, _("Name case-insensitive"), DUPE_MATCH_NAME_CI, dw);
	dupe_menu_add_item(store, _("Size"), DUPE_MATCH_SIZE, dw);
	dupe_menu_add_item(store, _("Date"), DUPE_MATCH_DATE, dw);
	dupe_menu_add_item(store, _("Dimensions"), DUPE_MATCH_DIM, dw);
	dupe_menu_add_item(store, _("Checksum"), DUPE_MATCH_SUM, dw);
	dupe_menu_add_item(store, _("Path"), DUPE_MATCH_PATH, dw);
	dupe_menu_add_item(store, _("Similarity (high - 95)"), DUPE_MATCH_SIM_HIGH, dw);
	dupe_menu_add_item(store, _("Similarity (med. - 90)"), DUPE_MATCH_SIM_MED, dw);
	dupe_menu_add_item(store, _("Similarity (low - 85)"), DUPE_MATCH_SIM_LOW, dw);
	dupe_menu_add_item(store, _("Similarity (custom)"), DUPE_MATCH_SIM_CUSTOM, dw);
	dupe_menu_add_item(store, _("Name ≠ content"), DUPE_MATCH_NAME_CONTENT, dw);
	dupe_menu_add_item(store, _("Name case-insensitive ≠ content"), DUPE_MATCH_NAME_CI_CONTENT, dw);
	dupe_menu_add_item(store, _("Show all"), DUPE_MATCH_ALL, dw);

	g_signal_connect(G_OBJECT(dw->combo), "changed",
			 G_CALLBACK(dupe_menu_type_cb), dw);
}

/*
 *-------------------------------------------------------------------
 * list view columns
 *-------------------------------------------------------------------
 */

/* this overrides the low default of a GtkCellRenderer from 100 to CELL_HEIGHT_OVERRIDE, something sane for our purposes */

#define CELL_HEIGHT_OVERRIDE 512

void cell_renderer_height_override(GtkCellRenderer *renderer)
{
	GParamSpec *spec;

	spec = g_object_class_find_property(G_OBJECT_GET_CLASS(G_OBJECT(renderer)), "height");
	if (spec && G_IS_PARAM_SPEC_INT(spec))
		{
		GParamSpecInt *spec_int;

		spec_int = G_PARAM_SPEC_INT(spec);
		if (spec_int->maximum < CELL_HEIGHT_OVERRIDE) spec_int->maximum = CELL_HEIGHT_OVERRIDE;
		}
}

static GdkColor *dupe_listview_color_shifted(GtkWidget *widget)
{
	static GdkColor color;
	static GtkWidget *done = nullptr;

	if (done != widget)
		{
		GtkStyle *style;

		style = gtk_widget_get_style(widget);
		memcpy(&color, &style->base[GTK_STATE_NORMAL], sizeof(color));
		shift_color(&color, -1, 0);
		done = widget;
		}

	return &color;
}

static void dupe_listview_color_cb(GtkTreeViewColumn *, GtkCellRenderer *cell,
				   GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	gboolean set;

	gtk_tree_model_get(tree_model, iter, DUPE_COLUMN_COLOR, &set, -1);
	g_object_set(G_OBJECT(cell),
		     "cell-background-gdk", dupe_listview_color_shifted(dw->listview),
		     "cell-background-set", set, NULL);
}

static void dupe_listview_add_column(DupeWindow *dw, GtkWidget *listview, gint n, const gchar *title, gboolean image, gboolean right_justify)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, title);
	gtk_tree_view_column_set_min_width(column, 4);
	gtk_tree_view_column_set_sort_column_id(column, n);

	if (n != DUPE_COLUMN_RANK &&
	    n != DUPE_COLUMN_THUMB)
		{
		gtk_tree_view_column_set_resizable(column, TRUE);
		}

	if (!image)
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
		renderer = gtk_cell_renderer_text_new();
		if (right_justify)
			{
			g_object_set(G_OBJECT(renderer), "xalign", 1.0, NULL);
			}
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_add_attribute(column, renderer, "text", n);
		}
	else
		{
		gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
		renderer = gtk_cell_renderer_pixbuf_new();
		cell_renderer_height_override(renderer);
		gtk_tree_view_column_pack_start(column, renderer, TRUE);
		gtk_tree_view_column_add_attribute(column, renderer, "pixbuf", n);
		}

	if (listview == dw->listview)
		{
		/* sets background before rendering */
		gtk_tree_view_column_set_cell_data_func(column, renderer, dupe_listview_color_cb, dw, nullptr);
		}

	gtk_tree_view_append_column(GTK_TREE_VIEW(listview), column);
}

static void dupe_listview_set_height(GtkWidget *listview, gboolean thumb)
{
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;
	GList *list;

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), DUPE_COLUMN_THUMB - 1);
	if (!column) return;

	gtk_tree_view_column_set_fixed_width(column, (thumb) ? options->thumbnails.max_width : 4);
	gtk_tree_view_column_set_visible(column, thumb);

	list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
	if (!list) return;
	cell = static_cast<GtkCellRenderer *>(list->data);
	g_list_free(list);

	g_object_set(G_OBJECT(cell), "height", (thumb) ? options->thumbnails.max_height : -1, NULL);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(listview));
}

static void dupe_listview_show_rank(GtkWidget *listview, gboolean rank)
{
	GtkTreeViewColumn *column;

	column = gtk_tree_view_get_column(GTK_TREE_VIEW(listview), DUPE_COLUMN_RANK - 1);
	if (!column) return;

	gtk_tree_view_column_set_visible(column, rank);
}

/*
 *-------------------------------------------------------------------
 * misc cb
 *-------------------------------------------------------------------
 */

static void dupe_window_show_thumb_cb(GtkWidget *widget, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dw->show_thumbs = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	options->duplicates_thumbnails = dw->show_thumbs;

	if (dw->show_thumbs)
		{
		if (!dw->working) dupe_thumb_step(dw);
		}
	else
		{
		GtkTreeModel *store;
		GtkTreeIter iter;
		gboolean valid;

		thumb_loader_free(dw->thumb_loader);
		dw->thumb_loader = nullptr;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview));
		valid = gtk_tree_model_get_iter_first(store, &iter);

		while (valid)
			{
			gtk_list_store_set(GTK_LIST_STORE(store), &iter, DUPE_COLUMN_THUMB, NULL, -1);
			valid = gtk_tree_model_iter_next(store, &iter);
			}
		dupe_window_update_progress(dw, nullptr, 0.0, FALSE);
		}

	dupe_listview_set_height(dw->listview, dw->show_thumbs);
}

static void dupe_window_rotation_invariant_cb(GtkWidget *widget, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	options->rot_invariant_sim = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	dupe_window_recompare(dw);
}

static void dupe_window_custom_threshold_cb(GtkWidget *widget, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	DupeMatchType match_type;
	GtkTreeModel *store;
	gboolean valid;
	GtkTreeIter iter;

	options->duplicates_similarity_threshold = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
	dw->match_mask = DUPE_MATCH_SIM_CUSTOM;

	store = gtk_combo_box_get_model(GTK_COMBO_BOX(dw->combo));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		gtk_tree_model_get(store, &iter, DUPE_MENU_COLUMN_MASK, &match_type, -1);
		if (match_type == DUPE_MATCH_SIM_CUSTOM)
			{
			break;
			}
		valid = gtk_tree_model_iter_next(store, &iter);
		}

	gtk_combo_box_set_active_iter(GTK_COMBO_BOX(dw->combo), &iter);
	dupe_window_recompare(dw);
}

static gboolean dupe_window_keypress_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	gboolean stop_signal = FALSE;
	gboolean on_second;
	GtkWidget *listview;
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	DupeItem *di = nullptr;

	on_second = gtk_widget_has_focus(dw->second_listview);

	if (on_second)
		{
		listview = dw->second_listview;
		}
	else
		{
		listview = dw->listview;
		}

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
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
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		}
	g_list_free_full(slist, reinterpret_cast<GDestroyNotify>(gtk_tree_path_free));

	if (event->state & GDK_CONTROL_MASK)
		{
		if (!on_second)
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
					file_util_copy(nullptr, dupe_listview_get_selection(dw, listview),
						       nullptr, dw->window);
					break;
				case 'M': case 'm':
					file_util_move(nullptr, dupe_listview_get_selection(dw, listview),
						       nullptr, dw->window);
					break;
				case 'R': case 'r':
					file_util_rename(nullptr, dupe_listview_get_selection(dw, listview), dw->window);
					break;
				case 'D': case 'd':
					options->file_ops.safe_delete_enable = TRUE;
					file_util_delete(nullptr, dupe_listview_get_selection(dw, listview), dw->window);
					break;
				default:
					stop_signal = FALSE;
					break;
				}
			}

		if (!stop_signal)
			{
			stop_signal = TRUE;
			switch (event->keyval)
				{
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
					if (on_second)
						{
						dupe_second_clear(dw);
						dupe_window_recompare(dw);
						}
					else
						{
						dupe_window_clear(dw);
						}
					break;
				case 'L': case 'l':
					dupe_window_append_file_list(dw, FALSE);
					break;
				case 'T': case 't':
					gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dw->button_thumbs),
						!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dw->button_thumbs)));
					break;
				case 'W': case 'w':
					dupe_window_close(dw);
					break;
				default:
					stop_signal = FALSE;
					break;
				}
			}
		}
	else
		{
		stop_signal = TRUE;
		switch (event->keyval)
			{
			case GDK_KEY_Return: case GDK_KEY_KP_Enter:
				dupe_menu_view(dw, di, listview, FALSE);
				break;
			case 'V': case 'v':
				dupe_menu_view(dw, di, listview, TRUE);
				break;
			case GDK_KEY_Delete: case GDK_KEY_KP_Delete:
				dupe_window_remove_selection(dw, listview);
				break;
			case 'C': case 'c':
				if (!on_second)
					{
					dupe_window_collection_from_selection(dw);
					}
				break;
			case '0':
				options->duplicates_select_type = DUPE_SELECT_NONE;
				dupe_listview_select_dupes(dw, DUPE_SELECT_NONE);
				break;
			case '1':
				options->duplicates_select_type = DUPE_SELECT_GROUP1;
				dupe_listview_select_dupes(dw, DUPE_SELECT_GROUP1);
				break;
			case '2':
				options->duplicates_select_type = DUPE_SELECT_GROUP2;
				dupe_listview_select_dupes(dw, DUPE_SELECT_GROUP2);
				break;
			case GDK_KEY_Menu:
			case GDK_KEY_F10:
				if (!on_second)
					{
					GtkWidget *menu;

					menu = dupe_menu_popup_main(dw, di);
					gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER, nullptr);
					}
				else
					{
					GtkWidget *menu;

					menu = dupe_menu_popup_second(dw, di);
					gtk_menu_popup_at_widget(GTK_MENU(menu), widget, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER, nullptr);
					}
				break;
			default:
				stop_signal = FALSE;
				break;
			}
		}
	if (!stop_signal && is_help_key(event))
		{
		help_window_show("GuideImageSearchFindingDuplicates.html");
		stop_signal = TRUE;
		}

	return stop_signal;
}


void dupe_window_clear(DupeWindow *dw)
{
	GtkListStore *store;

	dupe_check_stop(dw);

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview)));
	gtk_list_store_clear(store);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(dw->listview));

	g_list_free(dw->dupes);
	dw->dupes = nullptr;

	g_list_free_full(dw->list, reinterpret_cast<GDestroyNotify>(dupe_item_free));
	dw->list = nullptr;
	dw->set_count = 0;

	dupe_match_reset_list(dw->second_list);

	dupe_window_update_count(dw, FALSE);
	dupe_window_update_progress(dw, nullptr, 0.0, FALSE);
}

static void dupe_window_get_geometry(DupeWindow *dw)
{
	GdkWindow *window;
	LayoutWindow *lw = nullptr;

	layout_valid(&lw);

	if (!dw || !lw) return;

	window = gtk_widget_get_window(dw->window);
	gdk_window_get_position(window, &lw->options.dupe_window.x, &lw->options.dupe_window.y);
	lw->options.dupe_window.w = gdk_window_get_width(window);
	lw->options.dupe_window.h = gdk_window_get_height(window);
}

void dupe_window_close(DupeWindow *dw)
{
	dupe_check_stop(dw);

	dupe_window_get_geometry(dw);

	dupe_window_list = g_list_remove(dupe_window_list, dw);
	gtk_widget_destroy(dw->window);

	g_list_free(dw->dupes);
	g_list_free_full(dw->list, reinterpret_cast<GDestroyNotify>(dupe_item_free));

	g_list_free_full(dw->second_list, reinterpret_cast<GDestroyNotify>(dupe_item_free));

	file_data_unregister_notify_func(dupe_notify_cb, dw);

	g_thread_pool_free(dw->dupe_comparison_thread_pool, TRUE, TRUE);

	g_free(dw);
}

static gint dupe_window_close_cb(GtkWidget *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	dupe_window_close(dw);

	return TRUE;
}

static gint dupe_window_delete(GtkWidget *, GdkEvent *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	dupe_window_close(dw);

	return TRUE;
}

static void dupe_help_cb(GtkAction *, gpointer)
{
	help_window_show("GuideImageSearchFindingDuplicates.html");
}

static gint default_sort_cb(GtkTreeModel *, GtkTreeIter *, GtkTreeIter *, gpointer)
{
	return 0;
}

static gint column_sort_cb(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	auto sortable = static_cast<GtkTreeSortable *>(data);
	gint ret = 0;
	gchar *rank_str_a, *rank_str_b;
	gint rank_int_a;
	gint rank_int_b;
	gint group_a;
	gint group_b;
	gint sort_column_id;
	GtkSortType sort_order;
	DupeItem *di_a;
	DupeItem *di_b;

	gtk_tree_sortable_get_sort_column_id(sortable, &sort_column_id, &sort_order);

	gtk_tree_model_get(model, a, DUPE_COLUMN_RANK, &rank_str_a, DUPE_COLUMN_SET, &group_a, DUPE_COLUMN_POINTER, &di_a, -1);

	gtk_tree_model_get(model, b, DUPE_COLUMN_RANK, &rank_str_b, DUPE_COLUMN_SET, &group_b, DUPE_COLUMN_POINTER, &di_b, -1);

	if (group_a == group_b)
		{
		switch (sort_column_id)
			{
			case DUPE_COLUMN_NAME:
				ret = utf8_compare(di_a->fd->name, di_b->fd->name, TRUE);
				break;
			case DUPE_COLUMN_SIZE:
				if (di_a->fd->size == di_b->fd->size)
					{
					ret = 0;
					}
				else
					{
					ret = (di_a->fd->size > di_b->fd->size) ? 1 : -1;
					}
				break;
			case DUPE_COLUMN_DATE:
				if (di_a->fd->date == di_b->fd->date)
					{
					ret = 0;
					}
				else
					{
					ret = (di_a->fd->date > di_b->fd->date) ? 1 : -1;
					}
				break;
			case DUPE_COLUMN_DIMENSIONS:
				if ((di_a->width == di_b->width) && (di_a->height == di_b->height))
					{
					ret = 0;
					}
				else
					{
					ret = ((di_a->width * di_a->height) > (di_b->width * di_b->height)) ? 1 : -1;
					}
				break;
			case DUPE_COLUMN_RANK:
				rank_int_a = atoi(rank_str_a);
				rank_int_b = atoi(rank_str_b);
				if (rank_int_a == 0) rank_int_a = 101;
				if (rank_int_b == 0) rank_int_b = 101;

				if (rank_int_a == rank_int_b)
					{
					ret = 0;
					}
				else
					{
					ret = (rank_int_a > rank_int_b) ? 1 : -1;
					}
				break;
			case DUPE_COLUMN_PATH:
				ret = utf8_compare(di_a->fd->path, di_b->fd->path, TRUE);
				break;
			}
		}
	else if (group_a < group_b)
		{
		ret = (sort_order == GTK_SORT_ASCENDING) ? 1 : -1;
		}
	else
		{
		ret = (sort_order == GTK_SORT_ASCENDING) ? -1 : 1;
		}

	return ret;
}

static void column_clicked_cb(GtkWidget *,  gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	options->duplicates_match = DUPE_SELECT_NONE;
	dupe_listview_select_dupes(dw, DUPE_SELECT_NONE);
}

/* collection and files can be NULL */
DupeWindow *dupe_window_new()
{
	DupeWindow *dw;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *scrolled;
	GtkWidget *frame;
	GtkWidget *status_box;
	GtkWidget *controls_box;
	GtkWidget *button_box;
	GtkWidget *label;
	GtkWidget *button;
	GtkListStore *store;
	GtkTreeSelection *selection;
	GdkGeometry geometry;
	LayoutWindow *lw = nullptr;

	layout_valid(&lw);

	dw = g_new0(DupeWindow, 1);
	dw->add_files_queue = nullptr;
	dw->add_files_queue_id = 0;

	dw->match_mask = DUPE_MATCH_NAME;
	if (options->duplicates_match == DUPE_MATCH_NAME) dw->match_mask = DUPE_MATCH_NAME;
	if (options->duplicates_match == DUPE_MATCH_SIZE) dw->match_mask = DUPE_MATCH_SIZE;
	if (options->duplicates_match == DUPE_MATCH_DATE) dw->match_mask = DUPE_MATCH_DATE;
	if (options->duplicates_match == DUPE_MATCH_DIM) dw->match_mask = DUPE_MATCH_DIM;
	if (options->duplicates_match == DUPE_MATCH_SUM) dw->match_mask = DUPE_MATCH_SUM;
	if (options->duplicates_match == DUPE_MATCH_PATH) dw->match_mask = DUPE_MATCH_PATH;
	if (options->duplicates_match == DUPE_MATCH_SIM_HIGH) dw->match_mask = DUPE_MATCH_SIM_HIGH;
	if (options->duplicates_match == DUPE_MATCH_SIM_MED) dw->match_mask = DUPE_MATCH_SIM_MED;
	if (options->duplicates_match == DUPE_MATCH_SIM_LOW) dw->match_mask = DUPE_MATCH_SIM_LOW;
	if (options->duplicates_match == DUPE_MATCH_SIM_CUSTOM) dw->match_mask = DUPE_MATCH_SIM_CUSTOM;
	if (options->duplicates_match == DUPE_MATCH_NAME_CI) dw->match_mask = DUPE_MATCH_NAME_CI;
	if (options->duplicates_match == DUPE_MATCH_NAME_CONTENT) dw->match_mask = DUPE_MATCH_NAME_CONTENT;
	if (options->duplicates_match == DUPE_MATCH_NAME_CI_CONTENT) dw->match_mask = DUPE_MATCH_NAME_CI_CONTENT;
	if (options->duplicates_match == DUPE_MATCH_ALL) dw->match_mask = DUPE_MATCH_ALL;

	dw->window = window_new(GTK_WINDOW_TOPLEVEL, "dupe", nullptr, nullptr, _("Find duplicates"));
	DEBUG_NAME(dw->window);

	geometry.min_width = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.min_height = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.base_width = DUPE_DEF_WIDTH;
	geometry.base_height = DUPE_DEF_HEIGHT;
	gtk_window_set_geometry_hints(GTK_WINDOW(dw->window), nullptr, &geometry,
				      static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE));

	if (lw && options->save_window_positions)
		{
		gtk_window_set_default_size(GTK_WINDOW(dw->window), lw->options.dupe_window.w, lw->options.dupe_window.h);
		gtk_window_move(GTK_WINDOW(dw->window), lw->options.dupe_window.x, lw->options.dupe_window.y);
		}
	else
		{
		gtk_window_set_default_size(GTK_WINDOW(dw->window), DUPE_DEF_WIDTH, DUPE_DEF_HEIGHT);
		}

	gtk_window_set_resizable(GTK_WINDOW(dw->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(dw->window), 0);

	g_signal_connect(G_OBJECT(dw->window), "delete_event",
			 G_CALLBACK(dupe_window_delete), dw);
	g_signal_connect(G_OBJECT(dw->window), "key_press_event",
			 G_CALLBACK(dupe_window_keypress_cb), dw);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(dw->window), vbox);
	gtk_widget_show(vbox);

	dw->table = gtk_table_new(1, 3, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), dw->table, TRUE, TRUE, 0);
	gtk_widget_show(dw->table);

	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_table_attach_defaults(GTK_TABLE(dw->table), scrolled, 0, 2, 0, 1);
	gtk_widget_show(scrolled);

	store = gtk_list_store_new(DUPE_COLUMN_COUNT, G_TYPE_POINTER, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INT, G_TYPE_INT);
	dw->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	dw->sortable = GTK_TREE_SORTABLE(store);

	gtk_tree_sortable_set_sort_func(dw->sortable, DUPE_COLUMN_RANK, column_sort_cb, dw->sortable, nullptr);
	gtk_tree_sortable_set_sort_func(dw->sortable, DUPE_COLUMN_SET, default_sort_cb, dw->sortable, nullptr);
	gtk_tree_sortable_set_sort_func(dw->sortable, DUPE_COLUMN_THUMB, default_sort_cb, dw->sortable, nullptr);
	gtk_tree_sortable_set_sort_func(dw->sortable, DUPE_COLUMN_NAME, column_sort_cb, dw->sortable, nullptr);
	gtk_tree_sortable_set_sort_func(dw->sortable, DUPE_COLUMN_SIZE, column_sort_cb, dw->sortable, nullptr);
	gtk_tree_sortable_set_sort_func(dw->sortable, DUPE_COLUMN_DATE, column_sort_cb, dw->sortable, nullptr);
	gtk_tree_sortable_set_sort_func(dw->sortable, DUPE_COLUMN_DIMENSIONS, column_sort_cb, dw->sortable, nullptr);
	gtk_tree_sortable_set_sort_func(dw->sortable, DUPE_COLUMN_PATH, column_sort_cb, dw->sortable, nullptr);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_MULTIPLE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(dw->listview), TRUE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(dw->listview), FALSE);

	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_RANK, _("Rank"), FALSE, TRUE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_THUMB, _("Thumb"), TRUE, FALSE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_NAME, _("Name"), FALSE, FALSE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_SIZE, _("Size"), FALSE, TRUE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_DATE, _("Date"), FALSE, TRUE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_DIMENSIONS, _("Dimensions"), FALSE, FALSE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_PATH, _("Path"), FALSE, FALSE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_SET, _("Set"), FALSE, FALSE);

	g_signal_connect(gtk_tree_view_get_column(GTK_TREE_VIEW(dw->listview), DUPE_COLUMN_RANK - 1), "clicked", (GCallback)column_clicked_cb, dw);
	g_signal_connect(gtk_tree_view_get_column(GTK_TREE_VIEW(dw->listview), DUPE_COLUMN_NAME - 1), "clicked", (GCallback)column_clicked_cb, dw);
	g_signal_connect(gtk_tree_view_get_column(GTK_TREE_VIEW(dw->listview), DUPE_COLUMN_SIZE - 1), "clicked", (GCallback)column_clicked_cb, dw);
	g_signal_connect(gtk_tree_view_get_column(GTK_TREE_VIEW(dw->listview), DUPE_COLUMN_DATE - 1), "clicked", (GCallback)column_clicked_cb, dw);
	g_signal_connect(gtk_tree_view_get_column(GTK_TREE_VIEW(dw->listview), DUPE_COLUMN_DIMENSIONS - 1), "clicked", (GCallback)column_clicked_cb, dw);
	g_signal_connect(gtk_tree_view_get_column(GTK_TREE_VIEW(dw->listview), DUPE_COLUMN_PATH - 1), "clicked", (GCallback)column_clicked_cb, dw);

	gtk_container_add(GTK_CONTAINER(scrolled), dw->listview);
	gtk_widget_show(dw->listview);

	dw->second_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_table_attach_defaults(GTK_TABLE(dw->table), dw->second_vbox, 2, 3, 0, 1);
	if (dw->second_set)
		{
		gtk_table_set_col_spacings(GTK_TABLE(dw->table), PREF_PAD_GAP);
		gtk_widget_show(dw->second_vbox);
		}
	else
		{
		gtk_table_set_col_spacings(GTK_TABLE(dw->table), 0);
		}

	scrolled = gtk_scrolled_window_new(nullptr, nullptr);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(dw->second_vbox), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	store = gtk_list_store_new(2, G_TYPE_POINTER, G_TYPE_STRING);
	dw->second_listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->second_listview));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_MULTIPLE);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(dw->second_listview), TRUE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(dw->second_listview), FALSE);

	dupe_listview_add_column(dw, dw->second_listview, 1, _("Compare to:"), FALSE, FALSE);

	gtk_container_add(GTK_CONTAINER(scrolled), dw->second_listview);
	gtk_widget_show(dw->second_listview);

	dw->second_status_label = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(dw->second_vbox), dw->second_status_label, FALSE, FALSE, 0);
	gtk_widget_show(dw->second_status_label);

	pref_line(dw->second_vbox, GTK_ORIENTATION_HORIZONTAL);

	status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), status_box, FALSE, FALSE, 0);
	gtk_widget_show(status_box);

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(status_box), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	dw->status_label = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(frame), dw->status_label);
	gtk_widget_show(dw->status_label);

	dw->extra_label = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dw->extra_label), 0.0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(dw->extra_label), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(dw->extra_label), TRUE);
	gtk_box_pack_start(GTK_BOX(status_box), dw->extra_label, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(dw->extra_label);

	controls_box = pref_box_new(vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);
	dw->controls_box = controls_box;

	dw->button_thumbs = gtk_check_button_new_with_label(_("Thumbnails"));
	gtk_widget_set_tooltip_text(GTK_WIDGET(dw->button_thumbs), "Ctrl-T");
	dw->show_thumbs = options->duplicates_thumbnails;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dw->button_thumbs), dw->show_thumbs);
	g_signal_connect(G_OBJECT(dw->button_thumbs), "toggled",
			 G_CALLBACK(dupe_window_show_thumb_cb), dw);
	gtk_box_pack_start(GTK_BOX(controls_box), dw->button_thumbs, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(dw->button_thumbs);

	label = gtk_label_new(_("Compare by:"));
	gtk_box_pack_start(GTK_BOX(controls_box), label, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(label);

	dupe_menu_setup(dw);
	gtk_box_pack_start(GTK_BOX(controls_box), dw->combo, FALSE, FALSE, 0);
	gtk_widget_show(dw->combo);

	label = gtk_label_new(_("Custom Threshold"));
	gtk_box_pack_start(GTK_BOX(controls_box), label, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(label);
	dw->custom_threshold = gtk_spin_button_new_with_range(1, 100, 1);
	gtk_widget_set_tooltip_text(GTK_WIDGET(dw->custom_threshold), "Custom similarity threshold\n(Use tab key to set value)");
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(dw->custom_threshold), options->duplicates_similarity_threshold);
	g_signal_connect(G_OBJECT(dw->custom_threshold), "value_changed", G_CALLBACK(dupe_window_custom_threshold_cb), dw);
	gtk_box_pack_start(GTK_BOX(controls_box), dw->custom_threshold, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(dw->custom_threshold);

	button = gtk_check_button_new_with_label(_("Sort"));
	gtk_widget_set_tooltip_text(GTK_WIDGET(button), "Sort by group totals");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), options->sort_totals);
	g_signal_connect(G_OBJECT(button), "toggled", G_CALLBACK(dupe_sort_totals_toggle_cb), dw);
	gtk_box_pack_start(GTK_BOX(controls_box), button, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(button);

	dw->button_rotation_invariant = gtk_check_button_new_with_label(_("Ignore Orientation"));
	gtk_widget_set_tooltip_text(GTK_WIDGET(dw->button_rotation_invariant), "Ignore image orientation");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dw->button_rotation_invariant), options->rot_invariant_sim);
	g_signal_connect(G_OBJECT(dw->button_rotation_invariant), "toggled",
			 G_CALLBACK(dupe_window_rotation_invariant_cb), dw);
	gtk_box_pack_start(GTK_BOX(controls_box), dw->button_rotation_invariant, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(dw->button_rotation_invariant);

	button = gtk_check_button_new_with_label(_("Compare two file sets"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), dw->second_set);
	g_signal_connect(G_OBJECT(button), "toggled",
			 G_CALLBACK(dupe_second_set_toggle_cb), dw);
	gtk_box_pack_start(GTK_BOX(controls_box), button, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(button);

	button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);
	gtk_widget_show(button_box);

	hbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(hbox), GTK_BUTTONBOX_END);
	gtk_box_set_spacing(GTK_BOX(hbox), PREF_PAD_SPACE);
	gtk_box_pack_end(GTK_BOX(button_box), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	button = pref_button_new(nullptr, GQ_ICON_HELP, _("Help"), G_CALLBACK(dupe_help_cb), nullptr);
	gtk_widget_set_tooltip_text(GTK_WIDGET(button), "F1");
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_STOP, _("Stop"), G_CALLBACK(dupe_check_stop_cb), dw);
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);

	button = pref_button_new(nullptr, GQ_ICON_CLOSE, _("Close"), G_CALLBACK(dupe_window_close_cb), dw);
	gtk_widget_set_tooltip_text(GTK_WIDGET(button), "Ctrl-W");
	gtk_container_add(GTK_CONTAINER(hbox), button);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_grab_default(button);
	gtk_widget_show(button);
	dupe_dnd_init(dw);

	/* order is important here, dnd_init should be seeing mouse
	 * presses before we possibly handle (and stop) the signal
	 */
	g_signal_connect(G_OBJECT(dw->listview), "button_press_event",
			 G_CALLBACK(dupe_listview_press_cb), dw);
	g_signal_connect(G_OBJECT(dw->listview), "button_release_event",
			 G_CALLBACK(dupe_listview_release_cb), dw);
	g_signal_connect(G_OBJECT(dw->second_listview), "button_press_event",
			 G_CALLBACK(dupe_listview_press_cb), dw);
	g_signal_connect(G_OBJECT(dw->second_listview), "button_release_event",
			 G_CALLBACK(dupe_listview_release_cb), dw);

	gtk_widget_show(dw->window);

	dupe_listview_set_height(dw->listview, dw->show_thumbs);
	g_signal_emit_by_name(G_OBJECT(dw->combo), "changed");

	dupe_window_update_count(dw, TRUE);
	dupe_window_update_progress(dw, nullptr, 0.0, FALSE);

	dupe_window_list = g_list_append(dupe_window_list, dw);

	file_data_register_notify_func(dupe_notify_cb, dw, NOTIFY_PRIORITY_MEDIUM);

	g_mutex_init(&dw->thread_count_mutex);
	g_mutex_init(&dw->search_matches_mutex);
	dw->dupe_comparison_thread_pool = g_thread_pool_new(dupe_comparison_func, dw, options->threads.duplicates, FALSE, nullptr);

	return dw;
}

/*
 *-------------------------------------------------------------------
 * dnd confirm dir
 *-------------------------------------------------------------------
 */

struct CDupeConfirmD {
	DupeWindow *dw;
	GList *list;
};

static void confirm_dir_list_cancel(GtkWidget *, gpointer)
{
	/* do nothing */
}

static void confirm_dir_list_add(GtkWidget *, gpointer data)
{
	auto d = static_cast<CDupeConfirmD *>(data);
	GList *work;

	dupe_window_add_files(d->dw, d->list, FALSE);

	work = d->list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;
		if (isdir(fd->path))
			{
			GList *list;

			filelist_read(fd, &list, nullptr);
			list = filelist_filter(list, FALSE);
			if (list)
				{
				dupe_window_add_files(d->dw, list, FALSE);
				filelist_free(list);
				}
			}
		}
}

static void confirm_dir_list_recurse(GtkWidget *, gpointer data)
{
	auto d = static_cast<CDupeConfirmD *>(data);
	dupe_window_add_files(d->dw, d->list, TRUE);
}

static void confirm_dir_list_skip(GtkWidget *, gpointer data)
{
	auto d = static_cast<CDupeConfirmD *>(data);
	dupe_window_add_files(d->dw, d->list, FALSE);
}

static void confirm_dir_list_destroy(GtkWidget *, gpointer data)
{
	auto d = static_cast<CDupeConfirmD *>(data);
	filelist_free(d->list);
	g_free(d);
}

static GtkWidget *dupe_confirm_dir_list(DupeWindow *dw, GList *list)
{
	GtkWidget *menu;
	CDupeConfirmD *d;

	d = g_new0(CDupeConfirmD, 1);
	d->dw = dw;
	d->list = list;

	menu = popup_menu_short_lived();
	g_signal_connect(G_OBJECT(menu), "destroy",
			 G_CALLBACK(confirm_dir_list_destroy), d);

	menu_item_add_stock(menu, _("Dropped list includes folders."), GQ_ICON_DND, nullptr, nullptr);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("_Add contents"), GQ_ICON_OK, G_CALLBACK(confirm_dir_list_add), d);
	menu_item_add_icon(menu, _("Add contents _recursive"), GQ_ICON_ADD, G_CALLBACK(confirm_dir_list_recurse), d);
	menu_item_add_icon(menu, _("_Skip folders"), GQ_ICON_REMOVE, G_CALLBACK(confirm_dir_list_skip), d);
	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("Cancel"), GQ_ICON_CANCEL, G_CALLBACK(confirm_dir_list_cancel), d);

	return menu;
}

/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */

static GtkTargetEntry dupe_drag_types[] = {
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
};
static gint n_dupe_drag_types = 2;

static GtkTargetEntry dupe_drop_types[] = {
	{ const_cast<gchar *>(TARGET_APP_COLLECTION_MEMBER_STRING), 0, TARGET_APP_COLLECTION_MEMBER },
	{ const_cast<gchar *>("text/uri-list"), 0, TARGET_URI_LIST }
};
static gint n_dupe_drop_types = 2;

static void dupe_dnd_data_set(GtkWidget *widget, GdkDragContext *,
			      GtkSelectionData *selection_data, guint info,
			      guint, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	GList *list;

	switch (info)
		{
		case TARGET_URI_LIST:
		case TARGET_TEXT_PLAIN:
			list = dupe_listview_get_selection(dw, widget);
			if (!list) return;
			uri_selection_data_set_uris_from_filelist(selection_data, list);
			filelist_free(list);
			break;
		default:
			break;
		}
}

static void dupe_dnd_data_get(GtkWidget *widget, GdkDragContext *context,
			      gint, gint,
			      GtkSelectionData *selection_data, guint info,
			      guint, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	GtkWidget *source;
	GList *list = nullptr;
	GList *work;

	if (dw->add_files_queue_id > 0)
		{
		warning_dialog(_("Find duplicates"), _("Please wait for the current file selection to be loaded."), GQ_ICON_DIALOG_INFO, dw->window);

		return;
		}

	source = gtk_drag_get_source_widget(context);
	if (source == dw->listview || source == dw->second_listview) return;

	dw->second_drop = (dw->second_set && widget == dw->second_listview);

	switch (info)
		{
		case TARGET_APP_COLLECTION_MEMBER:
			collection_from_dnd_data(reinterpret_cast<const gchar *>(gtk_selection_data_get_data(selection_data)), &list, nullptr);
			break;
		case TARGET_URI_LIST:
			list = uri_filelist_from_gtk_selection_data(selection_data);
			work = list;
			while (work)
				{
				auto fd = static_cast<FileData *>(work->data);
				if (isdir(fd->path))
					{
					GtkWidget *menu;
					menu = dupe_confirm_dir_list(dw, list);
					gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
					return;
					}
				work = work->next;
				}
			break;
		default:
			list = nullptr;
			break;
		}

	if (list)
		{
		dupe_window_add_files(dw, list, FALSE);
		filelist_free(list);
		}
}

static void dupe_dest_set(GtkWidget *widget, gboolean enable)
{
	if (enable)
		{
		gtk_drag_dest_set(widget,
			static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP),
			dupe_drop_types, n_dupe_drop_types,
			static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK));

		}
	else
		{
		gtk_drag_dest_unset(widget);
		}
}

static void dupe_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	dupe_dest_set(dw->listview, FALSE);
	dupe_dest_set(dw->second_listview, FALSE);

	if (dw->click_item && !dupe_listview_item_is_selected(dw, dw->click_item, widget))
		{
		GtkListStore *store;
		GtkTreeIter iter;

		store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));
		if (dupe_listview_find_item(store, dw->click_item, &iter) >= 0)
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

	if (dw->show_thumbs &&
	    widget == dw->listview &&
	    dw->click_item && dw->click_item->pixbuf)
		{
		GtkTreeSelection *selection;
		gint items;

		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
		items = gtk_tree_selection_count_selected_rows(selection);
		dnd_set_drag_icon(widget, context, dw->click_item->pixbuf, items);
		}
}

static void dupe_dnd_end(GtkWidget *, GdkDragContext *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);
	dupe_dest_set(dw->listview, TRUE);
	dupe_dest_set(dw->second_listview, TRUE);
}

static void dupe_dnd_init(DupeWindow *dw)
{
	gtk_drag_source_set(dw->listview, static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
			    dupe_drag_types, n_dupe_drag_types,
			    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	g_signal_connect(G_OBJECT(dw->listview), "drag_data_get",
			 G_CALLBACK(dupe_dnd_data_set), dw);
	g_signal_connect(G_OBJECT(dw->listview), "drag_begin",
			 G_CALLBACK(dupe_dnd_begin), dw);
	g_signal_connect(G_OBJECT(dw->listview), "drag_end",
			 G_CALLBACK(dupe_dnd_end), dw);

	dupe_dest_set(dw->listview, TRUE);
	g_signal_connect(G_OBJECT(dw->listview), "drag_data_received",
			 G_CALLBACK(dupe_dnd_data_get), dw);

	gtk_drag_source_set(dw->second_listview, static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
			    dupe_drag_types, n_dupe_drag_types,
			    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	g_signal_connect(G_OBJECT(dw->second_listview), "drag_data_get",
			 G_CALLBACK(dupe_dnd_data_set), dw);
	g_signal_connect(G_OBJECT(dw->second_listview), "drag_begin",
			 G_CALLBACK(dupe_dnd_begin), dw);
	g_signal_connect(G_OBJECT(dw->second_listview), "drag_end",
			 G_CALLBACK(dupe_dnd_end), dw);

	dupe_dest_set(dw->second_listview, TRUE);
	g_signal_connect(G_OBJECT(dw->second_listview), "drag_data_received",
			 G_CALLBACK(dupe_dnd_data_get), dw);
}

/*
 *-------------------------------------------------------------------
 * maintenance (move, delete, etc.)
 *-------------------------------------------------------------------
 */

static void dupe_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	if (!(type & NOTIFY_CHANGE) || !fd->change) return;

	DEBUG_1("Notify dupe: %s %04x", fd->path, type);

	switch (fd->change->type)
		{
		case FILEDATA_CHANGE_MOVE:
		case FILEDATA_CHANGE_RENAME:
			dupe_item_update_fd(dw, fd);
			break;
		case FILEDATA_CHANGE_COPY:
			break;
		case FILEDATA_CHANGE_DELETE:
			/* Update the UI only once, after the operation finishes */
			break;
		case FILEDATA_CHANGE_UNSPECIFIED:
		case FILEDATA_CHANGE_WRITE_METADATA:
			break;
		}

}

/**
 * @brief Refresh window after a file delete operation
 * @param success (ud->phase != UTILITY_PHASE_CANCEL) #file_util_dialog_run
 * @param dest_path Not used
 * @param data #DupeWindow
 *
 * If the window is refreshed after each file of a large set is deleted,
 * the UI slows to an unacceptable level. The #FileUtilDoneFunc is used
 * to call this function once, when the entire delete operation is completed.
 */
static void delete_finished_cb(gboolean success, const gchar *, gpointer data)
{
	auto dw = static_cast<DupeWindow *>(data);

	if (!success)
		{
		return;
		}

	dupe_window_remove_selection(dw, dw->listview);
}

/*
 *-------------------------------------------------------------------
 * Export duplicates data
 *-------------------------------------------------------------------
 */

enum SeparatorType {
	EXPORT_CSV = 0,
	EXPORT_TSV
};

struct ExportDupesData
{
	FileDialog *dialog;
	SeparatorType separator;
	DupeWindow *dupewindow;
};

static void export_duplicates_close(ExportDupesData *edd)
{
	if (edd->dialog) file_dialog_close(edd->dialog);
	edd->dialog = nullptr;
}

static void export_duplicates_data_cancel_cb(FileDialog *, gpointer data)
{
	auto edd = static_cast<ExportDupesData *>(data);

	export_duplicates_close(edd);
}

static void export_duplicates_data_save_cb(FileDialog *fdlg, gpointer data)
{
	auto edd = static_cast<ExportDupesData *>(data);
	GError *error = nullptr;
	GtkTreeModel *store;
	GtkTreeIter iter;
	DupeItem *di;
	GFileOutputStream *gfstream;
	GFile *out_file;
	GString *output_string;
	gchar *sep;
	gchar* rank;
	GList *work;
	GtkTreeSelection *selection;
	GList *slist;
	gchar *thumb_cache;
	gchar **rank_split;
	GtkTreePath *tpath;
	gboolean color_old = FALSE;
	gboolean color_new = FALSE;
	gint match_count;
	gchar *name;

	history_list_add_to_key("export_duplicates", fdlg->dest_path, -1);

	out_file = g_file_new_for_path(fdlg->dest_path);

	gfstream = g_file_replace(out_file, nullptr, TRUE, G_FILE_CREATE_NONE, nullptr, &error);
	if (error)
		{
		log_printf(_("Error creating Export duplicates data file: Error: %s\n"), error->message);
		g_error_free(error);
		return;
		}

	sep = g_strdup((edd->separator == EXPORT_CSV) ?  "," : "\t");
	output_string = g_string_new(g_strjoin(sep, _("Match"), _("Group"), _("Similarity"), _("Set"), _("Thumbnail"), _("Name"), _("Size"), _("Date"), _("Width"), _("Height"), _("Path\n"), NULL));

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(edd->dupewindow->listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;

	tpath = static_cast<GtkTreePath *>(work->data);
	gtk_tree_model_get_iter(store, &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, DUPE_COLUMN_COLOR, &color_new, -1);
	color_old = !color_new;
	match_count = 0;

	while (work)
		{
		tpath = static_cast<GtkTreePath *>(work->data);
		gtk_tree_model_get_iter(store, &iter, tpath);

		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, DUPE_COLUMN_POINTER, &di, -1);

		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, DUPE_COLUMN_COLOR, &color_new, -1);
		if (color_new != color_old)
			{
			match_count++;
			}
		color_old = color_new;
		output_string = g_string_append(output_string, g_strdup_printf("%d", match_count));
		output_string = g_string_append(output_string, sep);

		if ((dupe_match_find_parent(edd->dupewindow, di) == di))
			{
			output_string = g_string_append(output_string, "1");
			}
		else
			{
			output_string = g_string_append(output_string, "2");
			}
		output_string = g_string_append(output_string, sep);

		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, DUPE_COLUMN_RANK, &rank, -1);
		rank_split = g_strsplit_set(rank, " [(", -1);
		if (rank_split[0] == nullptr)
			{
			output_string = g_string_append(output_string, "");
			}
		else
			{
			output_string = g_string_append(output_string, g_strdup_printf("%s", rank_split[0]));
			}
		output_string = g_string_append(output_string, sep);
		g_free(rank);
		g_strfreev(rank_split);

		output_string = g_string_append(output_string, g_strdup_printf("%d", (di->second + 1)));
		output_string = g_string_append(output_string, sep);

		thumb_cache = cache_find_location(CACHE_TYPE_THUMB, di->fd->path);
		if (thumb_cache)
			{
			output_string = g_string_append(output_string, thumb_cache);
			g_free(thumb_cache);
			}
		else
			{
			output_string = g_string_append(output_string, "");
			}
		output_string = g_string_append(output_string, sep);

		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, DUPE_COLUMN_NAME, &name, -1);
		output_string = g_string_append(output_string, name);
		output_string = g_string_append(output_string, sep);
		g_free(name);

		output_string = g_string_append(output_string, g_strdup_printf("%" PRIu64, di->fd->size));
		output_string = g_string_append(output_string, sep);
		output_string = g_string_append(output_string, text_from_time(di->fd->date));
		output_string = g_string_append(output_string, sep);
		output_string = g_string_append(output_string, g_strdup_printf("%d", (di->width ? di->width : 0)));
		output_string = g_string_append(output_string, sep);
		output_string = g_string_append(output_string, g_strdup_printf("%d", (di->height ? di->height : 0)));
		output_string = g_string_append(output_string, sep);
		output_string = g_string_append(output_string, di->fd->path);
		output_string = g_string_append_c(output_string, '\n');

		work = work->next;
		}

	g_output_stream_write(G_OUTPUT_STREAM(gfstream), output_string->str, strlen(output_string->str), nullptr, &error);

	g_free(sep);
	g_string_free(output_string, TRUE);
	g_object_unref(gfstream);
	g_object_unref(out_file);

	export_duplicates_close(edd);
}

static void pop_menu_export(GList *, gpointer dupe_window, gpointer data)
{
	const gint index = GPOINTER_TO_INT(data);
	auto dw = static_cast<DupeWindow *>(dupe_window);
	const gchar *title = "Export duplicates data";
	const gchar *default_path = "/tmp/";
	gchar *file_extension;
	ExportDupesData *edd;
	const gchar *previous_path;

	edd = g_new0(ExportDupesData, 1);
	edd->dialog = file_util_file_dlg(title, "export_duplicates", nullptr, export_duplicates_data_cancel_cb, edd);

	switch (index)
		{
		case EXPORT_CSV:
			edd->separator = EXPORT_CSV;
			file_extension = g_strdup(".csv");
			break;
		case EXPORT_TSV:
			edd->separator = EXPORT_TSV;
			file_extension = g_strdup(".tsv");
			break;
		default:
			return;
		}

	generic_dialog_add_message(GENERIC_DIALOG(edd->dialog), nullptr, title, nullptr, FALSE);
	file_dialog_add_button(edd->dialog, GQ_ICON_SAVE, _("Save"), export_duplicates_data_save_cb, TRUE);

	previous_path = history_list_find_last_path_by_key("export_duplicates");

	file_dialog_add_path_widgets(edd->dialog, default_path, previous_path, "export_duplicates", file_extension, _("Export Files"));

	edd->dupewindow = dw;

	gtk_widget_show(GENERIC_DIALOG(edd->dialog)->dialog);

	g_free(file_extension);
}

static void dupe_pop_menu_export_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw;
	GList *selection_list;

	dw = static_cast<DupeWindow *>(submenu_item_get_data(widget));
	selection_list = dupe_listview_get_selection(dw, dw->listview);
	pop_menu_export(selection_list, dw, data);

	filelist_free(selection_list);
}

static GtkWidget *submenu_add_export(GtkWidget *menu, GtkWidget **menu_item, GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkWidget *submenu;

	item = menu_item_add(menu, _("_Export"), nullptr, nullptr);

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);

	menu_item_add_icon_sensitive(submenu, _("Export to csv"),
					GQ_ICON_EXPORT, TRUE, G_CALLBACK(func), GINT_TO_POINTER(0));
	menu_item_add_icon_sensitive(submenu, _("Export to tab-delimited"),
					GQ_ICON_EXPORT, TRUE, G_CALLBACK(func), GINT_TO_POINTER(1));

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
	if (menu_item) *menu_item = item;

	return submenu;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
