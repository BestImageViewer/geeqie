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

#include <inttypes.h>

#include "main.h"
#include "dupe.h"

#include "cache.h"
#include "collect.h"
#include "collect-table.h"
#include "dnd.h"
#include "editors.h"
#include "filedata.h"
#include "history_list.h"
#include "image-load.h"
#include "img-view.h"
#include "layout.h"
#include "layout_image.h"
#include "layout_util.h"
#include "md5-util.h"
#include "menu.h"
#include "misc.h"
#include "pixbuf_util.h"
#include "print.h"
#include "thumb.h"
#include "ui_fileops.h"
#include "ui_menu.h"
#include "ui_misc.h"
#include "ui_tree_edit.h"
#include "uri_utils.h"
#include "utilops.h"
#include "window.h"

#include <gdk/gdkkeysyms.h> /* for keyboard values */


#include <math.h>


#define DUPE_DEF_WIDTH 800
#define DUPE_DEF_HEIGHT 400

/* column assignment order (simply change them here) */
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
	DUPE_COLUMN_COUNT	/* total columns */
};


static GList *dupe_window_list = NULL;	/* list of open DupeWindow *s */

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

static GtkWidget *submenu_add_export(GtkWidget *menu, GtkWidget **menu_item, GCallback func, gpointer data);
static void dupe_pop_menu_export_cb(GtkWidget *widget, gpointer data);
/*
 * ------------------------------------------------------------------
 * Window updates
 * ------------------------------------------------------------------
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

static guint64 msec_time(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) == -1) return 0;

	return (guint64)tv.tv_sec * 1000000 + (guint64)tv.tv_usec;
}

static gint dupe_iterations(gint n)
{
	return (n * ((n + 1) / 2));
}

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

			rem = (t - d) ? ((gdouble)(dw->setup_time_count / 1000000) / (t - d)) * d : 0;

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
			status_text = NULL;
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

	if (!gtk_widget_get_window(widget)) return;

	if (icon == -1)
		{
		cursor = NULL;
		}
	else
		{
		cursor = gdk_cursor_new(icon);
		}

	gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);

	if (cursor) gdk_cursor_unref(cursor);
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
	DupeItem *parent = NULL;
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

static void dupe_list_free(GList *list)
{
	GList *work = list;
	while (work)
		{
		DupeItem *di = work->data;
		work = work->next;
		dupe_item_free(di);
		}
	g_list_free(list);
}

/*
static DupeItem *dupe_item_find_fd_by_list(FileData *fd, GList *work)
{
	while (work)
		{
		DupeItem *di = work->data;

		if (di->fd == fd) return di;

		work = work->next;
		}

	return NULL;
}
*/

/*
static DupeItem *dupe_item_find_fd(DupeWindow *dw, FileData *fd)
{
	DupeItem *di;

	di = dupe_item_find_fd_by_list(fd, dw->list);
	if (!di && dw->second_set) di = dupe_item_find_fd_by_list(fd, dw->second_list);

	return di;
}
*/

static DupeItem *dupe_item_find_path_by_list(const gchar *path, GList *work)
{
	while (work)
		{
		DupeItem *di = work->data;

		if (strcmp(di->fd->path, path) == 0) return di;

		work = work->next;
		}

	return NULL;
}

static DupeItem *dupe_item_find_path(DupeWindow *dw, const gchar *path)
{
	DupeItem *di;

	di = dupe_item_find_path_by_list(path, dw->list);
	if (!di && dw->second_set) di = dupe_item_find_path_by_list(path, dw->second_list);

	return di;
}

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
			cd->sim = NULL;
			}
		if (di->width == 0 && di->height == 0 && cd->dimensions)
			{
			di->width = cd->width;
			di->height = cd->height;
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
		cd->path = cache_get_location(CACHE_TYPE_SIM, di->fd->path, TRUE, NULL);

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
			dm = child->group->data;
			rank = (gint)floor(dm->rank);
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

	text[DUPE_COLUMN_THUMB] = "";
	text[DUPE_COLUMN_NAME] = (gchar *)di->fd->name;
	text[DUPE_COLUMN_SIZE] = text_from_size(di->fd->size);
	text[DUPE_COLUMN_DATE] = (gchar *)text_from_time(di->fd->date);
	if (di->width > 0 && di->height > 0)
		{
		text[DUPE_COLUMN_DIMENSIONS] = g_strdup_printf("%d x %d", di->width, di->height);
		}
	else
		{
		text[DUPE_COLUMN_DIMENSIONS] = g_strdup("");
		}
	text[DUPE_COLUMN_PATH] = di->fd->path;
	text[DUPE_COLUMN_COLOR] = NULL;

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
		DupeItem *parent = work->data;
		GList *temp;

		dupe_listview_add(dw, parent, NULL);

		temp = g_list_last(parent->group);
		while (temp)
			{
			DupeMatch *dm = temp->data;
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

	if (g_list_find(dw->dupes, di) != NULL)
		{
		if (!dw->color_frozen) dupe_listview_realign_colors(dw);
		}
}


static GList *dupe_listview_get_filelist(DupeWindow *dw, GtkWidget *listview)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	gboolean valid;
	GList *list = NULL;

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


static GList *dupe_listview_get_selection(DupeWindow *dw, GtkWidget *listview)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	GList *list = NULL;
	GList *work;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (work)
		{
		GtkTreePath *tpath = work->data;
		DupeItem *di = NULL;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		if (di)
			{
			list = g_list_prepend(list, file_data_ref(di->fd));
			}
		work = work->next;
		}
	g_list_foreach(slist, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(slist);

	return g_list_reverse(list);
}

static gboolean dupe_listview_item_is_selected(DupeWindow *dw, DupeItem *di, GtkWidget *listview)
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
		GtkTreePath *tpath = work->data;
		DupeItem *di_n;
		GtkTreeIter iter;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di_n, -1);
		if (di_n == di) found = TRUE;
		work = work->next;
		}
	g_list_foreach(slist, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(slist);

	return found;
}

static void dupe_listview_select_dupes(DupeWindow *dw, DupeSelectType parents)
{
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	gboolean valid;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	gtk_tree_selection_unselect_all(selection);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview));
	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		DupeItem *di;

		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		if ((dupe_match_find_parent(dw, di) == di) == (parents == DUPE_SELECT_GROUP1))
			{
			gtk_tree_selection_select_iter(selection, &iter);
			}
		valid = gtk_tree_model_iter_next(store, &iter);
		}
}

/*
 * ------------------------------------------------------------------
 * Match group manipulation
 * ------------------------------------------------------------------
 */

static DupeMatch *dupe_match_find_match(DupeItem *child, DupeItem *parent)
{
	GList *work;

	work = parent->group;
	while (work)
		{
		DupeMatch *dm = work->data;
		if (dm->di == child) return dm;
		work = work->next;
		}
	return NULL;
}

static void dupe_match_link_child(DupeItem *child, DupeItem *parent, gdouble rank)
{
	DupeMatch *dm;

	dm = g_new0(DupeMatch, 1);
	dm->di = child;
	dm->rank = rank;
	parent->group = g_list_append(parent->group, dm);
}

static void dupe_match_link(DupeItem *a, DupeItem *b, gdouble rank)
{
	dupe_match_link_child(a, b, rank);
	dupe_match_link_child(b, a, rank);
}

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

static void dupe_match_unlink(DupeItem *a, DupeItem *b)
{
	dupe_match_unlink_child(a, b);
	dupe_match_unlink_child(b, a);
}

static void dupe_match_link_clear(DupeItem *parent, gboolean unlink_children)
{
	GList *work;

	work = parent->group;
	while (work)
		{
		DupeMatch *dm = work->data;
		work = work->next;

		if (unlink_children) dupe_match_unlink_child(parent, dm->di);

		g_free(dm);
		}

	g_list_free(parent->group);
	parent->group = NULL;
	parent->group_rank = 0.0;
}

static gint dupe_match_link_exists(DupeItem *child, DupeItem *parent)
{
	return (dupe_match_find_match(child, parent) != NULL);
}

static gdouble dupe_match_link_rank(DupeItem *child, DupeItem *parent)
{
	DupeMatch *dm;

	dm = dupe_match_find_match(child, parent);
	if (dm) return dm->rank;

	return 0.0;
}

static DupeItem *dupe_match_highest_rank(DupeItem *child)
{
	DupeMatch *dr;
	GList *work;

	dr = NULL;
	work = child->group;
	while (work)
		{
		DupeMatch *dm = work->data;
		if (!dr || dm->rank > dr->rank) dr = dm;
		work = work->next;
		}

	return (dr) ? dr->di : NULL;
}

static void dupe_match_rank_update(DupeItem *parent)
{
	GList *work;
	gdouble rank = 0.0;
	gint c = 0;

	work = parent->group;
	while (work)
		{
		DupeMatch *dm = work->data;
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
		DupeMatch *dm = work->data;
		if (g_list_find(dw->dupes, dm->di)) return dm->di;
		work = work->next;
		}

	return NULL;
}

static void dupe_match_reset_list(GList *work)
{
	while (work)
		{
		DupeItem *di = work->data;
		work = work->next;

		dupe_match_link_clear(di, FALSE);
		}
}

static void dupe_match_reparent(DupeWindow *dw, DupeItem *old, DupeItem *new)
{
	GList *work;

	if (!old || !new || !dupe_match_link_exists(old, new)) return;

	dupe_match_link_clear(new, TRUE);
	work = old->group;
	while (work)
		{
		DupeMatch *dm = work->data;
		dupe_match_unlink_child(old, dm->di);
		dupe_match_link_child(new, dm->di, dm->rank);
		work = work->next;
		}

	new->group = old->group;
	old->group = NULL;

	work = g_list_find(dw->dupes, old);
	if (work) work->data = new;
}

static void dupe_match_print_group(DupeItem *di)
{
	GList *work;

	log_printf("+ %f %s\n", di->group_rank, di->fd->name);

	work = di->group;
	while (work)
		{
		DupeMatch *dm = work->data;
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
		DupeItem *di = work->data;
		dupe_match_print_group(di);
		work = work->next;
		}
}

/* level 3, unlinking and orphan handling */
static GList *dupe_match_unlink_by_rank(DupeItem *child, DupeItem *parent, GList *list, DupeWindow *dw)
{
	DupeItem *best;

	best = dupe_match_highest_rank(parent);
	if (best == child || dupe_match_highest_rank(child) == parent)
		{
		GList *work;
		gdouble rank;

		DEBUG_2("link found %s to %s [%d]", child->fd->name, parent->fd->name, g_list_length(parent->group));

		work = parent->group;
		while (work)
			{
			DupeMatch *dm = work->data;
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

		rank = dupe_match_link_rank(child, parent);
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
static GList *dupe_match_group_filter(GList *list, DupeItem *di, DupeWindow *dw)
{
	GList *work;

	work = g_list_last(di->group);
	while (work)
		{
		DupeMatch *dm = work->data;
		work = work->prev;
		list = dupe_match_unlink_by_rank(di, dm->di, list, dw);
		}

	return list;
}

/* level 1 (top) */
static GList *dupe_match_group_trim(GList *list, DupeWindow *dw)
{
	GList *work;

	work = list;
	while (work)
		{
		DupeItem *di = work->data;
		if (!di->second) list = dupe_match_group_filter(list, di, dw);
		work = work->next;
		if (di->second) list = g_list_remove(list, di);
		}

	return list;
}

static gint dupe_match_sort_groups_cb(gconstpointer a, gconstpointer b)
{
	DupeMatch *da = (DupeMatch *)a;
	DupeMatch *db = (DupeMatch *)b;

	if (da->rank > db->rank) return -1;
	if (da->rank < db->rank) return 1;
	return 0;
}

static void dupe_match_sort_groups(GList *list)
{
	GList *work;

	work = list;
	while (work)
		{
		DupeItem *di = work->data;
		di->group = g_list_sort(di->group, dupe_match_sort_groups_cb);
		work = work->next;
		}
}

static gint dupe_match_totals_sort_cb(gconstpointer a, gconstpointer b)
{
	DupeItem *da = (DupeItem *)a;
	DupeItem *db = (DupeItem *)b;

	if (g_list_length(da->group) > g_list_length(db->group)) return -1;
	if (g_list_length(da->group) < g_list_length(db->group)) return 1;

	if (da->group_rank < db->group_rank) return -1;
	if (da->group_rank > db->group_rank) return 1;

	return 0;
}

static gint dupe_match_rank_sort_cb(gconstpointer a, gconstpointer b)
{
	DupeItem *da = (DupeItem *)a;
	DupeItem *db = (DupeItem *)b;

	if (da->group_rank > db->group_rank) return -1;
	if (da->group_rank < db->group_rank) return 1;
	return 0;
}

/* returns allocated GList of dupes sorted by rank */
static GList *dupe_match_rank_sort(GList *source_list)
{
	GList *list = NULL;
	GList *work;

	work = source_list;
	while (work)
		{
		DupeItem *di = work->data;

		if (di->group)
			{
			dupe_match_rank_update(di);
			list = g_list_prepend(list, di);
			}

		work = work->next;
		}

	return g_list_sort(list, dupe_match_rank_sort_cb);
}

/* returns allocated GList of dupes sorted by totals */
static GList *dupe_match_totals_sort(GList *source_list)
{
	source_list = g_list_sort(source_list, dupe_match_totals_sort_cb);

	source_list = g_list_first(source_list);
	return g_list_reverse(source_list);
}

static void dupe_match_rank(DupeWindow *dw)
{
	GList *list;

	list = dupe_match_rank_sort(dw->list);

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

static gboolean dupe_match(DupeItem *a, DupeItem *b, DupeMatchType mask, gdouble *rank, gint fast)
{
	*rank = 0.0;

	if (a->fd->path == b->fd->path) return FALSE;

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
		else if (mask & DUPE_MATCH_SIM_CUSTOM) m = (gdouble)options->duplicates_similarity_threshold / 100.0;
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

static void dupe_list_check_match(DupeWindow *dw, DupeItem *needle, GList *start)
{
	GList *work;

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

	while (work)
		{
		DupeItem *di = work->data;

		/* speed opt: forward for second set, back for simple compare */
		if (dw->second_set)
			work = work->next;
		else
			work = work->prev;

		if (!dupe_match_link_exists(needle, di))
			{
			gdouble rank;

			if (dupe_match(di, needle, dw->match_mask, &rank, TRUE))
				{
				dupe_match_link(di, needle, rank);
				}
			}
		}
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

	dupe_listview_set_thumb(dw, di, NULL);
}

static void dupe_thumb_error_cb(ThumbLoader *tl, gpointer data)
{
	DupeWindow *dw = data;

	dupe_thumb_do(dw);
	dupe_thumb_step(dw);
}

static void dupe_thumb_done_cb(ThumbLoader *tl, gpointer data)
{
	DupeWindow *dw = data;

	dupe_thumb_do(dw);
	dupe_thumb_step(dw);
}

static void dupe_thumb_step(DupeWindow *dw)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	DupeItem *di = NULL;
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
			di = NULL;
			}
		valid = gtk_tree_model_iter_next(store, &iter);
		}
	if (valid)
		{
		while (gtk_tree_model_iter_next(store, &iter)) length++;
		}

	if (!di)
		{
		dw->thumb_item = NULL;
		thumb_loader_free(dw->thumb_loader);
		dw->thumb_loader = NULL;

		dupe_window_update_progress(dw, NULL, 0.0, FALSE);
		return;
		}

	dupe_window_update_progress(dw, _("Loading thumbs..."),
				    length == 0 ? 0.0 : (gdouble)(row) / length, FALSE);

	dw->thumb_item = di;
	thumb_loader_free(dw->thumb_loader);
	dw->thumb_loader = thumb_loader_new(options->thumbnails.max_width, options->thumbnails.max_height);

	thumb_loader_set_callbacks(dw->thumb_loader,
				   dupe_thumb_done_cb,
				   dupe_thumb_error_cb,
				   NULL,
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
	if (dw->idle_id || dw->img_loader || dw->thumb_loader)
		{
		g_source_remove(dw->idle_id);
		dw->idle_id = 0;
		dupe_window_update_progress(dw, NULL, 0.0, FALSE);
		widget_set_cursor(dw->listview, -1);
		}

	thumb_loader_free(dw->thumb_loader);
	dw->thumb_loader = NULL;

	image_loader_free(dw->img_loader);
	dw->img_loader = NULL;
}

static void dupe_loader_done_cb(ImageLoader *il, gpointer data)
{
	DupeWindow *dw = data;
	GdkPixbuf *pixbuf;

	pixbuf = image_loader_get_pixbuf(il);

	if (dw->setup_point)
		{
		DupeItem *di = dw->setup_point->data;

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
	dw->img_loader = NULL;

	dw->idle_id = g_idle_add(dupe_check_cb, dw);
}

static void dupe_setup_reset(DupeWindow *dw)
{
	dw->setup_point = NULL;
	dw->setup_n = 0;
	dw->setup_time = msec_time();
	dw->setup_time_count = 0;
}

static GList *dupe_setup_point_step(DupeWindow *dw, GList *p)
{
	if (!p) return NULL;

	if (p->next) return p->next;

	if (dw->second_set && g_list_first(p) == dw->list) return dw->second_list;

	return NULL;
}

static gboolean dupe_check_cb(gpointer data)
{
	DupeWindow *dw = data;

	if (!dw->idle_id) return FALSE;

	if (!dw->setup_done)
		{
		if ((dw->match_mask & DUPE_MATCH_SUM) &&
		    !(dw->setup_mask & DUPE_MATCH_SUM) )
			{
			if (!dw->setup_point) dw->setup_point = dw->list;

			while (dw->setup_point)
				{
				DupeItem *di = dw->setup_point->data;

				dw->setup_point = dupe_setup_point_step(dw, dw->setup_point);
				dw->setup_n++;

				if (!di->md5sum)
					{
					dupe_window_update_progress(dw, _("Reading checksums..."),
						dw->setup_count == 0 ? 0.0 : (gdouble)(dw->setup_n - 1) / dw->setup_count, FALSE);

					if (options->thumbnails.enable_caching)
						{
						dupe_item_read_cache(di);
						if (di->md5sum) return TRUE;
						}

					di->md5sum = md5_text_from_file_utf8(di->fd->path, "");
					if (options->thumbnails.enable_caching)
						{
						dupe_item_write_cache(di);
						}
					return TRUE;
					}
				}
			dw->setup_mask |= DUPE_MATCH_SUM;
			dupe_setup_reset(dw);
			}
		if ((dw->match_mask & DUPE_MATCH_DIM) &&
		    !(dw->setup_mask & DUPE_MATCH_DIM) )
			{
			if (!dw->setup_point) dw->setup_point = dw->list;

			while (dw->setup_point)
				{
				DupeItem *di = dw->setup_point->data;

				dw->setup_point = dupe_setup_point_step(dw, dw->setup_point);
				dw->setup_n++;
				if (di->width == 0 && di->height == 0)
					{
					dupe_window_update_progress(dw, _("Reading dimensions..."),
						dw->setup_count == 0 ? 0.0 : (gdouble)(dw->setup_n - 1) / dw->setup_count, FALSE);

					if (options->thumbnails.enable_caching)
						{
						dupe_item_read_cache(di);
						if (di->width != 0 || di->height != 0) return TRUE;
						}

					image_load_dimensions(di->fd, &di->width, &di->height);
					if (options->thumbnails.enable_caching)
						{
						dupe_item_write_cache(di);
						}
					return TRUE;
					}
				}
			dw->setup_mask |= DUPE_MATCH_DIM;
			dupe_setup_reset(dw);
			}
		if ((dw->match_mask & DUPE_MATCH_SIM_HIGH ||
		     dw->match_mask & DUPE_MATCH_SIM_MED ||
		     dw->match_mask & DUPE_MATCH_SIM_LOW ||
		     dw->match_mask & DUPE_MATCH_SIM_CUSTOM) &&
		    !(dw->setup_mask & DUPE_MATCH_SIM_MED) )
			{
			if (!dw->setup_point) dw->setup_point = dw->list;

			while (dw->setup_point)
				{
				DupeItem *di = dw->setup_point->data;

				if (!di->simd)
					{
					dupe_window_update_progress(dw, _("Reading similarity data..."),
						dw->setup_count == 0 ? 0.0 : (gdouble)dw->setup_n / dw->setup_count, FALSE);

					if (options->thumbnails.enable_caching)
						{
						dupe_item_read_cache(di);
						if (cache_sim_data_filled(di->simd))
							{
							image_sim_alternate_processing(di->simd);
							return TRUE;
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
						dw->img_loader = NULL;
						return TRUE;
						}
					dw->idle_id = 0;
					return FALSE;
					}

				dw->setup_point = dupe_setup_point_step(dw, dw->setup_point);
				dw->setup_n++;
				}
			dw->setup_mask |= DUPE_MATCH_SIM_MED;
			dupe_setup_reset(dw);
			}
		dupe_window_update_progress(dw, _("Comparing..."), 0.0, FALSE);
		dw->setup_done = TRUE;
		dupe_setup_reset(dw);
		dw->setup_count = g_list_length(dw->list);
		}

	if (!dw->working)
		{
		if (dw->setup_count > 0)
			{
			dw->setup_count = 0;
			dupe_window_update_progress(dw, _("Sorting..."), 1.0, TRUE);
			return TRUE;
			}
		dw->idle_id = 0;
		dupe_window_update_progress(dw, NULL, 0.0, FALSE);

		dupe_match_rank(dw);
		dupe_window_update_count(dw, FALSE);

		dupe_listview_populate(dw);

		/* check thumbs */
		if (dw->show_thumbs) dupe_thumb_step(dw);

		widget_set_cursor(dw->listview, -1);

		return FALSE;
		}

	dupe_list_check_match(dw, (DupeItem *)dw->working->data, dw->working);
	dupe_window_update_progress(dw, _("Comparing..."), dw->setup_count == 0 ? 0.0 : (gdouble) dw->setup_n / dw->setup_count, FALSE);
	dw->setup_n++;

	dw->working = dw->working->prev;

	return TRUE;
}

static void dupe_check_start(DupeWindow *dw)
{
	dw->setup_done = FALSE;

	dw->setup_count = g_list_length(dw->list);
	if (dw->second_set) dw->setup_count += g_list_length(dw->second_list);

	dw->setup_mask = 0;
	dupe_setup_reset(dw);

	dw->working = g_list_last(dw->list);

	dupe_window_update_count(dw, TRUE);
	widget_set_cursor(dw->listview, GDK_WATCH);

	if (dw->idle_id) return;

	dw->idle_id = g_idle_add(dupe_check_cb, dw);
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
			dw->img_loader = NULL;
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

				dm = parent->group->data;
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

static gboolean dupe_item_remove_by_path(DupeWindow *dw, const gchar *path)
{
	DupeItem *di;

	di = dupe_item_find_path(dw, path);
	if (!di) return FALSE;

	dupe_item_remove(dw, di);

	return TRUE;
}

static void dupe_files_add(DupeWindow *dw, CollectionData *collection, CollectInfo *info,
			   FileData *fd, gboolean recurse)
{
	DupeItem *di = NULL;

	if (info)
		{
		di = dupe_item_new(info->fd);
		}
	else if (fd)
		{
		if (isfile(fd->path))
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
					dupe_files_add(dw, NULL, NULL, (FileData *)work->data, TRUE);
					work = work->next;
					}
				filelist_free(f);
				work = d;
				while (work)
					{
					dupe_files_add(dw, NULL, NULL, (FileData *)work->data, TRUE);
					work = work->next;
					}
				filelist_free(d);
				}
			}
		}

	if (!di) return;

	/* Ensure images in the lists have unique FileDatas */
	GList *work;
	DupeItem *di_list;
	work = g_list_first(dw->list);
	while (work)
		{
		di_list = work->data;
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
			di_list = work->data;
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

void dupe_window_add_collection(DupeWindow *dw, CollectionData *collection)
{
	CollectInfo *info;

	info = collection_get_first(collection);
	while (info)
		{
		dupe_files_add(dw, collection, info, NULL, FALSE);
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
		FileData *fd = work->data;
		work = work->next;

		dupe_files_add(dw, NULL, NULL, fd, recurse);
		}

	dupe_check_start(dw);
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
		DupeItem *di = work->data;

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

	hbox = gtk_hbox_new(FALSE, 10);

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
			       NULL, NULL);
	generic_dialog_add_button(gd, GTK_STOCK_CLOSE, NULL, NULL, TRUE);

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
	dw->dupes = NULL;

	dupe_match_reset_list(dw->list);
	dupe_match_reset_list(dw->second_list);

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
			layout_image_set_collection(NULL, di->collection, di->info);
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
			layout_set_fd(NULL, di->fd);
			}
		}
}

static void dupe_window_remove_selection(DupeWindow *dw, GtkWidget *listview)
{
	GtkTreeSelection *selection;
	GtkTreeModel *store;
	GtkTreeIter iter;
	GList *slist;
	GList *list = NULL;
	GList *work;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(listview));
	slist = gtk_tree_selection_get_selected_rows(selection, &store);
	work = slist;
	while (work)
		{
		GtkTreePath *tpath = work->data;
		DupeItem *di = NULL;

		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		if (di) list = g_list_prepend(list, di);
		work = work->next;
		}
	g_list_foreach(slist, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(slist);

	dw->color_frozen = TRUE;
	work = list;
	while (work)
		{
		DupeItem *di;

		di = work->data;
		work = work->next;
		dupe_item_remove(dw, di);
		}
	dw->color_frozen = FALSE;

	g_list_free(list);

	dupe_listview_realign_colors(dw);
}

static void dupe_window_edit_selected(DupeWindow *dw, const gchar *key)
{
	file_util_start_editor_from_filelist(key, dupe_listview_get_selection(dw, dw->listview), NULL, dw->window);
}

static void dupe_window_collection_from_selection(DupeWindow *dw)
{
	CollectWindow *w;
	GList *list;

	list = dupe_listview_get_selection(dw, dw->listview);
	w = collection_window_new(NULL);
	collection_table_add_filelist(w->table, list);
	filelist_free(list);
}

static void dupe_window_append_file_list(DupeWindow *dw, gint on_second)
{
	GList *list;

	dw->second_drop = (dw->second_set && on_second);

	list = layout_list(NULL);
	dupe_window_add_files(dw, list, FALSE);
	filelist_free(list);
}

/*
 *-------------------------------------------------------------------
 * main pop-up menu callbacks
 *-------------------------------------------------------------------
 */

static void dupe_menu_view_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	if (dw->click_item) dupe_menu_view(dw, dw->click_item, dw->listview, FALSE);
}

static void dupe_menu_viewnew_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	if (dw->click_item) dupe_menu_view(dw, dw->click_item, dw->listview, TRUE);
}

static void dupe_menu_select_all_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;
	GtkTreeSelection *selection;

	options->duplicates_select_type = DUPE_SELECT_NONE;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	gtk_tree_selection_select_all(selection);
}

static void dupe_menu_select_none_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;
	GtkTreeSelection *selection;

	options->duplicates_select_type = DUPE_SELECT_NONE;
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	gtk_tree_selection_unselect_all(selection);
}

static void dupe_menu_select_dupes_set1_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	options->duplicates_select_type = DUPE_SELECT_GROUP1;
	dupe_listview_select_dupes(dw, DUPE_SELECT_GROUP1);
}

static void dupe_menu_select_dupes_set2_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	options->duplicates_select_type = DUPE_SELECT_GROUP2;
	dupe_listview_select_dupes(dw, DUPE_SELECT_GROUP2);
}

static void dupe_menu_edit_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw;
	const gchar *key = data;

	dw = submenu_item_get_data(widget);
	if (!dw) return;

	dupe_window_edit_selected(dw, key);
}

static void dupe_menu_print_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;
	FileData *fd;

	fd = (dw->click_item) ? dw->click_item->fd : NULL;

	print_window_new(fd,
			 dupe_listview_get_selection(dw, dw->listview),
			 dupe_listview_get_filelist(dw, dw->listview), dw->window);
}

static void dupe_menu_copy_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	file_util_copy(NULL, dupe_listview_get_selection(dw, dw->listview), NULL, dw->window);
}

static void dupe_menu_move_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	file_util_move(NULL, dupe_listview_get_selection(dw, dw->listview), NULL, dw->window);
}

static void dupe_menu_rename_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	file_util_rename(NULL, dupe_listview_get_selection(dw, dw->listview), dw->window);
}

static void dupe_menu_delete_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	options->file_ops.safe_delete_enable = FALSE;
	file_util_delete(NULL, dupe_listview_get_selection(dw, dw->listview), dw->window);
}

static void dupe_menu_move_to_trash_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	options->file_ops.safe_delete_enable = TRUE;
	file_util_delete(NULL, dupe_listview_get_selection(dw, dw->listview), dw->window);
}

static void dupe_menu_copy_path_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	file_util_copy_path_list_to_clipboard(dupe_listview_get_selection(dw, dw->listview), TRUE);
}

static void dupe_menu_copy_path_unquoted_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	file_util_copy_path_list_to_clipboard(dupe_listview_get_selection(dw, dw->listview), FALSE);
}

static void dupe_menu_remove_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	dupe_window_remove_selection(dw, dw->listview);
}

static void dupe_menu_clear_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	dupe_window_clear(dw);
}

static void dupe_menu_close_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	dupe_window_close(dw);
}

static void dupe_menu_popup_destroy_cb(GtkWidget *widget, gpointer data)
{
	GList *editmenu_fd_list = data;

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

	dw = submenu_item_get_data(widget);
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

	on_row = (di != NULL);

	menu = popup_menu_short_lived();

	menu_item_add_sensitive(menu, _("_View"), on_row,
				G_CALLBACK(dupe_menu_view_cb), dw);
	menu_item_add_stock_sensitive(menu, _("View in _new window"), GTK_STOCK_NEW, on_row,
				G_CALLBACK(dupe_menu_viewnew_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_sensitive(menu, _("Select all"), (dw->dupes != NULL),
				G_CALLBACK(dupe_menu_select_all_cb), dw);
	menu_item_add_sensitive(menu, _("Select none"), (dw->dupes != NULL),
				G_CALLBACK(dupe_menu_select_none_cb), dw);
	menu_item_add_sensitive(menu, _("Select group _1 duplicates"), (dw->dupes != NULL),
				G_CALLBACK(dupe_menu_select_dupes_set1_cb), dw);
	menu_item_add_sensitive(menu, _("Select group _2 duplicates"), (dw->dupes != NULL),
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

	menu_item_add_stock_sensitive(menu, _("Print..."), GTK_STOCK_PRINT, on_row,
				G_CALLBACK(dupe_menu_print_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_stock_sensitive(menu, _("_Copy..."), GTK_STOCK_COPY, on_row,
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
	menu_item_add_stock_sensitive(menu,
				options->file_ops.confirm_move_to_trash ? _("Move to Trash...") :
					_("Move to Trash"), PIXBUF_INLINE_ICON_TRASH, on_row,
				G_CALLBACK(dupe_menu_move_to_trash_cb), dw);
	menu_item_add_stock_sensitive(menu,
				options->file_ops.confirm_delete ? _("_Delete...") :
					_("_Delete"), GTK_STOCK_DELETE, on_row,
				G_CALLBACK(dupe_menu_delete_cb), dw);

	menu_item_add_divider(menu);
	menu_item_add_stock_sensitive(menu, _("Rem_ove"), GTK_STOCK_REMOVE, on_row,
				G_CALLBACK(dupe_menu_remove_cb), dw);
	menu_item_add_stock_sensitive(menu, _("C_lear"), GTK_STOCK_CLEAR, (dw->list != NULL),
				G_CALLBACK(dupe_menu_clear_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_stock(menu, _("Close _window"), GTK_STOCK_CLOSE,
			    G_CALLBACK(dupe_menu_close_cb), dw);

	return menu;
}

static gboolean dupe_listview_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	DupeWindow *dw = data;
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	DupeItem *di = NULL;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, NULL, NULL, NULL))
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
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, bevent->button, bevent->time);
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
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, NULL, FALSE);
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
	DupeWindow *dw = data;
	GtkTreeModel *store;
	GtkTreePath *tpath;
	GtkTreeIter iter;
	DupeItem *di = NULL;

	if (bevent->button != MOUSE_BUTTON_LEFT && bevent->button != MOUSE_BUTTON_MIDDLE) return TRUE;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));

	if ((bevent->x != 0 || bevent->y != 0) &&
	    gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, NULL, NULL, NULL))
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
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, NULL, FALSE);
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
	dw->dupes = NULL;

	dupe_list_free(dw->second_list);
	dw->second_list = NULL;

	dupe_match_reset_list(dw->list);

	dupe_second_update_status(dw);
}

static void dupe_second_menu_view_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	if (dw->click_item) dupe_menu_view(dw, dw->click_item, dw->second_listview, FALSE);
}

static void dupe_second_menu_viewnew_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	if (dw->click_item) dupe_menu_view(dw, dw->click_item, dw->second_listview, TRUE);
}

static void dupe_second_menu_select_all_cb(GtkWidget *widget, gpointer data)
{
	GtkTreeSelection *selection;
	DupeWindow *dw = data;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->second_listview));
	gtk_tree_selection_select_all(selection);
}

static void dupe_second_menu_select_none_cb(GtkWidget *widget, gpointer data)
{
	GtkTreeSelection *selection;
	DupeWindow *dw = data;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->second_listview));
	gtk_tree_selection_unselect_all(selection);
}

static void dupe_second_menu_remove_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	dupe_window_remove_selection(dw, dw->second_listview);
}

static void dupe_second_menu_clear_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	dupe_second_clear(dw);
	dupe_window_recompare(dw);
}

static GtkWidget *dupe_menu_popup_second(DupeWindow *dw, DupeItem *di)
{
	GtkWidget *menu;
	gboolean notempty = (dw->second_list != NULL);
	gboolean on_row = (di != NULL);

	menu = popup_menu_short_lived();
	menu_item_add_sensitive(menu, _("_View"), on_row,
				G_CALLBACK(dupe_second_menu_view_cb), dw);
	menu_item_add_stock_sensitive(menu, _("View in _new window"), GTK_STOCK_NEW, on_row,
				G_CALLBACK(dupe_second_menu_viewnew_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_sensitive(menu, _("Select all"), notempty,
				G_CALLBACK(dupe_second_menu_select_all_cb), dw);
	menu_item_add_sensitive(menu, _("Select none"), notempty,
				G_CALLBACK(dupe_second_menu_select_none_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_stock_sensitive(menu, _("Rem_ove"), GTK_STOCK_REMOVE, on_row,
				      G_CALLBACK(dupe_second_menu_remove_cb), dw);
	menu_item_add_stock_sensitive(menu, _("C_lear"), GTK_STOCK_CLEAR, notempty,
				   G_CALLBACK(dupe_second_menu_clear_cb), dw);
	menu_item_add_divider(menu);
	menu_item_add_stock(menu, _("Close _window"), GTK_STOCK_CLOSE,
			    G_CALLBACK(dupe_menu_close_cb), dw);

	return menu;
}

static void dupe_second_set_toggle_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

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
	DupeWindow *dw = data;

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

static void dupe_menu_type_cb(GtkWidget *combo, gpointer data)
{
	DupeWindow *dw = data;
	GtkTreeModel *store;
	GtkTreeIter iter;

	store = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter)) return;
	gtk_tree_model_get(store, &iter, DUPE_MENU_COLUMN_MASK, &dw->match_mask, -1);

	options->duplicates_match = dw->match_mask;

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
	dupe_menu_add_item(store, _("Similarity (high)"), DUPE_MATCH_SIM_HIGH, dw);
	dupe_menu_add_item(store, _("Similarity"), DUPE_MATCH_SIM_MED, dw);
	dupe_menu_add_item(store, _("Similarity (low)"), DUPE_MATCH_SIM_LOW, dw);
	dupe_menu_add_item(store, _("Similarity (custom)"), DUPE_MATCH_SIM_CUSTOM, dw);

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
	static GtkWidget *done = NULL;

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

static void dupe_listview_color_cb(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
				   GtkTreeModel *tree_model, GtkTreeIter *iter, gpointer data)
{
	DupeWindow *dw = data;
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
		gtk_tree_view_column_set_cell_data_func(column, renderer, dupe_listview_color_cb, dw, NULL);
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

	list = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
	if (!list) return;
	cell = list->data;
	g_list_free(list);

	g_object_set(G_OBJECT(cell), "height", (thumb) ? options->thumbnails.max_height : -1, NULL);
	gtk_tree_view_columns_autosize(GTK_TREE_VIEW(listview));
}


/*
 *-------------------------------------------------------------------
 * misc cb
 *-------------------------------------------------------------------
 */

static void dupe_window_show_thumb_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

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
		dw->thumb_loader = NULL;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(dw->listview));
		valid = gtk_tree_model_get_iter_first(store, &iter);

		while (valid)
			{
			gtk_list_store_set(GTK_LIST_STORE(store), &iter, DUPE_COLUMN_THUMB, NULL, -1);
			valid = gtk_tree_model_iter_next(store, &iter);
			}
		dupe_window_update_progress(dw, NULL, 0.0, FALSE);
		}

	dupe_listview_set_height(dw->listview, dw->show_thumbs);
}

static void dupe_window_rotation_invariant_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;

	options->rot_invariant_sim = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	dupe_window_recompare(dw);
}

static void dupe_window_custom_threshold_cb(GtkWidget *widget, gpointer data)
{
	DupeWindow *dw = data;
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

static void dupe_popup_menu_pos_cb(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer data)
{
	GtkWidget *view = data;
	GtkTreePath *tpath;
	gint cx, cy, cw, ch;
	gint column;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(view), &tpath, NULL);
	if (!tpath) return;

	if (gtk_tree_view_get_column(GTK_TREE_VIEW(view), DUPE_COLUMN_NAME - 1) != NULL)
		{
		column = DUPE_COLUMN_NAME - 1;
		}
	else
		{
		/* dw->second_listview */
		column = 0;
		}
	tree_view_get_cell_clamped(GTK_TREE_VIEW(view), tpath, column, TRUE, &cx, &cy, &cw, &ch);
	gtk_tree_path_free(tpath);
	cy += ch;
	popup_menu_position_clamp(menu, &cx, &cy, 0);
	*x = cx;
	*y = cy;
}

static gboolean dupe_window_keypress_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	DupeWindow *dw = data;
	gboolean stop_signal = FALSE;
	gboolean on_second;
	GtkWidget *listview;
	GtkTreeModel *store;
	GtkTreeSelection *selection;
	GList *slist;
	DupeItem *di = NULL;

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
		tpath = last->data;

		/* last is newest selected file */
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DUPE_COLUMN_POINTER, &di, -1);
		}
	g_list_foreach(slist, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(slist);

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
					file_util_copy(NULL, dupe_listview_get_selection(dw, listview),
						       NULL, dw->window);
					break;
				case 'M': case 'm':
					file_util_move(NULL, dupe_listview_get_selection(dw, listview),
						       NULL, dw->window);
					break;
				case 'R': case 'r':
					file_util_rename(NULL, dupe_listview_get_selection(dw, listview), dw->window);
					break;
				case 'D': case 'd':
					options->file_ops.safe_delete_enable = TRUE;
					file_util_delete(NULL, dupe_listview_get_selection(dw, listview), dw->window);
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
					gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
						       dupe_popup_menu_pos_cb, listview, 0, GDK_CURRENT_TIME);
					}
				else
					{
					GtkWidget *menu;

					menu = dupe_menu_popup_second(dw, di);
					gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
						       dupe_popup_menu_pos_cb, listview, 0, GDK_CURRENT_TIME);
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
	dw->dupes = NULL;

	dupe_list_free(dw->list);
	dw->list = NULL;

	dupe_match_reset_list(dw->second_list);

	dupe_window_update_count(dw, FALSE);
	dupe_window_update_progress(dw, NULL, 0.0, FALSE);
}

void dupe_window_close(DupeWindow *dw)
{
	dupe_check_stop(dw);

	dupe_window_list = g_list_remove(dupe_window_list, dw);
	gtk_widget_destroy(dw->window);

	g_list_free(dw->dupes);
	dupe_list_free(dw->list);

	dupe_list_free(dw->second_list);

	file_data_unregister_notify_func(dupe_notify_cb, dw);

	g_free(dw);
}

static gint dupe_window_delete(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	DupeWindow *dw = data;
	dupe_window_close(dw);

	return TRUE;
}

/* collection and files can be NULL */
DupeWindow *dupe_window_new()
{
	DupeWindow *dw;
	GtkWidget *vbox;
	GtkWidget *scrolled;
	GtkWidget *frame;
	GtkWidget *status_box;
	GtkWidget *label;
	GtkWidget *button;
	GtkListStore *store;
	GtkTreeSelection *selection;
	GdkGeometry geometry;

	dw = g_new0(DupeWindow, 1);

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

	dw->window = window_new(GTK_WINDOW_TOPLEVEL, "dupe", NULL, NULL, _("Find duplicates"));
	DEBUG_NAME(dw->window);

	geometry.min_width = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.min_height = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.base_width = DUPE_DEF_WIDTH;
	geometry.base_height = DUPE_DEF_HEIGHT;
	gtk_window_set_geometry_hints(GTK_WINDOW(dw->window), NULL, &geometry,
				      GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE);

	gtk_window_set_default_size(GTK_WINDOW(dw->window), DUPE_DEF_WIDTH, DUPE_DEF_HEIGHT);

	gtk_window_set_resizable(GTK_WINDOW(dw->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(dw->window), 0);

	g_signal_connect(G_OBJECT(dw->window), "delete_event",
			 G_CALLBACK(dupe_window_delete), dw);
	g_signal_connect(G_OBJECT(dw->window), "key_press_event",
			 G_CALLBACK(dupe_window_keypress_cb), dw);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(dw->window), vbox);
	gtk_widget_show(vbox);

	dw->table = gtk_table_new(1, 3, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), dw->table, TRUE, TRUE, 0);
	gtk_widget_show(dw->table);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_table_attach_defaults(GTK_TABLE(dw->table), scrolled, 0, 2, 0, 1);
	gtk_widget_show(scrolled);

	store = gtk_list_store_new(9, G_TYPE_POINTER, G_TYPE_STRING, GDK_TYPE_PIXBUF,
				   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	dw->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dw->listview));
	gtk_tree_selection_set_mode(GTK_TREE_SELECTION(selection), GTK_SELECTION_MULTIPLE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(dw->listview), TRUE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(dw->listview), FALSE);

	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_RANK, "", FALSE, TRUE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_THUMB, "", TRUE, FALSE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_NAME, _("Name"), FALSE, FALSE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_SIZE, _("Size"), FALSE, TRUE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_DATE, _("Date"), FALSE, TRUE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_DIMENSIONS, _("Dimensions"), FALSE, FALSE);
	dupe_listview_add_column(dw, dw->listview, DUPE_COLUMN_PATH, _("Path"), FALSE, FALSE);

	gtk_container_add(GTK_CONTAINER(scrolled), dw->listview);
	gtk_widget_show(dw->listview);

	dw->second_vbox = gtk_vbox_new(FALSE, 0);
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

	scrolled = gtk_scrolled_window_new(NULL, NULL);
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

	status_box = pref_box_new(vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);

	label = gtk_label_new(_("Compare by:"));
	gtk_box_pack_start(GTK_BOX(status_box), label, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(label);

	dupe_menu_setup(dw);
	gtk_box_pack_start(GTK_BOX(status_box), dw->combo, FALSE, FALSE, 0);
	gtk_widget_show(dw->combo);

	dw->button_thumbs = gtk_check_button_new_with_label(_("Thumbnails"));
	dw->show_thumbs = options->duplicates_thumbnails;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dw->button_thumbs), dw->show_thumbs);
	g_signal_connect(G_OBJECT(dw->button_thumbs), "toggled",
			 G_CALLBACK(dupe_window_show_thumb_cb), dw);
	gtk_box_pack_start(GTK_BOX(status_box), dw->button_thumbs, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(dw->button_thumbs);

	dw->button_rotation_invariant = gtk_check_button_new_with_label(_("Ignore Rotation"));
	gtk_widget_set_tooltip_text(GTK_WIDGET(dw->button_rotation_invariant), "Ignore image orientation");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dw->button_rotation_invariant), options->rot_invariant_sim);
	g_signal_connect(G_OBJECT(dw->button_rotation_invariant), "toggled",
			 G_CALLBACK(dupe_window_rotation_invariant_cb), dw);
	gtk_box_pack_start(GTK_BOX(status_box), dw->button_rotation_invariant, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(dw->button_rotation_invariant);

	button = gtk_check_button_new_with_label(_("Compare two file sets"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), dw->second_set);
	g_signal_connect(G_OBJECT(button), "toggled",
			 G_CALLBACK(dupe_second_set_toggle_cb), dw);
	gtk_box_pack_end(GTK_BOX(status_box), button, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(button);

	status_box = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), status_box, FALSE, FALSE, 0);
	gtk_widget_show(status_box);

	frame = gtk_frame_new(NULL);
	DEBUG_NAME(frame);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(status_box), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	dw->status_label = gtk_label_new("");
	gtk_container_add(GTK_CONTAINER(frame), dw->status_label);
	gtk_widget_show(dw->status_label);

	button = gtk_check_button_new_with_label(_("Sort"));
	gtk_widget_set_tooltip_text(GTK_WIDGET(button), "Sort by group totals");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), options->sort_totals);
	g_signal_connect(G_OBJECT(button), "toggled",
			 G_CALLBACK(dupe_sort_totals_toggle_cb), dw);
	gtk_box_pack_start(GTK_BOX(status_box), button, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(button);

	label = gtk_label_new(_("Custom Threshold"));
	gtk_box_pack_start(GTK_BOX(status_box), label, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(label);
	dw->custom_threshold = gtk_spin_button_new_with_range(1, 100, 1);
	gtk_widget_set_tooltip_text(GTK_WIDGET(dw->custom_threshold), "Custom similarity threshold");
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(dw->custom_threshold), options->duplicates_similarity_threshold);
	g_signal_connect(G_OBJECT(dw->custom_threshold), "value_changed",
													G_CALLBACK(dupe_window_custom_threshold_cb), dw);
	gtk_box_pack_start(GTK_BOX(status_box), dw->custom_threshold, FALSE, FALSE, PREF_PAD_SPACE);
	gtk_widget_show(dw->custom_threshold);

	dw->extra_label = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dw->extra_label), 0.0);
#if GTK_CHECK_VERSION(3,0,0)
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(dw->extra_label), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(dw->extra_label), TRUE);
#endif
	gtk_box_pack_end(GTK_BOX(status_box), dw->extra_label, FALSE, FALSE, 0);
	gtk_widget_show(dw->extra_label);

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

	dupe_window_update_count(dw, TRUE);
	dupe_window_update_progress(dw, NULL, 0.0, FALSE);

	dupe_window_list = g_list_append(dupe_window_list, dw);

	file_data_register_notify_func(dupe_notify_cb, dw, NOTIFY_PRIORITY_MEDIUM);

	return dw;
}

/*
 *-------------------------------------------------------------------
 * dnd confirm dir
 *-------------------------------------------------------------------
 */

typedef struct {
	DupeWindow *dw;
	GList *list;
} CDupeConfirmD;

static void confirm_dir_list_cancel(GtkWidget *widget, gpointer data)
{
	/* do nothing */
}

static void confirm_dir_list_add(GtkWidget *widget, gpointer data)
{
	CDupeConfirmD *d = data;
	GList *work;

	dupe_window_add_files(d->dw, d->list, FALSE);

	work = d->list;
	while (work)
		{
		FileData *fd = work->data;
		work = work->next;
		if (isdir(fd->path))
			{
			GList *list;

			filelist_read(fd, &list, NULL);
			list = filelist_filter(list, FALSE);
			if (list)
				{
				dupe_window_add_files(d->dw, list, FALSE);
				filelist_free(list);
				}
			}
		}
}

static void confirm_dir_list_recurse(GtkWidget *widget, gpointer data)
{
	CDupeConfirmD *d = data;
	dupe_window_add_files(d->dw, d->list, TRUE);
}

static void confirm_dir_list_skip(GtkWidget *widget, gpointer data)
{
	CDupeConfirmD *d = data;
	dupe_window_add_files(d->dw, d->list, FALSE);
}

static void confirm_dir_list_destroy(GtkWidget *widget, gpointer data)
{
	CDupeConfirmD *d = data;
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

	menu_item_add_stock(menu, _("Dropped list includes folders."), GTK_STOCK_DND_MULTIPLE, NULL, NULL);
	menu_item_add_divider(menu);
	menu_item_add_stock(menu, _("_Add contents"), GTK_STOCK_OK, G_CALLBACK(confirm_dir_list_add), d);
	menu_item_add_stock(menu, _("Add contents _recursive"), GTK_STOCK_ADD, G_CALLBACK(confirm_dir_list_recurse), d);
	menu_item_add_stock(menu, _("_Skip folders"), GTK_STOCK_REMOVE, G_CALLBACK(confirm_dir_list_skip), d);
	menu_item_add_divider(menu);
	menu_item_add_stock(menu, _("Cancel"), GTK_STOCK_CANCEL, G_CALLBACK(confirm_dir_list_cancel), d);

	return menu;
}

/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */

static GtkTargetEntry dupe_drag_types[] = {
	{ "text/uri-list", 0, TARGET_URI_LIST },
	{ "text/plain", 0, TARGET_TEXT_PLAIN }
};
static gint n_dupe_drag_types = 2;

static GtkTargetEntry dupe_drop_types[] = {
	{ TARGET_APP_COLLECTION_MEMBER_STRING, 0, TARGET_APP_COLLECTION_MEMBER },
	{ "text/uri-list", 0, TARGET_URI_LIST }
};
static gint n_dupe_drop_types = 2;

static void dupe_dnd_data_set(GtkWidget *widget, GdkDragContext *context,
			      GtkSelectionData *selection_data, guint info,
			      guint time, gpointer data)
{
	DupeWindow *dw = data;
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
			      gint x, gint y,
			      GtkSelectionData *selection_data, guint info,
			      guint time, gpointer data)
{
	DupeWindow *dw = data;
	GtkWidget *source;
	GList *list = NULL;
	GList *work;

	source = gtk_drag_get_source_widget(context);
	if (source == dw->listview || source == dw->second_listview) return;

	dw->second_drop = (dw->second_set && widget == dw->second_listview);

	switch (info)
		{
		case TARGET_APP_COLLECTION_MEMBER:
			collection_from_dnd_data((gchar *)gtk_selection_data_get_data(selection_data), &list, NULL);
			break;
		case TARGET_URI_LIST:
			list = uri_filelist_from_gtk_selection_data(selection_data);
			work = list;
			while (work)
				{
				FileData *fd = work->data;
				if (isdir(fd->path))
					{
					GtkWidget *menu;
					menu = dupe_confirm_dir_list(dw, list);
					gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, time);
					return;
					}
				work = work->next;
				}
			break;
		default:
			list = NULL;
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
			GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP,
			dupe_drop_types, n_dupe_drop_types,
			GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK);

		}
	else
		{
		gtk_drag_dest_unset(widget);
		}
}

static void dupe_dnd_begin(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	DupeWindow *dw = data;
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
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, NULL, FALSE);
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

static void dupe_dnd_end(GtkWidget *widget, GdkDragContext *context, gpointer data)
{
	DupeWindow *dw = data;
	dupe_dest_set(dw->listview, TRUE);
	dupe_dest_set(dw->second_listview, TRUE);
}

static void dupe_dnd_init(DupeWindow *dw)
{
	gtk_drag_source_set(dw->listview, GDK_BUTTON1_MASK | GDK_BUTTON2_MASK,
			    dupe_drag_types, n_dupe_drag_types,
			    GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
	g_signal_connect(G_OBJECT(dw->listview), "drag_data_get",
			 G_CALLBACK(dupe_dnd_data_set), dw);
	g_signal_connect(G_OBJECT(dw->listview), "drag_begin",
			 G_CALLBACK(dupe_dnd_begin), dw);
	g_signal_connect(G_OBJECT(dw->listview), "drag_end",
			 G_CALLBACK(dupe_dnd_end), dw);

	dupe_dest_set(dw->listview, TRUE);
	g_signal_connect(G_OBJECT(dw->listview), "drag_data_received",
			 G_CALLBACK(dupe_dnd_data_get), dw);

	gtk_drag_source_set(dw->second_listview, GDK_BUTTON1_MASK | GDK_BUTTON2_MASK,
			    dupe_drag_types, n_dupe_drag_types,
			    GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
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
	DupeWindow *dw = data;

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
			while (dupe_item_remove_by_path(dw, fd->path));
			break;
		case FILEDATA_CHANGE_UNSPECIFIED:
		case FILEDATA_CHANGE_WRITE_METADATA:
			break;
		}

}

/*
 *-------------------------------------------------------------------
 * Export duplicates data
 *-------------------------------------------------------------------
 */

 typedef enum {
	EXPORT_CSV = 0,
	EXPORT_TSV
} SeparatorType;

typedef struct _ExportDupesData ExportDupesData;
struct _ExportDupesData
{
	FileDialog *dialog;
	SeparatorType separator;
	DupeWindow *dupewindow;
};

static void export_duplicates_close(ExportDupesData *edd)
{
	if (edd->dialog) file_dialog_close(edd->dialog);
	edd->dialog = NULL;
}

static void export_duplicates_data_cancel_cb(FileDialog *fdlg, gpointer data)
{
	ExportDupesData *edd = data;

	export_duplicates_close(edd);
}

static void export_duplicates_data_save_cb(FileDialog *fdlg, gpointer data)
{
	ExportDupesData *edd = data;
	GError *error = NULL;
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

	gfstream = g_file_replace(out_file, NULL, TRUE, G_FILE_CREATE_NONE, NULL, &error);
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

	tpath = work->data;
	gtk_tree_model_get_iter(store, &iter, tpath);
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, DUPE_COLUMN_COLOR, &color_new, -1);
	color_old = !color_new;
	match_count = 0;

	while (work)
		{
		tpath = work->data;
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
		if (rank_split[0] == NULL)
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

		output_string = g_string_append(output_string, g_strdup_printf("%"PRIu64, di->fd->size));
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

	g_output_stream_write(G_OUTPUT_STREAM(gfstream), output_string->str, strlen(output_string->str), NULL, &error);

	g_free(sep);
	g_string_free(output_string, TRUE);
	g_object_unref(gfstream);
	g_object_unref(out_file);

	export_duplicates_close(edd);
}

static void pop_menu_export(GList *selection_list, gpointer dupe_window, gpointer data)
{
	const gint index = GPOINTER_TO_INT(data);
	DupeWindow *dw = dupe_window;
	gchar *title = "Export duplicates data";
	gchar *default_path = "/tmp/";
	gchar *file_extension;
	const gchar *stock_id;
	ExportDupesData *edd;
	const gchar *previous_path;

	edd = g_new0(ExportDupesData, 1);
	edd->dialog = file_util_file_dlg(title, "export_duplicates", NULL, export_duplicates_data_cancel_cb, edd);

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

	stock_id = GTK_STOCK_SAVE;

	generic_dialog_add_message(GENERIC_DIALOG(edd->dialog), NULL, title, NULL, FALSE);
	file_dialog_add_button(edd->dialog, stock_id, NULL, export_duplicates_data_save_cb, TRUE);

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

	dw = submenu_item_get_data(widget);
	selection_list = dupe_listview_get_selection(dw, dw->listview);
	pop_menu_export(selection_list, dw, data);

	filelist_free(selection_list);
}

static GtkWidget *submenu_add_export(GtkWidget *menu, GtkWidget **menu_item, GCallback func, gpointer data)
{
	GtkWidget *item;
	GtkWidget *submenu;

	item = menu_item_add(menu, _("Export"), NULL, NULL);

	submenu = gtk_menu_new();
	g_object_set_data(G_OBJECT(submenu), "submenu_data", data);

	menu_item_add_stock_sensitive(submenu, _("Export to csv"),
					GTK_STOCK_INDEX, TRUE, G_CALLBACK(func), GINT_TO_POINTER(0));
	menu_item_add_stock_sensitive(submenu, _("Export to tab-delimited"),
					GTK_STOCK_INDEX, TRUE, G_CALLBACK(func), GINT_TO_POINTER(1));

	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
	if (menu_item) *menu_item = item;

	return submenu;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
