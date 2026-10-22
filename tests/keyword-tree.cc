/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <string>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "main-defines.h"
#include "metadata.h"
#include "options.h"
#include "rcfile.h"
#include "ui-fileops.h"

namespace
{

void wait_for_autosave()
{
	const gint64 deadline = g_get_monotonic_time() + 2300 * G_TIME_SPAN_MILLISECOND;
	while (g_get_monotonic_time() < deadline)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_usleep(1000);
		}
}

class KeywordTreeAutosave : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check()) GTEST_SKIP() << "Requires a display";
		path = g_build_filename(get_rc_dir(), RC_FILE_NAME, nullptr);
		if (g_file_test(path, static_cast<GFileTest>(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_SYMLINK)))
			{
			GTEST_SKIP() << "Requires an unused configuration file";
			}
		ASSERT_EQ(g_mkdir_with_parents(get_rc_dir(), 0700), 0);
		if (!options) options = conf_options_new();
		saved_positions = options->save_window_positions;
		options->save_window_positions = FALSE;
		previous_app = g_application_get_default();
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.KeywordAutosaveTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		app = GTK_APPLICATION(g_object_ref(test_app));
		g_application_set_default(G_APPLICATION(app));
		keyword_tree_autosave_set_enabled(FALSE);
		tree = keyword_tree_get_or_new();
		g_autoptr(GString) text = g_string_new(nullptr);
		RcString rc{text, 0};
		keyword_tree_write_config(rc);
		saved_tree = "<gq><global>" + std::string(text->str) + "</global></gq>";
		keyword_tree_disconnect_marks();
		clear_tree();
		initialized = true;
	}

	void TearDown() override
	{
		if (initialized)
			{
			keyword_tree_autosave_set_enabled(FALSE);
			keyword_tree_disconnect_marks();
			clear_tree();
			EXPECT_TRUE(load_config_from_buf(saved_tree.c_str(), saved_tree.size(), FALSE));
			options->save_window_positions = saved_positions;
			g_unlink(path);
			g_rmdir(path);
			}
		if (app)
			{
			g_application_set_default(previous_app);
			g_object_unref(app);
			}
		g_free(path);
	}

	GtkTreeIter append(const gchar *name, GtkTreeIter *parent = nullptr)
	{
		GtkTreeIter iter;
		gtk_tree_store_append(tree, &iter, parent);
		keyword_set(tree, &iter, name, TRUE);
		return iter;
	}

	void clear_tree()
	{
		GtkTreeIter iter;
		while (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tree), &iter)) keyword_delete(tree, &iter);
	}

	std::string contents()
	{
		g_autofree gchar *text = nullptr;
		if (!g_file_get_contents(path, &text, nullptr, nullptr)) return {};
		return text;
	}

	GtkTreeStore *tree = nullptr;
	gchar *path = nullptr;
	std::string saved_tree;
	GtkApplication *app = nullptr;
	GApplication *previous_app = nullptr;
	gboolean saved_positions = FALSE;
	bool initialized = false;
};

TEST_F(KeywordTreeAutosave, SavesEditsAndSubsequentReorderingAndDeletion)
{
	keyword_tree_autosave_set_enabled(TRUE);
	auto first = append("First");
	auto child = append("Child", &first);
	keyword_set(tree, &child, "Renamed", FALSE);
	auto second = append("Second");
	meta_data_connect_mark_with_keyword(GTK_TREE_MODEL(tree), &second, 0);
	EXPECT_FALSE(g_file_test(path, G_FILE_TEST_EXISTS));
	wait_for_autosave();
	std::string saved = contents();
	EXPECT_NE(saved.find("name = \"Renamed\""), std::string::npos);
	EXPECT_NE(saved.find("kw = \"false\""), std::string::npos);
	EXPECT_NE(saved.find("mark = \"1\""), std::string::npos);

	gtk_tree_store_move_before(tree, &second, &first);
	keyword_delete(tree, &child);
	wait_for_autosave();
	saved = contents();
	EXPECT_EQ(saved.find("Renamed"), std::string::npos);
	EXPECT_LT(saved.find("name = \"Second\""), saved.find("name = \"First\""));
}

TEST_F(KeywordTreeAutosave, DoesNotSaveLoadingDefaultsOrDisplayOnlyChanges)
{
	keyword_tree_autosave_set_enabled(TRUE);
	keyword_tree_set_default(tree);
	const std::string config = "<gq><global><keyword_tree><keyword name=\"Loaded\" kw=\"true\"/></keyword_tree></global></gq>";
	ASSERT_TRUE(load_config_from_buf(config.c_str(), config.size(), FALSE));
	wait_for_autosave();
	EXPECT_FALSE(g_file_test(path, G_FILE_TEST_EXISTS));
	ASSERT_TRUE(g_file_set_contents(path, "Unchanged", -1, nullptr));
	GtkTreeIter iter;
	ASSERT_TRUE(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(tree), &iter));
	keyword_hide_in(tree, &iter, this);
	wait_for_autosave();
	EXPECT_EQ(contents(), "Unchanged");
}

TEST_F(KeywordTreeAutosave, FailedSaveRetainsUnsavedState)
{
	ASSERT_EQ(g_mkdir(path, 0700), 0);
	keyword_tree_autosave_set_enabled(TRUE);
	auto iter = append("Before failure");
	wait_for_autosave();
	EXPECT_TRUE(g_file_test(path, G_FILE_TEST_IS_DIR));
	ASSERT_EQ(g_rmdir(path), 0);
	const std::string config = "<gq><global/></gq>";
	ASSERT_TRUE(load_config_from_buf(config.c_str(), config.size(), FALSE));
	// A notification with unchanged persistent contents must still retry.
	keyword_set(tree, &iter, "Before failure", TRUE);
	wait_for_autosave();
	EXPECT_NE(contents().find("Before failure"), std::string::npos);
}

TEST_F(KeywordTreeAutosave, DisablingCancelsPendingSave)
{
	keyword_tree_autosave_set_enabled(TRUE);
	append("Pending");
	keyword_tree_autosave_set_enabled(FALSE);
	wait_for_autosave();
	EXPECT_FALSE(g_file_test(path, G_FILE_TEST_EXISTS));
}

} // namespace
