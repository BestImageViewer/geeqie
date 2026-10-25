/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <gtk/gtk.h>

#include "filedata.h"
#include "fullscreen.h"
#include "image.h"
#include "layout-image.h"
#include "layout.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "view-file.h"

namespace
{

TEST(ImageRectangle, PortraitOrientationUpdatesExistingAndNewSelections)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	auto *window = GTK_WINDOW(gtk_window_new());
	auto *imd = image_new(FALSE);
	gtk_window_set_default_size(window, 800, 600);
	gtk_window_set_child(window, imd->widget);
	g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 800, 600);
	gdk_pixbuf_fill(pixels, 0xffffffff);
	image_change_pixbuf(imd, pixels, 1.0, FALSE);
	gtk_window_present(window);
	for (int i = 0; i < 100; ++i)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_usleep(1000);
		}
	const auto saved_draw = options->draw_rectangle;
	options->draw_rectangle = TRUE;
	const auto saved_ratio = options->rectangle_draw_aspect_ratio;
	const auto saved_portrait = options->rectangle_draw_portrait;
	for (const auto mode : {RECTANGLE_DRAW_ASPECT_RATIO_SIXTEEN_NINE,
	                        RECTANGLE_DRAW_ASPECT_RATIO_FOUR_THREE, RECTANGLE_DRAW_ASPECT_RATIO_THREE_TWO})
		{
		options->rectangle_draw_aspect_ratio = mode;
		options->rectangle_draw_portrait = FALSE;
		image_select_rectangle(imd, TRUE);
		GdkRectangle landscape{};
		EXPECT_TRUE(image_get_selected_rectangle(imd, landscape));
		EXPECT_GT(landscape.width, landscape.height);
		options->rectangle_draw_portrait = TRUE;
		image_rectangle_update_aspect_ratio();
		GdkRectangle portrait{};
		EXPECT_TRUE(image_get_selected_rectangle(imd, portrait));
		EXPECT_GT(portrait.height, portrait.width);
		EXPECT_NEAR(static_cast<double>(portrait.height) / portrait.width,
		            static_cast<double>(landscape.width) / landscape.height, 0.02);
		EXPECT_NEAR(portrait.x + (portrait.width / 2.0), landscape.x + (landscape.width / 2.0), 2);
		EXPECT_NEAR(portrait.y + (portrait.height / 2.0), landscape.y + (landscape.height / 2.0), 2);
		EXPECT_GE(portrait.x, 0);
		EXPECT_GE(portrait.y, 0);
		EXPECT_LE(portrait.x + portrait.width, 800);
		EXPECT_LE(portrait.y + portrait.height, 600);
		image_select_rectangle(imd, TRUE);
		EXPECT_TRUE(image_get_selected_rectangle(imd, portrait));
		EXPECT_GT(portrait.height, portrait.width);
		options->rectangle_draw_portrait = FALSE;
		image_rectangle_update_aspect_ratio();
		EXPECT_TRUE(image_get_selected_rectangle(imd, landscape));
		EXPECT_GT(landscape.width, landscape.height);
		}
	image_select_rectangle(imd, FALSE);
	options->draw_rectangle = saved_draw;
	options->rectangle_draw_aspect_ratio = saved_ratio;
	options->rectangle_draw_portrait = saved_portrait;
	gtk_window_destroy(window);
}

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

TEST_F(LayoutImageSelection, PrintingUsesClickedPaneDespiteOtherSelections)
{
	for (gint i = 0; i < MAX_SPLIT_IMAGES; ++i)
		{
		g_autofree gchar *path = g_strdup_printf("/tmp/geeqie-print-pane-%d.jpg", i);
		auto fd = FileData::new_simple(path);
		image_set_fd(layout.split_images[i], fd);
		}
	layout.image = layout.split_images[0];
	for (gint i : {0, 1, 2}) select(i);
	g_autoptr(FileDataList) files = layout_image_print_list(&layout, layout.split_images[3]->widget);
	ASSERT_EQ(g_list_length(files), 1U);
	EXPECT_EQ(files->data, image_get_fd(layout.split_images[3]));
	EXPECT_EQ(layout.image, layout.split_images[0]);
	// The print request owns its source even after the pane changes image.
	image_set_fd(layout.split_images[3], nullptr);
	EXPECT_STREQ(static_cast<FileData *>(files->data)->path, "/tmp/geeqie-print-pane-3.jpg");
}

TEST_F(LayoutImageSelection, PrintingEmptyPaneDoesNotUseOtherSelectedImages)
{
	auto fd = FileData::new_simple("/tmp/geeqie-print-other-pane.jpg");
	image_set_fd(layout.split_images[0], fd);
	layout.image = layout.split_images[0];
	select(0);
	g_autoptr(FileDataList) empty = layout_image_print_list(&layout, layout.split_images[1]->widget);
	EXPECT_EQ(empty, nullptr);
	g_autoptr(FileDataList) active = layout_image_print_list(&layout);
	ASSERT_EQ(g_list_length(active), 1U);
	EXPECT_EQ(active->data, static_cast<FileData *>(fd));
}

TEST_F(LayoutImageSelection, PrintingFullscreenUsesFullscreenImage)
{
	auto normal_fd = FileData::new_simple("/tmp/geeqie-print-normal.jpg");
	auto fullscreen_fd = FileData::new_simple("/tmp/geeqie-print-fullscreen.jpg");
	image_set_fd(layout.split_images[0], normal_fd);
	image_set_fd(layout.split_images[3], fullscreen_fd);
	layout.image = layout.split_images[0];
	FullScreenData fullscreen{};
	fullscreen.imd = layout.split_images[3];
	layout.full_screen = &fullscreen;
	g_autoptr(FileDataList) files = layout_image_print_list(&layout);
	layout.full_screen = nullptr;
	ASSERT_EQ(g_list_length(files), 1U);
	EXPECT_EQ(files->data, static_cast<FileData *>(fullscreen_fd));
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
