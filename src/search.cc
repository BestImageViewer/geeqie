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

#include "search.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "actions.h"
#include "bar-keywords.h"
#include "cache.h"
#include "collect.h"
#include "compat.h"
#include "dnd.h"
#include "editors.h"
#include "filedata.h"
#include "history-list.h"
#include "image-load.h"
#include "img-view.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "menu.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-util.h"
#include "print.h"
#include "similar.h"
#include "thumb.h"
#include "ui-bookmark.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"
#include "utilops.h"
#include "window.h"

namespace {

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
	SEARCH_COLUMN_RANK = 0,
	SEARCH_COLUMN_THUMB,
	SEARCH_COLUMN_NAME,
	SEARCH_COLUMN_SIZE,
	SEARCH_COLUMN_DATE,
	SEARCH_COLUMN_DIMENSIONS,
	SEARCH_COLUMN_PATH,
	SEARCH_COLUMN_COUNT	/* total columns */
};

struct MatchFileData;

struct SearchResultRow
{
	GObject parent;
	MatchFileData *mfd;
	GdkPixbuf *thumb;
};

struct SearchResultRowClass
{
	GObjectClass parent_class;
};

G_DEFINE_TYPE(SearchResultRow, search_result_row, G_TYPE_OBJECT)

enum
{
	SEARCH_RESULT_ROW_CHANGED,
	SEARCH_RESULT_ROW_SIGNAL_COUNT
};

guint search_result_row_signals[SEARCH_RESULT_ROW_SIGNAL_COUNT];

void search_result_row_finalize(GObject *object)
{
	auto *row = reinterpret_cast<SearchResultRow *>(object);
	g_clear_object(&row->thumb);
	G_OBJECT_CLASS(search_result_row_parent_class)->finalize(object);
}

void search_result_row_class_init(SearchResultRowClass *klass)
{
	G_OBJECT_CLASS(klass)->finalize = search_result_row_finalize;
	search_result_row_signals[SEARCH_RESULT_ROW_CHANGED] =
		g_signal_new("changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		             0, nullptr, nullptr, nullptr, G_TYPE_NONE, 0);
}

void search_result_row_init(SearchResultRow *)
{
}

SearchResultRow *search_result_row_new(MatchFileData *mfd, GdkPixbuf *thumb)
{
	auto *row = static_cast<SearchResultRow *>(g_object_new(search_result_row_get_type(), nullptr));
	row->mfd = mfd;
	row->thumb = thumb ? GDK_PIXBUF(g_object_ref(thumb)) : nullptr;
	return row;
}

struct SearchUi
{
	GtkWidget *window;

	GtkWidget *box_search; // main container

	// "Search" row
	GtkWidget *menu_path;
	GtkWidget *path_entry;
	GtkWidget *check_recurse;

	GtkWidget *box_collection;
	GtkWidget *entry_collection;

	// "File" row
	GtkWidget *menu_name;
	GtkWidget *entry_name;

	// "File size" row
	GtkWidget *menu_size;
	GtkWidget *spin_size;
	GtkWidget *spin_size_end;

	// "File date" row
	GtkWidget *menu_date;
	GtkWidget *date_sel;
	GtkWidget *date_sel_end;
	GtkWidget *date_type;

	// "Image dimensions" row
	GtkWidget *menu_dimensions;
	GtkWidget *box_dimensions_end;

	// "Image content" row
	GtkWidget *spin_similarity;
	GtkWidget *entry_similarity;

	// "Keywords" row
	GtkWidget *menu_keywords;
	GtkWidget *entry_keywords;

	// "Comment" row
	GtkWidget *menu_comment;
	GtkWidget *entry_comment;

	// "Exif" row
	GtkWidget *menu_exif;
	GtkWidget *entry_exif_tag;
	GtkWidget *entry_exif_value;

	// "Image rating" row
	GtkWidget *menu_rating;
	GtkWidget *spin_rating;
	GtkWidget *spin_rating_end;

	// "Image geocoded" row
	GtkWidget *menu_gps;
	GtkWidget *spin_gps;
	GtkWidget *units_gps;
	GtkWidget *entry_gps_coord;

	// "Image class" row
	GtkWidget *menu_class;
	GtkWidget *class_type;

	// "Marks" row
	GtkWidget *menu_marks;
	GtkWidget *marks_type;

	GtkWidget *result_view;
	GListStore *result_store;
	GtkMultiSelection *result_selection;
	GtkColumnViewColumn *result_columns[SEARCH_COLUMN_COUNT];

	// bottom bar
	GtkWidget *button_thumbs;
	GtkWidget *label_status;
	GtkWidget *label_progress;
	GtkWidget *button_start;
	GtkWidget *button_stop;
	GtkWidget *spinner;
};

using GetFileDate = std::function<time_t(FileData *)>;

struct SearchDateType
{
	const gchar *name;
	GetFileDate get_file_date;
};

const SearchDateType search_date_types[] = {
    { _("Modified"), [](FileData *fd){ return fd->date; } },
    { _("Status Changed"), [](FileData *fd){ return fd->cdate; } },
    { _("Original"), [](FileData *fd){ read_exif_time_data(fd); return fd->exifdate; } },
    { _("Digitized"), [](FileData *fd){ read_exif_time_digitized_data(fd); return fd->exifdate_digitized; } },
};

struct SearchDate
{
	void set_date(GtkWidget *date_sel);
	[[nodiscard]] time_t to_time() const;
	bool is_equal(const std::tm *lt) const;

private:
	gint year;
	gint month;
	gint mday;
};

void SearchDate::set_date(GtkWidget *date_sel)
{
	g_autoptr(GDateTime) date = date_selection_get(date_sel);

	mday = g_date_time_get_day_of_month(date);
	month = g_date_time_get_month(date);
	year = g_date_time_get_year(date);
}

time_t SearchDate::to_time() const
{
	std::tm lt;

	lt.tm_sec = 0;
	lt.tm_min = 0;
	lt.tm_hour = 0;
	lt.tm_mday = mday;
	lt.tm_mon = month - 1;
	lt.tm_year = year - 1900;
	lt.tm_isdst = 0;

	return mktime(&lt);
}

bool SearchDate::is_equal(const std::tm *lt) const
{
	return (year - 1900) == lt->tm_year &&
	       (month - 1) == lt->tm_mon &&
	       mday == lt->tm_mday;
}

struct SearchData
{
	SearchUi ui;

	FileData *search_dir_fd;
	gboolean   search_path_recurse;
	gchar *search_name;
	GRegex *search_name_regex;
	gboolean   search_name_match_case;
	gboolean   search_name_symbolic_link;
	gint64 search_size;
	gint64 search_size_end;
	GetFileDate get_file_date;
	SearchDate search_date;
	SearchDate search_date_end;
	GqSize search_dimensions;
	GqSize search_dimensions_end;
	gint   search_similarity;
	gchar *search_similarity_path;
	std::unique_ptr<CacheData> search_similarity_cd;
	GList *search_keyword_list;
	gchar *search_comment;
	GRegex *search_comment_regex;
	GRegex *search_exif_regex;
	gchar *search_exif_tag;
	gchar *search_exif_value;
	gboolean search_exif_match_case;
	gint   search_rating;
	gint   search_rating_end;
	gboolean   search_comment_match_case;
	gint search_gps;
	gdouble search_lat;
	gdouble search_lon;
	gdouble search_earth_radius;
	FileFormatClass search_class;
	gint search_marks;

	MatchType search_type;

	MatchType match_name;
	MatchType match_size;
	MatchType match_date;
	MatchType match_dimensions;
	MatchType match_keywords;
	MatchType match_comment;
	MatchType match_exif;
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
	gboolean match_exif_enable;
	gboolean match_rating_enable;
	gboolean match_gps_enable;
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
	std::unique_ptr<CacheData> img_cd;

	FileData *click_fd;

	ThumbLoader *thumb_loader;
	gboolean thumb_enable;
	FileData *thumb_fd;
};

struct MatchFileData
{
	FileData *fd;
	GqSize dimensions;
	gint rank;
};

struct MatchList
{
	const gchar *text;
	MatchType type;
};

constexpr std::array<MatchList, 4> text_search_menu_path{{
	{ N_("folder"),		SEARCH_MATCH_NONE },
	{ N_("comments"),	SEARCH_MATCH_ALL },
	{ N_("results"),	SEARCH_MATCH_CONTAINS },
	{ N_("collection"),	SEARCH_MATCH_COLLECTION }
}};

constexpr std::array<MatchList, 3> text_search_menu_name{{
	{ N_("name contains"),	SEARCH_MATCH_NAME_CONTAINS },
	{ N_("name is"),	SEARCH_MATCH_NAME_EQUAL },
	{ N_("path contains"),	SEARCH_MATCH_PATH_CONTAINS }
}};

constexpr std::array<MatchList, 4> text_search_menu_size{{
	{ N_("equal to"),	SEARCH_MATCH_EQUAL },
	{ N_("less than"),	SEARCH_MATCH_UNDER },
	{ N_("greater than"),	SEARCH_MATCH_OVER },
	{ N_("between"),	SEARCH_MATCH_BETWEEN }
}};

constexpr std::array<MatchList, 4> text_search_menu_date{{
	{ N_("equal to"),	SEARCH_MATCH_EQUAL },
	{ N_("before"),		SEARCH_MATCH_UNDER },
	{ N_("after"),		SEARCH_MATCH_OVER },
	{ N_("between"),	SEARCH_MATCH_BETWEEN }
}};

constexpr auto &text_search_menu_dimensions = text_search_menu_size;

constexpr std::array<MatchList, 3> text_search_menu_keywords{{
	{ N_("match all"),	SEARCH_MATCH_ALL },
	{ N_("match any"),	SEARCH_MATCH_ANY },
	{ N_("exclude"),	SEARCH_MATCH_NONE }
}};

constexpr std::array<MatchList, 2> text_search_menu_comment{{
	{ N_("contains"),	SEARCH_MATCH_CONTAINS },
	{ N_("miss"),		SEARCH_MATCH_NONE }
}};

constexpr auto &text_search_menu_exif = text_search_menu_comment;

constexpr auto &text_search_menu_rating = text_search_menu_size;

constexpr std::array<MatchList, 3> text_search_menu_gps{{
	{ N_("not geocoded"),	SEARCH_MATCH_NONE },
	{ N_("less than"),	SEARCH_MATCH_UNDER },
	{ N_("greater than"),	SEARCH_MATCH_OVER }
}};

constexpr std::array<MatchList, 2> text_search_menu_class{{
	{ N_("is"),	SEARCH_MATCH_EQUAL },
	{ N_("is not"),	SEARCH_MATCH_NONE }
}};

constexpr auto &text_search_menu_marks = text_search_menu_class;

constexpr gint DEF_SEARCH_WIDTH = 700;
constexpr gint DEF_SEARCH_HEIGHT = 650;

constexpr gint SEARCH_BUFFER_MATCH_LOAD = 20;
constexpr gint SEARCH_BUFFER_MATCH_HIT = 5;
constexpr gint SEARCH_BUFFER_MATCH_MISS = 1;
constexpr gint SEARCH_BUFFER_FLUSH_SIZE = 99;

constexpr auto FORMAT_CLASS_BROKEN = static_cast<FileFormatClass>(FILE_FORMAT_CLASSES + 1);


template<typename T>
bool match_is_between(T val, T a, T b)
{
	return (b > a) ? (a <= val && val <= b) : (b <= val && val <= a);
}

double to_radians(gdouble deg)
{
	return deg * M_PI / 180.0;
}

/**
 * @brief Get distance between two lat/long points
 * @param sd @ref SearchData
 * @param latitude Degrees
 * @param longitude Degrees
 * @returns Distance in km/miles/nautical miles
 * 
 * Equirectangular approximation. \n
 * Error is probably insignificant for this application: \n
 * < 10 km       0.1% \n
 * 10 – 100 km   0.1%–0.5% \n
 * 100 – 1000 km 0.5%–2% \n
 * \> 1000 km     ≥ 2–5% \n
 */
gdouble get_gps_range(const SearchData *sd, gdouble latitude, gdouble longitude)
{
	gdouble x = to_radians(sd->search_lon - longitude) * std::cos(to_radians((latitude + sd->search_lat) / 2));
	gdouble y = to_radians(sd->search_lat - latitude);

	return std::sqrt((x * x) + (y * y)) * sd->search_earth_radius;
}

#define MATCH_TYPE_KEY "match_type"

bool menu_choice_get_match_type(GtkWidget *drop_down, MatchType &type)
{
	GObject *item = G_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(drop_down)));
	if (!item) return false;

	type = static_cast<MatchType>(GPOINTER_TO_INT(g_object_get_data(item, MATCH_TYPE_KEY)));
	return true;
}

GString *get_marks_string(gint mark_num)
{
	GString *marks_string = g_string_new(_("Mark "));
	g_string_append_printf(marks_string, "%d", mark_num + 1);

	if (g_strcmp0(marks_string->str, options->marks_tooltips[mark_num]) != 0)
		{
		g_string_append_printf(marks_string, " %s", options->marks_tooltips[mark_num]);
		}

	return marks_string;
}

} // namespace

static gint search_result_selection_count(SearchData *sd, gint64 *bytes = nullptr);
static gint search_result_count(SearchData *sd, gint64 *bytes = nullptr);

static void search_notify_cb(FileData *fd, NotifyType type, gpointer data);
static void search_start_do(SearchData *sd);
static void search_result_menu(SearchData *sd, bool on_row, GtkWidget *parent = nullptr, gdouble x = 0, gdouble y = 0);

/*
 *-------------------------------------------------------------------
 * utils
 *-------------------------------------------------------------------
 */

static void search_status_update(SearchData *sd)
{
	g_autofree gchar *buf = nullptr;
	gint t;
	gint s;
	gint64 t_bytes;
	gint64 s_bytes;

	t = search_result_count(sd, &t_bytes);
	s = search_result_selection_count(sd, &s_bytes);

	g_autofree gchar *tt = text_from_size_abrev(t_bytes);

	if (s > 0)
		{
		g_autofree gchar *ts = text_from_size_abrev(s_bytes);
		buf = g_strdup_printf(_("%s, %d files (%s, %d)"), tt, t, ts, s);
		}
	else
		{
		buf = g_strdup_printf(_("%s, %d files"), tt, t);
		}

	gtk_label_set_text(GTK_LABEL(sd->ui.label_status), buf);
}

static void search_progress_update(SearchData *sd, gboolean search, gdouble thumbs)
{
	if (search || thumbs >= 0.0)
		{
		const gchar *message;

		if (search && (sd->search_folder_list || sd->search_file_list))
			message = _("Searching…");
		else if (thumbs >= 0.0)
			message = _("Loading thumbs…");
		else
			message = "";

		g_autofree gchar *buf = g_strdup_printf("%s(%d / %d)", message, sd->search_count, sd->search_total);
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(sd->ui.label_progress), buf);
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(sd->ui.label_progress),
		                              (thumbs >= 0.0) ? thumbs : 0.0);
		}
	else
		{
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(sd->ui.label_progress), "");
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(sd->ui.label_progress), 0.0);
		}
}

/*
 *-------------------------------------------------------------------
 * result list
 *-------------------------------------------------------------------
 */

static guint search_result_find_row(SearchData *sd, FileData *fd)
{
	for (guint position = 0; position < g_list_model_get_n_items(G_LIST_MODEL(sd->ui.result_store)); position++)
		{
		auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), position));
		const gboolean match = row->mfd->fd == fd;
		g_object_unref(row);
		if (match) return position;
		}
	return GTK_INVALID_LIST_POSITION;
}

static gboolean search_result_row_selected(SearchData *sd, FileData *fd)
{
	GListModel *model = gtk_multi_selection_get_model(sd->ui.result_selection);
	for (guint position = 0; position < g_list_model_get_n_items(model); position++)
		{
		if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(sd->ui.result_selection), position)) continue;
		auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(model, position));
		const gboolean match = row->mfd->fd == fd;
		g_object_unref(row);
		if (match) return TRUE;
		}

	return FALSE;
}

static gint search_result_selection_util(SearchData *sd, gint64 *bytes, GList **list)
{
	gint n = 0;
	gint64 total = 0;
	GList *plist = nullptr;

	GListModel *model = gtk_multi_selection_get_model(sd->ui.result_selection);
	for (guint position = 0; position < g_list_model_get_n_items(model); position++)
		{
		if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(sd->ui.result_selection), position)) continue;
		n++;
		if (bytes || list)
			{
			auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(model, position));
			total += row->mfd->fd->size;
			if (list) plist = g_list_prepend(plist, file_data_ref(row->mfd->fd));
			g_object_unref(row);
			}
		}

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

static gint search_result_count(SearchData *sd, gint64 *bytes)
{
	const guint n = g_list_model_get_n_items(G_LIST_MODEL(sd->ui.result_store));
	gint64 total = 0;
	if (bytes)
		{
		for (guint position = 0; position < n; position++)
			{
			auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), position));
			total += row->mfd->fd->size;
			g_object_unref(row);
			}
		}

	if (bytes) *bytes = total;

	return n;
}

static GdkPixbuf *search_scale_thumb(GdkPixbuf *pixbuf);

static void search_result_append(SearchData *sd, MatchFileData *mfd)
{
	FileData *fd;
	fd = mfd->fd;

	if (!fd) return;

	g_autoptr(GdkPixbuf) thumb = search_scale_thumb(fd->thumb_pixbuf);

	auto *row = search_result_row_new(mfd, thumb);
	g_list_store_append(sd->ui.result_store, row);
	g_object_unref(row);
}

static GList *search_result_refine_list(SearchData *sd)
{
	GList *list = nullptr;
	for (guint position = 0; position < g_list_model_get_n_items(G_LIST_MODEL(sd->ui.result_store)); position++)
		{
		auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), position));
		list = g_list_prepend(list, row->mfd->fd);
		g_free(row->mfd);
		row->mfd = nullptr;
		g_object_unref(row);
		}

	/* clear it here, so that the FileData in list is not freed */
	g_list_store_remove_all(sd->ui.result_store);

	return g_list_reverse(list);
}

static void search_result_clear(SearchData *sd)
{
	while (g_list_model_get_n_items(G_LIST_MODEL(sd->ui.result_store)) > 0)
		{
		auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), 0));
		file_data_unref(row->mfd->fd);
		g_free(row->mfd);
		g_object_unref(row);
		g_list_store_remove(sd->ui.result_store, 0);
		}

	sd->click_fd = nullptr;

	thumb_loader_free(sd->thumb_loader);
	sd->thumb_loader = nullptr;
	sd->thumb_fd = nullptr;

	search_status_update(sd);
}

static void search_result_remove_item(SearchData *sd, MatchFileData *mfd, guint position)
{
	if (!mfd || position == GTK_INVALID_LIST_POSITION) return;
	g_list_store_remove(sd->ui.result_store, position);
	if (sd->click_fd == mfd->fd) sd->click_fd = nullptr;
	if (sd->thumb_fd == mfd->fd) sd->thumb_fd = nullptr;
	file_data_unref(mfd->fd);
	g_free(mfd);
}

static void search_result_remove(SearchData *sd, FileData *fd)
{
	for (guint position = 0; position < g_list_model_get_n_items(G_LIST_MODEL(sd->ui.result_store)); position++)
		{
		auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), position));
		MatchFileData *mfd = row->mfd;
		const gboolean match = mfd->fd == fd;
		g_object_unref(row);
		if (match)
			{
			search_result_remove_item(sd, mfd, position);
			return;
			}
		}
}

static void search_result_remove_selection(SearchData *sd)
{
	GList *flist = nullptr;

	GListModel *model = gtk_multi_selection_get_model(sd->ui.result_selection);
	for (guint position = 0; position < g_list_model_get_n_items(model); position++)
		{
		if (!gtk_selection_model_is_selected(GTK_SELECTION_MODEL(sd->ui.result_selection), position)) continue;
		auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(model, position));
		flist = g_list_prepend(flist, row->mfd->fd);
		g_object_unref(row);
		}

	GList *work = flist;
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
	file_util_start_editor_from_filelist(key, search_result_selection_list(sd), nullptr, sd->ui.window);
}

static void search_result_collection_from_selection_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	g_autoptr(FileDataList) list = search_result_selection_list(sd);
	collection_by_index_add_filelist(-1, list);
}

static gboolean search_result_update_idle_cb(gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_status_update(sd);

	sd->update_idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void search_result_select_cb(GtkSelectionModel *, guint, guint, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!sd->update_idle_id)
		{
		sd->update_idle_id = g_idle_add(search_result_update_idle_cb, sd);
		}

}

/*
 *-------------------------------------------------------------------
 * result list thumbs
 *-------------------------------------------------------------------
 */

static void search_result_thumb_step(SearchData *sd);

static GdkPixbuf *search_scale_thumb(GdkPixbuf *pixbuf)
{
	if (!pixbuf) return nullptr;

	gint width;
	gint height;
	pixbuf_scale_aspect(options->thumbnails.size.width,
	                    options->thumbnails.size.height,
	                    gdk_pixbuf_get_width(pixbuf),
	                    gdk_pixbuf_get_height(pixbuf),
	                    width, height);

	if (width == gdk_pixbuf_get_width(pixbuf) && height == gdk_pixbuf_get_height(pixbuf))
		{
		return GDK_PIXBUF(g_object_ref(pixbuf));
		}

	return gdk_pixbuf_scale_simple(pixbuf, width, height, options->thumbnails.quality);
}


static void search_result_thumb_set(SearchData *sd, FileData *fd, guint position)
{
	if (position == GTK_INVALID_LIST_POSITION) position = search_result_find_row(sd, fd);
	if (position != GTK_INVALID_LIST_POSITION)
		{
		auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), position));
		g_autoptr(GdkPixbuf) thumb = search_scale_thumb(fd->thumb_pixbuf);
		g_set_object(&row->thumb, thumb);
		g_signal_emit(row, search_result_row_signals[SEARCH_RESULT_ROW_CHANGED], 0);
		g_object_unref(row);
		}
}

static void search_result_thumb_do(SearchData *sd)
{
	FileData *fd;

	if (!sd->thumb_loader || !sd->thumb_fd) return;
	fd = sd->thumb_fd;

	search_result_thumb_set(sd, fd, GTK_INVALID_LIST_POSITION);
}

static void search_result_thumb_done_cb(ThumbLoader *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_result_thumb_do(sd);
	search_result_thumb_step(sd);
}

static void search_result_thumb_step(SearchData *sd)
{
	MatchFileData *mfd = nullptr;
	gint row = 0;
	const guint length = g_list_model_get_n_items(G_LIST_MODEL(sd->ui.result_store));
	if (!sd->thumb_enable)
		{
		for (guint position = 0; position < length; position++)
			{
			auto *result_row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), position));
			g_clear_object(&result_row->thumb);
			g_object_unref(result_row);
			}
		return;
		}

	for (guint position = 0; position < length && !mfd; position++)
		{
		auto *result_row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), position));
		mfd = result_row->mfd;
		if (result_row->thumb || mfd->fd->thumb_pixbuf)
			{
			if (!result_row->thumb) search_result_thumb_set(sd, mfd->fd, position);
			row++;
			mfd = nullptr;
			}
		g_object_unref(result_row);
		}

	if (!mfd)
		{
		sd->thumb_fd = nullptr;
		thumb_loader_free(sd->thumb_loader);
		sd->thumb_loader = nullptr;

		search_progress_update(sd, TRUE, -1.0);
		return;
		}

	search_progress_update(sd, FALSE, static_cast<gdouble>(row) / length);

	sd->thumb_fd = mfd->fd;
	thumb_loader_free(sd->thumb_loader);
	sd->thumb_loader = thumb_loader_new(options->thumbnails.size.width, options->thumbnails.size.height);

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
	GtkColumnViewColumn *column = sd->ui.result_columns[SEARCH_COLUMN_THUMB];
	if (!column) return;
	gtk_column_view_column_set_fixed_width(column, sd->thumb_enable ? options->thumbnails.size.width + 4 : 4);
}

static void search_result_thumb_enable(SearchData *sd, gboolean enable)
{
	if (sd->thumb_enable == enable) return;

	if (sd->thumb_enable)
		{
		thumb_loader_free(sd->thumb_loader);
		sd->thumb_loader = nullptr;
		for (guint position = 0; position < g_list_model_get_n_items(G_LIST_MODEL(sd->ui.result_store)); position++)
			{
			auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), position));
			g_clear_object(&row->thumb);
			g_object_unref(row);
			}
		search_progress_update(sd, TRUE, -1.0);
		}

	GtkColumnViewColumn *column = sd->ui.result_columns[SEARCH_COLUMN_THUMB];
	if (column)
		{
		gtk_column_view_column_set_visible(column, enable);
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

static void sr_menu_view_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (sd->click_fd) layout_set_fd(nullptr, sd->click_fd);
}

static void sr_menu_viewnew_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	g_autoptr(FileDataList) list = search_result_selection_list(sd);
	view_window_new_from_list(list);
}

static void sr_menu_select_all_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	gtk_selection_model_select_all(GTK_SELECTION_MODEL(sd->ui.result_selection));
}

static void sr_menu_select_none_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(sd->ui.result_selection));
}

static void sr_menu_edit_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto *sd = static_cast<SearchData *>(data);
	if (!sd) return;

	const char *key = g_variant_get_string(parameter, nullptr);

	search_result_edit_selected(sd, key);
}

static void sr_menu_print_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	print_window_new(search_result_selection_list(sd), sd->ui.window);
}

static void sr_menu_copy_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_copy(nullptr, search_result_selection_list(sd), nullptr, sd->ui.window);
}

static void sr_menu_move_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_move(nullptr, search_result_selection_list(sd), nullptr, sd->ui.window);
}

static void sr_menu_rename_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_rename(nullptr, search_result_selection_list(sd), sd->ui.window);
}

template<gboolean safe_delete>
static void sr_menu_delete_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_delete(nullptr, search_result_selection_list(sd), sd->ui.window, safe_delete);
}

template<gboolean quoted>
static void sr_menu_copy_path_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	file_util_path_list_to_clipboard(search_result_selection_list(sd), quoted, ClipboardAction::COPY);
}

static void sr_menu_play_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	start_editor_from_file(options->image_l_click_video_editor, sd->click_fd);
}

/**
 * @brief Add file selection list to a collection
 * @param[in] widget
 * @param[in] data Index to the collection list menu item selected, or -1 for new collection
 *
 */
static void search_pop_menu_collections_cb(GSimpleAction *, GVariant *parameter, gpointer data)
{
	auto *sd = static_cast<SearchData *>(data);

	int index = g_variant_get_int32(parameter);

	g_autoptr(FileDataList) selection_list = search_result_selection_list(sd);
	collection_by_index_add_filelist(index, selection_list);
}

static void search_thumbnails_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *sd = static_cast<SearchData *>(data);

	auto *button_thumbs = GTK_CHECK_BUTTON(sd->ui.button_thumbs);
	gtk_check_button_set_active(button_thumbs, !gtk_check_button_get_active(button_thumbs));
}

static void search_result_menu_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *sd = static_cast<SearchData *>(data);
	search_result_menu(sd, search_result_selection_count(sd, nullptr) > 0);
}

/*
 *-------------------------------------------------------------------
 * result list input
 *-------------------------------------------------------------------
 */

static SearchResultRow *search_result_row_from_widget(GtkWidget *widget)
{
	if (auto *row = static_cast<SearchResultRow *>(g_object_get_data(G_OBJECT(widget), "search-result-row")))
		{
		return row;
		}

	for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *row = search_result_row_from_widget(child)) return row;
		}

	return nullptr;
}

static SearchResultRow *search_result_at_point(SearchData *sd, gdouble x, gdouble y, guint *position)
{
	GtkWidget *picked = gtk_widget_pick(sd->ui.result_view, x, y, GTK_PICK_DEFAULT);
	while (picked && picked != sd->ui.result_view)
		{
		SearchResultRow *row = search_result_row_from_widget(picked);
		if (row)
			{
			GListModel *model = gtk_multi_selection_get_model(sd->ui.result_selection);
			for (guint i = 0; i < g_list_model_get_n_items(model); i++)
				{
				auto *candidate = static_cast<SearchResultRow *>(g_list_model_get_item(model, i));
				const gboolean match = candidate == row;
				g_object_unref(candidate);
				if (match)
					{
					if (position) *position = i;
					return row;
					}
				}
			}
		picked = gtk_widget_get_parent(picked);
		}
	if (position) *position = GTK_INVALID_LIST_POSITION;
	return nullptr;
}

static void search_result_press_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
	auto *sd = static_cast<SearchData *>(data);
	guint position;
	SearchResultRow *row = search_result_at_point(sd, x, y, &position);
	MatchFileData *mfd = row ? row->mfd : nullptr;

	sd->click_fd = mfd ? mfd->fd : nullptr;

	const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

	if (button == GDK_BUTTON_SECONDARY)
		{
		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
		}

	if (!mfd)
		{
		if (button == GDK_BUTTON_SECONDARY)
			{
			search_result_menu(sd, FALSE, widget, x, y);
			}
		return;
		}

	if (button == GDK_BUTTON_MIDDLE)
		{
		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
		return;
		}

	if (button == GDK_BUTTON_SECONDARY)
		{
		if (!search_result_row_selected(sd, mfd->fd))
			{
			gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(sd->ui.result_selection));
			gtk_selection_model_select_item(GTK_SELECTION_MODEL(sd->ui.result_selection), position, TRUE);
			}

		search_result_menu(sd, TRUE, widget, x, y);
		return;
		}

	const GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

	if (button == GDK_BUTTON_PRIMARY && n_press == 1 &&
	    !(state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) &&
	    search_result_row_selected(sd, mfd->fd))
		{
		/* this selection handled on release_cb */
		gtk_widget_grab_focus(widget);
		}
}

static void search_result_release_cb(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer data)
{
	const guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

	if (button != GDK_BUTTON_PRIMARY && button != GDK_BUTTON_MIDDLE)
		{
		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
		return;
		}

	auto *sd = static_cast<SearchData *>(data);
	guint position;
	SearchResultRow *row = (x != 0 || y != 0) ? search_result_at_point(sd, x, y, &position) : nullptr;
	MatchFileData *mfd = row ? row->mfd : nullptr;
	if (button == GDK_BUTTON_PRIMARY && n_press == 2 && mfd && sd->click_fd == mfd->fd)
		{
		layout_set_fd(nullptr, mfd->fd);
		}

	if (button == GDK_BUTTON_MIDDLE)
		{
		if (mfd && sd->click_fd == mfd->fd)
			{
			if (search_result_row_selected(sd, mfd->fd))
				{
				gtk_selection_model_unselect_item(GTK_SELECTION_MODEL(sd->ui.result_selection), position);
				}
			else
				{
				gtk_selection_model_select_item(GTK_SELECTION_MODEL(sd->ui.result_selection), position, FALSE);
				}
			}

		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
		return;
		}

	const GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(gesture));

	if (mfd && sd->click_fd == mfd->fd &&
	    !(state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) &&
	    search_result_row_selected(sd, mfd->fd))
		{
		gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(sd->ui.result_selection));
		gtk_selection_model_select_item(GTK_SELECTION_MODEL(sd->ui.result_selection), position, TRUE);

		gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
		}
}

static void search_remove_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *sd = reinterpret_cast<SearchData *>(data);

	search_result_remove_selection(sd);
}

static void search_start_action_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto *sd = static_cast<SearchData *>(data);

	search_start_do(sd);
}

static void search_start_button_cb(GtkWidget *, gpointer data)
{
	auto *sd = static_cast<SearchData *>(data);

	search_start_do(sd);
}

/**
 * @brief Handle text box keystrokes instead of accelerators
 * @param window 
 * @param state 
 * 
 * If the accelerators are not disabled, keystrokes in text boxes
 * cause the accelerator actions to run
 */
static void enable_window_actions(GtkApplicationWindow *window, bool state)
{
	GActionGroup *group = G_ACTION_GROUP(window);
	g_auto(GStrv) names = g_action_group_list_actions(group);

	for (int i = 0; names[i] != nullptr; i++)
		{
		GAction *action = g_action_map_lookup_action(G_ACTION_MAP(window), names[i]);

		if (G_IS_SIMPLE_ACTION(action))
			{
			g_simple_action_set_enabled(G_SIMPLE_ACTION(action), state);
			}
		}
}

static void text_box_on_focus_in_cb(GtkEventControllerFocus *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	enable_window_actions(GTK_APPLICATION_WINDOW(sd->ui.window), FALSE);
}

static void text_box_on_focus_out_cb(GtkEventControllerFocus *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	enable_window_actions(GTK_APPLICATION_WINDOW(sd->ui.window), TRUE);
}

static void search_entry_attach_focus_controller(GtkWidget *widget, SearchData *sd)
{
	GtkEventController *focus_controller = gtk_event_controller_focus_new();
	g_signal_connect(focus_controller, "enter",
			 G_CALLBACK(text_box_on_focus_in_cb), sd);
	g_signal_connect(focus_controller, "leave",
			 G_CALLBACK(text_box_on_focus_out_cb), sd);
	gtk_widget_add_controller(widget, focus_controller);
}

static void search_win_result_clear_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	search_result_clear(sd);
}

static void search_result_menu(SearchData *sd, bool on_row, GtkWidget *parent, gdouble x, gdouble y)
{
	GAction *action;
	g_autoptr(GtkBuilder) builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-search.ui");
	GMenu *menu_model = G_MENU(gtk_builder_get_object(builder, "menu-search"));
	GList *editmenu_fd_list;

	editmenu_fd_list = search_result_selection_list(sd);

	GMenu *plugins_menu = G_MENU(gtk_builder_get_object(builder, "plugins-submenu"));
	plugins_menu_populate(plugins_menu, "win.search-win-plugin-run", editmenu_fd_list);

	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-plugin-run");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);

	GMenu *collections_menu = G_MENU(gtk_builder_get_object(builder, "collections-submenu"));
	submenu_add_collections_new(collections_menu, "win.search-win-collections");
	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-collections");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);

	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-play");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), (on_row && sd->click_fd && sd->click_fd->format_class == FORMAT_CLASS_VIDEO));

	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-view");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);

	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-view-in-new-window");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);

	const bool empty = search_result_count(sd) == 0;

	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-select-all");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !empty);
	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-select-none");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), !empty);
	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-print");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);
	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-copy");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);
	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-move");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);
	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-rename");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);

	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-copy-path");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);
	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-copy-path-unquoted");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);

	if (options->file_ops.confirm_move_to_trash)
		{
		menu_item_include_ellipsis(G_MENU_MODEL(menu_model), "win.search-win-delete");
		}
	if (options->file_ops.confirm_delete)
		{
		menu_item_include_ellipsis(G_MENU_MODEL(menu_model), "win.search-win-delete-permanent");
		}

	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-delete");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);
	action = g_action_map_lookup_action(G_ACTION_MAP(sd->ui.window), "search-win-delete-permanent");
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), on_row);

	GtkWidget *menu = parent ? popup_menu_at(menu_model, parent, x, y) : popup_menu(menu_model, sd->ui.result_view);
 	g_signal_connect_swapped(G_OBJECT(menu), "destroy", G_CALLBACK(file_data_list_free), editmenu_fd_list);
}

static GdkContentProvider *search_dnd_prepare(GtkDragSource *source, gdouble, gdouble, gpointer data)
{
	auto *sd = static_cast<SearchData *>(data);
	GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
	SearchResultRow *row = search_result_row_from_widget(widget);
	sd->click_fd = row ? row->mfd->fd : nullptr;
	guint position = GTK_INVALID_LIST_POSITION;
	GListModel *model = gtk_multi_selection_get_model(sd->ui.result_selection);
	for (guint i = 0; row && i < g_list_model_get_n_items(model); i++)
		{
		auto *candidate = static_cast<SearchResultRow *>(g_list_model_get_item(model, i));
		const gboolean match = candidate == row;
		g_object_unref(candidate);
		if (match)
			{
			position = i;
			break;
			}
		}

	if (sd->click_fd && !search_result_row_selected(sd, sd->click_fd))
		{
		if (position != GTK_INVALID_LIST_POSITION)
			{
			gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(sd->ui.result_selection));
			gtk_selection_model_select_item(GTK_SELECTION_MODEL(sd->ui.result_selection), position, TRUE);
			}
		}

	g_autoptr(FileDataList) list = search_result_selection_list(sd);
	if (!list) return nullptr;

	if (sd->click_fd)
		{
		dnd_set_drag_icon(source, sd->click_fd->thumb_pixbuf, g_list_length(list), sd->click_fd);
		}

	return dnd_file_list_content_provider(list);
}

enum class SearchDndDestination
{
	Path,
	Similarity,
	Gps
};

struct SearchDndDropData
{
	GtkWidget *entry;
	SearchDndDestination destination;
};

static void search_dnd_file_received(GdkDrop *drop, GList *list, gpointer data)
{
	auto *drop_data = static_cast<SearchDndDropData *>(data);
	auto action = GDK_ACTION_NONE;

	if (list)
		{
		auto *fd = static_cast<FileData *>(list->data);
		g_autofree gchar *text = nullptr;

		switch (drop_data->destination)
			{
			case SearchDndDestination::Path:
				text = g_strdup(fd->path);
				break;
			case SearchDndDestination::Similarity:
				text = g_strdup(fd->path);
				break;
			case SearchDndDestination::Gps:
				const gdouble latitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude", 1000);
				const gdouble longitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude", 1000);
				text = (latitude != 1000 && longitude != 1000) ?
				       g_strdup_printf("%f %f", latitude, longitude) :
				       g_strdup(_("Image is not geocoded"));
				break;
			}

		entry_set_text(GTK_ENTRY(drop_data->entry), text);
		gtk_widget_set_tooltip_text(drop_data->entry, text);
		action = GDK_ACTION_COPY;
		}

	gdk_drop_finish(drop, action);
	g_object_unref(drop_data->entry);
	g_free(drop_data);
}

static gboolean search_dnd_drop(GtkDropTargetAsync *, GdkDrop *drop, gdouble, gdouble, gpointer data)
{
	auto *drop_data = g_new(SearchDndDropData, 1);
	*drop_data = *static_cast<SearchDndDropData *>(data);
	g_object_ref(drop_data->entry);
	dnd_read_file_list_async(drop, search_dnd_file_received, drop_data);

	return TRUE;
}

static void search_dnd_drop_data_free(gpointer data, GClosure *)
{
	g_free(data);
}

static void search_dnd_add_drop_target(GtkWidget *widget, SearchDndDestination destination)
{
	GdkContentFormats *formats = dnd_file_drop_formats();
	GtkDropTargetAsync *drop_target = gtk_drop_target_async_new(formats, GDK_ACTION_COPY);
	auto *drop_data = g_new(SearchDndDropData, 1);
	drop_data->entry = widget;
	drop_data->destination = destination;
	g_signal_connect_data(drop_target, "drop", G_CALLBACK(search_dnd_drop), drop_data,
	                      search_dnd_drop_data_free, G_CONNECT_DEFAULT);
	gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(drop_target));
}

/*
 *-------------------------------------------------------------------
 * search core
 *-------------------------------------------------------------------
 */

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
	g_clear_handle_id(&sd->search_idle_id, g_source_remove);

	image_loader_free(sd->img_loader);
	sd->img_loader = nullptr;
	sd->img_cd.reset();

	sd->search_similarity_cd.reset();

	search_buffer_flush(sd);

	file_data_list_free(sd->search_folder_list);
	sd->search_folder_list = nullptr;

	g_list_free(sd->search_done_list);
	sd->search_done_list = nullptr;

	file_data_list_free(sd->search_file_list);
	sd->search_file_list = nullptr;

	sd->match_broken_enable = FALSE;

	gtk_widget_set_sensitive(sd->ui.box_search, TRUE);
	gtk_spinner_stop(GTK_SPINNER(sd->ui.spinner));
	gtk_widget_set_sensitive(sd->ui.button_start, TRUE);
	gtk_widget_set_sensitive(sd->ui.button_stop, FALSE);
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
			cd->set_dimensions({-1, -1});
			}
		}
	else if (cd && pixbuf)
		{
		if (!cd->dimensions)
			{
			cd->set_dimensions({gdk_pixbuf_get_width(pixbuf),
			                    gdk_pixbuf_get_height(pixbuf)});
			}

		if (sd->match_similarity_enable && !cd->similarity)
			{
			ImageSimilarityData sim{ pixbuf };

			cd->set_similarity(sim);
			}

		if (options->thumbnails.enable_caching &&
		    sd->img_loader && image_loader_get_fd(sd->img_loader))
			{
			const FileData *fd = image_loader_get_fd(sd->img_loader);

			cd->save(fd->path);
			}
		}

	image_loader_free(sd->img_loader);
	sd->img_loader = nullptr;

	sd->search_idle_id = g_idle_add(search_step_cb, sd);
}

static void search_file_load_done_cb(ImageLoader *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);
	search_file_load_process(sd, sd->img_cd.get());
}

static gboolean search_file_do_extra(SearchData *sd, MatchFileData &mfd, gboolean &match)
{
	gboolean tmatch = TRUE;
	gboolean tested = FALSE;

	if (!sd->img_cd)
		{
		sd->img_cd = std::make_unique<CacheData>(mfd.fd->path);

		if ((sd->match_dimensions_enable && !sd->img_cd->dimensions) ||
		    (sd->match_similarity_enable && !sd->img_cd->similarity) ||
		    sd->match_broken_enable)
			{
			sd->img_loader = image_loader_new(mfd.fd);
			g_signal_connect(G_OBJECT(sd->img_loader), "error", (GCallback)search_file_load_done_cb, sd);
			g_signal_connect(G_OBJECT(sd->img_loader), "done", (GCallback)search_file_load_done_cb, sd);
			if (image_loader_start(sd->img_loader))
				{
				return TRUE;
				}

			image_loader_free(sd->img_loader);
			sd->img_loader = nullptr;
			}
		}

	const auto &dimensions = sd->img_cd->dimensions; // prevent clang-tidy bugprone-unchecked-optional-access

	if (sd->match_broken_enable && dimensions)
		{
		tested = TRUE;
		tmatch = FALSE;
		if (sd->match_class == SEARCH_MATCH_EQUAL && dimensions->width == -1)
			{
			tmatch = TRUE;
			}
		else if (sd->match_class == SEARCH_MATCH_NONE && dimensions->width != -1)
			{
			tmatch = TRUE;
			}
		}

	if (tmatch && sd->match_dimensions_enable && dimensions)
		{
		tmatch = FALSE;
		tested = TRUE;

		if (sd->match_dimensions == SEARCH_MATCH_EQUAL)
			{
			tmatch = (dimensions.value() == sd->search_dimensions);
			}
		else if (sd->match_dimensions == SEARCH_MATCH_UNDER)
			{
			tmatch = (dimensions->width < sd->search_dimensions.width && dimensions->height < sd->search_dimensions.height);
			}
		else if (sd->match_dimensions == SEARCH_MATCH_OVER)
			{
			tmatch = (dimensions->width > sd->search_dimensions.width && dimensions->height > sd->search_dimensions.height);
			}
		else if (sd->match_dimensions == SEARCH_MATCH_BETWEEN)
			{
			tmatch = match_is_between(dimensions->width, sd->search_dimensions.width, sd->search_dimensions_end.width) &&
			         match_is_between(dimensions->height, sd->search_dimensions.height, sd->search_dimensions_end.height);
			}
		}

	if (tmatch && sd->match_similarity_enable && sd->img_cd->similarity)
		{
		tmatch = FALSE;
		tested = TRUE;

		/** @FIXME implement similarity checking */
		if (sd->search_similarity_cd && sd->search_similarity_cd->similarity)
			{
			gdouble result;

			result = image_sim_compare_fast(sd->search_similarity_cd->similarity.get(), sd->img_cd->similarity.get(),
			                                static_cast<gdouble>(sd->search_similarity) / 100.0);
			result *= 100.0;
			if (result >= static_cast<gdouble>(sd->search_similarity))
				{
				tmatch = TRUE;
				mfd.rank = static_cast<gint>(result);
				}
			}
		}

	if (dimensions)
		{
		mfd.dimensions = dimensions.value();
		}

	sd->img_cd.reset();

	match = (tmatch && tested);

	return FALSE;
}

static gboolean search_file_next(SearchData *sd)
{
	FileData *fd;
	gboolean match = TRUE;
	gboolean tested = FALSE;
	gboolean extra_only = FALSE;

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

		if (!sd->search_name_symbolic_link || (sd->search_name_symbolic_link && islink(fd->path)))
			{
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
					g_autofree gchar *haystack = g_utf8_strdown(fd_name_or_path, -1);
					match = g_regex_match(sd->search_name_regex, haystack, static_cast<GRegexMatchFlags>(0), nullptr);
					}
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
			match = match_is_between(fd->size, sd->search_size, sd->search_size_end);
			}
		}

	if (match && sd->match_date_enable)
		{
		tested = TRUE;
		match = FALSE;

		constexpr time_t seconds_per_day = 60 * 60 * 24;
		const time_t file_date = sd->get_file_date(fd);

		if (sd->match_date == SEARCH_MATCH_EQUAL)
			{
			struct tm *lt;

			lt = localtime(&file_date);
			match = (lt && sd->search_date.is_equal(lt));
			}
		else if (sd->match_date == SEARCH_MATCH_UNDER)
			{
			match = (file_date < sd->search_date.to_time());
			}
		else if (sd->match_date == SEARCH_MATCH_OVER)
			{
			match = (file_date > sd->search_date.to_time() + seconds_per_day - 1);
			}
		else if (sd->match_date == SEARCH_MATCH_BETWEEN)
			{
			time_t a = sd->search_date.to_time();
			time_t b = sd->search_date_end.to_time();

			std::tie(a, b) = std::minmax(a, b); // @TODO Use structured binding in C++17
			match = match_is_between(file_date, a, b + seconds_per_day - 1);
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
			GList *needle = sd->search_keyword_list;

			if (sd->match_keywords == SEARCH_MATCH_ALL)
				{
				gboolean found = TRUE;

				while (needle && found)
					{
					found = (g_list_find_custom(list, needle->data,
					                            reinterpret_cast<GCompareFunc>(g_ascii_strcasecmp)) != nullptr);
					needle = needle->next;
					}

				match = found;
				}
			else if (sd->match_keywords == SEARCH_MATCH_ANY)
				{
				gboolean found = FALSE;

				while (needle && !found)
					{
					found = (g_list_find_custom(list, needle->data,
					                            reinterpret_cast<GCompareFunc>(g_ascii_strcasecmp)) != nullptr);
					needle = needle->next;
					}

				match = found;
				}
			else if (sd->match_keywords == SEARCH_MATCH_NONE)
				{
				gboolean found = FALSE;

				while (needle && !found)
					{
					found = (g_list_find_custom(list, needle->data,
					                            reinterpret_cast<GCompareFunc>(g_ascii_strcasecmp)) != nullptr);
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

	if (match && sd->match_comment_enable && sd->search_comment && sd->search_comment[0] != '\0')
		{
		tested = TRUE;
		match = FALSE;

		g_autofree gchar *comment = metadata_read_string(fd, COMMENT_KEY, METADATA_PLAIN);

		if (comment)
			{
			if (!sd->search_comment_match_case)
				{
				g_autofree gchar *tmp = g_utf8_strdown(comment, -1);
				std::swap(comment, tmp);
				}

			if (sd->match_comment == SEARCH_MATCH_CONTAINS)
				{
				match = g_regex_match(sd->search_comment_regex, comment, static_cast<GRegexMatchFlags>(0), nullptr);
				}
			else if (sd->match_comment == SEARCH_MATCH_NONE)
				{
				match = !g_regex_match(sd->search_comment_regex, comment, static_cast<GRegexMatchFlags>(0), nullptr);
				}
			}
		else
			{
			match = (sd->match_comment == SEARCH_MATCH_NONE);
			}
		}

	if (match && sd->match_exif_enable && sd->search_exif_tag && sd->search_exif_tag[0] != '\0')
		{
		tested = TRUE;
		match = FALSE;

		g_autofree gchar *exif_tag_result = metadata_read_string(fd, sd->search_exif_tag, METADATA_FORMATTED);

		if (exif_tag_result)
			{
			if (!sd->search_exif_match_case)
				{
				g_autofree gchar *tmp = g_utf8_strdown(exif_tag_result, -1);
				std::swap(exif_tag_result, tmp);
				}

			if (sd->match_exif == SEARCH_MATCH_CONTAINS)
				{
				match = g_regex_match(sd->search_exif_regex, exif_tag_result, static_cast<GRegexMatchFlags>(0), nullptr);
				}
			else if (sd->match_exif == SEARCH_MATCH_NONE)
				{
				match = !g_regex_match(sd->search_exif_regex, exif_tag_result, static_cast<GRegexMatchFlags>(0), nullptr);
				}
			}
		else
			{
			match = (sd->match_exif == SEARCH_MATCH_NONE);
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
			match = match_is_between(rating, sd->search_rating, sd->search_rating_end);
			}
		}

	if (match && sd->match_class_enable)
		{
		tested = TRUE;
		match = FALSE;

		if (sd->search_class != FORMAT_CLASS_BROKEN)
			{
			match = (sd->match_class == SEARCH_MATCH_EQUAL && fd->format_class == sd->search_class) ||
			        (sd->match_class == SEARCH_MATCH_NONE && fd->format_class != sd->search_class);
			}
		else
			{
			match = sd->match_broken_enable = fd->format_class == FORMAT_CLASS_IMAGE || fd->format_class == FORMAT_CLASS_RAWIMAGE ||
			                                  fd->format_class == FORMAT_CLASS_VIDEO || fd->format_class == FORMAT_CLASS_DOCUMENT;
			}
		}

	if (match && sd->match_marks_enable)
		{
		tested = TRUE;
		match = FALSE;

		if (sd->match_marks == SEARCH_MATCH_EQUAL)
			{
			match = (fd->marks & sd->search_marks);
			}
		else
			{
			if (sd->search_marks == -1)
				{
				match = fd->marks ? FALSE : TRUE;
				}
			else
				{
				match = (fd->marks & sd->search_marks) ? FALSE : TRUE;
				}
			}
		}

	if (match && sd->match_gps_enable)
		{
		/* Calculate the distance the image is from the specified origin.
		* This is a standard algorithm. A simplified one may be faster.
		*/
		tested = TRUE;
		match = FALSE;

		const gdouble latitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLatitude", 1000);
		const gdouble longitude = metadata_read_GPS_coord(fd, "Xmp.exif.GPSLongitude", 1000);
		const bool image_has_gps = (latitude != 1000 && longitude != 1000);

		if (sd->match_gps == SEARCH_MATCH_NONE)
			{
			match = !image_has_gps;
			}
		else if (image_has_gps)
			{
			const gdouble range = get_gps_range(sd, latitude, longitude);
			match = (sd->match_gps == SEARCH_MATCH_UNDER && range <= sd->search_gps) ||
			        (sd->match_gps == SEARCH_MATCH_OVER && range > sd->search_gps);
			}
		}

	MatchFileData mfd_extra{ fd, {0, 0}, 0 };
	if ((match || extra_only) && (sd->match_dimensions_enable || sd->match_similarity_enable || sd->match_broken_enable))
		{
		tested = TRUE;

		if (search_file_do_extra(sd, mfd_extra, match))
			{
			sd->search_buffer_count += SEARCH_BUFFER_MATCH_LOAD;
			return TRUE;
			}
		}

	sd->search_file_list = g_list_remove(sd->search_file_list, fd);

	if (tested && match)
		{
		auto mfd = g_new(MatchFileData, 1);
		*mfd = mfd_extra;

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

	if (!sd->search_folder_list)
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

					fdp = static_cast<FileData *>(work->data);
					link = work;
					work = work->next;

					g_autofree gchar *meta_path = cache_find_location(CacheType::METADATA, fdp->path);
					if (!meta_path)
						{
						list = g_list_delete_link(list, link);
						file_data_unref(fdp);
						}
					}
				}
			}

		if (success)
			{
			list = filelist_sort(list, {SORT_NAME, TRUE, TRUE});
			sd->search_file_list = list;

			if (sd->search_path_recurse)
				{
				dlist = filelist_sort(dlist, {SORT_NAME, TRUE, TRUE});
				sd->search_folder_list = g_list_concat(dlist, sd->search_folder_list);
				}
			else
				{
				file_data_list_free(dlist);
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
	search_file_load_process(sd, sd->search_similarity_cd.get());
}

static GRegex *create_search_regex(const gchar *pattern)
{
	g_autoptr(GError) error = nullptr;
	GRegex *regex = g_regex_new(pattern, static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), &error);
	if (error)
		{
		log_printf("Error: could not compile regular expression %s\n%s\n", pattern, error->message);
		regex = g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
		}

	return regex;
}

static void search_start(SearchData *sd)
{
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

	if (!sd->search_exif_match_case)
		{
		/* convert to lowercase here, so that this is only done once per search */
		gchar *tmp = g_utf8_strdown(sd->search_exif_value, -1);
		g_free(sd->search_exif_value);
		sd->search_exif_value = tmp;
		}

	if(sd->search_name_regex)
		{
		g_regex_unref(sd->search_name_regex);
		}
	sd->search_name_regex = create_search_regex(sd->search_name);

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
	sd->search_comment_regex = create_search_regex(sd->search_comment);

	if(sd->search_exif_regex)
		{
		g_regex_unref(sd->search_exif_regex);
		}
	sd->search_exif_regex = create_search_regex(sd->search_exif_value);

	sd->search_count = 0;
	sd->search_total = 0;

	gtk_widget_set_sensitive(sd->ui.box_search, FALSE);
	gtk_spinner_start(GTK_SPINNER(sd->ui.spinner));
	gtk_widget_set_sensitive(sd->ui.button_start, FALSE);
	gtk_widget_set_sensitive(sd->ui.button_stop, TRUE);
	search_progress_update(sd, TRUE, -1.0);

	if (sd->match_similarity_enable &&
	    !sd->search_similarity_cd &&
	    isfile(sd->search_similarity_path))
		{
		sd->search_similarity_cd = std::make_unique<CacheData>(sd->search_similarity_path);

		if (!sd->search_similarity_cd->similarity)
			{
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

static void search_start_do(SearchData *sd)
{
	if (sd->search_folder_list)
		{
		search_stop(sd);
		search_result_thumb_step(sd);
		return;
		}

	if (sd->match_name_enable)
		{
		menu_choice_get_match_type(sd->ui.menu_name, sd->match_name);

		history_combo_append_history(sd->ui.entry_name, nullptr);

		g_free(sd->search_name);
		sd->search_name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(sd->ui.entry_name)));
		}

	/* XXX */
	if (sd->match_comment_enable)
		{
		menu_choice_get_match_type(sd->ui.menu_comment, sd->match_comment);

		g_free(sd->search_comment);
		sd->search_comment = g_strdup(gtk_editable_get_text(GTK_EDITABLE(sd->ui.entry_comment)));
		}

	if (sd->match_exif_enable)
		{
		menu_choice_get_match_type(sd->ui.menu_exif, sd->match_exif);

		g_free(sd->search_exif_tag);
		sd->search_exif_tag = g_strdup(gtk_editable_get_text(GTK_EDITABLE(sd->ui.entry_exif_tag)));

		g_free(sd->search_exif_value);
		sd->search_exif_value = g_strdup(gtk_editable_get_text(GTK_EDITABLE(sd->ui.entry_exif_value)));
		}

	g_free(sd->search_similarity_path);
	sd->search_similarity_path = g_strdup(gtk_editable_get_text(GTK_EDITABLE(sd->ui.entry_similarity)));
	if (sd->match_similarity_enable)
		{
		if (!isfile(sd->search_similarity_path))
			{
			file_util_warning_dialog(_("File not found"),
			                         _("Please enter an existing file for image content."),
			                         GQ_ICON_DIALOG_WARNING, sd->ui.window);
			return;
			}
		tab_completion_append_to_history(sd->ui.entry_similarity, sd->search_similarity_path);
		}

	/* Check the coordinate entry.
	* If the result is not sensible, it should get blocked.
	*/
	if (sd->match_gps_enable && sd->match_gps != SEARCH_MATCH_NONE)
		{
		g_autofree gchar *entry_text = decode_geo_parameters(gtk_editable_get_text(GTK_EDITABLE(sd->ui.entry_gps_coord)));

		sd->search_lat = 1000;
		sd->search_lon = 1000;
		sscanf(entry_text, " %lf  %lf ", &sd->search_lat, &sd->search_lon);
		if (entry_text == nullptr || g_strstr_len(entry_text, -1, "Error") ||
		    sd->search_lat < -90 || sd->search_lat > 90 ||
		    sd->search_lon < -180 || sd->search_lon > 180)
			{
			file_util_warning_dialog(_("Entry does not contain a valid lat/long value"), entry_text, GQ_ICON_DIALOG_WARNING, sd->ui.window);
			return;
			}

		GObject *item = G_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(sd->ui.units_gps)));
		const char *units_gps = item ? gtk_string_object_get_string(GTK_STRING_OBJECT(item)) : nullptr;

		if (g_strcmp0(units_gps, _("km")) == 0)
			{
			constexpr gdouble KM_EARTH_RADIUS = 6371;
			sd->search_earth_radius = KM_EARTH_RADIUS;
			}
		else if (g_strcmp0(units_gps, _("miles")) == 0)
			{
			constexpr gdouble MILES_EARTH_RADIUS = 3959;
			sd->search_earth_radius = MILES_EARTH_RADIUS;
			}
		else
			{
			constexpr gdouble NAUTICAL_MILES_EARTH_RADIUS = 3440;
			sd->search_earth_radius = NAUTICAL_MILES_EARTH_RADIUS;
			}
		}

	if (sd->match_keywords_enable)
		{
		menu_choice_get_match_type(sd->ui.menu_keywords, sd->match_keywords);

		g_list_free_full(sd->search_keyword_list, g_free);
		sd->search_keyword_list = keyword_list_pull(sd->ui.entry_keywords);
		}

	if (sd->match_date_enable)
		{
		GObject *item = G_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(sd->ui.date_type)));
		const char *date_type = item ? gtk_string_object_get_string(GTK_STRING_OBJECT(item)) : nullptr;
		const auto it = std::find_if(std::cbegin(search_date_types), std::cend(search_date_types),
		                             [date_type](const SearchDateType &sdt){ return g_strcmp0(date_type, sdt.name) == 0; });
		if (it != std::cend(search_date_types))
			sd->get_file_date = it->get_file_date;
		else
			sd->get_file_date = [](FileData *fd){ return fd->date; };

		sd->search_date.set_date(sd->ui.date_sel);
		sd->search_date_end.set_date(sd->ui.date_sel_end);
		}

	if (sd->match_class_enable)
		{
		menu_choice_get_match_type(sd->ui.menu_class, sd->match_class);

		GObject *item = G_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(sd->ui.class_type)));
		const char *class_type = item ? gtk_string_object_get_string(GTK_STRING_OBJECT(item)) : nullptr;

		if (g_strcmp0(class_type, _("Image")) == 0)
			{
			sd->search_class = FORMAT_CLASS_IMAGE;
			}
		else if (g_strcmp0(class_type, _("Raw Image")) == 0)
			{
			sd->search_class = FORMAT_CLASS_RAWIMAGE;
			}
		else if (g_strcmp0(class_type, _("Video")) == 0)
			{
			sd->search_class = FORMAT_CLASS_VIDEO;
			}
		else if (g_strcmp0(class_type, _("Document")) == 0)
			{
			sd->search_class = FORMAT_CLASS_DOCUMENT;
			}
		else if (g_strcmp0(class_type, _("Metadata")) == 0)
			{
			sd->search_class = FORMAT_CLASS_META;
			}
		else if (g_strcmp0(class_type, _("Archive")) == 0)
			{
			sd->search_class = FORMAT_CLASS_ARCHIVE;
			}
		else if (g_strcmp0(class_type, _("Unknown")) == 0)
			{
			sd->search_class = FORMAT_CLASS_UNKNOWN;
			}
		else
			{
			sd->search_class = FORMAT_CLASS_BROKEN;
			}
		}

	if (sd->match_marks_enable)
		{
		menu_choice_get_match_type(sd->ui.menu_marks, sd->match_marks);

		sd->search_marks = -1;

		GObject *item = G_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(sd->ui.marks_type)));
		const char *marks_type = item ? gtk_string_object_get_string(GTK_STRING_OBJECT(item)) : nullptr;

		if (g_strcmp0(marks_type, _("Any mark")) != 0)
			{
			for (gint i = 0; i < FILEDATA_MARKS_SIZE; i++)
				{
				g_autoptr(GString) marks_string = get_marks_string(i);

				if (g_strcmp0(marks_type, marks_string->str) == 0)
					{
					sd->search_marks = 1 << i;
					}
				}
			}
		}

	gtk_column_view_column_set_visible(sd->ui.result_columns[SEARCH_COLUMN_DIMENSIONS], sd->match_dimensions_enable);

	gtk_column_view_column_set_visible(sd->ui.result_columns[SEARCH_COLUMN_RANK], sd->match_similarity_enable);
	if (!sd->match_similarity_enable)
		{
		auto *sorter = GTK_COLUMN_VIEW_SORTER(gtk_column_view_get_sorter(GTK_COLUMN_VIEW(sd->ui.result_view)));
		if (gtk_column_view_sorter_get_primary_sort_column(sorter) == sd->ui.result_columns[SEARCH_COLUMN_RANK])
			{
			gtk_column_view_sort_by_column(GTK_COLUMN_VIEW(sd->ui.result_view),
			                               sd->ui.result_columns[SEARCH_COLUMN_PATH], GTK_SORT_ASCENDING);
			}
		}

	if (sd->search_type == SEARCH_MATCH_NONE)
		{
		/* search path */
		g_autofree gchar *path = remove_trailing_slash(gtk_editable_get_text(GTK_EDITABLE(sd->ui.path_entry)));
		if (isdir(path))
			{
			file_data_unref(sd->search_dir_fd);
			sd->search_dir_fd = file_data_new_dir(path);

			tab_completion_append_to_history(sd->ui.path_entry, sd->search_dir_fd->path);

			search_start(sd);
			}
		else
			{
			file_util_warning_dialog(_("Folder not found"),
			                         _("Please enter an existing folder to search."),
			                         GQ_ICON_DIALOG_WARNING, sd->ui.window);
			}
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
		const char *collection = gtk_editable_get_text(GTK_EDITABLE(sd->ui.entry_collection));

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
			file_util_warning_dialog(_("Collection not found"), _("Please enter an existing collection name."), GQ_ICON_DIALOG_WARNING, sd->ui.window);
			}
		}
}

/*
 *-------------------------------------------------------------------
 * window construct
 *-------------------------------------------------------------------
 */

static void search_thumb_toggle_cb(GtkWidget *button, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_result_thumb_enable(sd, gtk_check_button_get_active(GTK_CHECK_BUTTON(button)));
}

static gint search_result_sort_cb(gconstpointer item_a, gconstpointer item_b, gpointer data)
{
	gint n = GPOINTER_TO_INT(data);
	auto *fda = reinterpret_cast<const SearchResultRow *>(item_a)->mfd;
	auto *fdb = reinterpret_cast<const SearchResultRow *>(item_b)->mfd;

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
			return fda->dimensions.area() - fdb->dimensions.area();
		case SEARCH_COLUMN_PATH:
			return utf8_compare(fda->fd->path, fdb->fd->path, TRUE);
			break;
		default:
			break;
		}

	return 0;
}

static void search_result_factory_setup(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer data)
{
	auto *column_data = static_cast<std::pair<SearchData *, gint> *>(data);
	const gint column = column_data->second;
	gtk_list_item_set_activatable(list_item, FALSE);
	GtkWidget *child;
	if (column == SEARCH_COLUMN_THUMB)
		{
		child = gtk_picture_new();
		gtk_picture_set_can_shrink(GTK_PICTURE(child), TRUE);
		}
	else
		{
		child = gtk_label_new(nullptr);
		gtk_label_set_xalign(GTK_LABEL(child),
		                     (column == SEARCH_COLUMN_SIZE || column == SEARCH_COLUMN_DATE) ? 1.0 : 0.0);
		}
	gtk_widget_set_margin_start(child, 4);
	gtk_widget_set_margin_end(child, 4);
	g_object_set_data(G_OBJECT(child), "search-result-column", GINT_TO_POINTER(column));
	GtkDragSource *drag_source = gtk_drag_source_new();
	gtk_drag_source_set_actions(drag_source, static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag_source), 0);
	g_signal_connect(drag_source, "prepare", G_CALLBACK(search_dnd_prepare), column_data->first);
	gtk_widget_add_controller(child, GTK_EVENT_CONTROLLER(drag_source));
	gtk_list_item_set_child(list_item, child);
}

static void search_result_cell_update(SearchResultRow *row, GtkWidget *child)
{
	const gint column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(child), "search-result-column"));

	if (column == SEARCH_COLUMN_THUMB)
		{
		gtk_widget_set_size_request(child, options->thumbnails.size.width,
		                            options->thumbnails.size.height);
		g_autoptr(GdkTexture) texture = row->thumb ? pixbuf_to_texture(row->thumb) : nullptr;
		gtk_picture_set_paintable(GTK_PICTURE(child), GDK_PAINTABLE(texture));
		return;
		}

	g_autofree gchar *allocated_text = nullptr;
	const gchar *text = "";
	switch (column)
		{
		case SEARCH_COLUMN_RANK:
			allocated_text = g_strdup_printf("%d", row->mfd->rank);
			text = allocated_text;
			break;
		case SEARCH_COLUMN_NAME: text = row->mfd->fd->name; break;
		case SEARCH_COLUMN_SIZE:
			allocated_text = text_from_size(row->mfd->fd->size);
			text = allocated_text;
			break;
		case SEARCH_COLUMN_DATE: text = text_from_time(row->mfd->fd->date); break;
		case SEARCH_COLUMN_DIMENSIONS:
			if (row->mfd->dimensions.width > 0 && row->mfd->dimensions.height > 0)
				{
				allocated_text = g_strdup_printf("%d x %d", row->mfd->dimensions.width, row->mfd->dimensions.height);
				}
			text = allocated_text;
			break;
		case SEARCH_COLUMN_PATH: text = row->mfd->fd->path; break;
		default: g_assert_not_reached();
		}
	gtk_label_set_text(GTK_LABEL(child), text ? text : "");
}

static void search_result_factory_bind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	auto *row = static_cast<SearchResultRow *>(gtk_list_item_get_item(list_item));
	GtkWidget *child = gtk_list_item_get_child(list_item);
	g_object_set_data(G_OBJECT(child), "search-result-row", row);
	const gulong handler_id = g_signal_connect(row, "changed", G_CALLBACK(search_result_cell_update), child);
	g_object_set_data(G_OBJECT(list_item), "search-result-changed-handler", GSIZE_TO_POINTER(handler_id));
	search_result_cell_update(row, child);
}

static void search_result_factory_unbind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	auto *row = static_cast<SearchResultRow *>(gtk_list_item_get_item(list_item));
	GtkWidget *child = gtk_list_item_get_child(list_item);
	const gulong handler_id = GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(list_item), "search-result-changed-handler"));
	if (row && handler_id) g_signal_handler_disconnect(row, handler_id);
	g_object_set_data(G_OBJECT(list_item), "search-result-changed-handler", nullptr);
	g_object_set_data(G_OBJECT(child), "search-result-row", nullptr);
}

static void search_result_add_column(SearchData *sd, gint n, const gchar *title, gboolean image, gboolean)
{
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	auto *column_data = new std::pair<SearchData *, gint>(sd, n);
	g_object_set_data_full(G_OBJECT(factory), "search-result-column-data", column_data,
	                       [](gpointer data){ delete static_cast<std::pair<SearchData *, gint> *>(data); });
	g_signal_connect(factory, "setup", G_CALLBACK(search_result_factory_setup), column_data);
	g_signal_connect(factory, "bind", G_CALLBACK(search_result_factory_bind), GINT_TO_POINTER(n));
	g_signal_connect(factory, "unbind", G_CALLBACK(search_result_factory_unbind), nullptr);

	GtkColumnViewColumn *column = gtk_column_view_column_new(title, factory);
	gtk_column_view_column_set_resizable(column, !image);
	gtk_column_view_column_set_fixed_width(column, image ? 4 : -1);
	if (!image)
		{
		GtkSorter *sorter = GTK_SORTER(gtk_custom_sorter_new(search_result_sort_cb, GINT_TO_POINTER(n), nullptr));
		gtk_column_view_column_set_sorter(column, sorter);
		g_object_unref(sorter);
		}
	sd->ui.result_columns[n] = column;
	gtk_column_view_append_column(GTK_COLUMN_VIEW(sd->ui.result_view), column);
	g_object_unref(column);
}

static void menu_choice_path_cb(GtkWidget *drop_down, GParamSpec *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(drop_down, sd->search_type)) return;

	gtk_widget_set_visible(gtk_widget_get_parent(sd->ui.check_recurse),
	                       sd->search_type == SEARCH_MATCH_NONE);
	gtk_widget_set_visible(sd->ui.box_collection, sd->search_type == SEARCH_MATCH_COLLECTION);
}

static void menu_choice_size_cb(GtkWidget *drop_down, GParamSpec *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(drop_down, sd->match_size)) return;

	gtk_widget_set_visible(gtk_widget_get_parent(sd->ui.spin_size_end),
	                       sd->match_size == SEARCH_MATCH_BETWEEN);
}

static void menu_choice_rating_cb(GtkWidget *drop_down, GParamSpec *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(drop_down, sd->match_rating)) return;

	gtk_widget_set_visible(gtk_widget_get_parent(sd->ui.spin_rating_end),
	                       sd->match_rating == SEARCH_MATCH_BETWEEN);
}

static void menu_choice_date_cb(GtkWidget *drop_down, GParamSpec *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(drop_down, sd->match_date)) return;

	gtk_widget_set_visible(gtk_widget_get_parent(sd->ui.date_sel_end),
	                       sd->match_date == SEARCH_MATCH_BETWEEN);
}

static void menu_choice_dimensions_cb(GtkWidget *drop_down, GParamSpec *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(drop_down, sd->match_dimensions)) return;

	gtk_widget_set_visible(sd->ui.box_dimensions_end,
	                       sd->match_dimensions == SEARCH_MATCH_BETWEEN);
}

static void menu_choice_spin_cb(GtkAdjustment *adjustment, gpointer data)
{
	auto value = static_cast<gint *>(data);

	*value = static_cast<gint>(gtk_adjustment_get_value(adjustment));
}

static void menu_choice_gps_cb(GtkWidget *drop_down, GParamSpec *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (!menu_choice_get_match_type(drop_down, sd->match_gps)) return;

	gtk_widget_set_visible(gtk_widget_get_parent(sd->ui.spin_gps),
	                       sd->match_gps != SEARCH_MATCH_NONE);
}

static GtkWidget *menu_spin(GtkWidget *box, gdouble min, gdouble max, gpointer data)
{
	GtkWidget *spin = gtk_spin_button_new_with_range(min, max, 1);
	const auto *value = static_cast<const gint *>(data);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), static_cast<gdouble>(*value));

	GtkAdjustment *adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin));
	g_signal_connect(G_OBJECT(adj), "value_changed",
	                 G_CALLBACK(menu_choice_spin_cb), data);

	gtk_box_append(GTK_BOX(box), spin);

	return spin;
}

static void menu_dimensions_spin(GtkWidget *box, GqSize &dimensions)
{
	constexpr std::size_t dimension_max = 1000000;

	menu_spin(box, 0, dimension_max, &dimensions.width);
	pref_label_new(box, "x");
	menu_spin(box, 0, dimension_max, &dimensions.height);
}

static void menu_choice_check_cb(GtkWidget *button, gpointer data)
{
	auto widget = static_cast<GtkWidget *>(data);
	gboolean active;
	gboolean *value;

	active = gtk_check_button_get_active(GTK_CHECK_BUTTON(button));
	gtk_widget_set_sensitive(widget, active);

	value = static_cast<gboolean *>(g_object_get_data(G_OBJECT(button), "check_var"));
	if (value) *value = active;
}

template<size_t N>
static GtkWidget *menu_choice_menu(GtkWidget *box, const std::array<MatchList, N> &items,
                                   GCallback selected_cb, gpointer data)
{
	GtkStringList *string_list = gtk_string_list_new(nullptr);
	guint index = 0;
	for (const MatchList &item : items)
		{
		gtk_string_list_append(string_list, _(item.text));

		g_autoptr(GObject) obj_item = G_OBJECT(g_list_model_get_item(G_LIST_MODEL(string_list), index++));
		g_object_set_data(obj_item, MATCH_TYPE_KEY, GINT_TO_POINTER(item.type));
		}

	GtkWidget *drop_down = gtk_drop_down_new(G_LIST_MODEL(string_list), nullptr);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(drop_down), 0);
	gtk_box_append(GTK_BOX(box), drop_down);

	if (selected_cb) g_signal_connect(G_OBJECT(drop_down), "notify::selected", selected_cb, data);

	return drop_down;
}

static GtkWidget *menu_choice(GtkWidget *box, const gchar *text, gboolean *value,
                              GtkWidget **check = nullptr)
{
	GtkWidget *base_box;
	GtkWidget *hbox;
	GtkWidget *button;

	base_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	gtk_box_append(GTK_BOX(box), base_box);

	button = gtk_check_button_new();
	if (value) gtk_check_button_set_active(GTK_CHECK_BUTTON(button), *value);
	gtk_box_append(GTK_BOX(base_box), button);
	if (check) *check = button;
	if (value) g_object_set_data(G_OBJECT(button), "check_var", value);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_widget_set_hexpand(hbox, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(base_box))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(hbox, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(base_box))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(base_box), hbox);

	g_signal_connect(G_OBJECT(button), "toggled",
			 G_CALLBACK(menu_choice_check_cb), hbox);
	gtk_widget_set_sensitive(hbox, (value) ? *value : FALSE);

	pref_label_new(hbox, text);

	return hbox;
}

static void search_window_get_geometry(SearchData *sd)
{
	LayoutWindow *lw = get_current_layout();
	if (!sd || !lw) return;

	lw->options.search_window = widget_get_position_geometry(sd->ui.window);
}

static void search_window_close_cb(GSimpleAction *, GVariant *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_window_get_geometry(sd);

	gtk_window_destroy(GTK_WINDOW(sd->ui.window));
}

static void search_window_close_button_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_window_get_geometry(sd);

	gtk_window_destroy(GTK_WINDOW(sd->ui.window));
}

static void search_window_help_button_cb(GtkWidget *, gpointer)
{
	help_window_show("GuideImageSearchSearch.html");
}

static void search_window_help_action_cb(GSimpleAction *, GVariant *, gpointer)
{
	help_window_show("GuideImageSearchSearch.html");
}

static gboolean search_window_delete_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	search_window_get_geometry(sd);

	gtk_window_destroy(GTK_WINDOW(sd->ui.window));

	return TRUE;
}

static void search_window_destroy_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	g_clear_handle_id(&sd->update_idle_id, g_source_remove);

	static const auto mfd_fd_unref = [](gpointer data)
	{
		file_data_unref(static_cast<MatchFileData *>(data)->fd);
	};
	g_list_free_full(sd->search_buffer_list, mfd_fd_unref);
	sd->search_buffer_list = nullptr;

	search_stop(sd);
	search_result_clear(sd);

	g_idle_remove_by_data(sd);

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
	g_clear_object(&sd->ui.result_selection);
	g_clear_object(&sd->ui.result_store);

	delete sd;
}

static void select_collection_response_cb(GFile *file, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	if (file)
		{
		g_autofree gchar *filename = g_file_get_path(file);
		g_autofree gchar *path_noext = remove_extension_from_path(filename);
		g_autofree gchar *collection = g_path_get_basename(path_noext);

		entry_set_text(GTK_ENTRY(sd->ui.entry_collection), collection);

		g_autoptr(GFile) parent = g_file_get_parent(file);

		if (parent != nullptr)
			{
			g_autofree gchar *dirname = g_file_get_path(parent);
			history_list_add_to_key("search_collection", dirname, -1);
			}
		}
}

static void select_collection_clicked_cb(GtkWidget *, gpointer data)
{
	auto sd = static_cast<SearchData *>(data);

	FileDialogData fdd{};

	fdd.accept_text = _("Open");
	fdd.action = FileDialogAction::OPEN;
	fdd.callback = select_collection_response_cb;
	fdd.data = sd;
	fdd.filename = get_collections_dir();
	fdd.filter = GQ_COLLECTION_EXT;
	fdd.filter_description = _("Collection files");
	fdd.history_key = "open_collection";
	fdd.title = _("Select collection");

	file_dialog_show(fdd);
}

/* static const ActionDef search_actions[]
 */
#include "search-actions.inc"

void search_new(FileData *dir_fd, FileData *example_file)
{
	GtkWidget *vbox;
	GtkWidget *hbox2;
	GtkWidget *pad_box;
	GtkWidget *frame;
	GtkWidget *scrolled;

	auto *sd = new SearchData();

	sd->search_dir_fd = file_data_ref(dir_fd);
	sd->search_path_recurse = TRUE;
	sd->search_size = 0;
	sd->search_dimensions = { 640, 480 };
	sd->search_dimensions_end = { 1024, 768 };

	sd->search_type = SEARCH_MATCH_NONE;

	sd->match_name = SEARCH_MATCH_NAME_CONTAINS;
	sd->match_size = SEARCH_MATCH_EQUAL;
	sd->match_date = SEARCH_MATCH_EQUAL;
	sd->match_dimensions = SEARCH_MATCH_EQUAL;
	sd->match_keywords = SEARCH_MATCH_ALL;
	sd->match_comment = SEARCH_MATCH_CONTAINS;
	sd->match_exif = SEARCH_MATCH_CONTAINS;
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

	sd->ui.window = window_new("search", nullptr, _("Image search"));
	DEBUG_NAME(sd->ui.window);

	gtk_window_set_resizable(GTK_WINDOW(sd->ui.window), TRUE);

	gtk_widget_set_size_request(sd->ui.window, DEFAULT_MINIMAL_WINDOW_SIZE, DEFAULT_MINIMAL_WINDOW_SIZE);
	gtk_window_set_default_size(GTK_WINDOW(sd->ui.window), DEF_SEARCH_WIDTH, DEF_SEARCH_HEIGHT);

	LayoutWindow *lw = get_current_layout();
	if (lw && options->save_window_positions)
		{
		gtk_window_set_default_size(GTK_WINDOW(sd->ui.window), lw->options.search_window.width, lw->options.search_window.height);
		}
	else
		{
		gtk_window_set_default_size(GTK_WINDOW(sd->ui.window), DEF_SEARCH_WIDTH, DEF_SEARCH_HEIGHT);
		}

	g_signal_connect(G_OBJECT(sd->ui.window), "close-request",
	                 G_CALLBACK(search_window_delete_cb), sd);
	g_signal_connect(G_OBJECT(sd->ui.window), "destroy",
	                 G_CALLBACK(search_window_destroy_cb), sd);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_widget_set_margin_top(vbox, PREF_PAD_GAP);
	gtk_widget_set_margin_bottom(vbox, PREF_PAD_GAP);
	gtk_widget_set_margin_start(vbox, PREF_PAD_GAP);
	gtk_widget_set_margin_end(vbox, PREF_PAD_GAP);
	gtk_window_set_child(GTK_WINDOW(sd->ui.window), vbox);

	sd->ui.box_search = pref_box_new(vbox, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	GtkWidget *hbox = pref_box_new(sd->ui.box_search, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	pref_label_new(hbox, _("Search:"));

	sd->ui.menu_path = menu_choice_menu(hbox, text_search_menu_path,
	                                    G_CALLBACK(menu_choice_path_cb), sd);

	hbox2 = pref_box_new(hbox, TRUE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	sd->ui.path_entry = tab_completion_new_with_history(hbox2, sd->search_dir_fd->path,
	                                                    "search_path", -1);

	search_entry_attach_focus_controller(sd->ui.path_entry, sd);

	tab_completion_add_select_button(sd->ui.path_entry, nullptr, TRUE, nullptr, nullptr, nullptr);
	sd->ui.check_recurse = pref_checkbox_new_int(hbox2, _("Recurse"),
	                                             sd->search_path_recurse, &sd->search_path_recurse);

	sd->ui.box_collection = pref_box_new(hbox, TRUE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	sd->ui.entry_collection = gtk_entry_new();
	entry_set_text(GTK_ENTRY(sd->ui.entry_collection), "");
	gtk_widget_set_hexpand(sd->ui.entry_collection, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(sd->ui.box_collection))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(sd->ui.entry_collection, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(sd->ui.box_collection))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(sd->ui.box_collection), sd->ui.entry_collection);

	GtkWidget *button_fd = gtk_button_new_with_label("…");
	g_signal_connect(G_OBJECT(button_fd), "clicked", G_CALLBACK(select_collection_clicked_cb), sd);
	gtk_box_append(GTK_BOX(sd->ui.box_collection), button_fd);

	gtk_widget_set_visible(sd->ui.box_collection, FALSE);

	/* Search for file name */
	hbox = menu_choice(sd->ui.box_search, _("File"), &sd->match_name_enable);
	sd->ui.menu_name = menu_choice_menu(hbox, text_search_menu_name,
	                                    nullptr, nullptr);
	GtkWidget *combo = history_combo_new(&sd->ui.entry_name, "", "search_name", -1);
	gtk_widget_set_hexpand(combo, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(combo, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(hbox), combo);
	pref_checkbox_new_int(hbox, _("Match case"),
			      sd->search_name_match_case, &sd->search_name_match_case);
	pref_checkbox_new_int(hbox, _("Symbolic link"), sd->search_name_symbolic_link, &sd->search_name_symbolic_link);
	gtk_widget_set_tooltip_text(combo,
	                            _("When set to 'contains' or 'path contains', this field uses Perl Compatible Regular Expressions.\ne.g. use \n.*\\.jpg\n and not \n*.jpg\n\nSee the Help file."));

	search_entry_attach_focus_controller(sd->ui.entry_name, sd);

	/* Search for file size */
	hbox = menu_choice(sd->ui.box_search, _("File size is"), &sd->match_size_enable);
	sd->ui.menu_size = menu_choice_menu(hbox, text_search_menu_size,
	                                    G_CALLBACK(menu_choice_size_cb), sd);
	constexpr std::size_t file_size_max = 1024*1024*1024;
	sd->ui.spin_size = menu_spin(hbox, 0, file_size_max, &sd->search_size);
	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_append(GTK_BOX(hbox), hbox2);
	pref_label_new(hbox2, _("and"));
	sd->ui.spin_size_end = menu_spin(hbox2, 0, file_size_max, &sd->search_size_end);

	/* Search for file date */
	hbox = menu_choice(sd->ui.box_search, _("File date is"), &sd->match_date_enable);
	sd->ui.menu_date = menu_choice_menu(hbox, text_search_menu_date,
	                                    G_CALLBACK(menu_choice_date_cb), sd);

	sd->ui.date_sel = date_selection_new();
	date_selection_time_set(sd->ui.date_sel, time(nullptr));
	gtk_box_append(GTK_BOX(hbox), sd->ui.date_sel);

	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_append(GTK_BOX(hbox), hbox2);
	pref_label_new(hbox2, _("and"));
	sd->ui.date_sel_end = date_selection_new();
	date_selection_time_set(sd->ui.date_sel_end, time(nullptr));
	gtk_box_append(GTK_BOX(hbox2), sd->ui.date_sel_end);

	GtkStringList *date_list = gtk_string_list_new(nullptr);
	for (const SearchDateType &sdt : search_date_types)
		{
		gtk_string_list_append(date_list, sdt.name);
		}
	sd->ui.date_type = gtk_drop_down_new(G_LIST_MODEL(date_list), nullptr);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(sd->ui.date_type), 0);
	gtk_widget_set_tooltip_text(sd->ui.date_type, "Modified (mtime)\nStatus Changed (ctime)\nOriginal (Exif.Photo.DateTimeOriginal)\nDigitized (Exif.Photo.DateTimeDigitized)");
	gtk_box_append(GTK_BOX(hbox), sd->ui.date_type);

	/* Search for image dimensions */
	hbox = menu_choice(sd->ui.box_search, _("Image dimensions are"), &sd->match_dimensions_enable);
	sd->ui.menu_dimensions = menu_choice_menu(hbox, text_search_menu_dimensions,
	                                          G_CALLBACK(menu_choice_dimensions_cb), sd);
	pad_box = pref_box_new(hbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 2);
	menu_dimensions_spin(pad_box, sd->search_dimensions);
	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_box_append(GTK_BOX(hbox), hbox2);
	pref_label_new(hbox2, _("and"));
	pref_spacer(hbox2, PREF_PAD_SPACE - (2*2));
	menu_dimensions_spin(hbox2, sd->search_dimensions_end);
	sd->ui.box_dimensions_end = hbox2;

	/* Search for image similarity */
	hbox = menu_choice(sd->ui.box_search, _("Image content is"), &sd->match_similarity_enable);
	sd->ui.spin_similarity = menu_spin(hbox, 80, 100, &sd->search_similarity);

	/* xgettext:no-c-format */
	pref_label_new(hbox, _("% similar to"));

	sd->ui.entry_similarity = tab_completion_new_with_history(hbox, sd->search_similarity_path ?
	                                                              sd->search_similarity_path : "",
	                                                          "search_similarity_path", -1);
	search_entry_attach_focus_controller(sd->ui.entry_similarity, sd);

	tab_completion_add_select_button(sd->ui.entry_similarity, nullptr, FALSE, nullptr, nullptr, nullptr);
	pref_checkbox_new_int(hbox, _("Ignore rotation"),
				options->rot_invariant_sim, &options->rot_invariant_sim);

	/* Search for image keywords */
	GtkWidget *check_keywords = nullptr;
	hbox = menu_choice(sd->ui.box_search, _("Keywords"), &sd->match_keywords_enable,
	                   &check_keywords);
	sd->ui.menu_keywords = menu_choice_menu(hbox, text_search_menu_keywords,
	                                        nullptr, nullptr);
	sd->ui.entry_keywords = gtk_entry_new();
	gtk_widget_set_hexpand(sd->ui.entry_keywords, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(sd->ui.entry_keywords, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(hbox), sd->ui.entry_keywords);

	search_entry_attach_focus_controller(sd->ui.entry_keywords, sd);

	gtk_widget_set_sensitive(sd->ui.entry_keywords, sd->match_keywords_enable);
	g_signal_connect(G_OBJECT(check_keywords), "toggled",
	                 G_CALLBACK(menu_choice_check_cb), sd->ui.entry_keywords);

	/* Search for image comment */
	GtkWidget *check_comment = nullptr;
	hbox = menu_choice(sd->ui.box_search, _("Comment"), &sd->match_comment_enable,
	                   &check_comment);
	sd->ui.menu_comment = menu_choice_menu(hbox, text_search_menu_comment,
	                                       nullptr, nullptr);
	sd->ui.entry_comment = gtk_entry_new();
	gtk_widget_set_hexpand(sd->ui.entry_comment, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(sd->ui.entry_comment, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(hbox), sd->ui.entry_comment);

	search_entry_attach_focus_controller(sd->ui.entry_comment, sd);

	gtk_widget_set_sensitive(sd->ui.entry_comment, sd->match_comment_enable);
	g_signal_connect(G_OBJECT(check_comment), "toggled",
	                 G_CALLBACK(menu_choice_check_cb), sd->ui.entry_comment);
	pref_checkbox_new_int(hbox, _("Match case"),
			      sd->search_comment_match_case, &sd->search_comment_match_case);
	gtk_widget_set_tooltip_text(sd->ui.entry_comment,
	                            _("This field uses Perl Compatible Regular Expressions.\ne.g. use \nabc.*ghk\n and not \nabc*ghk\n\nSee the Help file."));

	/* Search for Exif tag */
	GtkWidget *check_exif = nullptr;
	hbox = menu_choice(sd->ui.box_search, _("Exif"), &sd->match_exif_enable,
	                   &check_exif);
	sd->ui.menu_exif = menu_choice_menu(hbox, text_search_menu_exif,
	                                    nullptr, nullptr);

	pref_label_new(hbox, _("Tag"));

	sd->ui.entry_exif_tag = gtk_entry_new();
	gtk_widget_set_hexpand(sd->ui.entry_exif_tag, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(sd->ui.entry_exif_tag, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(hbox), sd->ui.entry_exif_tag);

	search_entry_attach_focus_controller(sd->ui.entry_exif_tag, sd);

	gtk_widget_set_sensitive(sd->ui.entry_exif_tag, sd->match_exif_enable);
	g_signal_connect(G_OBJECT(check_exif), "toggled",
	                 G_CALLBACK(menu_choice_check_cb), sd->ui.entry_exif_tag);
	gtk_widget_set_tooltip_text(sd->ui.entry_exif_tag,
	                            _("e.g. Exif.Image.Model\nThis always case-sensitive\n\nYou may drag-and-drop from the Exif Window\n\nSee https://exiv2.org/tags.html"));

	pref_label_new(hbox, _("Value"));

	sd->ui.entry_exif_value = gtk_entry_new();
	gtk_widget_set_hexpand(sd->ui.entry_exif_value, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(sd->ui.entry_exif_value, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(hbox), sd->ui.entry_exif_value);

	search_entry_attach_focus_controller(sd->ui.entry_exif_value, sd);

	gtk_widget_set_sensitive(sd->ui.entry_exif_value, sd->match_exif_enable);
	g_signal_connect(G_OBJECT(check_exif), "toggled",
	                 G_CALLBACK(menu_choice_check_cb), sd->ui.entry_exif_value);

	gtk_widget_set_tooltip_text(sd->ui.entry_exif_value,
	                            _("e.g. Canon EOS\n\nThis field uses Perl Compatible Regular Expressions.\ne.g. use \nabc.*ghk\n and not \nabc*ghk\n\nSee the Help file."));

	pref_checkbox_new_int(hbox, _("Match case"), sd->search_exif_match_case, &sd->search_exif_match_case);

	/* Search for image rating */
	hbox = menu_choice(sd->ui.box_search, _("Image rating is"), &sd->match_rating_enable);
	sd->ui.menu_rating = menu_choice_menu(hbox, text_search_menu_rating,
	                                      G_CALLBACK(menu_choice_rating_cb), sd);
	constexpr gint rating_min = -1;
	constexpr gint rating_max = 5;
	sd->ui.spin_size = menu_spin(hbox, rating_min, rating_max, &sd->search_rating);
	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_append(GTK_BOX(hbox), hbox2);
	pref_label_new(hbox2, _("and"));
	sd->ui.spin_rating_end = menu_spin(hbox2, rating_min, rating_max, &sd->search_rating_end);

	/* Search for images within a specified range of a lat/long coordinate
	*/
	hbox = menu_choice(sd->ui.box_search, _("Image is"), &sd->match_gps_enable);
	sd->ui.menu_gps = menu_choice_menu(hbox, text_search_menu_gps,
	                                   G_CALLBACK(menu_choice_gps_cb), sd);

	hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_append(GTK_BOX(hbox), hbox2);
	sd->ui.spin_gps = menu_spin(hbox2, 1, 9999, &sd->search_gps);

	static const char *units_strings[] = { _("km"), _("miles"), _("n.m."), nullptr };
	sd->ui.units_gps = gtk_drop_down_new_from_strings(units_strings);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(sd->ui.units_gps), 0);
	gtk_widget_set_tooltip_text(sd->ui.units_gps, "kilometres, miles or nautical miles");
	gtk_box_append(GTK_BOX(hbox2), sd->ui.units_gps);

	pref_label_new(hbox2, _("from"));

	sd->ui.entry_gps_coord = gtk_entry_new();
	gtk_editable_set_editable(GTK_EDITABLE(sd->ui.entry_gps_coord), TRUE);
	gtk_widget_set_has_tooltip(sd->ui.entry_gps_coord, TRUE);
	gtk_widget_set_tooltip_text(sd->ui.entry_gps_coord,
	                            _("Enter a coordinate in the form:\n89.123 179.456\nor drag-and-drop a geo-coded image\nor left-click on the map and paste\nor cut-and-paste or drag-and-drop\nan internet search URL\nSee the Help file"));
	gtk_widget_set_hexpand(sd->ui.entry_gps_coord, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox2))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(sd->ui.entry_gps_coord, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox2))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(hbox2), sd->ui.entry_gps_coord);
	gtk_widget_set_sensitive(sd->ui.entry_gps_coord, TRUE);


	/* Search for image class */
	hbox = menu_choice(sd->ui.box_search, _("Image class"), &sd->match_class_enable);
	sd->ui.menu_class = menu_choice_menu(hbox, text_search_menu_class,
	                                     nullptr, nullptr);

	static const char *class_strings[] = {
	    _("Image"),
	    _("Raw Image"),
	    _("Video"),
	    _("Document"),
	    _("Metadata"),
	    _("Archive"),
	    _("Unknown"),
	    _("Broken"),
	    nullptr
	};
	sd->ui.class_type = gtk_drop_down_new_from_strings(class_strings);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(sd->ui.class_type), 0);
	gtk_box_append(GTK_BOX(hbox), sd->ui.class_type);

	/* Search for image marks */
	hbox = menu_choice(sd->ui.box_search, _("Marks"), &sd->match_marks_enable);
	sd->ui.menu_marks = menu_choice_menu(hbox, text_search_menu_marks,
	                                     nullptr, nullptr);

	GtkStringList *marks_list = gtk_string_list_new(nullptr);
	gtk_string_list_append(marks_list, _("Any mark"));
	for (gint i = 0; i < FILEDATA_MARKS_SIZE; i++)
		{
		g_autoptr(GString) marks_string = get_marks_string(i);

		gtk_string_list_append(marks_list, marks_string->str);
		}
	sd->ui.marks_type = gtk_drop_down_new(G_LIST_MODEL(marks_list), nullptr);
	gtk_box_append(GTK_BOX(hbox), sd->ui.marks_type);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(sd->ui.marks_type), 0);

	/* Done the types of searches */

	scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(vbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(vbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(vbox), scrolled);

	sd->ui.result_store = g_list_store_new(search_result_row_get_type());
	GtkSortListModel *sort_model = gtk_sort_list_model_new(G_LIST_MODEL(g_object_ref(sd->ui.result_store)), nullptr);
	sd->ui.result_selection = gtk_multi_selection_new(G_LIST_MODEL(sort_model));
	sd->ui.result_view = gtk_column_view_new(GTK_SELECTION_MODEL(g_object_ref(sd->ui.result_selection)));
	gtk_sort_list_model_set_sorter(sort_model, gtk_column_view_get_sorter(GTK_COLUMN_VIEW(sd->ui.result_view)));
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), sd->ui.result_view);

	g_signal_connect(sd->ui.result_selection, "selection-changed", G_CALLBACK(search_result_select_cb), sd);

	search_result_add_column(sd, SEARCH_COLUMN_RANK, _("Rank"), FALSE, FALSE);
	search_result_add_column(sd, SEARCH_COLUMN_THUMB, _("Thumb"), TRUE, FALSE);
	search_result_add_column(sd, SEARCH_COLUMN_NAME, _("Name"), FALSE, FALSE);
	search_result_add_column(sd, SEARCH_COLUMN_SIZE, _("Size"), FALSE, TRUE);
	search_result_add_column(sd, SEARCH_COLUMN_DATE, _("Date"), FALSE, TRUE);
	search_result_add_column(sd, SEARCH_COLUMN_DIMENSIONS, _("Dimensions"), FALSE, FALSE);
	search_result_add_column(sd, SEARCH_COLUMN_PATH, _("Path"), FALSE, FALSE);

	GtkGesture *gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
	g_signal_connect(gesture, "pressed", G_CALLBACK(search_result_press_cb), sd);
	g_signal_connect(gesture, "released", G_CALLBACK(search_result_release_cb), sd);
	gtk_widget_add_controller(sd->ui.result_view, GTK_EVENT_CONTROLLER(gesture));

	GtkGesture *context_gesture = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(context_gesture), GDK_BUTTON_SECONDARY);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(context_gesture), GTK_PHASE_CAPTURE);
	g_signal_connect(context_gesture, "pressed", G_CALLBACK(search_result_press_cb), sd);
	gtk_widget_add_controller(sd->ui.result_view, GTK_EVENT_CONTROLLER(context_gesture));

	search_dnd_add_drop_target(sd->ui.path_entry, SearchDndDestination::Path);
	search_dnd_add_drop_target(sd->ui.entry_similarity, SearchDndDestination::Similarity);
	search_dnd_add_drop_target(sd->ui.entry_gps_coord, SearchDndDestination::Gps);

	hbox = pref_box_new(vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, 0);

	sd->ui.button_thumbs = pref_checkbox_new(hbox, _("Thumbnails"), FALSE,
	                                         G_CALLBACK(search_thumb_toggle_cb), sd);
	g_autofree gchar *thumbs_accel = action_accelerator_label("win.search-win-thumbnails");
	gtk_widget_set_tooltip_text(sd->ui.button_thumbs, thumbs_accel);

	frame = gtk_frame_new(nullptr);
	DEBUG_NAME(frame);
	gtk_widget_add_css_class(frame, "frame");
	gtk_widget_set_hexpand(frame, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(frame, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	if (gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_HORIZONTAL)
		{
		gtk_widget_set_margin_end(frame, PREF_PAD_SPACE);
		}
	else
		{
		gtk_widget_set_margin_bottom(frame, PREF_PAD_SPACE);
		}
	gtk_box_append(GTK_BOX(hbox), frame);

	sd->ui.label_status = gtk_label_new("");
	gtk_widget_set_size_request(sd->ui.label_status, 50, -1);
	gtk_frame_set_child(GTK_FRAME(frame), sd->ui.label_status);

	sd->ui.label_progress = gtk_progress_bar_new();
	gtk_widget_set_size_request(sd->ui.label_progress, 50, -1);

	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(sd->ui.label_progress), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(sd->ui.label_progress), TRUE);

	gtk_widget_set_hexpand(sd->ui.label_progress, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(sd->ui.label_progress, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(hbox))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	gtk_box_append(GTK_BOX(hbox), sd->ui.label_progress);

	sd->ui.spinner = gtk_spinner_new();
	gtk_box_append(GTK_BOX(hbox), sd->ui.spinner);

	GtkWidget *button_help = pref_button_new(hbox, GQ_ICON_HELP, _("Help"), G_CALLBACK(search_window_help_button_cb), sd);
	g_autofree gchar *help_accel = action_accelerator_label("win.search-win-help");
	gtk_widget_set_tooltip_text(button_help, help_accel);
	gtk_widget_set_sensitive(button_help, TRUE);
	pref_spacer(hbox, PREF_PAD_BUTTON_GAP);
	sd->ui.button_start = pref_button_new(hbox, GQ_ICON_FIND, _("Find"), G_CALLBACK(search_start_button_cb), sd);
	g_autofree gchar *start_accel = action_accelerator_label("win.search-win-search-start");
	gtk_widget_set_tooltip_text(sd->ui.button_start, start_accel);
	pref_spacer(hbox, PREF_PAD_BUTTON_GAP);
	sd->ui.button_stop = pref_button_new(hbox, GQ_ICON_STOP, _("Stop"), G_CALLBACK(search_start_button_cb), sd);
	g_autofree gchar *stop_accel = action_accelerator_label("win.search-win-search-start");
	gtk_widget_set_tooltip_text(sd->ui.button_stop, stop_accel);
	gtk_widget_set_sensitive(sd->ui.button_stop, FALSE);
	pref_spacer(hbox, PREF_PAD_BUTTON_GAP);
	GtkWidget *button_close = pref_button_new(hbox, GQ_ICON_CLOSE, _("Close"), G_CALLBACK(search_window_close_button_cb), sd);
	g_autofree gchar *close_accel = action_accelerator_label("win.search-win-window-close");
	gtk_widget_set_tooltip_text(button_close, close_accel);
	gtk_widget_set_sensitive(button_close, TRUE);

	search_result_thumb_enable(sd, TRUE);
	search_result_thumb_enable(sd, FALSE);
	gtk_column_view_column_set_visible(sd->ui.result_columns[SEARCH_COLUMN_RANK], FALSE);
	gtk_column_view_column_set_visible(sd->ui.result_columns[SEARCH_COLUMN_DIMENSIONS],
	                                   sd->match_dimensions_enable);

	search_status_update(sd);
	search_progress_update(sd, FALSE, -1.0);

	file_data_register_notify_func(search_notify_cb, sd, NOTIFY_PRIORITY_MEDIUM);

	GApplication *app = g_application_get_default();
	register_actions_from_table(GTK_APPLICATION(app), sd->ui.window, search_actions, get_keyfile_merged(), sd);

	gtk_window_present(GTK_WINDOW(sd->ui.window));
}

/*
 *-------------------------------------------------------------------
 * maintenance (move, delete, etc.)
 *-------------------------------------------------------------------
 */

static void search_result_change_path(SearchData *sd, FileData *fd)
{
	for (guint position = 0; position < g_list_model_get_n_items(G_LIST_MODEL(sd->ui.result_store));)
		{
		auto *row = static_cast<SearchResultRow *>(g_list_model_get_item(G_LIST_MODEL(sd->ui.result_store), position));
		MatchFileData *mfd = row->mfd;
		if (mfd->fd == fd)
			{
			if (fd->change && fd->change->dest)
				{
				gpointer items[] = { row };
				g_list_store_splice(sd->ui.result_store, position, 1, items, 1);
				position++;
				}
			else
				{
				g_object_unref(row);
				search_result_remove_item(sd, mfd, position);
				continue;
				}
			}
		else
			{
			position++;
			}
		g_object_unref(row);
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

const ActionDef *get_search_actions()
{
	return search_actions;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
