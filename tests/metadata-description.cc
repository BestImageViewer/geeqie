/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include "bar.h"
#include "filedata.h"
#include "metadata.h"
#include "options.h"
#include "osd.h"

namespace
{

TEST(MetadataDescription, SavedSidebarTitles)
{
	EXPECT_STREQ(bar_pane_translate_title(PANE_COMMENT, "comment", "Comment"), _("Description"));

	EXPECT_STREQ(bar_pane_translate_title(PANE_COMMENT, "comment", "Field notes"), "Field notes");
}

TEST(MetadataDescription, LegacyOsdAlias)
{
	if (!options) options = conf_options_new();
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-description-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "image.jpg", nullptr);
	ASSERT_TRUE(g_file_set_contents(path, "", 0, nullptr));
	auto *fd = file_data_new_simple(path);
	ASSERT_NE(fd, nullptr);
	metadata_write_string(fd, COMMENT_KEY, "A <beautiful> view");

	const OsdTemplate vars;
	g_autofree gchar *description = image_osd_mkinfo("%description%", fd, vars);
	g_autofree gchar *comment = image_osd_mkinfo("%comment%", fd, vars);
	EXPECT_STREQ(description, "A &lt;beautiful&gt; view");
	EXPECT_STREQ(comment, description);
	g_autofree gchar *short_description = image_osd_mkinfo("%description:4%", fd, vars);
	g_autofree gchar *short_comment = image_osd_mkinfo("%comment:4%", fd, vars);
	EXPECT_STREQ(short_comment, short_description);

	if (fd->modified_xmp) metadata_write_queue_remove(fd);
	file_data_unref(fd);
	g_unlink(path);
	g_rmdir(directory);
}

GtkWidget *find_description_button(GtkWidget *widget)
{
	if (GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), _("Description")) == 0) return widget;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		auto *button = find_description_button(child);
		if (button) return button;
		}
	return nullptr;
}

TEST(MetadataDescription, OsdPickerInsertsDescription)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "No GTK display available";
	auto *window = gtk_window_new();
	auto *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child(GTK_WINDOW(window), box);
	auto *view = gtk_text_view_new();
	gtk_box_append(GTK_BOX(box), view);
	auto *picker = osd_new(4, view);
	gtk_box_append(GTK_BOX(box), picker);
	auto *button = find_description_button(picker);
	EXPECT_NE(button, nullptr);
	if (button)
		{
		g_signal_emit_by_name(button, "clicked");
		auto *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
		GtkTextIter start;
		GtkTextIter end;
		gtk_text_buffer_get_bounds(buffer, &start, &end);
		g_autofree gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
		EXPECT_STREQ(text, "%description%");
		}
	gtk_window_destroy(GTK_WINDOW(window));
}

} // namespace
