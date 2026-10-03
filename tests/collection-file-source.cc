/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <string>
#include <vector>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "cache.h"
#include "collect.h"
#include "collect-io.h"
#include "filedata.h"
#include "filefilter.h"
#include "history-list.h"
#include "intl.h"
#include "layout.h"
#include "options.h"
#include "sort-type.h"
#include "thumb.h"
#include "ui-fileops.h"
#include "view-dir.h"
#include "view-file.h"

namespace
{

GtkWidget *find_button_by_label(GtkWidget *widget, const gchar *label)
{
	if (GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0) return widget;
	for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		if (GtkWidget *button = find_button_by_label(child, label)) return button;
	return nullptr;
}

GtkWidget *collection_confirm_dialog(GtkWidget *parent)
{
	GList *windows = gtk_window_list_toplevels();
	GtkWidget *dialog = nullptr;
	for (GList *work = windows; work; work = work->next)
		{
		auto *window = GTK_WINDOW(work->data);
		if (gtk_window_get_transient_for(window) == GTK_WINDOW(parent))
			{
			dialog = GTK_WIDGET(window);
			break;
			}
		}
	g_list_free(windows);
	return dialog;
}

class CollectionFileSource : public testing::Test
{
protected:
	void SetUp() override
	{
		if (auto *recent = history_list_find_by_key("recent")) saved_recent = *recent;
		if (!options) options = conf_options_new();
		setup_default_options(options);
		filter_add_defaults();
		filter_rebuild();
		directory = g_dir_make_tmp("geeqie-collection-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		for (const char *name : {"a", "b"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			ASSERT_EQ(g_mkdir(path, 0700), 0);
			}
		cd = collection_new(nullptr);
		first = make_file("a", "same.svg");
		second = make_file("b", "same.svg");
		unlisted = make_file("a", "unlisted.svg");
		ASSERT_TRUE(collection_add(cd, second, FALSE));
		ASSERT_TRUE(collection_add(cd, first, FALSE));
		view.source = FileViewSource::COLLECTION;
		view.collection = cd;
		view.collection_order = g_hash_table_new(g_direct_hash, g_direct_equal);
		view.sort = {SORT_NONE, TRUE, TRUE};
	}

	FileData *make_file(const char *folder, const char *name)
	{
		g_autofree gchar *path = g_build_filename(directory, folder, name, nullptr);
		constexpr char svg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'><rect width='8' height='8' fill='red'/></svg>";
		if (!g_file_set_contents(path, svg, -1, nullptr)) return nullptr;
		paths.emplace_back(path);
		return file_data_new_group(path);
	}

	void TearDown() override
	{
		file_data_list_free(files);
		for (GList *work = view.monitored_files; work; work = work->next)
			file_data_unregister_real_time_monitor(static_cast<FileData *>(work->data));
		g_list_free(view.monitored_files);
		g_clear_pointer(&view.collection_order, g_hash_table_destroy);
		if (cd) collection_unref(cd);
		file_data_unref(first);
		file_data_unref(second);
		file_data_unref(unlisted);
		for (const auto &path : paths) g_remove(path.c_str());
		for (const char *name : {"a", "b"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			g_rmdir(path);
			}
		g_rmdir(directory);
		g_free(directory);
		history_list_free_key("recent");
		for (auto work = saved_recent.crbegin(); work != saved_recent.crend(); ++work)
			{
			history_list_add_to_key("recent", work->c_str(), 0);
			}
	}

	gchar *directory = nullptr;
	CollectionData *cd = nullptr;
	FileData *first = nullptr;
	FileData *second = nullptr;
	FileData *unlisted = nullptr;
	ViewFile view{};
	GList *files = nullptr;
	std::vector<std::string> paths;
	HistoryList saved_recent;
};

TEST_F(CollectionFileSource, ManagerUpdatesExternalCollectionsFromHistory)
{
	g_autofree gchar *path = g_build_filename(directory, "external.gqv", nullptr);
	g_autofree gchar *other_path = g_build_filename(directory, "other.gqv", nullptr);
	paths.emplace_back(path);
	paths.emplace_back(other_path);
	ASSERT_TRUE(collection_save(cd, path));
	ASSERT_TRUE(collection_save(cd, other_path));
	collection_unref(cd);
	cd = nullptr;

	// History entries that are missing, directories, or not .gqv files are ignored.
	g_autofree gchar *ignored_path = g_build_filename(directory, "ignored.txt", nullptr);
	paths.emplace_back(ignored_path);
	g_autofree gchar *ignored_contents = g_strdup_printf("#Geeqie collection\n\"%s\"\n", first->path);
	ASSERT_TRUE(g_file_set_contents(ignored_path, ignored_contents, -1, nullptr));
	history_list_add_to_key("recent", ignored_path, 0);
	g_autofree gchar *missing_path = g_build_filename(directory, "missing.gqv", nullptr);
	history_list_add_to_key("recent", missing_path, 0);
	g_autofree gchar *directory_path = g_build_filename(directory, "directory.gqv", nullptr);
	ASSERT_EQ(g_mkdir(directory_path, 0700), 0);
	history_list_add_to_key("recent", directory_path, 0);

	FileData *renamed = make_file("a", "renamed.svg");
	ASSERT_NE(renamed, nullptr);
	ASSERT_TRUE(file_data_add_ci(first, FILEDATA_CHANGE_RENAME, first->path, renamed->path));
	collect_manager_notify_cb(first, NOTIFY_CHANGE, nullptr);
	collect_manager_flush();
	file_data_change_info_free(first->change, first);

	FileData *moved = make_file("b", "moved.svg");
	ASSERT_NE(moved, nullptr);
	ASSERT_TRUE(file_data_add_ci(renamed, FILEDATA_CHANGE_MOVE, renamed->path, moved->path));
	collect_manager_notify_cb(renamed, NOTIFY_CHANGE, nullptr);
	collect_manager_flush();
	file_data_change_info_free(renamed->change, renamed);

	for (const gchar *collection_path : {path, other_path})
		{
		g_autofree gchar *contents = nullptr;
		ASSERT_TRUE(g_file_get_contents(collection_path, &contents, nullptr, nullptr));
		EXPECT_NE(std::string(contents).find(moved->path), std::string::npos);
		EXPECT_EQ(std::string(contents).find(first->path), std::string::npos);
		EXPECT_EQ(std::string(contents).find(renamed->path), std::string::npos);
		EXPECT_NE(std::string(contents).find(second->path), std::string::npos);
		}

	// External collections are no longer maintained after they leave history.
	history_list_item_remove("recent", path);
	history_list_item_remove("recent", other_path);
	FileData *later = make_file("a", "later.svg");
	ASSERT_NE(later, nullptr);
	ASSERT_TRUE(file_data_add_ci(moved, FILEDATA_CHANGE_MOVE, moved->path, later->path));
	collect_manager_notify_cb(moved, NOTIFY_CHANGE, nullptr);
	collect_manager_flush();
	file_data_change_info_free(moved->change, moved);
	for (const gchar *collection_path : {path, other_path})
		{
		g_autofree gchar *contents = nullptr;
		ASSERT_TRUE(g_file_get_contents(collection_path, &contents, nullptr, nullptr));
		EXPECT_NE(std::string(contents).find(moved->path), std::string::npos);
		EXPECT_EQ(std::string(contents).find(later->path), std::string::npos);
		}
	file_data_unref(later);
	g_autofree gchar *ignored_after = nullptr;
	ASSERT_TRUE(g_file_get_contents(ignored_path, &ignored_after, nullptr, nullptr));
	EXPECT_STREQ(ignored_after, ignored_contents);
	EXPECT_FALSE(g_file_test(missing_path, G_FILE_TEST_EXISTS));
	EXPECT_EQ(g_rmdir(directory_path), 0);
	file_data_unref(renamed);
	file_data_unref(moved);
}

TEST_F(CollectionFileSource, ManagerRenamesToLongerPathAndRemovesFile)
{
	ASSERT_EQ(g_mkdir_with_parents(get_collections_dir(), 0700), 0);
	g_autofree gchar *basename = g_path_get_basename(directory);
	g_autofree gchar *name = g_strconcat(basename, ".gqv", nullptr);
	g_autofree gchar *path = g_build_filename(get_collections_dir(), name, nullptr);
	paths.emplace_back(path);
	ASSERT_TRUE(collection_save(cd, path));

	const std::string long_name = std::string(200, 'x') + ".svg";
	FileData *renamed = make_file("a", long_name.c_str());
	ASSERT_NE(renamed, nullptr);
	ASSERT_TRUE(file_data_add_ci(first, FILEDATA_CHANGE_RENAME, first->path, renamed->path));
	collect_manager_moved(first);
	collect_manager_flush();
	file_data_change_info_free(first->change, first);

	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(path, &contents, nullptr, nullptr));
	EXPECT_NE(std::string(contents).find(renamed->path), std::string::npos);
	EXPECT_EQ(std::string(contents).find(first->path), std::string::npos);
	EXPECT_NE(std::string(contents).find(second->path), std::string::npos);

	collection_unref(cd);
	cd = nullptr;
	collect_manager_remove(renamed, path);
	collect_manager_flush();
	g_clear_pointer(&contents, g_free);
	ASSERT_TRUE(g_file_get_contents(path, &contents, nullptr, nullptr));
	EXPECT_EQ(std::string(contents).find(renamed->path), std::string::npos);
	EXPECT_NE(std::string(contents).find(second->path), std::string::npos);
	file_data_unref(renamed);
}

TEST_F(CollectionFileSource, ManagerIgnoresNonCollectionFiles)
{
	ASSERT_EQ(g_mkdir_with_parents(get_collections_dir(), 0700), 0);
	g_autofree gchar *basename = g_path_get_basename(directory);
	g_autofree gchar *name = g_strconcat(basename, ".gqv", nullptr);
	g_autofree gchar *path = g_build_filename(get_collections_dir(), name, nullptr);
	paths.emplace_back(path);
	ASSERT_TRUE(collection_save(cd, path));

	// Non-collection files must not be parsed or rewritten by the manager.
	g_autofree gchar *image_name = g_strconcat("image-", basename, ".jpg", nullptr);
	g_autofree gchar *image_path = g_build_filename(get_collections_dir(), image_name, nullptr);
	paths.emplace_back(image_path);
	g_autofree gchar *image_contents = g_strdup_printf("#Geeqie collection\n\"%s\"\n", first->path);
	ASSERT_TRUE(g_file_set_contents(image_path, image_contents, -1, nullptr));

	FileData *renamed = make_file("a", "next.svg");
	ASSERT_NE(renamed, nullptr);
	ASSERT_TRUE(file_data_add_ci(first, FILEDATA_CHANGE_RENAME, first->path, renamed->path));
	collect_manager_moved(first);
	collect_manager_flush();
	file_data_change_info_free(first->change, first);

	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(path, &contents, nullptr, nullptr));
	EXPECT_NE(std::string(contents).find(renamed->path), std::string::npos);
	EXPECT_EQ(std::string(contents).find(first->path), std::string::npos);
	EXPECT_NE(std::string(contents).find(second->path), std::string::npos);
	g_autofree gchar *image_after = nullptr;
	ASSERT_TRUE(g_file_get_contents(image_path, &image_after, nullptr, nullptr));
	EXPECT_STREQ(image_after, image_contents);

	file_data_unref(renamed);
}

TEST_F(CollectionFileSource, MissingEntriesSurviveLoadDisplayAndSave)
{
	for (gboolean relative : {false, true})
		{
		g_autofree gchar *path = g_build_filename(directory, "missing.gqv", nullptr);
		paths.emplace_back(path);
		cd->relative_paths = relative;
		ASSERT_TRUE(collection_save(cd, path));
		g_remove(first->path);
		ASSERT_TRUE(collection_load(cd, path, COLLECTION_LOAD_NONE));
		EXPECT_EQ(g_list_length(cd->list), 2U);
		EXPECT_NE(collection_list_find_fd(cd->list, first), nullptr);
		ASSERT_TRUE(vf_read_source(&view, &files));
		EXPECT_NE(g_list_find(files, first), nullptr);
		file_data_list_free(files);
		files = nullptr;
		ASSERT_TRUE(collection_save(cd, path));
		ASSERT_TRUE(collection_load(cd, path, COLLECTION_LOAD_NONE));
		EXPECT_NE(collection_list_find_fd(cd->list, first), nullptr);
		ASSERT_TRUE(collection_remove(cd, first));
		ASSERT_TRUE(collection_save(cd, path));
		ASSERT_TRUE(collection_load(cd, path, COLLECTION_LOAD_NONE));
		EXPECT_EQ(collection_list_find_fd(cd->list, first), nullptr);
		ASSERT_TRUE(collection_add_unchecked(cd, first, FALSE));
		}
}

TEST_F(CollectionFileSource, OrdinaryPluginDoesNotRequireWritableSourceDirectory)
{
	g_autofree gchar *source_directory = g_path_get_dirname(first->path);
	ASSERT_EQ(g_chmod(source_directory, 0500), 0);
	EXPECT_EQ(g_chmod(first->path, 0400), 0);
	gboolean added = file_data_sc_add_ci_unspecified(first, nullptr);
	EXPECT_TRUE(added);
	if (added)
		{
		EXPECT_EQ(file_data_verify_ci(first, nullptr), 0);
		file_data_change_info_free(first->change, first);
		}

	// An explicitly supplied destination must still be checked for write access.
	added = file_data_sc_add_ci_unspecified(first, second->path);
	EXPECT_TRUE(added);
	if (added)
		{
		g_autofree gchar *destination_directory = g_path_get_dirname(second->path);
		EXPECT_EQ(g_chmod(destination_directory, 0500), 0);
		EXPECT_NE(file_data_verify_ci(first, nullptr) & CHANGE_WARN_NO_WRITE_PERM_DEST_DIR, 0);
		EXPECT_EQ(g_chmod(destination_directory, 0700), 0);
		file_data_change_info_free(first->change, first);
		}
	EXPECT_EQ(g_chmod(first->path, 0600), 0);
	EXPECT_EQ(g_chmod(source_directory, 0700), 0);
}

TEST_F(CollectionFileSource, MissingFileNotifiesOnlyOnAvailabilityChanges)
{
	gint rereads = 0;
	const auto notify = [](FileData *, NotifyType type, gpointer data)
		{
		if (type & NOTIFY_REREAD) ++*static_cast<gint *>(data);
		};
	ASSERT_TRUE(file_data_register_notify_func(notify, &rereads, NOTIFY_PRIORITY_LOW));
	EXPECT_FALSE(file_data_check_changed_files(first));
	EXPECT_EQ(g_remove(first->path), 0);
	EXPECT_TRUE(file_data_check_changed_files(first));
	EXPECT_EQ(rereads, 1);
	gint version = first->version;
	for (gint i = 0; i < 3; ++i) EXPECT_FALSE(file_data_check_changed_files(first));
	EXPECT_EQ(rereads, 1);
	EXPECT_EQ(first->version, version);
	EXPECT_TRUE(g_file_set_contents(first->path, "", 0, nullptr));
	EXPECT_TRUE(file_data_check_changed_files(first));
	EXPECT_FALSE(first->missing);
	EXPECT_EQ(rereads, 2);
	EXPECT_FALSE(file_data_check_changed_files(first));
	EXPECT_EQ(g_remove(first->path), 0);
	EXPECT_TRUE(file_data_check_changed_files(first));
	EXPECT_EQ(rereads, 3);
	EXPECT_FALSE(file_data_check_changed_files(first));
	EXPECT_TRUE(file_data_unregister_notify_func(notify, &rereads));
}

TEST_F(CollectionFileSource, MissingSourceUsesCachedThumbnail)
{
	options->thumbnails.enable_caching = TRUE;
	for (gboolean standard : {false, true})
		{
		options->thumbnails.spec_standard = standard;
		g_autofree gchar *cache_path = nullptr;
		g_autofree gchar *uri = g_filename_to_uri(first->path, nullptr, nullptr);
		if (standard)
			{
			g_autofree gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_MD5, uri, -1);
			g_autofree gchar *name = g_strconcat(hash, ".png", nullptr);
			cache_path = g_build_filename(get_thumbnails_standard_cache_dir(), "normal", name, nullptr);
			}
		else
			{
			cache_path = cache_get_location(CacheType::THUMB, first->path);
			}
		g_autofree gchar *parent = g_path_get_dirname(cache_path);
		ASSERT_EQ(g_mkdir_with_parents(parent, 0700), 0);
		paths.emplace_back(cache_path);
		g_autoptr(GdkPixbuf) cached = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 128, 128);
		gdk_pixbuf_fill(cached, 0xff0000ff);
		ASSERT_TRUE(gdk_pixbuf_save(cached, cache_path, "png", nullptr,
		                           "tEXt::Thumb::URI", uri, "tEXt::Thumb::MTime", "1", nullptr));
		g_remove(first->path);
		g_clear_object(&first->thumb_pixbuf);
		gint completed = 0;
		const auto done = [](ThumbLoader *, gpointer data) { *static_cast<gint *>(data) = 1; };
		const auto error = [](ThumbLoader *, gpointer data) { *static_cast<gint *>(data) = -1; };
		ThumbLoader *loader = thumb_loader_new(128, 128);
		thumb_loader_set_callbacks(loader, done, error, nullptr, &completed);
		gboolean started = thumb_loader_start(loader, first);
		if (started)
			{
			gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
			while (!completed && g_get_monotonic_time() < deadline)
				{
				g_main_context_iteration(nullptr, FALSE);
				g_usleep(1000);
				}
			}
		EXPECT_TRUE(started);
		EXPECT_EQ(completed, 1);
		if (first->thumb_pixbuf)
			{
			const guchar *pixels = gdk_pixbuf_get_pixels(first->thumb_pixbuf);
			EXPECT_EQ(pixels[0], 255);
			EXPECT_EQ(pixels[1], 0);
			EXPECT_EQ(pixels[2], 0);
			}
		else ADD_FAILURE() << "Cached thumbnail was not loaded";
		thumb_loader_free(loader);
		EXPECT_TRUE(g_file_test(cache_path, G_FILE_TEST_EXISTS));
		}
}

TEST_F(CollectionFileSource, RelativePathsRoundTripAndSaveAs)
{
	g_autofree gchar *path = g_build_filename(directory, "a", "relative.gqv", nullptr);
	paths.emplace_back(path);
	cd->relative_paths = TRUE;
	ASSERT_TRUE(collection_save(cd, path));
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(path, &contents, nullptr, nullptr));
	EXPECT_NE(std::string(contents).find("\"./same.svg\""), std::string::npos);
	EXPECT_NE(std::string(contents).find("\"../b/same.svg\""), std::string::npos);
	ASSERT_TRUE(collection_load(cd, path, COLLECTION_LOAD_NONE));
	EXPECT_TRUE(cd->relative_paths);
	EXPECT_NE(collection_list_find_fd(cd->list, first), nullptr);
	EXPECT_NE(collection_list_find_fd(cd->list, second), nullptr);

	g_autofree gchar *other = g_build_filename(directory, "b", "relative.gqv", nullptr);
	paths.emplace_back(other);
	ASSERT_TRUE(collection_save(cd, other));
	ASSERT_TRUE(collection_load(cd, other, COLLECTION_LOAD_NONE));
	EXPECT_NE(collection_list_find_fd(cd->list, first), nullptr);
	EXPECT_NE(collection_list_find_fd(cd->list, second), nullptr);
}

TEST_F(CollectionFileSource, SaveAsNotifiesAfterClearingModifiedState)
{
	g_autofree gchar *first_path = g_build_filename(directory, "first.gqv", nullptr);
	g_autofree gchar *second_path = g_build_filename(directory, "second.gqv", nullptr);
	paths.emplace_back(first_path);
	paths.emplace_back(second_path);
	ASSERT_TRUE(collection_save(cd, first_path));
	ASSERT_TRUE(collection_add(cd, unlisted, FALSE));
	gint dirty_notifications = 0;
	const auto changed = [](CollectionData *collection, gpointer data)
		{
		if (collection->changed) ++*static_cast<gint *>(data);
		};
	collection_add_listener(cd, changed, &dirty_notifications);
	ASSERT_TRUE(collection_save(cd, second_path));
	EXPECT_EQ(dirty_notifications, 0);
	EXPECT_FALSE(cd->changed);
	EXPECT_STREQ(cd->path, second_path);
	collection_remove_listener(cd, changed, &dirty_notifications);
}

TEST_F(CollectionFileSource, LoadsMixedPathsAndAppendPreservesSaveMode)
{
	g_autofree gchar *path = g_build_filename(directory, "a", "mixed.gqv", nullptr);
	paths.emplace_back(path);
	g_autofree gchar *contents = g_strdup_printf("#Geeqie collection\n\"./same.svg\"\n\"../b/./same.svg\"\n\"%s\"\n", unlisted->path);
	ASSERT_TRUE(g_file_set_contents(path, contents, -1, nullptr));
	ASSERT_TRUE(collection_load(cd, path, COLLECTION_LOAD_APPEND));
	EXPECT_FALSE(cd->relative_paths);
	ASSERT_TRUE(collection_load(cd, path, COLLECTION_LOAD_NONE));
	EXPECT_TRUE(cd->relative_paths);
	EXPECT_EQ(g_list_length(cd->list), 3U);
	EXPECT_NE(collection_list_find_fd(cd->list, first), nullptr);
	EXPECT_NE(collection_list_find_fd(cd->list, second), nullptr);
	EXPECT_NE(collection_list_find_fd(cd->list, unlisted), nullptr);
}

TEST_F(CollectionFileSource, AbsolutePathsRemainDefault)
{
	g_autofree gchar *path = g_build_filename(directory, "absolute.gqv", nullptr);
	paths.emplace_back(path);
	ASSERT_FALSE(cd->relative_paths);
	ASSERT_TRUE(collection_save(cd, path));
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(path, &contents, nullptr, nullptr));
	EXPECT_NE(std::string(contents).find(first->path), std::string::npos);
	EXPECT_NE(std::string(contents).find(second->path), std::string::npos);
	cd->relative_paths = TRUE;
	ASSERT_TRUE(collection_load(cd, path, COLLECTION_LOAD_NONE));
	EXPECT_FALSE(cd->relative_paths);
}

TEST_F(CollectionFileSource, EmptyRelativeCollectionRetainsSaveMode)
{
	g_autofree gchar *path = g_build_filename(directory, "empty.gqv", nullptr);
	paths.emplace_back(path);
	ASSERT_TRUE(collection_remove(cd, first));
	ASSERT_TRUE(collection_remove(cd, second));
	cd->relative_paths = TRUE;
	ASSERT_TRUE(collection_save(cd, path));
	cd->relative_paths = FALSE;
	ASSERT_TRUE(collection_load(cd, path, COLLECTION_LOAD_NONE));
	EXPECT_TRUE(cd->relative_paths);
}

TEST_F(CollectionFileSource, ReadsOnlyMembersAndKeepsCollectionOrder)
{
	ASSERT_TRUE(vf_read_source(&view, &files));
	ASSERT_EQ(g_list_length(files), 2U);
	EXPECT_EQ(files->data, second);
	EXPECT_EQ(files->next->data, first);
	EXPECT_EQ(g_list_find(files, unlisted), nullptr);
	view.sort.method = SORT_NAME;
	files = vf_filelist_sort(&view, files);
	EXPECT_NE(vf_filelist_compare(&view, first, second), 0);
	view.sort.method = SORT_NONE;
	files = vf_filelist_sort(&view, files);
	EXPECT_EQ(files->data, second);
	EXPECT_EQ(cd->list->data, collection_list_find_fd(cd->list, second));
}

TEST_F(CollectionFileSource, NotifiesAllSubscribersAndRemovalLeavesFileIntact)
{
	gint first_count = 0;
	gint second_count = 0;
	const auto changed = [](CollectionData *, gpointer data) { ++*static_cast<gint *>(data); };
	collection_add_listener(cd, changed, &first_count);
	collection_add_listener(cd, changed, &second_count);
	EXPECT_TRUE(collection_remove(cd, first));
	EXPECT_TRUE(g_file_test(first->path, G_FILE_TEST_IS_REGULAR));
	EXPECT_EQ(first_count, 1);
	EXPECT_EQ(second_count, 1);
	collection_remove_listener(cd, changed, &first_count);
	EXPECT_TRUE(collection_add(cd, first, FALSE));
	EXPECT_EQ(first_count, 1);
	EXPECT_EQ(second_count, 2);
	collection_remove_listener(cd, changed, &second_count);
}

TEST_F(CollectionFileSource, CollectionManagerUpdatesOpenCollectionWithoutSeparateWindow)
{
	g_autofree gchar *path = g_build_filename(directory, "managed.gqv", nullptr);
	paths.emplace_back(path);
	ASSERT_TRUE(collection_save(cd, path));
	ASSERT_EQ(collection_list_find_fd(cd->list, unlisted), nullptr);

	collect_manager_add(unlisted, path);
	EXPECT_NE(collection_list_find_fd(cd->list, unlisted), nullptr);
	collect_manager_remove(unlisted, path);
	EXPECT_EQ(collection_list_find_fd(cd->list, unlisted), nullptr);
}

TEST_F(CollectionFileSource, MissingMemberDoesNotAddOtherFilesOrChangeMembership)
{
	ASSERT_EQ(g_remove(first->path), 0);
	ASSERT_TRUE(vf_read_source(&view, &files));
	ASSERT_EQ(g_list_length(files), 2U);
	EXPECT_NE(g_list_find(files, first), nullptr);
	EXPECT_NE(g_list_find(files, second), nullptr);
	EXPECT_EQ(g_list_length(cd->list), 2U);
}

TEST_F(CollectionFileSource, CollectionFilesAreVirtualFoldersInAnyDirectory)
{
	const gchar *collection_directory = get_collections_dir();
	if (!g_str_has_prefix(collection_directory, "/tmp/")) GTEST_SKIP() << "Requires an isolated home directory";
	ASSERT_EQ(g_mkdir_with_parents(collection_directory, 0700), 0);
	g_autofree gchar *unique = g_uuid_string_random();
	g_autofree gchar *name = g_strconcat(unique, ".gqv", nullptr);
	g_autofree gchar *path = g_build_filename(collection_directory, name, nullptr);
	ASSERT_TRUE(g_file_set_contents(path, "#Geeqie collection\n#end\n", -1, nullptr));
	paths.emplace_back(path);
	FileData *collection_file = file_data_new_simple(path);
	EXPECT_TRUE(vd_is_collection(collection_file));
	EXPECT_FALSE(vd_is_collection(first));

	FileData *folder = file_data_new_dir(collection_directory);
	GList *directories = nullptr;
	ASSERT_TRUE(vd_read_directories(folder, &directories));
	EXPECT_NE(g_list_find(directories, collection_file), nullptr);
	file_data_list_free(directories);

	ViewFile directory_view{};
	directory_view.source = FileViewSource::DIRECTORY;
	directory_view.dir_fd = folder;
	GList *ordinary_files = nullptr;
	ASSERT_TRUE(vf_read_source(&directory_view, &ordinary_files));
	EXPECT_EQ(g_list_find(ordinary_files, collection_file), nullptr);
	file_data_list_free(ordinary_files);

	g_autofree gchar *outside = g_build_filename(directory, name, nullptr);
	ASSERT_TRUE(g_file_set_contents(outside, "#Geeqie collection\n#end\n", -1, nullptr));
	paths.emplace_back(outside);
	FileData *ordinary_collection = file_data_new_simple(outside);
	EXPECT_TRUE(vd_is_collection(ordinary_collection));
	FileData *outside_folder = file_data_new_dir(directory);
	directories = nullptr;
	ASSERT_TRUE(vd_read_directories(outside_folder, &directories));
	EXPECT_NE(g_list_find(directories, ordinary_collection), nullptr);
	file_data_list_free(directories);
	file_data_unref(outside_folder);
	file_data_unref(ordinary_collection);
	file_data_unref(folder);
	file_data_unref(collection_file);
}

TEST_F(CollectionFileSource, LeavingModifiedCollectionCanCancelOrDiscard)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	g_autoptr(GtkApplication) app = gtk_application_new("org.geeqie.CollectionConfirmTest", G_APPLICATION_NON_UNIQUE);
	g_application_set_default(G_APPLICATION(app));
	ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
	GtkWidget *window = gtk_window_new();
	LayoutWindow layout{};
	layout.window = window;
	layout.vf = &view;
	cd->changed = TRUE;
	gboolean continued = FALSE;

	EXPECT_FALSE(layout_confirm_collection_leave(&layout, [&continued]() { continued = TRUE; }, FALSE));
	GtkWidget *dialog = collection_confirm_dialog(window);
	ASSERT_NE(dialog, nullptr);
	GtkWidget *cancel = find_button_by_label(dialog, _("Cancel"));
	ASSERT_NE(cancel, nullptr);
	g_signal_emit_by_name(cancel, "clicked");
	EXPECT_FALSE(continued);
	EXPECT_TRUE(cd->changed);
	EXPECT_EQ(layout.collection_confirm_data, nullptr);

	EXPECT_FALSE(layout_confirm_collection_leave(&layout, [&continued]() { continued = TRUE; }, FALSE));
	dialog = collection_confirm_dialog(window);
	ASSERT_NE(dialog, nullptr);
	GtkWidget *discard = find_button_by_label(dialog, _("_Discard"));
	ASSERT_NE(discard, nullptr);
	g_signal_emit_by_name(discard, "clicked");
	EXPECT_TRUE(continued);
	EXPECT_FALSE(cd->changed);
	EXPECT_EQ(layout.collection_confirm_data, nullptr);
	gtk_window_destroy(GTK_WINDOW(window));
}

TEST_F(CollectionFileSource, LeavingModifiedCollectionCanSave)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	g_autoptr(GtkApplication) app = gtk_application_new("org.geeqie.CollectionConfirmSaveTest", G_APPLICATION_NON_UNIQUE);
	g_application_set_default(G_APPLICATION(app));
	ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
	g_autofree gchar *path = g_build_filename(directory, "save.gqv", nullptr);
	paths.emplace_back(path);
	ASSERT_TRUE(collection_save(cd, path));
	ASSERT_TRUE(collection_add(cd, unlisted, FALSE));
	GtkWidget *window = gtk_window_new();
	LayoutWindow layout{};
	layout.window = window;
	layout.vf = &view;
	gboolean continued = FALSE;

	EXPECT_FALSE(layout_confirm_collection_leave(&layout, [&continued]() { continued = TRUE; }, FALSE));
	GtkWidget *dialog = collection_confirm_dialog(window);
	ASSERT_NE(dialog, nullptr);
	GtkWidget *save = find_button_by_label(dialog, _("Save"));
	ASSERT_NE(save, nullptr);
	g_signal_emit_by_name(save, "clicked");
	EXPECT_TRUE(continued);
	EXPECT_FALSE(cd->changed);
	EXPECT_EQ(layout.collection_confirm_data, nullptr);
	gtk_window_destroy(GTK_WINDOW(window));
}

TEST_F(CollectionFileSource, MovingIconsPreservesSelectionAndCollectionOrder)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	ASSERT_TRUE(collection_add(cd, unlisted, FALSE));
	GtkWidget *window = gtk_window_new();
	auto *pane = vf_new(FILEVIEW_ICON, nullptr);
	gtk_window_set_child(GTK_WINDOW(window), pane->widget);
	ASSERT_TRUE(vf_set_collection(pane, cd));
	vf_sort_set(pane, {SORT_NAME, TRUE, TRUE});
	GList *moving = g_list_append(nullptr, second);
	moving = g_list_append(moving, first);
	vf_select_list(pane, moving);
	vf_collection_move(pane, moving, nullptr);
	ASSERT_TRUE(vf_refresh(pane));
	EXPECT_EQ(pane->sort.method, SORT_NONE);
	EXPECT_EQ(pane->list->data, unlisted);
	EXPECT_EQ(pane->list->next->data, second);
	EXPECT_EQ(pane->list->next->next->data, first);
	EXPECT_EQ(vf_selection_count(pane, nullptr), 2U);
	EXPECT_TRUE(cd->changed);
	vf_collection_move(pane, moving, unlisted);
	ASSERT_TRUE(vf_refresh(pane));
	EXPECT_EQ(pane->list->data, second);
	EXPECT_EQ(pane->list->next->data, first);
	EXPECT_EQ(pane->list->next->next->data, unlisted);
	vf_collection_move(pane, moving, first);
	ASSERT_TRUE(vf_refresh(pane));
	EXPECT_EQ(pane->list->data, second);
	EXPECT_EQ(g_list_length(cd->list), 3U);
	g_list_free(moving);
	cd->changed = FALSE;
	gtk_window_destroy(GTK_WINDOW(window));
}

TEST_F(CollectionFileSource, BothPaneTypesRefreshAndReturnToDirectory)
{
	if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
	ASSERT_TRUE(gtk_init_check());
	for (FileViewType type : {FILEVIEW_LIST, FILEVIEW_ICON})
		{
		GtkWidget *window = gtk_window_new();
		auto *pane = vf_new(type, nullptr);
		gtk_window_set_child(GTK_WINDOW(window), pane->widget);
		ASSERT_TRUE(vf_set_collection(pane, cd));
		EXPECT_EQ(vf_count(pane), 2U);
		vf_select_all(pane);
		EXPECT_EQ(vf_selection_count(pane, nullptr), 2U);
		g_autoptr(FileDataList) selected = vf_selection_get_list(pane);
		EXPECT_EQ(g_list_length(selected), 2U);
		EXPECT_NE(g_list_find(selected, first), nullptr);
		EXPECT_NE(g_list_find(selected, second), nullptr);
		EXPECT_TRUE(collection_add(cd, unlisted, FALSE));
		EXPECT_NE(pane->refresh_idle_id, 0U);
		EXPECT_TRUE(vf_refresh(pane));
		EXPECT_EQ(vf_count(pane), 3U);
		EXPECT_TRUE(collection_remove(cd, unlisted));
		EXPECT_TRUE(vf_refresh(pane));
		EXPECT_EQ(vf_count(pane), 2U);
		cd->changed = FALSE;
		FileDataRef folder(file_data_new_dir(directory));
		file_data_unref(folder);
		ASSERT_TRUE(vf_set_fd(pane, folder));
		EXPECT_EQ(pane->source, FileViewSource::DIRECTORY);
		EXPECT_EQ(pane->collection, nullptr);
		EXPECT_EQ(vf_count(pane), 0U);
		gtk_window_destroy(GTK_WINDOW(window));
		}
}

} // namespace
