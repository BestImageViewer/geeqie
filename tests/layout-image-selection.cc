/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <gtk/gtk.h>

#include "filedata.h"
#include "image.h"
#include "layout.h"
#include "layout-image.h"
#include "options.h"
#include "view-file.h"

namespace
{

class LayoutImageSelection : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		g_object_ref_sink(container);
		layout.split_mode = SPLIT_QUAD;
		for (gint i = 0; i < MAX_SPLIT_IMAGES; i++)
			gtk_box_append(GTK_BOX(container), layout_image_new(&layout, i));
	}

	void TearDown() override
	{
		if (!container) return;
		layout_image_selection_clear(&layout);
		for (auto *imd : layout.split_images)
			{
			gtk_box_remove(GTK_BOX(container), imd->widget);
			g_object_unref(imd->widget);
			}
		g_object_unref(container);
		g_clear_object(&layout.split_image_sizegroup);
	}

	GtkCheckButton *selection_button(gint index)
	{
		for (auto *child = gtk_widget_get_first_child(layout.split_images[index]->overlay); child; child = gtk_widget_get_next_sibling(child))
			if (GTK_IS_CHECK_BUTTON(child)) return GTK_CHECK_BUTTON(child);
		return nullptr;
	}

	void select(gint index)
	{
		auto *button = selection_button(index);
		ASSERT_NE(button, nullptr);
		gtk_check_button_set_active(button, TRUE);
	}

	LayoutWindow layout{};
	GtkWidget *container = nullptr;
};

TEST_F(LayoutImageSelection, CapturesOnlyThreeSelectedImagesAndOwnsReferences)
{
	for (gint i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		g_autofree gchar *path = g_strdup_printf("/tmp/geeqie-pane-selection-%d.jpg", i);
		auto *fd = file_data_new_no_grouping(path);
		image_set_fd(layout.split_images[i], fd);
		file_data_unref(fd);
		}
	for (gint i : {0, 2, 3}) select(i);
	g_autoptr(FileDataList) list = layout_image_selection_list(&layout);
	ASSERT_EQ(g_list_length(list), 3U);
	EXPECT_EQ(list->data, image_get_fd(layout.split_images[0]));
	EXPECT_EQ(list->next->data, image_get_fd(layout.split_images[2]));
	EXPECT_EQ(list->next->next->data, image_get_fd(layout.split_images[3]));

	image_set_fd(layout.split_images[0], nullptr);
	EXPECT_STREQ(static_cast<FileData *>(list->data)->path, "/tmp/geeqie-pane-selection-0.jpg");
	for (gint i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		EXPECT_FALSE(layout.split_images_selected[i]);
		EXPECT_FALSE(gtk_check_button_get_active(selection_button(i)));
		EXPECT_FALSE(gtk_widget_has_css_class(layout.split_images[i]->widget, "pane-selected"));
		}
}

TEST_F(LayoutImageSelection, DeduplicatesAndExcludesHiddenAndEmptyPanes)
{
	auto *fd = file_data_new_no_grouping("/tmp/geeqie-pane-selection-shared.jpg");
	for (gint i : {0, 1, 2}) image_set_fd(layout.split_images[i], fd);
	file_data_unref(fd);
	auto *hidden_fd = file_data_new_no_grouping("/tmp/geeqie-pane-selection-hidden.jpg");
	image_set_fd(layout.split_images[2], hidden_fd);
	file_data_unref(hidden_fd);
	gtk_widget_set_visible(layout.split_images[2]->widget, FALSE);
	for (gint i = 0; i < MAX_SPLIT_IMAGES; i++) select(i);
	g_autoptr(FileDataList) list = layout_image_selection_list(&layout);
	ASSERT_EQ(g_list_length(list), 1U);
	EXPECT_EQ(list->data, image_get_fd(layout.split_images[0]));
}

TEST_F(LayoutImageSelection, ContextMenusPreferPaneSelectionOverClickedImage)
{
	for (gint i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		g_autofree gchar *path = g_strdup_printf("/tmp/geeqie-pane-context-%d.jpg", i);
		auto *fd = file_data_new_no_grouping(path);
		image_set_fd(layout.split_images[i], fd);
		file_data_unref(fd);
		}
	layout.image = layout.split_images[3];
	for (gint i : {0, 1, 2}) select(i);

	ViewFile vf{};
	vf.layout = &layout;
	vf.click_fd = image_get_fd(layout.image);
	g_autoptr(FileDataList) image_menu = layout_image_context_list(&layout);
	g_autoptr(FileDataList) file_menu = vf_pop_menu_file_list(&vf);
	ASSERT_EQ(g_list_length(image_menu), 3U);
	ASSERT_EQ(g_list_length(file_menu), 3U);
	for (gint i : {0, 1, 2})
		{
		EXPECT_EQ(g_list_nth_data(image_menu, i), image_get_fd(layout.split_images[i]));
		EXPECT_EQ(g_list_nth_data(file_menu, i), image_get_fd(layout.split_images[i]));
		}
	EXPECT_EQ(g_list_find(image_menu, vf.click_fd), nullptr);
	EXPECT_EQ(g_list_find(file_menu, vf.click_fd), nullptr);

	vf.click_fd = nullptr;
	g_autoptr(FileDataList) empty_space_menu = vf_pop_menu_file_list(&vf);
	EXPECT_EQ(g_list_length(empty_space_menu), 3U);

	layout_image_selection_clear(&layout);
	g_autoptr(FileDataList) fallback = layout_image_context_list(&layout);
	ASSERT_EQ(g_list_length(fallback), 1U);
	EXPECT_EQ(fallback->data, image_get_fd(layout.image));
	EXPECT_EQ(vf_pop_menu_file_list(&vf), nullptr);
}

TEST_F(LayoutImageSelection, CheckboxPreservesActivePaneAndSurvivesFrameChanges)
{
	auto *fd = file_data_new_no_grouping("/tmp/geeqie-pane-checkbox.jpg");
	image_set_fd(layout.split_images[0], fd);
	file_data_unref(fd);
	layout.image = layout.split_images[3];
	layout.active_split_image = 3;
	auto *button = selection_button(0);
	ASSERT_NE(button, nullptr);
	EXPECT_STREQ(gtk_widget_get_tooltip_text(GTK_WIDGET(button)), "Select this image for file operations");
	gtk_check_button_set_active(button, TRUE);
	EXPECT_TRUE(layout.split_images_selected[0]);
	EXPECT_TRUE(gtk_widget_has_css_class(layout.split_images[0]->widget, "pane-selected"));
	EXPECT_EQ(layout.image, layout.split_images[3]);
	EXPECT_EQ(layout.active_split_image, 3);

	image_set_frame(layout.split_images[0], FALSE);
	EXPECT_EQ(selection_button(0), button);
	image_set_frame(layout.split_images[0], TRUE);
	EXPECT_EQ(selection_button(0), button);
	EXPECT_TRUE(gtk_check_button_get_active(button));

	gtk_check_button_set_active(button, FALSE);
	EXPECT_FALSE(layout.split_images_selected[0]);
	EXPECT_FALSE(gtk_widget_has_css_class(layout.split_images[0]->widget, "pane-selected"));

	gtk_check_button_set_active(selection_button(1), TRUE);
	EXPECT_FALSE(layout.split_images_selected[1]);
	EXPECT_FALSE(gtk_check_button_get_active(selection_button(1)));
}

} // namespace
