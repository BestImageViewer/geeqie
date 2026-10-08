/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <functional>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "filedata.h"
#include "intl.h"
#include "metadata-template.h"
#include "metadata.h"
#include "options.h"
#include "ui-fileops.h"

namespace
{

GtkWidget *find_widget(GtkWidget *widget, const std::function<bool(GtkWidget *)> &matches)
{
	if (matches(widget)) return widget;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *found = find_widget(child, matches)) return found;
		}
	return nullptr;
}

class MetadataTemplate : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		previous_app = g_application_get_default();
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.MetadataTemplateTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		app = GTK_APPLICATION(g_object_ref(test_app));
		g_application_set_default(G_APPLICATION(app));
		directory = g_dir_make_tmp("geeqie-template-test-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		for (const gchar *name : {"a.jpg", "b.jpg"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			ASSERT_TRUE(g_file_set_contents(path, "", 0, nullptr));
			files = g_list_append(files, file_data_new_simple(path));
			}
		metadata_template_dialog(nullptr, filelist_copy(files));
		GListModel *windows = gtk_window_get_toplevels();
		for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
			{
			g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
			if (g_str_has_prefix(gtk_window_get_title(window), _("Metadata Templates"))) dialog = GTK_WIDGET(window);
			}
		ASSERT_NE(dialog, nullptr);
	}

	void TearDown() override
	{
		if (dialog) click(_("Cancel"));
		for (GList *work = files; work; work = work->next)
			{
			auto *fd = static_cast<FileData *>(work->data);
			if (fd->modified_xmp) metadata_write_queue_remove(fd);
			g_unlink(fd->path);
			}
		file_data_list_free(files);
		if (directory) g_rmdir(directory);
		g_free(directory);
		if (app)
			{
			g_application_set_default(previous_app);
			g_object_unref(app);
			}
	}

	GtkWidget *check(const gchar *label)
	{
		return find_widget(dialog, [label](GtkWidget *widget)
			{
			return GTK_IS_CHECK_BUTTON(widget) && g_strcmp0(gtk_check_button_get_label(GTK_CHECK_BUTTON(widget)), label) == 0;
			});
	}

	GtkWidget *field_label(const gchar *label)
	{
		return find_widget(dialog, [label](GtkWidget *widget)
			{
			return GTK_IS_LABEL(widget) && g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)), label) == 0;
			});
	}

	GtkWidget *view(const gchar *key)
	{
		return find_widget(dialog, [key](GtkWidget *widget)
			{
			return GTK_IS_TEXT_VIEW(widget) && g_strcmp0(gtk_widget_get_name(widget), key) == 0;
			});
	}

	void set(const gchar *label, const gchar *key, const gchar *value)
	{
		ASSERT_NE(field_label(label), nullptr);
		ASSERT_NE(view(key), nullptr);
		if (gtk_drop_down_get_selected(GTK_DROP_DOWN(mode(key))) == 0) append(key, strcmp(key, KEYWORD_KEY) == 0);
		gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view(key))), value, -1);
	}

	void click(const gchar *label)
	{
		auto *button = find_widget(dialog, [label](GtkWidget *widget)
			{
			return GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0;
			});
		ASSERT_NE(button, nullptr);
		g_signal_emit_by_name(button, "clicked");
	}

	GtkWidget *named_widget(const gchar *name)
	{
		auto *expander = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_EXPANDER(widget); });
		if (expander) gtk_expander_set_expanded(GTK_EXPANDER(expander), TRUE);
		return find_widget(dialog, [name](GtkWidget *widget) { return g_strcmp0(gtk_widget_get_name(widget), name) == 0; });
	}

	void add_field(const gchar *label, const gchar *key, bool list = false)
	{
		auto *label_entry = named_widget("metadata-template-field-label");
		auto *key_entry = named_widget("metadata-template-field-key");
		ASSERT_NE(label_entry, nullptr);
		ASSERT_NE(key_entry, nullptr);
		gtk_editable_set_text(GTK_EDITABLE(label_entry), label);
		gtk_editable_set_text(GTK_EDITABLE(key_entry), key);
		gtk_check_button_set_active(GTK_CHECK_BUTTON(check(_("List of values (one per line)"))), list);
		click(_("Add field"));
	}

	GtkWidget *mode(const gchar *key)
	{
		auto *text_view = view(key);
		if (!text_view) return nullptr;
		auto *row = gtk_widget_get_parent(gtk_widget_get_parent(text_view));
		return find_widget(row, [](GtkWidget *widget) { return GTK_IS_DROP_DOWN(widget); });
	}

	void append(const gchar *key, bool active = true)
	{
		ASSERT_NE(mode(key), nullptr);
		gtk_drop_down_set_selected(GTK_DROP_DOWN(mode(key)), active ? 2 : 1);
	}

	void clear_field(const gchar *key)
	{
		ASSERT_NE(mode(key), nullptr);
		gtk_drop_down_set_selected(GTK_DROP_DOWN(mode(key)), 3);
	}

	void apply()
	{
		click(_("Apply to selection"));
		dialog = nullptr;
	}

	void expect_string(FileData *fd, const gchar *key, const gchar *expected)
	{
		g_autofree gchar *value = metadata_read_string(fd, key, METADATA_PLAIN);
		EXPECT_STREQ(value, expected);
	}

	GApplication *previous_app = nullptr;
	GtkApplication *app = nullptr;
	GtkWidget *dialog = nullptr;
	GList *files = nullptr;
	gchar *directory = nullptr;
};

TEST_F(MetadataTemplate, AppliesMultipleFieldsAndPreservesBlankAndDisabledFields)
{
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		metadata_write_string(fd, "Xmp.dc.rights", "Existing copyright");
		metadata_write_string(fd, "Xmp.photoshop.Credit", "Existing credit");
		metadata_write_string(fd, KEYWORD_KEY, "existing");
		}
	set(_("Description"), COMMENT_KEY, "Event description");
	set(_("Headline"), "Xmp.photoshop.Headline", "Event headline");
	set(_("Copyright"), "Xmp.dc.rights", "");
	set(_("Keywords"), KEYWORD_KEY, "existing, new\nnew");
	apply();
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		expect_string(fd, COMMENT_KEY, "Event description");
		expect_string(fd, "Xmp.photoshop.Headline", "Event headline");
		expect_string(fd, "Xmp.dc.rights", "Existing copyright");
		expect_string(fd, "Xmp.photoshop.Credit", "Existing credit");
		GList *keywords = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);
		ASSERT_EQ(g_list_length(keywords), 2U);
		EXPECT_STREQ(static_cast<gchar *>(keywords->data), "existing");
		EXPECT_STREQ(static_cast<gchar *>(keywords->next->data), "new");
		g_list_free_full(keywords, g_free);
		}
}

TEST_F(MetadataTemplate, ClearIsExplicitAndKeywordsCanReplace)
{
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		metadata_write_string(fd, COMMENT_KEY, "Old description");
		metadata_write_string(fd, KEYWORD_KEY, "old");
		}
	set(_("Description"), COMMENT_KEY, "Ignored when clearing");
	clear_field(COMMENT_KEY);
	set(_("Keywords"), KEYWORD_KEY, "new");
	append(KEYWORD_KEY, false);
	apply();
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		GList *description = metadata_read_list(fd, COMMENT_KEY, METADATA_PLAIN);
		EXPECT_EQ(description, nullptr);
		g_list_free_full(description, g_free);
		expect_string(fd, KEYWORD_KEY, "new");
		}
}

TEST_F(MetadataTemplate, SavesLoadsAndDeletesNamedTemplate)
{
	g_autofree gchar *name = g_uuid_string_random();
	auto *entry = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_ENTRY(widget); });
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), name);
	set(_("Description"), COMMENT_KEY, "Saved description");
	set(_("Copyright"), "Xmp.dc.rights", "Saved copyright");
	click(_("Save template"));
	g_autofree gchar *filename = g_strconcat(name, ".ini", nullptr);
	g_autofree gchar *path = g_build_filename(get_rc_dir(), "metadata-templates", filename, nullptr);
	ASSERT_TRUE(g_file_test(path, G_FILE_TEST_IS_REGULAR));
	auto *combo = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_DROP_DOWN(widget); });
	ASSERT_NE(combo, nullptr);
	const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo));
	click(_("New template"));
	EXPECT_STREQ(gtk_editable_get_text(GTK_EDITABLE(entry)), "");
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode("Xmp.dc.rights"))), 0U);
	set(_("Description"), COMMENT_KEY, "Unsaved description");
	gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), selected);
	click(_("Delete template"));
	EXPECT_FALSE(g_file_test(path, G_FILE_TEST_EXISTS));
	apply();
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		expect_string(fd, COMMENT_KEY, "Saved description");
		expect_string(fd, "Xmp.dc.rights", "Saved copyright");
		}
}

TEST_F(MetadataTemplate, SanitizedAndCaseInsensitiveCollisionsDoNotOverwriteTemplates)
{
	g_autofree gchar *prefix = g_uuid_string_random();
	auto *entry = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_ENTRY(widget); });
	ASSERT_NE(entry, nullptr);
	const gchar *endings[] = {"/Event", ":Event", ":event"};
	const gchar *filename_endings[] = {"_Event.ini", "_Event (2).ini", "_event (3).ini"};
	for (guint i = 0; i < 3; ++i)
		{
		g_autofree gchar *name = g_strconcat(prefix, endings[i], nullptr);
		gtk_editable_set_text(GTK_EDITABLE(entry), name);
		set(_("Description"), COMMENT_KEY, name);
		click(_("Save template"));
		}
	// Updating an exact template name must reuse its existing suffixed file.
	g_autofree gchar *updated_name = g_strconcat(prefix, endings[1], nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), updated_name);
	set(_("Description"), COMMENT_KEY, "Updated");
	click(_("Save template"));
	for (guint i = 0; i < 3; ++i)
		{
		g_autofree gchar *filename = g_strconcat(prefix, filename_endings[i], nullptr);
		g_autofree gchar *path = g_build_filename(get_rc_dir(), "metadata-templates", filename, nullptr);
		g_autoptr(GKeyFile) config = g_key_file_new();
		ASSERT_TRUE(g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, nullptr));
		g_autofree gchar *name = g_strconcat(prefix, endings[i], nullptr);
		g_autofree gchar *stored_name = g_key_file_get_string(config, "template", "name", nullptr);
		EXPECT_STREQ(stored_name, name);
		g_autofree gchar *description = g_key_file_get_string(config, COMMENT_KEY, "value", nullptr);
		EXPECT_STREQ(description, i == 1 ? "Updated" : name);
		g_unlink(path);
		}
}

TEST_F(MetadataTemplate, UnrelatedFilesAreNotOverwritten)
{
	g_autofree gchar *name = g_uuid_string_random();
	g_autofree gchar *template_directory = g_build_filename(get_rc_dir(), "metadata-templates", nullptr);
	ASSERT_EQ(g_mkdir_with_parents(template_directory, 0700), 0);
	g_autofree gchar *filename = g_strconcat(name, ".ini", nullptr);
	g_autofree gchar *occupied = g_build_filename(template_directory, filename, nullptr);
	ASSERT_TRUE(g_file_set_contents(occupied, "Unrelated file", -1, nullptr));
	auto *entry = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_ENTRY(widget); });
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), name);
	set(_("Description"), COMMENT_KEY, "New template");
	click(_("Save template"));
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(occupied, &contents, nullptr, nullptr));
	EXPECT_STREQ(contents, "Unrelated file");
	g_autofree gchar *suffixed = g_strdup_printf("%s (2).ini", name);
	g_autofree gchar *path = g_build_filename(template_directory, suffixed, nullptr);
	EXPECT_TRUE(g_file_test(path, G_FILE_TEST_IS_REGULAR));
	click(_("Delete template"));
	EXPECT_FALSE(g_file_test(path, G_FILE_TEST_EXISTS));
	EXPECT_TRUE(g_file_test(occupied, G_FILE_TEST_EXISTS));
	g_unlink(occupied);
}

TEST_F(MetadataTemplate, LegacyHashedFilenamesRemainUsable)
{
	g_autofree gchar *name = g_uuid_string_random();
	auto *entry = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_ENTRY(widget); });
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), name);
	set(_("Description"), COMMENT_KEY, "Legacy description");
	click(_("Save template"));
	g_autofree gchar *filename = g_strconcat(name, ".ini", nullptr);
	g_autofree gchar *path = g_build_filename(get_rc_dir(), "metadata-templates", filename, nullptr);
	g_autofree gchar *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, name, -1);
	g_autofree gchar *legacy_filename = g_strconcat(hash, ".ini", nullptr);
	g_autofree gchar *legacy_path = g_build_filename(get_rc_dir(), "metadata-templates", legacy_filename, nullptr);
	ASSERT_EQ(g_rename(path, legacy_path), 0);
	auto *combo = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_DROP_DOWN(widget); });
	ASSERT_NE(combo, nullptr);
	const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo));
	click(_("New template"));
	gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), selected);
	click(_("Delete template"));
	EXPECT_FALSE(g_file_test(legacy_path, G_FILE_TEST_EXISTS));
	apply();
	for (GList *work = files; work; work = work->next) expect_string(static_cast<FileData *>(work->data), COMMENT_KEY, "Legacy description");
}

TEST_F(MetadataTemplate, AddedFieldsAndListValuesSurviveSaveLoadAndApply)
{
	g_autofree gchar *name = g_uuid_string_random();
	auto *entry = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_ENTRY(widget); });
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), name);
	// Suggested fields populate the same editable label and key controls.
	auto *suggestion = named_widget("metadata-template-field-suggestion");
	ASSERT_NE(suggestion, nullptr);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(suggestion), 2);
	EXPECT_STREQ(gtk_editable_get_text(GTK_EDITABLE(named_widget("metadata-template-field-key"))), "Xmp.photoshop.City");
	click(_("Add field"));
	set(_("City"), "Xmp.photoshop.City", "Douglas");
	add_field("People shown", "Xmp.iptcExt.PersonInImage", true);
	set("People shown", "Xmp.iptcExt.PersonInImage", "Alice, Jr.\nBob");
	click(_("Save template"));
	g_autofree gchar *filename = g_strconcat(name, ".ini", nullptr);
	g_autofree gchar *path = g_build_filename(get_rc_dir(), "metadata-templates", filename, nullptr);
	g_autoptr(GKeyFile) config = g_key_file_new();
	ASSERT_TRUE(g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, nullptr));
	gsize count = 0;
	g_auto(GStrv) keys = g_key_file_get_string_list(config, "template", "custom_fields", &count, nullptr);
	ASSERT_EQ(count, 2U);
	EXPECT_STREQ(keys[0], "Xmp.photoshop.City");
	EXPECT_STREQ(keys[1], "Xmp.iptcExt.PersonInImage");
	auto *combo = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_DROP_DOWN(widget); });
	ASSERT_NE(combo, nullptr);
	const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo));
	click(_("New template"));
	EXPECT_EQ(view("Xmp.photoshop.City"), nullptr);
	EXPECT_EQ(view("Xmp.iptcExt.PersonInImage"), nullptr);
	add_field("Temporary field", "Xmp.photoshop.Country");
	gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), selected);
	EXPECT_EQ(view("Xmp.photoshop.Country"), nullptr);
	ASSERT_NE(field_label("People shown"), nullptr);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode("Xmp.iptcExt.PersonInImage"))), 1U);
	click(_("Delete template"));
	apply();
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		expect_string(fd, "Xmp.photoshop.City", "Douglas");
		GList *people = metadata_read_list(fd, "Xmp.iptcExt.PersonInImage", METADATA_PLAIN);
		ASSERT_EQ(g_list_length(people), 2U);
		EXPECT_STREQ(static_cast<gchar *>(people->data), "Alice, Jr.");
		EXPECT_STREQ(static_cast<gchar *>(people->next->data), "Bob");
		g_list_free_full(people, g_free);
		}
}

TEST_F(MetadataTemplate, InvalidAndDuplicateMetadataKeysAreRejected)
{
	add_field("Duplicate description", COMMENT_KEY);
	EXPECT_EQ(field_label("Duplicate description"), nullptr);
	for (const gchar *key : {"Exif.Image.Artist", "Xmp.", "Xmp..City", "Xmp.photoshop.", "Xmp.photoshop.City]"})
		{
		add_field("Invalid field", key);
		EXPECT_EQ(view(key), nullptr);
		EXPECT_EQ(field_label("Invalid field"), nullptr);
		}
	add_field("City field", "Xmp.photoshop.City");
	ASSERT_NE(view("Xmp.photoshop.City"), nullptr);
	add_field("Duplicate city", "Xmp.photoshop.City");
	EXPECT_EQ(field_label("Duplicate city"), nullptr);
}

TEST_F(MetadataTemplate, RemovingFieldUpdatesSavedTemplateWithoutChangingFileMetadata)
{
	for (GList *work = files; work; work = work->next)
		{
		metadata_write_string(static_cast<FileData *>(work->data), "Xmp.photoshop.City", "Existing city");
		}
	g_autofree gchar *name = g_uuid_string_random();
	auto *entry = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_ENTRY(widget); });
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), name);
	add_field("Custom city", "Xmp.photoshop.City");
	set("Custom city", "Xmp.photoshop.City", "Replacement city");
	click(_("Save template"));
	click(_("Remove field"));
	EXPECT_EQ(view("Xmp.photoshop.City"), nullptr);
	set(_("Description"), COMMENT_KEY, "Description only");
	click(_("Save template"));
	g_autofree gchar *filename = g_strconcat(name, ".ini", nullptr);
	g_autofree gchar *path = g_build_filename(get_rc_dir(), "metadata-templates", filename, nullptr);
	g_autoptr(GKeyFile) config = g_key_file_new();
	ASSERT_TRUE(g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, nullptr));
	EXPECT_FALSE(g_key_file_has_group(config, "Xmp.photoshop.City"));
	click(_("Delete template"));
	apply();
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		expect_string(fd, "Xmp.photoshop.City", "Existing city");
		expect_string(fd, COMMENT_KEY, "Description only");
		}
}

TEST_F(MetadataTemplate, AddedFieldsCanBeExplicitlyCleared)
{
	for (GList *work = files; work; work = work->next)
		{
		metadata_write_string(static_cast<FileData *>(work->data), "Xmp.photoshop.City", "Existing city");
		}
	add_field("Custom city", "Xmp.photoshop.City");
	clear_field("Xmp.photoshop.City");
	apply();
	for (GList *work = files; work; work = work->next)
		{
		GList *values = metadata_read_list(static_cast<FileData *>(work->data), "Xmp.photoshop.City", METADATA_PLAIN);
		EXPECT_EQ(values, nullptr);
		g_list_free_full(values, g_free);
		}
}

TEST_F(MetadataTemplate, TextAppendUsesEachFilesValueAndLeavesBlankFieldsUnchanged)
{
	guint index = 0;
	for (GList *work = files; work; work = work->next, ++index)
		{
		auto *fd = static_cast<FileData *>(work->data);
		metadata_write_string(fd, COMMENT_KEY, index == 0 ? "Existing description" : "");
		metadata_write_string(fd, "Xmp.photoshop.Instructions", index == 0 ? "Existing instructions\n" : "");
		metadata_write_string(fd, "Xmp.dc.rights", "Existing copyright");
		metadata_write_string(fd, "Xmp.photoshop.Headline", "Old headline");
		}
	set(_("Description"), COMMENT_KEY, "Additional description");
	append(COMMENT_KEY);
	add_field("Custom instructions", "Xmp.photoshop.Instructions");
	set("Custom instructions", "Xmp.photoshop.Instructions", "Additional instructions");
	append("Xmp.photoshop.Instructions");
	set(_("Copyright"), "Xmp.dc.rights", "");
	append("Xmp.dc.rights");
	set(_("Headline"), "Xmp.photoshop.Headline", "New headline");
	apply();
	index = 0;
	for (GList *work = files; work; work = work->next, ++index)
		{
		auto *fd = static_cast<FileData *>(work->data);
		expect_string(fd, COMMENT_KEY, index == 0 ? "Existing description\nAdditional description" : "Additional description");
		expect_string(fd, "Xmp.photoshop.Instructions", index == 0 ? "Existing instructions\nAdditional instructions" : "Additional instructions");
		expect_string(fd, "Xmp.dc.rights", "Existing copyright");
		expect_string(fd, "Xmp.photoshop.Headline", "New headline");
		}
}

TEST_F(MetadataTemplate, CustomListAppendRemovesDuplicates)
{
	for (GList *work = files; work; work = work->next)
		{
		metadata_write_string(static_cast<FileData *>(work->data), "Xmp.iptcExt.PersonInImage", "Alice");
		}
	add_field("People shown", "Xmp.iptcExt.PersonInImage", true);
	set("People shown", "Xmp.iptcExt.PersonInImage", "Alice\nBob\nBob");
	append("Xmp.iptcExt.PersonInImage");
	apply();
	for (GList *work = files; work; work = work->next)
		{
		GList *people = metadata_read_list(static_cast<FileData *>(work->data), "Xmp.iptcExt.PersonInImage", METADATA_PLAIN);
		ASSERT_EQ(g_list_length(people), 2U);
		EXPECT_STREQ(static_cast<gchar *>(people->data), "Alice");
		EXPECT_STREQ(static_cast<gchar *>(people->next->data), "Bob");
		g_list_free_full(people, g_free);
		}
}

TEST_F(MetadataTemplate, SavedModesAreRestoredAndClearOverridesAppend)
{
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		metadata_write_string(fd, COMMENT_KEY, "Original");
		metadata_write_string(fd, KEYWORD_KEY, "old");
		metadata_write_string(fd, "Xmp.photoshop.City", "Existing city");
		}
	g_autofree gchar *name = g_uuid_string_random();
	auto *entry = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_ENTRY(widget); });
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), name);
	set(_("Description"), COMMENT_KEY, "Added");
	append(COMMENT_KEY);
	set(_("Keywords"), KEYWORD_KEY, "new");
	append(KEYWORD_KEY, false);
	add_field("Custom city", "Xmp.photoshop.City");
	append("Xmp.photoshop.City");
	clear_field("Xmp.photoshop.City");
	EXPECT_TRUE(gtk_widget_get_sensitive(mode("Xmp.photoshop.City")));
	EXPECT_FALSE(gtk_widget_get_sensitive(gtk_widget_get_parent(view("Xmp.photoshop.City"))));
	click(_("Save template"));
	auto *combo = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_DROP_DOWN(widget); });
	ASSERT_NE(combo, nullptr);
	const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo));
	click(_("New template"));
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode(COMMENT_KEY))), 0U);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode(KEYWORD_KEY))), 0U);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), selected);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode(COMMENT_KEY))), 2U);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode(KEYWORD_KEY))), 1U);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode("Xmp.photoshop.City"))), 3U);
	click(_("Delete template"));
	apply();
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		expect_string(fd, COMMENT_KEY, "Original\nAdded");
		expect_string(fd, KEYWORD_KEY, "new");
		GList *city = metadata_read_list(fd, "Xmp.photoshop.City", METADATA_PLAIN);
		EXPECT_EQ(city, nullptr);
		g_list_free_full(city, g_free);
		}
}

TEST_F(MetadataTemplate, LegacyKeywordModeIsRespected)
{
	g_autofree gchar *name = g_uuid_string_random();
	auto *entry = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_ENTRY(widget); });
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), name);
	set(_("Keywords"), KEYWORD_KEY, "new");
	click(_("Save template"));
	g_autofree gchar *filename = g_strconcat(name, ".ini", nullptr);
	g_autofree gchar *path = g_build_filename(get_rc_dir(), "metadata-templates", filename, nullptr);
	g_autoptr(GKeyFile) config = g_key_file_new();
	ASSERT_TRUE(g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, nullptr));
	g_key_file_remove_key(config, KEYWORD_KEY, "action", nullptr);
	g_key_file_set_boolean(config, KEYWORD_KEY, "enabled", TRUE);
	auto *combo = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_DROP_DOWN(widget); });
	ASSERT_NE(combo, nullptr);
	const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo));
	for (const bool append_keywords : {false, true})
		{
		g_key_file_set_boolean(config, "template", "append_keywords", append_keywords);
		ASSERT_TRUE(g_key_file_save_to_file(config, path, nullptr));
		click(_("New template"));
		gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), selected);
		EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode(KEYWORD_KEY))), append_keywords ? 2U : 1U);
		}
	click(_("Delete template"));
}

TEST_F(MetadataTemplate, LeaveUnchangedPreservesExistingMetadataEvenWithEnteredText)
{
	for (GList *work = files; work; work = work->next)
		{
		metadata_write_string(static_cast<FileData *>(work->data), COMMENT_KEY, "Keep this description");
		}
	set(_("Description"), COMMENT_KEY, "Do not apply this text");
	gtk_drop_down_set_selected(GTK_DROP_DOWN(mode(COMMENT_KEY)), 0);
	EXPECT_FALSE(gtk_widget_get_sensitive(gtk_widget_get_parent(view(COMMENT_KEY))));
	EXPECT_TRUE(gtk_widget_get_sensitive(mode(COMMENT_KEY)));
	set(_("Headline"), "Xmp.photoshop.Headline", "Apply this headline");
	apply();
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		expect_string(fd, COMMENT_KEY, "Keep this description");
		expect_string(fd, "Xmp.photoshop.Headline", "Apply this headline");
		}
}

TEST_F(MetadataTemplate, LegacyCheckboxAndModeSettingsConvertToActions)
{
	g_autofree gchar *name = g_uuid_string_random();
	auto *entry = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_ENTRY(widget); });
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), name);
	set(_("Description"), COMMENT_KEY, "Added");
	set(_("Headline"), "Xmp.photoshop.Headline", "Ignored");
	set(_("Copyright"), "Xmp.dc.rights", "Ignored when clearing");
	click(_("Save template"));
	g_autofree gchar *filename = g_strconcat(name, ".ini", nullptr);
	g_autofree gchar *path = g_build_filename(get_rc_dir(), "metadata-templates", filename, nullptr);
	g_autoptr(GKeyFile) config = g_key_file_new();
	ASSERT_TRUE(g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, nullptr));
	for (const gchar *key : {COMMENT_KEY, "Xmp.photoshop.Headline", "Xmp.dc.rights"})
		{
		g_key_file_remove_key(config, key, "action", nullptr);
		g_key_file_set_boolean(config, key, "enabled", strcmp(key, "Xmp.photoshop.Headline") != 0);
		g_key_file_set_boolean(config, key, "clear", strcmp(key, COMMENT_KEY) != 0);
		g_key_file_set_string(config, key, "mode", "append");
		}
	ASSERT_TRUE(g_key_file_save_to_file(config, path, nullptr));
	auto *combo = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_DROP_DOWN(widget); });
	ASSERT_NE(combo, nullptr);
	const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo));
	click(_("New template"));
	gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), selected);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode(COMMENT_KEY))), 2U);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode("Xmp.photoshop.Headline"))), 0U);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(mode("Xmp.dc.rights"))), 3U);
	click(_("Delete template"));
}

TEST_F(MetadataTemplate, CancelDoesNotModifyFiles)
{
	set(_("Description"), COMMENT_KEY, "Do not apply");
	add_field("Custom city", "Xmp.photoshop.City");
	set("Custom city", "Xmp.photoshop.City", "Do not apply either");
	click(_("Cancel"));
	dialog = nullptr;
	for (GList *work = files; work; work = work->next)
		{
		auto *fd = static_cast<FileData *>(work->data);
		EXPECT_EQ(fd->modified_xmp, nullptr);
		}
}

} // namespace
