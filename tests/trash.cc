/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <functional>
#include <string>

#include <gio/gunixmounts.h>
#include <glib/gstdio.h>

#include "file-tree-task.h"
#include "filedata.h"
#include "options.h"
#include "trash.h"
#include "ui-fileops.h"
#include "utilops.h"

namespace
{

bool wait_for(const std::function<bool()> &ready)
{
	const gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
	while (!ready() && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		g_usleep(1000);
		}
	return ready();
}

bool has_count_dialog()
{
	GListModel *windows = gtk_window_get_toplevels();
	for (guint i = 0; i < g_list_model_get_n_items(windows); i++)
		{
		g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
		const gchar *title = gtk_window_get_title(window);
		if (title && strstr(title, "Move folder to Trash") != nullptr) return TRUE;
		}
	return FALSE;
}

GtkWidget *find_label(GtkWidget *widget, const gchar *text)
{
	if (GTK_IS_LABEL(widget) && strstr(gtk_label_get_text(GTK_LABEL(widget)), text)) return widget;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		if (auto *found = find_label(child, text)) return found;
	return nullptr;
}

GtkWidget *find_cancel_button(GtkWidget *widget)
{
	if (GTK_IS_BUTTON(widget))
		{
		const gchar *label = gtk_button_get_label(GTK_BUTTON(widget));
		if (label && strstr(label, "Cancel")) return widget;
		}
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		if (auto *found = find_cancel_button(child)) return found;
	return nullptr;
}

class TrashNotificationCapture
{
public:
	~TrashNotificationCapture()
	{
		if (owned)
			{
			g_autoptr(GVariant) reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus",
			                          "/org/freedesktop/DBus", "org.freedesktop.DBus", "ReleaseName",
			                          g_variant_new("(s)", "org.freedesktop.Notifications"), nullptr,
			                          G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
			}
		if (registration) g_dbus_connection_unregister_object(connection, registration);
		g_clear_pointer(&info, g_dbus_node_info_unref);
		g_clear_object(&connection);
		g_free(body);
	}

	bool start()
	{
		connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
		if (!connection) return false;
		info = g_dbus_node_info_new_for_xml(
		    "<node><interface name='org.freedesktop.Notifications'>"
		    "<method name='Notify'>"
		    "<arg type='s' direction='in'/><arg type='u' direction='in'/>"
		    "<arg type='s' direction='in'/><arg type='s' direction='in'/>"
		    "<arg type='s' direction='in'/><arg type='as' direction='in'/>"
		    "<arg type='a{sv}' direction='in'/><arg type='i' direction='in'/>"
		    "<arg type='u' direction='out'/>"
		    "</method></interface></node>", nullptr);
		static const GDBusInterfaceVTable vtable{notify, nullptr, nullptr, {nullptr}};
		registration = g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
		                                                info->interfaces[0], &vtable, this, nullptr, nullptr);
		if (!registration) return false;
		g_autoptr(GVariant) reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus",
		                          "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
		                          g_variant_new("(su)", "org.freedesktop.Notifications", 4u), G_VARIANT_TYPE("(u)"),
		                          G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr);
		if (!reply) return false;
		guint result;
		g_variant_get(reply, "(u)", &result);
		owned = result == 1;
		return owned;
	}

	void drain()
	{
		for (gint i = 0; i < 100; i++)
			{
			g_main_context_iteration(nullptr, FALSE);
			g_usleep(1000);
			}
	}

	guint count = 0;
	gchar *body = nullptr;

private:
	static void notify(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
	                   GVariant *parameters, GDBusMethodInvocation *invocation, gpointer data)
	{
		auto *capture = static_cast<TrashNotificationCapture *>(data);
		g_autoptr(GVariant) body = g_variant_get_child_value(parameters, 4);
		g_free(capture->body);
		capture->body = g_variant_dup_string(body, nullptr);
		capture->count++;
		g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", capture->count));
	}

	GDBusConnection *connection = nullptr;
	GDBusNodeInfo *info = nullptr;
	guint registration = 0;
	bool owned = false;
};

class EmptyFolderDelete : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		previous_app = g_application_get_default();
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.TrashTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		app = GTK_APPLICATION(g_object_ref(test_app));
		g_application_set_default(G_APPLICATION(app));
		if (!options) options = conf_options_new();
		saved_enable = options->file_ops.safe_delete_enable;
		saved_system = options->file_ops.use_system_trash;
		saved_no_trash = options->file_ops.no_trash;
		saved_path = options->file_ops.safe_delete_path;
		saved_limit = options->file_ops.safe_delete_folder_maxsize;
		saved_confirm_delete_dir = options->file_ops.confirm_delete_dir;
		saved_confirm_trash_dir = options->file_ops.confirm_move_dir_to_trash;
		root = g_dir_make_tmp("geeqie-empty-folder-XXXXXX", nullptr);
		ASSERT_NE(root, nullptr);
		folder = g_build_filename(root, "folder", nullptr);
		ASSERT_EQ(g_mkdir(folder, 0700), 0);
		options->file_ops.safe_delete_enable = TRUE;
		options->file_ops.use_system_trash = FALSE;
		options->file_ops.no_trash = FALSE;
		options->file_ops.safe_delete_path = g_build_filename(root, "trash", nullptr);
		options->file_ops.safe_delete_folder_maxsize = 0;
		trashed = g_build_filename(options->file_ops.safe_delete_path, "files", "folder", nullptr);
	}

	void TearDown() override
	{
		if (!root) return;
		file_util_trash_clear();
		g_autofree gchar *files = g_build_filename(options->file_ops.safe_delete_path, "files", nullptr);
		g_autofree gchar *info = g_build_filename(options->file_ops.safe_delete_path, "info", nullptr);
		g_rmdir(files);
		g_rmdir(info);
		g_rmdir(options->file_ops.safe_delete_path);
		g_free(options->file_ops.safe_delete_path);
		options->file_ops.safe_delete_enable = saved_enable;
		options->file_ops.use_system_trash = saved_system;
		options->file_ops.no_trash = saved_no_trash;
		options->file_ops.safe_delete_path = saved_path;
		options->file_ops.safe_delete_folder_maxsize = saved_limit;
		options->file_ops.confirm_delete_dir = saved_confirm_delete_dir;
		options->file_ops.confirm_move_dir_to_trash = saved_confirm_trash_dir;
		g_autoptr(GFile) root_file = g_file_new_for_path(root);
		rmdir_recursive(root_file, nullptr, nullptr);
		g_free(trashed);
		g_free(folder);
		g_free(root);
		GListModel *windows = gtk_window_get_toplevels();
		for (guint i = g_list_model_get_n_items(windows); i > 0; i--)
			{
			g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i - 1));
			if (gtk_window_get_application(window) == app) gtk_window_destroy(window);
			}
		g_application_set_default(previous_app);
		g_clear_object(&app);
	}

	bool delete_folder()
	{
		auto fd = FileData::new_dir(folder);
		if (!file_data_sc_add_ci_delete(fd)) return false;
		const bool result = file_data_sc_perform_ci(fd);
		file_data_sc_free_ci(fd);
		return result;
	}

	GtkApplication *app = nullptr;
	GApplication *previous_app = nullptr;
	gchar *root = nullptr;
	gchar *folder = nullptr;
	gchar *trashed = nullptr;
	gchar *saved_path = nullptr;
	gboolean saved_enable = FALSE;
	gboolean saved_system = FALSE;
	gboolean saved_no_trash = FALSE;
	gint saved_limit = 0;
	gboolean saved_confirm_delete_dir = FALSE;
	gboolean saved_confirm_trash_dir = FALSE;
};

TEST_F(EmptyFolderDelete, PrivateTrashPreservesFolderAndOriginalPath)
{
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	ASSERT_TRUE(isdir(trashed));
	g_autofree gchar *original = file_util_safe_trash_original_path(trashed);
	EXPECT_STREQ(original, folder);
	ASSERT_TRUE(file_util_safe_trash_restore(trashed, TRUE, nullptr));
	EXPECT_TRUE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
}

TEST_F(EmptyFolderDelete, ClearingTrashRemovesEmptyFoldersAndMetadata)
{
	ASSERT_TRUE(delete_folder());
	file_util_trash_clear();
	EXPECT_FALSE(isdir(trashed));
	g_autofree gchar *original = file_util_safe_trash_original_path(trashed);
	EXPECT_EQ(original, nullptr);
}

TEST_F(EmptyFolderDelete, SystemTrashPreservesFolderAndOriginalPath)
{
	// Only touch the system trash inside the test runner's disposable home.
	if (!g_str_has_prefix(g_get_home_dir(), "/tmp/tmp.") ||
	    !g_str_has_prefix(g_get_user_data_dir(), g_get_home_dir()))
		GTEST_SKIP() << "Requires an isolated temporary home";
	options->file_ops.use_system_trash = TRUE;
	ASSERT_EQ(g_rmdir(folder), 0);
	g_free(folder);
	folder = g_build_filename(root, filename_from_path(root), nullptr);
	ASSERT_EQ(g_mkdir(folder, 0700), 0);
	g_autofree gchar *subdir = g_build_filename(folder, "subdir", nullptr);
	g_autofree gchar *hidden = g_build_filename(subdir, ".hidden", nullptr);
	ASSERT_EQ(g_mkdir(subdir, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(hidden, "data", -1, nullptr));
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	g_autofree gchar *system_folder = g_build_filename(g_get_user_data_dir(), "Trash", "files", filename_from_path(folder), nullptr);
	ASSERT_TRUE(isdir(system_folder));
	g_autofree gchar *system_hidden = g_build_filename(system_folder, "subdir", ".hidden", nullptr);
	EXPECT_TRUE(isfile(system_hidden));
	g_autofree gchar *info_name = g_strconcat(filename_from_path(folder), ".trashinfo", nullptr);
	g_autofree gchar *info_path = g_build_filename(g_get_user_data_dir(), "Trash", "info", info_name, nullptr);
	g_autoptr(GKeyFile) info = g_key_file_new();
	ASSERT_TRUE(g_key_file_load_from_file(info, info_path, G_KEY_FILE_NONE, nullptr));
	g_autofree gchar *escaped = g_key_file_get_string(info, "Trash Info", "Path", nullptr);
	g_autofree gchar *original = g_uri_unescape_string(escaped, nullptr);
	EXPECT_STREQ(original, folder);
	EXPECT_TRUE(move_file(system_folder, folder));
	g_unlink(info_path);
}

TEST_F(EmptyFolderDelete, DuplicateFolderNamesKeepBothTrashEntries)
{
	ASSERT_TRUE(delete_folder());
	ASSERT_EQ(g_mkdir(folder, 0700), 0);
	ASSERT_TRUE(delete_folder());
	g_autofree gchar *second = g_build_filename(options->file_ops.safe_delete_path, "files", "folder.2", nullptr);
	EXPECT_TRUE(isdir(trashed));
	EXPECT_TRUE(isdir(second));
}

TEST_F(EmptyFolderDelete, DisabledTrashPermanentlyRemovesFolder)
{
	options->file_ops.safe_delete_enable = FALSE;
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
}

TEST_F(EmptyFolderDelete, PermanentDeleteOverrideRemovesFolder)
{
	options->file_ops.no_trash = TRUE;
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
}

TEST_F(EmptyFolderDelete, NonemptyFolderPreservesWholeTree)
{
	g_autofree gchar *subdir = g_build_filename(folder, "subdir", nullptr);
	g_autofree gchar *hidden = g_build_filename(subdir, ".hidden.unknown", nullptr);
	ASSERT_EQ(g_mkdir(subdir, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(hidden, "data", -1, nullptr));
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	g_autofree gchar *trashed_child = g_build_filename(trashed, "subdir", ".hidden.unknown", nullptr);
	EXPECT_TRUE(isfile(trashed_child));
	g_autofree gchar *files = g_build_filename(options->file_ops.safe_delete_path, "files", nullptr);
	g_autoptr(GDir) directory = g_dir_open(files, 0, nullptr);
	ASSERT_NE(directory, nullptr);
	EXPECT_STREQ(g_dir_read_name(directory), "folder");
	EXPECT_EQ(g_dir_read_name(directory), nullptr);
	ASSERT_TRUE(file_util_safe_trash_restore(trashed, FALSE, nullptr));
	EXPECT_TRUE(isfile(hidden));
	EXPECT_TRUE(isfile(trashed_child));
	g_autoptr(GFile) restored = g_file_new_for_path(folder);
	ASSERT_TRUE(rmdir_recursive(restored, nullptr, nullptr));
	ASSERT_TRUE(file_util_safe_trash_restore(trashed, TRUE, nullptr));
	EXPECT_TRUE(isfile(hidden));
	EXPECT_FALSE(isdir(trashed));
}

TEST_F(EmptyFolderDelete, RecursiveClearDoesNotFollowSymbolicLinks)
{
	g_autofree gchar *outside = g_build_filename(root, "outside", nullptr);
	g_autofree gchar *link = g_build_filename(folder, "link", nullptr);
	g_autofree gchar *broken = g_build_filename(folder, "broken", nullptr);
	ASSERT_TRUE(g_file_set_contents(outside, "keep", -1, nullptr));
	ASSERT_EQ(symlink(outside, link), 0);
	ASSERT_EQ(symlink("missing", broken), 0);
	ASSERT_TRUE(delete_folder());
	file_util_trash_clear();
	EXPECT_FALSE(isdir(trashed));
	EXPECT_TRUE(isfile(outside));
	g_autofree gchar *original = file_util_safe_trash_original_path(trashed);
	EXPECT_EQ(original, nullptr);
}

TEST_F(EmptyFolderDelete, PermanentDeleteRemovesTreeWithoutFollowingDirectoryLink)
{
	options->file_ops.safe_delete_enable = FALSE;
	g_autofree gchar *outside = g_build_filename(root, "outside", nullptr);
	g_autofree gchar *outside_file = g_build_filename(outside, "keep", nullptr);
	g_autofree gchar *link = g_build_filename(folder, "link", nullptr);
	g_autofree gchar *subdir = g_build_filename(folder, "subdir", nullptr);
	g_autofree gchar *child = g_build_filename(subdir, "child", nullptr);
	ASSERT_EQ(g_mkdir(outside, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(outside_file, "keep", -1, nullptr));
	ASSERT_EQ(symlink(outside, link), 0);
	ASSERT_EQ(g_mkdir(subdir, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	ASSERT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	EXPECT_TRUE(isfile(outside_file));
}

TEST_F(EmptyFolderDelete, CountsIncludeHiddenAndUnrecognizedFilesWithoutFollowingLinks)
{
	g_autofree gchar *subdir = g_build_filename(folder, "subdir", nullptr);
	g_autofree gchar *hidden = g_build_filename(subdir, ".hidden", nullptr);
	g_autofree gchar *unknown = g_build_filename(folder, "file.unknown", nullptr);
	g_autofree gchar *link = g_build_filename(folder, "link", nullptr);
	ASSERT_EQ(g_mkdir(subdir, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(hidden, "abc", -1, nullptr));
	ASSERT_TRUE(g_file_set_contents(unknown, "defg", -1, nullptr));
	ASSERT_EQ(symlink(subdir, link), 0);
	g_autoptr(GFile) tree = g_file_new_for_path(folder);
	FileTreeStats stats;
	ASSERT_TRUE(file_tree_stats(tree, stats, nullptr));
	EXPECT_EQ(stats.directories, 1);
	EXPECT_EQ(stats.files, 3);
	EXPECT_EQ(stats.bytes, 7 + strlen(subdir));
}

TEST_F(EmptyFolderDelete, CopyRefusesExistingDestination)
{
	g_autofree gchar *dest = g_build_filename(root, "dest", nullptr);
	g_autofree gchar *keep = g_build_filename(dest, "keep", nullptr);
	ASSERT_EQ(g_mkdir(dest, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(keep, "data", -1, nullptr));
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GFile) destination = g_file_new_for_path(dest);
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_tree_copy(source, destination, &error));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS));
	EXPECT_TRUE(isfile(keep));
}

TEST_F(EmptyFolderDelete, CopyRejectsDestinationInsideSourceThroughAlias)
{
	g_autofree gchar *alias = g_build_filename(root, "alias", nullptr);
	ASSERT_EQ(symlink(folder, alias), 0);
	g_autofree gchar *dest = g_build_filename(alias, "copy", nullptr);
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GFile) destination = g_file_new_for_path(dest);
	EXPECT_FALSE(file_tree_copy(source, destination, nullptr));
	EXPECT_FALSE(isdir(dest));
}

TEST_F(EmptyFolderDelete, SizeLimitEvictsCompleteTrees)
{
	TrashNotificationCapture notification;
	ASSERT_TRUE(notification.start());
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	g_autofree gchar *data = g_strnfill(700000, 'a');
	ASSERT_TRUE(g_file_set_contents(child, data, 700000, nullptr));
	ASSERT_TRUE(delete_folder());
	ASSERT_EQ(g_mkdir(folder, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(child, data, 700000, nullptr));
	options->file_ops.safe_delete_folder_maxsize = 1;
	ASSERT_TRUE(delete_folder());
	// The old tree was evicted as a unit; the new one uses the now-free original name.
	EXPECT_TRUE(isdir(trashed));
	g_autofree gchar *second = g_build_filename(options->file_ops.safe_delete_path, "files", "folder.2", nullptr);
	EXPECT_FALSE(isdir(second));
	g_autofree gchar *trashed_child = g_build_filename(trashed, "child", nullptr);
	EXPECT_EQ(filesize(trashed_child), 700000);
	notification.drain();
	ASSERT_EQ(notification.count, 1);
	ASSERT_NE(notification.body, nullptr);
	EXPECT_NE(strstr(notification.body, "Entries permanently deleted: 1"), nullptr);
	g_autofree gchar *size = g_format_size_full(700000, G_FORMAT_SIZE_IEC_UNITS);
	EXPECT_NE(strstr(notification.body, size), nullptr);
	file_util_trash_clear();
	notification.drain();
	EXPECT_EQ(notification.count, 1);

}

TEST_F(EmptyFolderDelete, FolderTrashWithoutConfirmationUsesWholeTreeOperation)
{
	options->file_ops.confirm_move_dir_to_trash = FALSE;
	options->file_ops.confirm_delete_dir = TRUE;
	g_autofree gchar *subdir = g_build_filename(folder, "subdir", nullptr);
	ASSERT_EQ(g_mkdir(subdir, 0700), 0);
	auto *fd = file_data_new_dir(folder);
	file_util_delete_dir(fd, nullptr);
	ASSERT_TRUE(wait_for([fd] { return fd->change == nullptr; }));
	EXPECT_FALSE(isdir(folder));
	EXPECT_TRUE(isdir(trashed));
	EXPECT_EQ(fd->change, nullptr);
	file_data_unref(fd);
}

TEST_F(EmptyFolderDelete, PermanentFolderDeleteWithoutConfirmationUsesSeparatePreference)
{
	options->file_ops.no_trash = TRUE;
	options->file_ops.confirm_delete_dir = FALSE;
	options->file_ops.confirm_move_dir_to_trash = TRUE;
	g_autofree gchar *subdir = g_build_filename(folder, "subdir", nullptr);
	ASSERT_EQ(g_mkdir(subdir, 0700), 0);
	auto *fd = file_data_new_dir(folder);
	file_util_delete_dir(fd, nullptr);
	ASSERT_TRUE(wait_for([fd] { return fd->change == nullptr; }));
	EXPECT_FALSE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
	EXPECT_EQ(fd->change, nullptr);
	file_data_unref(fd);
}

TEST_F(EmptyFolderDelete, IncompleteScanAndFailedClearRetainRestorationMetadata)
{
	if (geteuid() == 0) GTEST_SKIP() << "Requires permission enforcement";
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	ASSERT_TRUE(delete_folder());
	ASSERT_EQ(g_chmod(trashed, 0000), 0);
	g_autoptr(GFile) tree = g_file_new_for_path(trashed);
	FileTreeStats stats;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_tree_stats(tree, stats, &error));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED));
	file_util_trash_clear();
	GListModel *windows = gtk_window_get_toplevels();
	bool recovery_location_shown = false;
	for (guint i = 0; i < g_list_model_get_n_items(windows); i++)
		{
		g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
		if (find_label(GTK_WIDGET(window), trashed)) recovery_location_shown = true;
		}
	EXPECT_TRUE(recovery_location_shown);
	g_autofree gchar *original = file_util_safe_trash_original_path(trashed);
	EXPECT_STREQ(original, folder);
	ASSERT_EQ(g_chmod(trashed, 0700), 0);
}

TEST_F(EmptyFolderDelete, ConfirmationShowsRecursiveCountsAndCancelLeavesTreeIntact)
{
	options->file_ops.confirm_move_dir_to_trash = TRUE;
	g_autofree gchar *subdir = g_build_filename(folder, "subdir", nullptr);
	g_autofree gchar *child = g_build_filename(subdir, ".hidden.unknown", nullptr);
	ASSERT_EQ(g_mkdir(subdir, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	auto *fd = file_data_new_dir(folder);
	file_util_delete_dir(fd, nullptr);
	ASSERT_TRUE(wait_for(has_count_dialog));
	EXPECT_TRUE(isfile(child));
	ASSERT_NE(fd->change, nullptr);
	GListModel *windows = gtk_window_get_toplevels();
	GtkWidget *cancel = nullptr;
	for (guint i = 0; i < g_list_model_get_n_items(windows); i++)
		{
		g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
		if (find_label(GTK_WIDGET(window), "Subfolders: 1"))
			{
			EXPECT_NE(find_label(GTK_WIDGET(window), "symbolic links): 1"), nullptr);
			cancel = find_cancel_button(GTK_WIDGET(window));
			break;
			}
		}
	ASSERT_NE(cancel, nullptr);
	g_signal_emit_by_name(cancel, "clicked");
	EXPECT_EQ(fd->change, nullptr);
	EXPECT_TRUE(isfile(child));
	EXPECT_FALSE(isdir(trashed));
	file_data_unref(fd);
}

TEST_F(EmptyFolderDelete, FailedCopyKeepsSourceAndRemovesPartialDestination)
{
	if (geteuid() == 0) GTEST_SKIP() << "Requires permission enforcement";
	g_autofree gchar *child = g_build_filename(folder, "unreadable", nullptr);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	ASSERT_EQ(g_chmod(child, 0000), 0);
	g_autofree gchar *dest_path = g_build_filename(root, "copy", nullptr);
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GFile) dest = g_file_new_for_path(dest_path);
	EXPECT_FALSE(file_tree_copy(source, dest, nullptr));
	EXPECT_TRUE(isfile(child));
	EXPECT_FALSE(isdir(dest_path));
	ASSERT_EQ(g_chmod(child, 0600), 0);
}

TEST_F(EmptyFolderDelete, OversizedTreeIsRejectedBeforeEvictingOlderEntries)
{
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	ASSERT_TRUE(g_file_set_contents(child, "old", -1, nullptr));
	ASSERT_TRUE(delete_folder());
	g_autofree gchar *trashed_child = g_build_filename(trashed, "child", nullptr);
	g_autofree gchar *legacy = g_build_filename(options->file_ops.safe_delete_path, "000001_legacy", nullptr);
	ASSERT_TRUE(g_file_set_contents(legacy, "legacy", -1, nullptr));
	ASSERT_EQ(g_mkdir(folder, 0700), 0);
	g_autofree gchar *data = g_strnfill(1100000, 'a');
	ASSERT_TRUE(g_file_set_contents(child, data, 1100000, nullptr));
	options->file_ops.safe_delete_folder_maxsize = 1;
	TrashReport report;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_util_safe_unlink_full(folder, file_util_trash_settings(), nullptr, report, &error));
	ASSERT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE));
	EXPECT_NE(strstr(error->message, "too large for Geeqie Trash"), nullptr);
	EXPECT_EQ(filesize(child), 1100000);
	EXPECT_EQ(filesize(trashed_child), 3);
	EXPECT_EQ(filesize(legacy), 6);
	EXPECT_EQ(report.removed_entries, 0);
	EXPECT_EQ(report.removed_bytes, 0);
	g_autofree gchar *original = file_util_safe_trash_original_path(trashed);
	EXPECT_STREQ(original, folder);
	g_autofree gchar *second_info = g_build_filename(options->file_ops.safe_delete_path, "info", "folder.2.trashinfo", nullptr);
	EXPECT_FALSE(isfile(second_info));
}

TEST_F(EmptyFolderDelete, OversizedFileIsRejectedBeforeCreatingTrash)
{
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	g_autofree gchar *data = g_strnfill(1100000, 'a');
	ASSERT_TRUE(g_file_set_contents(child, data, 1100000, nullptr));
	options->file_ops.safe_delete_folder_maxsize = 1;
	TrashReport report;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_util_safe_unlink_full(child, file_util_trash_settings(), nullptr, report, &error));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE));
	EXPECT_EQ(filesize(child), 1100000);
	EXPECT_FALSE(isdir(options->file_ops.safe_delete_path));
}

TEST_F(EmptyFolderDelete, ItemExactlyAtTrashLimitIsAccepted)
{
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	g_autofree gchar *data = g_strnfill(1048576, 'a');
	ASSERT_TRUE(g_file_set_contents(child, data, 1048576, nullptr));
	options->file_ops.safe_delete_folder_maxsize = 1;
	EXPECT_TRUE(delete_folder());
	EXPECT_FALSE(isdir(folder));
	g_autofree gchar *trashed_child = g_build_filename(trashed, "child", nullptr);
	EXPECT_EQ(filesize(trashed_child), 1048576);
}

TEST_F(EmptyFolderDelete, DirectorySymbolicLinkIsTrashedAndRestoredWithoutDeletingTarget)
{
	options->file_ops.confirm_move_dir_to_trash = FALSE;
	g_autofree gchar *link = g_build_filename(root, "directory-link", nullptr);
	ASSERT_EQ(symlink(folder, link), 0);
	auto *fd = file_data_new_dir(link);
	file_util_delete_dir(fd, nullptr);
	ASSERT_TRUE(wait_for([fd] { return fd->change == nullptr; }));
	EXPECT_TRUE(isdir(folder));
	EXPECT_FALSE(islink(link));
	g_autofree gchar *trashed_link = g_build_filename(options->file_ops.safe_delete_path, "files", "directory-link", nullptr);
	EXPECT_TRUE(islink(trashed_link));
	ASSERT_TRUE(file_util_safe_trash_restore(trashed_link, TRUE, nullptr));
	EXPECT_TRUE(islink(link));
	EXPECT_TRUE(isdir(folder));
	file_data_unref(fd);
}

TEST_F(EmptyFolderDelete, RestoreRefusesConflictAndKeepsTrashMetadata)
{
	ASSERT_TRUE(delete_folder());
	ASSERT_EQ(g_mkdir(folder, 0700), 0);
	g_autofree gchar *keep = g_build_filename(folder, "keep", nullptr);
	ASSERT_TRUE(g_file_set_contents(keep, "data", -1, nullptr));
	EXPECT_FALSE(file_util_safe_trash_restore(trashed, TRUE, nullptr));
	EXPECT_TRUE(isfile(keep));
	EXPECT_TRUE(isdir(trashed));
	g_autofree gchar *original = file_util_safe_trash_original_path(trashed);
	EXPECT_STREQ(original, folder);
}

TEST_F(EmptyFolderDelete, CrossFilesystemMovePreservesTreeAndRelativeLinks)
{
	const gchar *test_root = g_getenv("GQ_TEST_CROSS_FILESYSTEM_ROOT");
	if (!test_root) GTEST_SKIP() << "Set GQ_TEST_CROSS_FILESYSTEM_ROOT to a writable second filesystem";
	struct TemporaryTree
	{
		gchar *path;
		~TemporaryTree()
		{
			if (!path) return;
			g_autoptr(GFile) file = g_file_new_for_path(path);
			rmdir_recursive(file, nullptr, nullptr);
			g_free(path);
		}
	} temporary{g_build_filename(test_root, "geeqie-tree-XXXXXX", nullptr)};
	ASSERT_NE(g_mkdtemp(temporary.path), nullptr);
	GStatBuf source_stat;
	GStatBuf dest_stat;
	ASSERT_TRUE(stat_utf8(root, &source_stat));
	ASSERT_TRUE(stat_utf8(temporary.path, &dest_stat));
	if (source_stat.st_dev == dest_stat.st_dev) GTEST_SKIP() << "Requires different filesystems";
	g_autofree gchar *subdir = g_build_filename(folder, "subdir", nullptr);
	g_autofree gchar *child = g_build_filename(subdir, ".hidden", nullptr);
	g_autofree gchar *link = g_build_filename(folder, "link", nullptr);
	ASSERT_EQ(g_mkdir(subdir, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	ASSERT_EQ(symlink("subdir/.hidden", link), 0);
	g_autofree gchar *dest_path = g_build_filename(temporary.path, "tree", nullptr);
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GFile) dest = g_file_new_for_path(dest_path);
	g_autoptr(GError) error = nullptr;
	ASSERT_TRUE(file_tree_move(source, dest, &error)) << (error ? error->message : "");
	EXPECT_FALSE(isdir(folder));
	g_autofree gchar *dest_child = g_build_filename(dest_path, "subdir", ".hidden", nullptr);
	g_autofree gchar *dest_link = g_build_filename(dest_path, "link", nullptr);
	EXPECT_TRUE(isfile(dest_child));
	g_autofree gchar *target = g_file_read_link(dest_link, nullptr);
	EXPECT_STREQ(target, "subdir/.hidden");
	ASSERT_TRUE(file_tree_move(dest, source, nullptr));
	EXPECT_TRUE(isfile(child));
}

} // namespace

TEST_F(EmptyFolderDelete, CancelledScanLeavesTreeIntact)
{
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	FileTreeOperation operation{cancellable, [](GFile *, gpointer data)
		{
		g_cancellable_cancel(G_CANCELLABLE(data));
		}, cancellable};
	FileTreeStats stats;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_tree_stats(source, stats, &error, &operation));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
	EXPECT_TRUE(isfile(child));
}

TEST_F(EmptyFolderDelete, CancelBeforeFirstRemovalLeavesTreeIntact)
{
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	FileTreeOperation operation;
	operation.cancellable = cancellable;
	operation.data = &operation;
	operation.progress = [](GFile *, gpointer data)
		{
		auto *operation = static_cast<FileTreeOperation *>(data);
		if (operation->phase == FileTreePhase::REMOVE) g_cancellable_cancel(operation->cancellable);
		};
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(rmdir_recursive(source, cancellable, &error, &operation));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
	EXPECT_TRUE(isfile(child));
}

TEST_F(EmptyFolderDelete, CancelledCopyRemovesPartialDestination)
{
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	g_autofree gchar *destination_path = g_build_filename(root, "copy", nullptr);
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GFile) destination = g_file_new_for_path(destination_path);
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	FileTreeOperation operation;
	operation.cancellable = cancellable;
	operation.data = &operation;
	operation.progress = [](GFile *file, gpointer data)
		{
		auto *operation = static_cast<FileTreeOperation *>(data);
		g_autofree gchar *name = g_file_get_basename(file);
		if (operation->phase == FileTreePhase::COPY && strcmp(name, "child") == 0)
			g_cancellable_cancel(operation->cancellable);
		};
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_tree_copy(source, destination, &error, &operation));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
	ASSERT_NE(error, nullptr);
	EXPECT_NE(strstr(error->message, "Copying did not complete"), nullptr);
	EXPECT_NE(strstr(error->message, "The incomplete copy was removed"), nullptr);
	EXPECT_NE(strstr(error->message, folder), nullptr);
	EXPECT_NE(strstr(error->message, destination_path), nullptr);
	EXPECT_TRUE(isfile(child));
	EXPECT_FALSE(isdir(destination_path));
}

TEST_F(EmptyFolderDelete, ModifiedSourceAfterCopyIsNotDeleted)
{
	const gchar *cross_root = g_getenv("GQ_TEST_CROSS_FILESYSTEM_ROOT");
	if (!cross_root) GTEST_SKIP() << "Set GQ_TEST_CROSS_FILESYSTEM_ROOT to a writable different filesystem";
	g_autofree gchar *template_path = g_build_filename(cross_root, "geeqie-changed-tree-XXXXXX", nullptr);
	ASSERT_NE(g_mkdtemp(template_path), nullptr);
	g_autofree gchar *destination_path = g_build_filename(template_path, "copy", nullptr);
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	ASSERT_TRUE(g_file_set_contents(child, "original", -1, nullptr));
	GStatBuf source_stat;
	GStatBuf dest_stat;
	ASSERT_TRUE(stat_utf8(folder, &source_stat));
	ASSERT_TRUE(stat_utf8(template_path, &dest_stat));
	if (source_stat.st_dev == dest_stat.st_dev)
		{
		g_rmdir(template_path);
		GTEST_SKIP() << "Requires different filesystems";
		}
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GFile) destination = g_file_new_for_path(destination_path);
	FileTreeOperation operation;
	operation.data = &operation;
	operation.progress = [](GFile *file, gpointer data)
		{
		auto *operation = static_cast<FileTreeOperation *>(data);
		if (operation->phase == FileTreePhase::REMOVE)
			{
			g_autofree gchar *path = g_file_get_path(file);
			const int descriptor = g_open(path, O_WRONLY, 0);
			if (descriptor >= 0)
				{
				EXPECT_EQ(write(descriptor, "modified", 8), 8);
				close(descriptor);
				}
			}
		};
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_tree_move(source, destination, &error, &operation));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_BUSY));
	ASSERT_NE(error, nullptr);
	EXPECT_NE(strstr(error->message, "Copying completed, but source removal did not finish"), nullptr);
	EXPECT_NE(strstr(error->message, "Source entries removed: 0"), nullptr);
	EXPECT_NE(strstr(error->message, folder), nullptr);
	EXPECT_NE(strstr(error->message, destination_path), nullptr);
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(child, &contents, nullptr, nullptr));
	EXPECT_STREQ(contents, "modified");
	g_autofree gchar *copy_child = g_build_filename(destination_path, "child", nullptr);
	g_autofree gchar *copy_contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(copy_child, &copy_contents, nullptr, nullptr));
	EXPECT_STREQ(copy_contents, "original");
	EXPECT_TRUE(rmdir_recursive(destination, nullptr, nullptr));
	g_rmdir(template_path);
}

TEST_F(EmptyFolderDelete, MountRootIsRejectedBeforeEnumeration)
{
	// Read-only check: never invoke deletion on a real mount in a test.
	g_autoptr(GFile) mount = g_file_new_for_path("/");
	FileTreeStats stats;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_tree_stats(mount, stats, &error));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED));
	EXPECT_EQ(stats.files, 0);
	EXPECT_EQ(stats.directories, 0);
}

TEST_F(EmptyFolderDelete, BackgroundTaskKeepsMainLoopResponsiveAndCanBeCancelled)
{
	bool done = false;
	bool cancelled = false;
	file_tree_task_run("Test cancellable folder task", nullptr,
		[](FileTreeOperation *operation, GError **error)
			{
			while (!g_cancellable_is_cancelled(operation->cancellable)) g_usleep(1000);
			return !g_cancellable_set_error_if_cancelled(operation->cancellable, error);
			},
		[&done, &cancelled](gboolean success, const GError *error)
			{
			done = true;
			cancelled = !success && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
			});
	GListModel *windows = gtk_window_get_toplevels();
	GtkWidget *cancel = nullptr;
	for (guint i = 0; i < g_list_model_get_n_items(windows); i++)
		{
		g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
		const gchar *title = gtk_window_get_title(window);
		if (title && strstr(title, "Test cancellable folder task"))
			cancel = find_cancel_button(GTK_WIDGET(window));
		}
	ASSERT_NE(cancel, nullptr);
	bool heartbeat = false;
	g_idle_add([](gpointer data) -> gboolean
		{
		*static_cast<bool *>(data) = true;
		return G_SOURCE_REMOVE;
		}, &heartbeat);
	EXPECT_TRUE(wait_for([&heartbeat] { return heartbeat; }));
	g_signal_emit_by_name(cancel, "clicked");
	EXPECT_TRUE(wait_for([&done] { return done; }));
	EXPECT_TRUE(cancelled);
}

TEST_F(EmptyFolderDelete, ChangingPreferencesAfterRequestDoesNotChangeDeletionMode)
{
	options->file_ops.confirm_move_dir_to_trash = TRUE;
	auto *fd = file_data_new_dir(folder);
	file_util_delete_dir(fd, nullptr);
	// Changing another operation's mode must not turn this request into permanent deletion.
	options->file_ops.no_trash = TRUE;
	ASSERT_TRUE(wait_for(has_count_dialog));
	GListModel *windows = gtk_window_get_toplevels();
	GtkWidget *confirm = nullptr;
	for (guint i = 0; i < g_list_model_get_n_items(windows); i++)
		{
		g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
		if (find_label(GTK_WIDGET(window), "Subfolders: 0"))
			{
			EXPECT_NE(find_label(GTK_WIDGET(window), "Using Geeqie Trash bin"), nullptr);
			confirm = gtk_window_get_default_widget(window);
			}
		}
	ASSERT_NE(confirm, nullptr);
	g_signal_emit_by_name(confirm, "clicked");
	ASSERT_TRUE(wait_for([fd] { return fd->change == nullptr; }));
	EXPECT_FALSE(isdir(folder));
	EXPECT_TRUE(isdir(trashed));
	file_data_unref(fd);
}

TEST_F(EmptyFolderDelete, PermanentDeletionHandlesHardLinkedFiles)
{
	g_autofree gchar *first = g_build_filename(folder, "first", nullptr);
	g_autofree gchar *second = g_build_filename(folder, "second", nullptr);
	ASSERT_TRUE(g_file_set_contents(first, "data", -1, nullptr));
	ASSERT_EQ(link(first, second), 0);
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GError) error = nullptr;
	EXPECT_TRUE(rmdir_recursive(source, nullptr, &error));
	EXPECT_EQ(error, nullptr);
	EXPECT_FALSE(isdir(folder));
}

TEST_F(EmptyFolderDelete, ReplacedAncestorIsNotTraversedDuringRemoval)
{
	g_autofree gchar *subdir = g_build_filename(folder, "subdir", nullptr);
	g_autofree gchar *child = g_build_filename(subdir, "child", nullptr);
	g_autofree gchar *outside = g_build_filename(root, "outside", nullptr);
	g_autofree gchar *outside_child = g_build_filename(outside, "child", nullptr);
	g_autofree gchar *saved = g_build_filename(root, "saved-subdir", nullptr);
	ASSERT_EQ(g_mkdir(subdir, 0700), 0);
	ASSERT_EQ(g_mkdir(outside, 0700), 0);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	ASSERT_TRUE(g_file_set_contents(outside_child, "outside", -1, nullptr));
	struct Replacement
	{
		FileTreeOperation operation;
		const gchar *subdir;
		const gchar *saved;
		const gchar *outside;
		bool replaced = false;
	} replacement{{}, subdir, saved, outside};
	replacement.operation.data = &replacement;
	replacement.operation.progress = [](GFile *, gpointer data)
		{
		auto *replacement = static_cast<Replacement *>(data);
		if (replacement->operation.phase == FileTreePhase::REMOVE && !replacement->replaced)
			{
			EXPECT_EQ(g_rename(replacement->subdir, replacement->saved), 0);
			EXPECT_EQ(symlink(replacement->outside, replacement->subdir), 0);
			replacement->replaced = true;
			}
		};
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(rmdir_recursive(source, nullptr, &error, &replacement.operation));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_BUSY));
	EXPECT_TRUE(replacement.replaced);
	EXPECT_TRUE(isfile(outside_child));
	g_autofree gchar *saved_child = g_build_filename(saved, "child", nullptr);
	EXPECT_TRUE(isfile(saved_child));
}

TEST_F(EmptyFolderDelete, MountedFileIsRejectedWithoutReadingItsContents)
{
	GList *mounts = g_unix_mounts_get(nullptr);
	std::string mounted_file;
	for (GList *item = mounts; item; item = item->next)
		{
		const gchar *path = g_unix_mount_get_mount_path(static_cast<GUnixMountEntry *>(item->data));
		GStatBuf stat_buf;
		if (lstat_utf8(path, &stat_buf) && S_ISREG(stat_buf.st_mode))
			{
			mounted_file = path;
			break;
			}
		}
	g_list_free_full(mounts, reinterpret_cast<GDestroyNotify>(g_unix_mount_free));
	if (mounted_file.empty()) GTEST_SKIP() << "No mounted regular file is available";
	g_autoptr(GFile) source = g_file_new_for_path(mounted_file.c_str());
	FileTreeStats stats;
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_tree_stats(source, stats, &error));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED));
	EXPECT_EQ(stats.bytes, 0);
}

TEST_F(EmptyFolderDelete, CancelledBackgroundCountReleasesPendingChange)
{
	options->file_ops.confirm_move_dir_to_trash = TRUE;
	auto *fd = file_data_new_dir(folder);
	file_util_delete_dir(fd, nullptr);
	GListModel *windows = gtk_window_get_toplevels();
	GtkWidget *cancel = nullptr;
	for (guint i = 0; i < g_list_model_get_n_items(windows); i++)
		{
		g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
		const gchar *title = gtk_window_get_title(window);
		if (title && strstr(title, "Checking folder contents")) cancel = find_cancel_button(GTK_WIDGET(window));
		}
	ASSERT_NE(cancel, nullptr);
	g_signal_emit_by_name(cancel, "clicked");
	ASSERT_TRUE(wait_for([fd] { return fd->change == nullptr; }));
	EXPECT_TRUE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
	EXPECT_FALSE(has_count_dialog());
	file_data_unref(fd);
}

TEST_F(EmptyFolderDelete, MultipleBackgroundRestoresCompleteWithoutConflict)
{
	ASSERT_TRUE(delete_folder());
	g_autofree gchar *second = g_build_filename(root, "second", nullptr);
	ASSERT_EQ(g_mkdir(second, 0700), 0);
	ASSERT_TRUE(file_util_safe_unlink(second));
	g_autofree gchar *second_trashed = g_build_filename(options->file_ops.safe_delete_path, "files", "second", nullptr);
	file_util_safe_trash_restore_async(trashed, TRUE, nullptr);
	file_util_safe_trash_restore_async(second_trashed, TRUE, nullptr);
	ASSERT_TRUE(wait_for([this, second]
		{
		if (!isdir(folder) || !isdir(second)) return false;
		GListModel *windows = gtk_window_get_toplevels();
		for (guint i = 0; i < g_list_model_get_n_items(windows); i++)
			{
			g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
			const gchar *title = gtk_window_get_title(window);
			if (title && strstr(title, "Restore from Trash")) return false;
			}
		return true;
		}));
	EXPECT_FALSE(isdir(trashed));
	EXPECT_FALSE(isdir(second_trashed));
}

TEST_F(EmptyFolderDelete, FailedCopyReportsRetainedIncompleteDestination)
{
	if (geteuid() == 0) GTEST_SKIP() << "Requires permission enforcement";
	g_autofree gchar *child = g_build_filename(folder, "child", nullptr);
	ASSERT_TRUE(g_file_set_contents(child, "data", -1, nullptr));
	g_autofree gchar *destination_path = g_build_filename(root, "incomplete-copy", nullptr);
	struct CopyFailure
	{
		FileTreeOperation operation;
		const gchar *destination;
	} failure{{}, destination_path};
	failure.operation.data = &failure;
	failure.operation.progress = [](GFile *file, gpointer data)
		{
		auto *failure = static_cast<CopyFailure *>(data);
		g_autofree gchar *name = g_file_get_basename(file);
		if (failure->operation.phase == FileTreePhase::COPY && strcmp(name, "child") == 0)
			{
			EXPECT_EQ(g_chmod(failure->destination, 0000), 0);
			}
		};
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GFile) destination = g_file_new_for_path(destination_path);
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_tree_copy(source, destination, &error, &failure.operation));
	EXPECT_TRUE(isfile(child));
	EXPECT_TRUE(isdir(destination_path));
	// Restore permissions before assertions that could abort fixture cleanup.
	EXPECT_EQ(g_chmod(destination_path, 0700), 0);
	ASSERT_NE(error, nullptr);
	EXPECT_NE(strstr(error->message, "Copying did not complete"), nullptr);
	EXPECT_NE(strstr(error->message, "An incomplete copy remains at"), nullptr);
	EXPECT_NE(strstr(error->message, destination_path), nullptr);
	EXPECT_NE(strstr(error->message, folder), nullptr);
}

TEST_F(EmptyFolderDelete, CancelledSourceRemovalReportsCompletedCopyAndRemainingSource)
{
	const gchar *cross_root = g_getenv("GQ_TEST_CROSS_FILESYSTEM_ROOT");
	if (!cross_root) GTEST_SKIP() << "Requires a writable second filesystem";
	g_autofree gchar *temporary = g_build_filename(cross_root, "geeqie-partial-move-XXXXXX", nullptr);
	ASSERT_NE(g_mkdtemp(temporary), nullptr);
	GStatBuf source_stat;
	GStatBuf destination_stat;
	ASSERT_TRUE(stat_utf8(folder, &source_stat));
	ASSERT_TRUE(stat_utf8(temporary, &destination_stat));
	if (source_stat.st_dev == destination_stat.st_dev)
		{
		g_rmdir(temporary);
		GTEST_SKIP() << "Requires different filesystems";
		}
	g_autofree gchar *first = g_build_filename(folder, "first", nullptr);
	g_autofree gchar *second = g_build_filename(folder, "second", nullptr);
	ASSERT_TRUE(g_file_set_contents(first, "first", -1, nullptr));
	ASSERT_TRUE(g_file_set_contents(second, "second", -1, nullptr));
	g_autofree gchar *destination_path = g_build_filename(temporary, "copy", nullptr);
	g_autoptr(GFile) source = g_file_new_for_path(folder);
	g_autoptr(GFile) destination = g_file_new_for_path(destination_path);
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	struct RemovalFailure
	{
		FileTreeOperation operation;
		guint calls = 0;
	} failure;
	failure.operation.cancellable = cancellable;
	failure.operation.data = &failure;
	failure.operation.progress = [](GFile *, gpointer data)
		{
		auto *failure = static_cast<RemovalFailure *>(data);
		if (failure->operation.phase == FileTreePhase::REMOVE && ++failure->calls == 2)
			g_cancellable_cancel(failure->operation.cancellable);
		};
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(file_tree_move(source, destination, &error, &failure.operation));
	EXPECT_TRUE(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
	FileTreeStats remaining;
	FileTreeStats copied;
	EXPECT_TRUE(file_tree_stats(source, remaining, nullptr));
	EXPECT_TRUE(file_tree_stats(destination, copied, nullptr));
	EXPECT_EQ(remaining.files, 1);
	EXPECT_EQ(copied.files, 2);
	EXPECT_EQ(copied.bytes, 11);
	if (error)
		{
		EXPECT_NE(strstr(error->message, "Copying completed, but source removal did not finish"), nullptr);
		EXPECT_NE(strstr(error->message, "Source entries removed: 1"), nullptr);
		EXPECT_NE(strstr(error->message, destination_path), nullptr);
		EXPECT_NE(strstr(error->message, folder), nullptr);
		}
	EXPECT_TRUE(rmdir_recursive(destination, nullptr, nullptr));
	g_rmdir(temporary);
}

TEST_F(EmptyFolderDelete, ExplicitPermanentDeletionBypassesEnabledTrash)
{
	options->file_ops.confirm_delete_dir = FALSE;
	options->file_ops.confirm_move_dir_to_trash = TRUE;
	auto *fd = file_data_new_dir(folder);
	file_util_delete_dir(fd, nullptr, FALSE);
	ASSERT_TRUE(wait_for([fd] { return fd->change == nullptr; }));
	EXPECT_FALSE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
	EXPECT_TRUE(options->file_ops.safe_delete_enable);
	EXPECT_FALSE(options->file_ops.no_trash);
	file_data_unref(fd);
}

TEST_F(EmptyFolderDelete, ExplicitTrashOverridesPermanentDefaultWithoutChangingSettings)
{
	options->file_ops.no_trash = TRUE;
	options->file_ops.safe_delete_enable = FALSE;
	options->file_ops.confirm_delete_dir = TRUE;
	options->file_ops.confirm_move_dir_to_trash = FALSE;
	auto *fd = file_data_new_dir(folder);
	file_util_delete_dir(fd, nullptr, TRUE);
	ASSERT_TRUE(wait_for([fd] { return fd->change == nullptr; }));
	EXPECT_FALSE(isdir(folder));
	EXPECT_TRUE(isdir(trashed));
	EXPECT_FALSE(options->file_ops.safe_delete_enable);
	EXPECT_TRUE(options->file_ops.no_trash);
	file_data_unref(fd);
}

TEST_F(EmptyFolderDelete, ExplicitPermanentDeletionUsesPermanentConfirmation)
{
	options->file_ops.confirm_delete_dir = TRUE;
	options->file_ops.confirm_move_dir_to_trash = FALSE;
	auto *fd = file_data_new_dir(folder);
	file_util_delete_dir(fd, nullptr, FALSE);
	GtkWidget *confirmation = nullptr;
	ASSERT_TRUE(wait_for([&confirmation]
		{
		GListModel *windows = gtk_window_get_toplevels();
		for (guint i = 0; i < g_list_model_get_n_items(windows); i++)
			{
			g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
			if (find_label(GTK_WIDGET(window), "Permanently delete folder?"))
				{
				confirmation = GTK_WIDGET(window);
				return true;
				}
			}
		return false;
		}));
	EXPECT_NE(find_label(gtk_window_get_default_widget(GTK_WINDOW(confirmation)), "Permanently Delete"), nullptr);
	GtkWidget *cancel = find_cancel_button(confirmation);
	ASSERT_NE(cancel, nullptr);
	g_signal_emit_by_name(cancel, "clicked");
	EXPECT_EQ(fd->change, nullptr);
	EXPECT_TRUE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
	file_data_unref(fd);
}

TEST_F(EmptyFolderDelete, ExplicitTrashUsesTrashConfirmationWithPermanentDefault)
{
	options->file_ops.no_trash = TRUE;
	options->file_ops.confirm_delete_dir = FALSE;
	options->file_ops.confirm_move_dir_to_trash = TRUE;
	auto *fd = file_data_new_dir(folder);
	file_util_delete_dir(fd, nullptr, TRUE);
	ASSERT_TRUE(wait_for(has_count_dialog));
	GListModel *windows = gtk_window_get_toplevels();
	GtkWidget *cancel = nullptr;
	for (guint i = 0; i < g_list_model_get_n_items(windows); i++)
		{
		g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
		if (find_label(GTK_WIDGET(window), "Move folder to Trash?"))
			{
			EXPECT_NE(find_label(gtk_window_get_default_widget(window), "Move to Trash"), nullptr);
			cancel = find_cancel_button(GTK_WIDGET(window));
			break;
			}
		}
	ASSERT_NE(cancel, nullptr);
	g_signal_emit_by_name(cancel, "clicked");
	EXPECT_EQ(fd->change, nullptr);
	EXPECT_TRUE(isdir(folder));
	EXPECT_FALSE(isdir(trashed));
	EXPECT_TRUE(options->file_ops.no_trash);
	file_data_unref(fd);
}
