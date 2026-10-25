/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <glib.h>
#include <glib/gstdio.h>

#include "dupe.h"
#include "filedata.h"
#include "similar.h"

namespace
{

void check_json(const gchar *json, const gchar *checks)
{
	g_autofree gchar *python = g_find_program_in_path("python3");
	ASSERT_NE(python, nullptr);
	g_autofree gchar *script = g_strconcat("import json,sys\nd=json.loads(sys.argv[1])\n", checks, nullptr);
	gchar *argv[] = {python, const_cast<gchar *>("-c"), script, const_cast<gchar *>(json), nullptr};
	g_autofree gchar *errors = nullptr;
	g_autoptr(GError) error = nullptr;
	gint status;
	ASSERT_TRUE(g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &errors, &status, &error));
	EXPECT_TRUE(g_spawn_check_wait_status(status, &error)) << (errors ? errors : "");
}

TEST(DuplicateExport, EmptyAndIncompleteResultsAreValidJson)
{
	g_autoptr(GString) absent = dupe_export_json(nullptr);
	check_json(absent->str, "assert d['version']==1 and d['status']=='no_window' and d['groups']==[]\n");
	DupeWindow window{};
	g_autoptr(GString) empty = dupe_export_json(&window);
	check_json(empty->str, "assert d['status']=='ready' and d['groups']==[]\n");
	window.idle_id = 1;
	g_autoptr(GString) busy = dupe_export_json(&window);
	check_json(busy->str, "assert d['status']=='incomplete' and d['groups']==[]\n");
	window.idle_id = 0;
	window.add_files_queue_id = 1;
	EXPECT_FALSE(dupe_export_ready(&window));
	window.add_files_queue_id = 0;
	window.recompare_id = 1;
	EXPECT_FALSE(dupe_export_ready(&window));
}

TEST(DuplicateExport, GroupsAndScoresDoNotDependOnRowsOrPartialSelection)
{
	auto *first = file_data_new_simple("/tmp/a,\"\n雪.jpg");
	auto *second = file_data_new_simple("/tmp/b\\image.jpg");
	auto *third = file_data_new_simple("/tmp/other.jpg");
	DupeItem parent{};
	DupeItem child{};
	DupeItem other{};
	parent.fd = first;
	child.fd = second;
	other.fd = third;
	child.second = TRUE;
	parent.dimensions = {640, 480};
	DupeMatch match{&child, 97.125};
	GList members{&match, nullptr, nullptr};
	parent.group = &members;
	DupeMatch other_match{&child, 92.5};
	GList other_members{&other_match, nullptr, nullptr};
	other.group = &other_members;
	GList other_group{&other, nullptr, nullptr};
	GList groups{&parent, &other_group, nullptr};
	other_group.prev = &groups;
	GList selection{second, nullptr, nullptr};
	DupeWindow window{};
	window.dupes = &groups;
	window.match_mask = DUPE_MATCH_SIM_CUSTOM;
	window.second_set = TRUE;
	window.comparison_threshold = 90;
	window.comparison_perceptual = TRUE;
	window.comparison_rotation_invariant = TRUE;
	window.ignore_same_folder = TRUE;
	g_autoptr(GString) json = dupe_export_json(&window, &selection);
	check_json(json->str, R"(
assert d['schema']=='org.geeqie.duplicates' and d['version']==1
assert d['status']=='ready' and d['criteria']==['similarity']
assert d['algorithm']=='phash' and d['threshold']==90 and d['ignore_orientation']
assert d['ignore_same_folder']
assert len(d['groups'])==2
g=d['groups'][0]
p,c=g['members']
assert g['representative_id']==p['id'] and p['id'].startswith('file:')
assert p['path']=='/tmp/a,"\n雪.jpg' and c['path']=='/tmp/b\\image.jpg'
assert not p['selected'] and c['selected']
assert p['set']==1 and c['set']==2
assert p['similarity_to_representative'] is None and c['similarity_to_representative']==97.125
assert p['width']==640 and p['height']==480 and c['width'] is None
assert d['groups'][1]['members'][1]['id']==c['id']
assert d['groups'][1]['members'][1]['similarity_to_representative']==92.5
)");
	window.match_mask = DUPE_MATCH_NAME;
	g_autoptr(GString) exact = dupe_export_json(&window);
	check_json(exact->str, "assert d['criteria']==['name'] and d['algorithm'] is None\n"
	                      "assert all(m['similarity_to_representative'] is None for m in d['groups'][0]['members'])\n");
	file_data_unref(first);
	file_data_unref(second);
	file_data_unref(third);
}

struct ScriptResult
{
	gboolean done = FALSE;
	gboolean success = FALSE;
};

void script_finished(GObject *, GAsyncResult *result, gpointer data)
{
	auto *state = static_cast<ScriptResult *>(data);
	g_autoptr(GError) error = nullptr;
	state->success = dupe_run_results_script_finish(result, &error);
	state->done = TRUE;
}

TEST(DuplicateExport, ScriptReceivesSnapshotAndTemporaryFileIsRemoved)
{
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-dupe-script-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *script_path = g_build_filename(directory, "script with spaces", nullptr);
	g_autofree gchar *copied_path = g_build_filename(directory, "snapshot.json", nullptr);
	g_autofree gchar *marker_path = g_build_filename(directory, "path", nullptr);
	g_autofree gchar *quoted_copy = g_shell_quote(copied_path);
	g_autofree gchar *quoted_marker = g_shell_quote(marker_path);
	g_autoptr(GFile) script = g_file_new_for_path(script_path);
	for (gint exit_status : {0, 7})
		{
		g_autofree gchar *body = g_strdup_printf("#!/bin/sh\ncat \"$1\" > %s\nprintf '%%s' \"$1\" > %s\nexit %d\n",
		                                        quoted_copy, quoted_marker, exit_status);
		ASSERT_TRUE(g_file_set_contents(script_path, body, -1, nullptr));
		ASSERT_EQ(g_chmod(script_path, 0700), 0);
		ScriptResult state;
		dupe_run_results_script(script, "{\"version\":1}", script_finished, &state);
		while (!state.done) g_main_context_iteration(nullptr, TRUE);
		EXPECT_EQ(state.success, exit_status == 0);
		g_autofree gchar *contents = nullptr;
		ASSERT_TRUE(g_file_get_contents(copied_path, &contents, nullptr, nullptr));
		EXPECT_STREQ(contents, "{\"version\":1}");
		g_autofree gchar *temporary_path = nullptr;
		ASSERT_TRUE(g_file_get_contents(marker_path, &temporary_path, nullptr, nullptr));
		EXPECT_FALSE(g_file_test(temporary_path, G_FILE_TEST_EXISTS));
		}
	ASSERT_EQ(g_chmod(script_path, 0600), 0);
	ScriptResult state;
	dupe_run_results_script(script, "{}", script_finished, &state);
	while (!state.done) g_main_context_iteration(nullptr, TRUE);
	EXPECT_FALSE(state.success);
	g_unlink(script_path);
	g_unlink(copied_path);
	g_unlink(marker_path);
	g_rmdir(directory);
}

} // namespace
