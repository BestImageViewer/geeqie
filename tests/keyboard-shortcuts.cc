/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <cstring>
#include <glib.h>

#include "accelerators.h"
#include "actions.h"
#include "keyboard-shortcuts.h"

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

} // namespace

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
