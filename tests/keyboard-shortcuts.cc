/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <cstring>
#include <map>
#include <string>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "accelerators.h"
#include "actions.h"
#include "convert-configuration.h"
#include "keyboard-shortcuts.h"
#include "main-defines.h"

namespace
{

guint substring_count(const gchar *text, const gchar *substring)
{
	guint count = 0;
	const gchar *match = text;

	while ((match = g_strstr_len(match, -1, substring)))
		{
		count++;
		match += strlen(substring);
		}

	return count;
}

TEST(KeyboardShortcuts, GeneratesNonEmptySectionsAndCombinedAlternatives)
{
	g_autoptr(GKeyFile) key_file = g_key_file_new();
	const gchar *preferences_accels[] = {"<Control>comma", "<Control>o"};

	g_key_file_set_string_list(key_file, "app.preferences", "accels", preferences_accels, G_N_ELEMENTS(preferences_accels));
	g_key_file_set_string(key_file, "win.advanced-exif-win-close", "accels", "<Control>w");
	g_key_file_set_string(key_file, "win.view-file-rename", "accels", "F2");
	g_key_file_set_string(key_file, "win.view-file-collection-save-as", "accels", "<Control><Shift>s");

	g_autofree gchar *xml = shortcuts_xml_from_keyfile(key_file);

	ASSERT_NE(xml, nullptr);
	const GMarkupParser parser = {};
	g_autoptr(GMarkupParseContext) context = g_markup_parse_context_new(&parser, G_MARKUP_DEFAULT_FLAGS, nullptr, nullptr);
	g_autoptr(GError) error = nullptr;
	EXPECT_TRUE(g_markup_parse_context_parse(context, xml, -1, &error));
	EXPECT_TRUE(g_markup_parse_context_end_parse(context, &error));
	EXPECT_NE(g_strstr_len(xml, -1, ">All Windows</property>"), nullptr);
	EXPECT_NE(g_strstr_len(xml, -1, ">Advanced EXIF Window</property>"), nullptr);
	EXPECT_NE(g_strstr_len(xml, -1, ">Files Pane</property>"), nullptr);
	EXPECT_EQ(g_strstr_len(xml, -1, ">Collection Window</property>"), nullptr);
	EXPECT_NE(g_strstr_len(xml, -1, "<property name=\"title\">Save collection as…</property>"), nullptr);
	EXPECT_NE(g_strstr_len(xml, -1, "&lt;Control&gt;comma &lt;Control&gt;o"), nullptr);
	EXPECT_EQ(substring_count(xml, "<property name=\"title\">Preferences…</property>"), 1);
	EXPECT_NE(g_strstr_len(xml, -1, "<property name=\"section-name\">mouse-arrow-keys</property>"), nullptr);
	EXPECT_EQ(g_strstr_len(xml, -1, "<property name=\"section-name\">search</property>"), nullptr);
}

TEST(KeyboardShortcuts, PrefersAppThenMainThenRemainingWindow)
{
	g_autoptr(GKeyFile) key_file = g_key_file_new();

	g_key_file_set_string(key_file, "app.open-file", "accels", "<Control>o");
	g_key_file_set_string(key_file, "win.main-win-open-archive", "accels", "<Control>o");
	g_key_file_set_string(key_file, "win.main-win-close-window", "accels", "<Control>w");
	g_key_file_set_string(key_file, "win.advanced-exif-win-close", "accels", "<Control>w");
	g_key_file_set_string(key_file, "win.advanced-exif-win-context-menu", "accels", "<Control>x");
	g_key_file_set_string(key_file, "win.view-file-collection-save-as", "accels", "<Control>x");

	g_autofree gchar *xml = shortcuts_xml_from_keyfile(key_file);

	ASSERT_NE(xml, nullptr);
	EXPECT_NE(g_strstr_len(xml, -1, "<property name=\"title\">Open file…</property>"), nullptr);
	EXPECT_EQ(g_strstr_len(xml, -1, "<property name=\"title\">Open archive</property>"), nullptr);
	EXPECT_NE(g_strstr_len(xml, -1, "<property name=\"title\">Close window</property>"), nullptr);
	EXPECT_EQ(g_strstr_len(xml, -1, "<property name=\"title\">Close</property>"), nullptr);
	EXPECT_NE(g_strstr_len(xml, -1, "<property name=\"title\">Context help</property>"), nullptr);
	EXPECT_EQ(g_strstr_len(xml, -1, "<property name=\"title\">Save collection as…</property>"), nullptr);
}

TEST(KeyboardShortcuts, AcceptsPrimaryForBackwardCompatibility)
{
	g_autoptr(GKeyFile) key_file = g_key_file_new();

	g_key_file_set_string(key_file, "app.open-file", "accels", "<Control>o");
	g_key_file_set_string(key_file, "win.main-win-open-archive", "accels", "<Primary>o");

	EXPECT_TRUE(accelerator_string_is_valid("<Primary>o"));

	g_autofree gchar *xml = shortcuts_xml_from_keyfile(key_file);

	ASSERT_NE(xml, nullptr);
	EXPECT_NE(g_strstr_len(xml, -1, "<property name=\"title\">Open file…</property>"), nullptr);
	EXPECT_EQ(g_strstr_len(xml, -1, "<property name=\"title\">Open archive</property>"), nullptr);
	EXPECT_EQ(substring_count(xml, "&lt;Control&gt;o"), 1);
}

TEST(KeyboardMap, PrefersAppThenMainThenRemainingWindow)
{
	g_autoptr(GKeyFile) key_file = g_key_file_new();
	g_autoptr(GPtrArray) shortcuts = g_ptr_array_new_with_free_func(g_free);

	g_key_file_set_string(key_file, "win.dupe-win-delete", "accels", "Delete"); // NOTRANSLATE: Test accelerator spelling.
	g_key_file_set_string(key_file, "win.main-win-delete", "accels", "Delete"); // NOTRANSLATE: Test accelerator spelling.
	g_key_file_set_string(key_file, "win.dupe-win-open", "accels", "<Control>o");
	g_key_file_set_string(key_file, "app.open", "accels", "<Control>o");
	g_key_file_set_string(key_file, "win.dupe-win-rename", "accels", "F2");
	g_key_file_set_string(key_file, "win.search-win-rename", "accels", "F2");

	get_actions_and_accelerators(key_file, shortcuts);

	ASSERT_EQ(shortcuts->len, 7);
	EXPECT_STREQ(static_cast<const gchar *>(g_ptr_array_index(shortcuts, 0)), "open");
	EXPECT_STREQ(static_cast<const gchar *>(g_ptr_array_index(shortcuts, 2)), "delete");
	EXPECT_STREQ(static_cast<const gchar *>(g_ptr_array_index(shortcuts, 4)), "rename");
	EXPECT_EQ(g_ptr_array_index(shortcuts, 6), nullptr);
}

TEST(KeyboardMap, FiltersSelectedWindowAndKeepsAppActions)
{
	g_autoptr(GKeyFile) key_file = g_key_file_new();
	g_autoptr(GPtrArray) shortcuts = g_ptr_array_new_with_free_func(g_free);

	g_key_file_set_string(key_file, "app.help", "accels", "F1");
	g_key_file_set_string(key_file, "win.main-win-delete", "accels", "Delete"); // NOTRANSLATE: Test accelerator spelling.
	g_key_file_set_string(key_file, "win.dupe-win-rename", "accels", "F2");

	get_actions_and_accelerators(key_file, shortcuts, "win.main-win-");

	ASSERT_EQ(shortcuts->len, 5);
	EXPECT_STREQ(static_cast<const gchar *>(g_ptr_array_index(shortcuts, 0)), "help");
	EXPECT_STREQ(static_cast<const gchar *>(g_ptr_array_index(shortcuts, 2)), "delete");
	EXPECT_EQ(g_ptr_array_index(shortcuts, 4), nullptr);
}

TEST(KeyboardMap, ViewSpecificBindingTakesPrecedenceInSelectedWindow)
{
	g_autoptr(GKeyFile) key_file = g_key_file_new();
	g_autoptr(GPtrArray) shortcuts = g_ptr_array_new_with_free_func(g_free);
	g_key_file_set_string(key_file, "app.help-contents", "accels", "F1");
	g_key_file_set_string(key_file, "win.log-win-run-command", "accels", "F1");
	get_actions_and_accelerators(key_file, shortcuts, "win.log-win-");
	ASSERT_EQ(shortcuts->len, 3U);
	EXPECT_STREQ(static_cast<const gchar *>(g_ptr_array_index(shortcuts, 0)), "run-command");
	EXPECT_TRUE(action_uses_context_shortcut("win.main-win-pan-right"));
	EXPECT_TRUE(action_uses_context_shortcut("win.image-win-pan-left-fast"));
	EXPECT_TRUE(action_uses_context_shortcut("win.pan-win-pan-half-up"));
	EXPECT_FALSE(action_uses_context_shortcut("app.help-contents"));
	g_autofree gchar *xml = shortcuts_xml_from_keyfile(key_file);
	EXPECT_NE(g_strstr_len(xml, -1, "Run command on selected log text"), nullptr); // NOTRANSLATE: Expected shortcut description.
}

TEST(KeyboardMap, BookmarkBindingsHaveTheirOwnScope)
{
	g_autoptr(GKeyFile) key_file = g_key_file_new();
	g_autoptr(GPtrArray) shortcuts = g_ptr_array_new_with_free_func(g_free);
	g_key_file_set_string(key_file, "win.main-win-pan-up-fast", "accels", "<Shift>Up");
	g_key_file_set_string(key_file, "bookmark.move-up", "accels", "<Shift>Up");
	get_actions_and_accelerators(key_file, shortcuts, "bookmark.");
	ASSERT_EQ(shortcuts->len, 3U);
	EXPECT_STREQ(static_cast<const gchar *>(g_ptr_array_index(shortcuts, 0)), "bookmark.move-up");
	EXPECT_TRUE(action_uses_context_shortcut("bookmark.move-up"));
	EXPECT_TRUE(action_uses_context_shortcut("bookmark.move-down"));
	g_autofree gchar *xml = shortcuts_xml_from_keyfile(key_file);
	EXPECT_NE(g_strstr_len(xml, -1, "Move bookmark or destination up"), nullptr); // NOTRANSLATE: Expected shortcut description.
}

GKeyFile *load_default_shortcuts()
{
	g_autoptr(GBytes) bytes = g_resources_lookup_data(GQ_RESOURCE_PATH_DATA "/accels.ini", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
	if (!bytes) return nullptr;
	auto *key_file = g_key_file_new();
	if (!g_key_file_load_from_bytes(key_file, bytes, G_KEY_FILE_NONE, nullptr))
		{
		g_key_file_unref(key_file);
		return nullptr;
		}
	return key_file;
}

TEST(KeyboardShortcuts, DefaultsHaveNoConflictingBindingsInTheSameWindow)
{
	g_autoptr(GKeyFile) key_file = load_default_shortcuts();
	ASSERT_NE(key_file, nullptr);
	g_auto(GStrv) groups = g_key_file_get_groups(key_file, nullptr);
	for (const auto *prefix : {"win.main-win-", "win.image-win-", "win.pan-win-", "win.dupe-win-",
	                           "win.search-win-", "win.advanced-exif-win-", "win.log-win-"})
		{
		std::map<std::string, std::string> owners;
		for (gchar **group = groups; *group; ++group)
			{
			if (!g_str_has_prefix(*group, "app.") && !g_str_has_prefix(*group, prefix) &&
			    !(g_str_equal(prefix, "win.main-win-") && g_str_has_prefix(*group, "win.view-file-"))) continue;
			g_auto(GStrv) accels = g_key_file_get_string_list(key_file, *group, "accels", nullptr, nullptr);
			for (gchar **accel = accels; accel && *accel; ++accel)
				{
				guint key;
				GdkModifierType mods;
				ASSERT_TRUE(gtk_accelerator_parse(*accel, &key, &mods)) << *group;
				g_autofree gchar *normalized = gtk_accelerator_name(key, mods);
				const auto inserted = owners.emplace(normalized, *group);
				EXPECT_TRUE(inserted.second || inserted.first->second == *group)
				    << prefix << ": " << normalized << " shared by " << inserted.first->second << " and " << *group;
				}
			}
		}
}

TEST(KeyboardShortcuts, DefaultsProvideStandardBindingsAndAvoidCustomAltShortcuts)
{
	g_autoptr(GKeyFile) key_file = load_default_shortcuts();
	ASSERT_NE(key_file, nullptr);
	const std::pair<const gchar *, const gchar *> standards[] = {
		{"app.help-contents", "F1"},
		{"app.help-shortcuts", "<Control>question"},
		{"win.main-win-back", "<Alt>Left"},
		{"win.main-win-forward", "<Alt>Right"},
		{"win.main-win-up", "<Alt>Up"},
		{"win.main-win-home", "<Alt>Home"},
		{"win.main-win-focus-address-bar", "<Control>l"},
		{"win.main-win-search", "<Control>f"},
		{"win.main-win-zoom-in", "<Control>plus"},
		{"win.main-win-zoom-out", "<Control>minus"},
		{"win.main-win-zoom-1-1", "<Control>0"},
		{"win.image-win-zoom-in", "<Control>plus"},
		{"win.image-win-zoom-out", "<Control>minus"},
		{"win.image-win-zoom-1-1", "<Control>0"},
		{"win.pan-win-zoom('in')", "<Control>plus"},
		{"win.pan-win-zoom('out')", "<Control>minus"},
		{"win.pan-win-zoom('1-1')", "<Control>0"},
		{"win.main-win-print", "<Control>p"},
		{"win.image-win-print", "<Control>p"},
		{"win.dupe-win-print", "<Control>p"},
		{"win.search-win-print", "<Control>p"},
		{"win.dupe-win-popup-menu", "<Shift>F10"},
		};
	for (const auto &standard : standards)
		{
		g_auto(GStrv) accels = g_key_file_get_string_list(key_file, standard.first, "accels", nullptr, nullptr);
		ASSERT_NE(accels, nullptr) << standard.first;
		EXPECT_TRUE(g_strv_contains(accels, standard.second)) << standard.first;
		}
	g_auto(GStrv) groups = g_key_file_get_groups(key_file, nullptr);
	for (gchar **group = groups; *group; ++group)
		{
		g_auto(GStrv) accels = g_key_file_get_string_list(key_file, *group, "accels", nullptr, nullptr);
		for (gchar **accel = accels; accel && *accel; ++accel)
			{
			guint key;
			GdkModifierType mods;
			ASSERT_TRUE(gtk_accelerator_parse(*accel, &key, &mods));
			EXPECT_FALSE(mods & GDK_SUPER_MASK) << *group;
			if (mods & GDK_ALT_MASK)
				EXPECT_TRUE(g_str_equal(*group, "win.main-win-back") || g_str_equal(*group, "win.main-win-forward") ||
				            g_str_equal(*group, "win.main-win-up") || g_str_equal(*group, "win.main-win-home")) << *group;
			}
		}
}

TEST(KeyboardShortcuts, LegacyProfileReplacesOverridesAndBacksUpOriginal)
{
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-legacy-shortcuts-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "accels.ini", nullptr);
	g_autofree gchar *backup = g_strconcat(path, "~", nullptr);
	const gchar *original = "[app.preferences]\naccels=F12\n[win.main-win-plugin-run::custom]\naccels=F6\n";
	ASSERT_TRUE(g_file_set_contents(path, original, -1, nullptr));
	g_autoptr(GError) error = nullptr;
	ASSERT_TRUE(restore_v2_shortcuts(path, &error)) << (error ? error->message : "");
	g_autofree gchar *saved = nullptr;
	ASSERT_TRUE(g_file_get_contents(backup, &saved, nullptr, nullptr));
	EXPECT_STREQ(saved, original);

	g_autoptr(GKeyFile) profile = g_key_file_new();
	ASSERT_TRUE(g_key_file_load_from_file(profile, path, G_KEY_FILE_NONE, nullptr));
	g_autofree gchar *preferences = g_key_file_get_string(profile, "app.preferences", "accels", nullptr);
	EXPECT_STREQ(preferences, "<Control>o");
	g_autofree gchar *open = g_key_file_get_string(profile, "app.open-file", "accels", nullptr);
	EXPECT_STREQ(open, "");
	EXPECT_FALSE(g_key_file_has_group(profile, "win.main-win-plugin-run::custom"));

	g_autoptr(GBytes) bytes = g_resources_lookup_data(GQ_RESOURCE_PATH_DATA "/accels-v2.ini", G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
	ASSERT_NE(bytes, nullptr);
	g_autoptr(GKeyFile) legacy = g_key_file_new();
	ASSERT_TRUE(g_key_file_load_from_bytes(legacy, bytes, G_KEY_FILE_NONE, nullptr));
	g_auto(GStrv) groups = g_key_file_get_groups(legacy, nullptr);
	for (gchar **group = groups; *group; ++group)
		{
		g_autofree gchar *expected = g_key_file_get_string(legacy, *group, "accels", nullptr);
		g_autofree gchar *actual = g_key_file_get_string(profile, *group, "accels", nullptr);
		EXPECT_STREQ(actual, expected) << *group;
		EXPECT_TRUE(accelerator_string_is_valid(expected)) << *group;
		}
	EXPECT_EQ(g_unlink(path), 0);
	EXPECT_EQ(g_unlink(backup), 0);
	EXPECT_EQ(g_rmdir(directory), 0);
}

TEST(KeyboardShortcuts, LegacyProfileRestoresSavedOverridesAndKeepsTheOldFile)
{
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-legacy-shortcuts-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "accels.ini", nullptr);
	g_autofree gchar *old_path = g_build_filename(directory, "accels", nullptr);
	g_autofree gchar *backup = g_strconcat(path, "~", nullptr);
	const gchar *original = "[app.preferences]\naccels=F6\n";
	const gchar *saved = R"accels(; GTK3 accelerator map: commented entries are unchanged defaults.
(gtk_accel_path "<Actions>/MenuActions/ZoomInAlt1" "<Control>F8")
; (gtk_accel_path "<Actions>/MenuActions/ZoomIn" "equal")
(gtk_accel_path "<Actions>/MenuActions/ZoomOut" "")
(gtk_accel_path "<Actions>/MenuActions/ZoomOutAlt1" "")
(gtk_accel_path "<Actions>/MenuActions/Rename" "")
(gtk_accel_path "<Actions>/MenuActions/Preferences" "<Control>F12")
; (gtk_accel_path "<Actions>/MenuActions/Copy" "F7")
(gtk_accel_path "<Actions>/MenuActions/ViewIcons" "<Control>F9")
(gtk_accel_path "<Actions>/MenuActions/RotateCW" "<Control>F10")
(gtk_accel_path "<Actions>/MenuActions/FullScreenAlt2" "F8")
; (gtk_accel_path "<Actions>/MenuActions/FullScreen" "f")
; (gtk_accel_path "<Actions>/MenuActions/FullScreenAlt1" "v")
(gtk_accel_path "<Actions>/MenuActionsExternal/org.geeqie.custom.desktop" "<Control>F11")
(gtk_accel_path "<Actions>/MenuActionsExternal/org.geeqie.disabled.desktop" "")
(gtk_accel_path "<Actions>/MenuActions/RemovedAction" "F7")
)accels";
	ASSERT_TRUE(g_file_set_contents(path, original, -1, nullptr));
	ASSERT_TRUE(g_file_set_contents(old_path, saved, -1, nullptr));
	g_autoptr(GError) error = nullptr;
	ASSERT_TRUE(restore_v2_shortcuts(path, &error)) << (error ? error->message : "");
	g_autoptr(GKeyFile) profile = g_key_file_new();
	ASSERT_TRUE(g_key_file_load_from_file(profile, path, G_KEY_FILE_NONE, nullptr));
	const std::pair<const gchar *, const gchar *> expected[] = {
		{"app.preferences", "<Control>F12;"},
		{"app.open-file", ""},
		{"win.main-win-zoom-in", "equal;<Control>F8;"},
		{"win.main-win-zoom-out", ""},
		{"win.main-win-rename", ""},
		{"win.main-win-copy", "<Control>c"},
		{"win.main-win-view-mode('icons')", "<Control>F9;"},
		{"win.main-win-rotate-cw-90", "<Control>F10;"},
		{"win.main-win-fullscreen", "f;v;F8;"},
		{"win.main-win-plugin-run::org.geeqie.custom.desktop", "<Control>F11;"},
		{"win.main-win-plugin-run::org.geeqie.disabled.desktop", ""},
		};
	for (const auto &binding : expected)
		{
		g_autofree gchar *actual = g_key_file_get_string(profile, binding.first, "accels", nullptr);
		EXPECT_STREQ(actual, binding.second) << binding.first;
		}
	EXPECT_FALSE(g_key_file_has_group(profile, "win.main-win-removed-action"));
	g_autofree gchar *old_contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(old_path, &old_contents, nullptr, nullptr));
	EXPECT_STREQ(old_contents, saved);
	g_autofree gchar *backup_contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(backup, &backup_contents, nullptr, nullptr));
	EXPECT_STREQ(backup_contents, original);
	EXPECT_EQ(g_unlink(path), 0);
	EXPECT_EQ(g_unlink(old_path), 0);
	EXPECT_EQ(g_unlink(backup), 0);
	EXPECT_EQ(g_rmdir(directory), 0);
}

TEST(KeyboardShortcuts, LegacyFileErrorsLeaveCurrentShortcutsAndBackupUnchanged)
{
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-legacy-shortcuts-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "accels.ini", nullptr);
	g_autofree gchar *old_path = g_build_filename(directory, "accels", nullptr);
	g_autofree gchar *backup = g_strconcat(path, "~", nullptr);
	const gchar *original = "[app.preferences]\naccels=F6\n";
	ASSERT_TRUE(g_file_set_contents(path, original, -1, nullptr));
	ASSERT_TRUE(g_file_set_contents(backup, "previous backup", -1, nullptr));
	ASSERT_EQ(g_mkdir(old_path, 0700), 0);
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(restore_v2_shortcuts(path, &error));
	EXPECT_NE(error, nullptr);
	ASSERT_EQ(g_rmdir(old_path), 0);
	for (const auto *invalid : {"invalid contents", "(gtk_accel_path broken)", "(gtk_accel_path \"<Actions>/MenuActions/Rename\" \"NoSuchKey\")"})
		{
		ASSERT_TRUE(g_file_set_contents(old_path, invalid, -1, nullptr));
		g_clear_error(&error);
		EXPECT_FALSE(restore_v2_shortcuts(path, &error));
		EXPECT_TRUE(g_error_matches(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_PARSE));
		}
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(path, &contents, nullptr, nullptr));
	EXPECT_STREQ(contents, original);
	g_autofree gchar *backup_contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(backup, &backup_contents, nullptr, nullptr));
	EXPECT_STREQ(backup_contents, "previous backup");
	EXPECT_EQ(g_unlink(path), 0);
	EXPECT_EQ(g_unlink(old_path), 0);
	EXPECT_EQ(g_unlink(backup), 0);
	EXPECT_EQ(g_rmdir(directory), 0);
}

TEST(KeyboardShortcuts, LegacyMergeKeepsUnmentionedAlternativesAndDoesNotPartiallyApplyErrors)
{
	g_autoptr(GKeyFile) profile = g_key_file_new();
	g_key_file_set_string(profile, "win.main-win-zoom-in", "accels", "equal;KP_Add");
	ASSERT_TRUE(merge_legacy_shortcuts(profile, "(gtk_accel_path \"<Actions>/MenuActions/ZoomIn\" \"F8\")", nullptr));
	g_autofree gchar *zoom = g_key_file_get_string(profile, "win.main-win-zoom-in", "accels", nullptr);
	EXPECT_STREQ(zoom, "F8;KP_Add;");
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(merge_legacy_shortcuts(profile,
	                                  "(gtk_accel_path \"<Actions>/MenuActions/ZoomIn\" \"F9\")\n(gtk_accel_path broken)", &error));
	g_autofree gchar *unchanged = g_key_file_get_string(profile, "win.main-win-zoom-in", "accels", nullptr);
	EXPECT_STREQ(unchanged, zoom);
}

TEST(KeyboardShortcuts, LegacyProfileReportsWriteFailure)
{
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-legacy-shortcuts-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "missing", "accels.ini", nullptr);
	g_autoptr(GError) error = nullptr;
	EXPECT_FALSE(restore_v2_shortcuts(path, &error));
	EXPECT_NE(error, nullptr);
	EXPECT_EQ(g_rmdir(directory), 0);
}

} // namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
