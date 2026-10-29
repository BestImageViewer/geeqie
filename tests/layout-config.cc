/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "bar-sort.h"
#include "bar.h"
#include "color-label.h"
#include "collect-io.h"
#include "collect.h"
#include "filedata.h"
#include "filefilter.h"
#include "history-list.h"
#include "image.h"
#include "intl.h"
#include "layout-config.h"
#include "layout-image.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "main.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "preferences.h"
#include "rcfile.h"
#include "search.h"
#include "ui-bookmark.h"
#include "ui-fileops.h"
#include "view-file.h"

namespace
{

GtkWidget *find_widget(GtkWidget *widget, GType type, const gchar *label = nullptr)
{
	if (G_TYPE_CHECK_INSTANCE_TYPE(widget, type))
		{
		const gchar *text = GTK_IS_BUTTON(widget) ? gtk_button_get_label(GTK_BUTTON(widget)) :
		                    GTK_IS_LABEL(widget) ? gtk_label_get_text(GTK_LABEL(widget)) : nullptr;
		if (!label || g_strcmp0(text, label) == 0) return widget;
		}
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		if (auto *found = find_widget(child, type, label)) return found;
	return nullptr;
}

GObject *find_controller(GtkWidget *widget, GType type, guint &skip)
{
	g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(widget);
	for (guint i = 0; i < g_list_model_get_n_items(controllers); i++)
		{
		g_autoptr(GObject) controller = G_OBJECT(g_list_model_get_item(controllers, i));
		if (!G_TYPE_CHECK_INSTANCE_TYPE(controller, type)) continue;
		if (skip == 0) return G_OBJECT(g_object_ref(controller));
		skip--;
		}
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		if (auto *found = find_controller(child, type, skip)) return found;
	return nullptr;
}

class LayoutConfigKeyboard : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		window = gtk_window_new();
		config = layout_config_new(2, "123");
		gtk_window_set_child(GTK_WINDOW(window), config);
		gtk_window_set_default_size(GTK_WINDOW(window), 600, 350);
		gtk_window_present(GTK_WINDOW(window));
		list = find_widget(config, GTK_TYPE_LIST_VIEW);
		ASSERT_NE(list, nullptr);
		selection = GTK_SINGLE_SELECTION(gtk_list_view_get_model(GTK_LIST_VIEW(list)));
		up = find_widget(config, GTK_TYPE_BUTTON, "Move _Up");
		down = find_widget(config, GTK_TYPE_BUTTON, "Move _Down");
		ASSERT_NE(up, nullptr);
		ASSERT_NE(down, nullptr);
		g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(list);
		for (guint i = 0; i < g_list_model_get_n_items(controllers); i++)
			{
			g_autoptr(GObject) controller = G_OBJECT(g_list_model_get_item(controllers, i));
			if (GTK_IS_EVENT_CONTROLLER_KEY(controller) &&
			    gtk_event_controller_get_propagation_phase(GTK_EVENT_CONTROLLER(controller)) == GTK_PHASE_CAPTURE)
				keys = GTK_EVENT_CONTROLLER_KEY(controller);
			}
		ASSERT_NE(keys, nullptr);
		ASSERT_TRUE(settle());
	}

	void TearDown() override
	{
		if (window) gtk_window_destroy(GTK_WINDOW(window));
		while (g_main_context_iteration(nullptr, FALSE)) {}
	}

	bool settle()
	{
		const gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
		while (g_get_monotonic_time() < deadline)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			if (gtk_widget_get_mapped(list) && gtk_widget_get_width(list) > 0) return true;
			g_usleep(1000);
			}
		return false;
	}

	gboolean key(guint keyval, GdkModifierType modifiers = GDK_CONTROL_MASK)
	{
		gboolean handled = FALSE;
		g_signal_emit_by_name(keys, "key-pressed", keyval, 0, modifiers, &handled);
		return handled;
	}

	void expect_order(const gchar *expected)
	{
		gint style = -1;
		g_autofree gchar *order = layout_config_get(config, &style);
		EXPECT_STREQ(order, expected);
		EXPECT_EQ(style, 2);
	}

	GtkWidget *window = nullptr;
	GtkWidget *config = nullptr;
	GtkWidget *list = nullptr;
	GtkWidget *up = nullptr;
	GtkWidget *down = nullptr;
	GtkSingleSelection *selection = nullptr;
	GtkEventControllerKey *keys = nullptr;
};

TEST_F(LayoutConfigKeyboard, ButtonsMoveTheSelectedPaneAndRespectBoundaries)
{
	expect_order("123");
	EXPECT_FALSE(gtk_widget_get_sensitive(up));
	EXPECT_TRUE(gtk_widget_get_sensitive(down));
	GObject *selected = G_OBJECT(gtk_single_selection_get_selected_item(selection));
	g_signal_emit_by_name(down, "clicked");
	expect_order("213");
	EXPECT_EQ(gtk_single_selection_get_selected(selection), 1);
	EXPECT_EQ(gtk_single_selection_get_selected_item(selection), selected);
	EXPECT_TRUE(gtk_widget_get_sensitive(up));
	EXPECT_TRUE(gtk_widget_get_sensitive(down));
	g_signal_emit_by_name(down, "clicked");
	expect_order("231");
	EXPECT_FALSE(gtk_widget_get_sensitive(down));
	EXPECT_EQ(gtk_single_selection_get_selected_item(selection), selected);
	g_signal_emit_by_name(up, "clicked");
	g_signal_emit_by_name(up, "clicked");
	expect_order("123");
	EXPECT_FALSE(gtk_widget_get_sensitive(up));
}

TEST_F(LayoutConfigKeyboard, ControlArrowsMoveWithoutWrappingOrHandlingOtherKeys)
{
	gtk_single_selection_set_selected(selection, 1);
	GObject *selected = G_OBJECT(gtk_single_selection_get_selected_item(selection));
	EXPECT_FALSE(key(GDK_KEY_Down, static_cast<GdkModifierType>(0)));
	EXPECT_FALSE(key(GDK_KEY_Up, static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK)));
	EXPECT_FALSE(key(GDK_KEY_Left));
	expect_order("123");
	EXPECT_TRUE(key(GDK_KEY_Up));
	expect_order("213");
	EXPECT_EQ(gtk_single_selection_get_selected(selection), 0);
	EXPECT_TRUE(key(GDK_KEY_Up));
	expect_order("213");
	EXPECT_TRUE(key(GDK_KEY_KP_Down));
	EXPECT_TRUE(key(GDK_KEY_Down));
	expect_order("132");
	EXPECT_EQ(gtk_single_selection_get_selected(selection), 2);
	EXPECT_EQ(gtk_single_selection_get_selected_item(selection), selected);
	EXPECT_TRUE(key(GDK_KEY_Down));
	expect_order("132");
}

TEST_F(LayoutConfigKeyboard, ReorderingRoundTripsAllPaneOrders)
{
	const gchar *orders[] = {"123", "132", "213", "231", "312", "321"};
	for (const gchar *order : orders)
		{
		g_autoptr(GtkWidget) variant = layout_config_new(1, order);
		g_object_ref_sink(variant);
		auto *variant_list = find_widget(variant, GTK_TYPE_LIST_VIEW);
		auto *variant_selection = GTK_SINGLE_SELECTION(gtk_list_view_get_model(GTK_LIST_VIEW(variant_list)));
		gtk_single_selection_set_selected(variant_selection, 1);
		GtkWidget *variant_up = find_widget(variant, GTK_TYPE_BUTTON, "Move _Up");
		g_signal_emit_by_name(variant_up, "clicked");
		gint style = -1;
		g_autofree gchar *saved = layout_config_get(variant, &style);
		gchar expected[] = {order[1], order[0], order[2], '\0'};
		EXPECT_STREQ(saved, expected);
		EXPECT_EQ(style, 1);
		g_autoptr(GtkWidget) restored = layout_config_new(style, saved);
		g_object_ref_sink(restored);
		g_autofree gchar *round_trip = layout_config_get(restored, &style);
		EXPECT_STREQ(round_trip, saved);
		}
}

TEST_F(LayoutConfigKeyboard, KeyboardMovesFocusWithTheSelectedPane)
{
	gtk_widget_grab_focus(list);
	EXPECT_TRUE(key(GDK_KEY_Down));
	EXPECT_TRUE(key(GDK_KEY_Down));
	expect_order("231");
	const gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
	bool focused_selected_pane = false;
	while (!focused_selected_pane && g_get_monotonic_time() < deadline)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		GtkWidget *focus = gtk_root_get_focus(GTK_ROOT(window));
		if (focus && focus != list && gtk_widget_is_ancestor(focus, list))
			{
			while (gtk_widget_get_parent(focus) != list) focus = gtk_widget_get_parent(focus);
			focused_selected_pane = find_widget(focus, GTK_TYPE_LABEL, "Tools") != nullptr;
			}
		if (!focused_selected_pane) g_usleep(1000);
		}
	EXPECT_TRUE(focused_selected_pane);
	EXPECT_EQ(gtk_single_selection_get_selected(selection), 2);
}

TEST_F(LayoutConfigKeyboard, DraggingStillMovesAndSelectsTheDraggedPane)
{
	gtk_single_selection_set_selected(selection, 1);
	guint skip = 0;
	g_autoptr(GObject) drag = find_controller(list, GTK_TYPE_DRAG_SOURCE, skip);
	skip = 2;
	g_autoptr(GObject) drop = find_controller(list, GTK_TYPE_DROP_TARGET, skip);
	ASSERT_NE(drag, nullptr);
	ASSERT_NE(drop, nullptr);
	g_autoptr(GdkContentProvider) provider = nullptr;
	g_signal_emit_by_name(drag, "prepare", 0.0, 0.0, &provider);
	ASSERT_NE(provider, nullptr);
	g_autoptr(GObject) source = G_OBJECT(g_list_model_get_item(G_LIST_MODEL(selection), 0));
	g_auto(GValue) value = G_VALUE_INIT;
	g_value_init(&value, G_OBJECT_TYPE(source));
	ASSERT_TRUE(gdk_content_provider_get_value(provider, &value, nullptr));
	GtkWidget *target = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(drop));
	gboolean accepted = FALSE;
	g_signal_emit_by_name(drop, "drop", &value, 0.0, static_cast<double>(gtk_widget_get_height(target)), &accepted);
	EXPECT_TRUE(accepted);
	expect_order("231");
	EXPECT_EQ(gtk_single_selection_get_selected(selection), 2);
	EXPECT_EQ(gtk_single_selection_get_selected_item(selection), source);
}

class PartialLayoutConfig : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		setup_default_options(options);
		saved_hamburger = options->hamburger_menu;
		saved_expand = options->expand_menu_toolbar;
		saved_menu_settings = true;
		filter_add_defaults();
		filter_rebuild();
		accel_map_load_merged();
		previous_app = g_application_get_default();
		if (previous_app) g_object_ref(previous_app);
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.PartialLayoutTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		app = GTK_APPLICATION(g_object_ref(test_app));
		g_application_set_default(G_APPLICATION(app));
		directory = g_dir_make_tmp("geeqie-partial-layout-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		const gchar *names[] = {"style", "order", "home_path", "startup_path", "show_info_pixel", "show_thumbnails", "animate", nullptr};
		const gchar *values[] = {"2", "321", directory, "2", "true", "true", "false", nullptr};
		layout = layout_new_from_config(names, values, FALSE);
		ASSERT_NE(layout, nullptr);
		id = layout->options.id;
	}

	void TearDown() override
	{
		if (layout) layout_free(layout);
		if (saved_menu_settings)
			{
			options->hamburger_menu = saved_hamburger;
			options->expand_menu_toolbar = saved_expand;
			}
		if (app)
			{
			g_application_set_default(previous_app);
			g_clear_object(&app);
			}
		g_clear_object(&previous_app);
		if (directory) g_rmdir(directory);
		g_free(directory);
	}

	void expect_preserved(bool info_pixel = true)
	{
		EXPECT_EQ(layout->options.style, 2);
		EXPECT_STREQ(layout->options.order, "321");
		EXPECT_STREQ(layout->options.id, id.c_str());
		EXPECT_STREQ(layout->options.home_path, directory);
		EXPECT_EQ(layout->options.startup_path, STARTUP_PATH_HOME);
		EXPECT_EQ(layout->options.show_info_pixel, info_pixel);
		EXPECT_TRUE(layout->options.show_thumbnails);
		EXPECT_FALSE(layout->options.animate);
	}

	static bool wait_layout(const std::function<bool()> &ready)
	{
		const auto deadline = g_get_monotonic_time() + (2 * G_TIME_SPAN_SECOND);
		while (g_get_monotonic_time() < deadline)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			if (ready()) return true;
			g_usleep(1000);
			}
		return false;
	}

	bool sort_width_settled()
	{
		return layout->sort_manager_restore_id == 0 && layout->bar_sort &&
		       gtk_widget_get_mapped(layout->bar_sort) && gtk_widget_get_width(layout->bar_sort) > 0 &&
		       gtk_widget_get_width(layout->utility_paned) == gtk_paned_get_position(GTK_PANED(layout->sort_manager_paned));
	}

	LayoutWindow *layout = nullptr;
	GtkApplication *app = nullptr;
	GApplication *previous_app = nullptr;
	gchar *directory = nullptr;
	std::string id;
	gboolean saved_hamburger = FALSE;
	gboolean saved_expand = FALSE;
	bool saved_menu_settings = false;
};

class SortCollectionHistory : public PartialLayoutConfig
{
protected:
	void SetUp() override
	{
		PartialLayoutConfig::SetUp();
		if (!layout) return;
		for (const char *key : {"collection_history", "sort_manager_external_collections", "sort_manager_collections", "sort_manager_selected_collection"})
			{
			if (const auto *items = history_list_find_by_key(key)) saved_history[key] = *items;
			else saved_history[key] = {};
			history_list_free_key(key);
			}
		for (const char *name : {"a", "b"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			ASSERT_EQ(g_mkdir(path, 0700), 0);
			}
		layout->bar_sort_enabled = TRUE;
		layout->options.bar_sort.mode = BarSort::MODE_COLLECTION;
		layout_bar_sort_set(layout, bar_sort_new_default(layout));
		ASSERT_NE(layout->bar_sort, nullptr);
		// The dropdown also contains an internal scroller; use the pane's direct child.
		for (auto *child = gtk_widget_get_first_child(layout->bar_sort); child; child = gtk_widget_get_next_sibling(child))
			if (GTK_IS_SCROLLED_WINDOW(child)) bookmarks = child;
		ASSERT_NE(bookmarks, nullptr);
	}

	void TearDown() override
	{
		if (layout) collect_manager_flush();
		for (const auto &path : paths)
			{
			g_chmod(path.c_str(), 0600);
			g_unlink(path.c_str());
			}
		if (directory)
			for (const char *name : {"a", "b"})
				{
				g_autofree gchar *path = g_build_filename(directory, name, nullptr);
				g_rmdir(path);
				}
		PartialLayoutConfig::TearDown();
		for (const auto &[key, items] : saved_history)
			{
			history_list_free_key(key.c_str());
			for (auto item = items.crbegin(); item != items.crend(); ++item) history_list_add_to_key(key.c_str(), item->c_str(), 0);
			}
	}

	std::string save(const char *name)
	{
		std::string path = std::string(directory) + "/" + name;
		paths.push_back(path);
		g_autoptr(CollectionData) cd = collection_new(nullptr);
		EXPECT_TRUE(collection_save(cd, path.c_str()));
		return path;
	}

	unsigned int occurrences(const char *path)
	{
		unsigned int count = 0;
		for (unsigned int i = 0; const char *destination = bookmark_list_get_path_at_index(bookmarks, i); ++i)
			if (g_strcmp0(destination, path) == 0) ++count;
		return count;
	}

	GtkWidget *bookmarks = nullptr;
	std::vector<std::string> paths;
	std::map<std::string, HistoryList> saved_history;
};

TEST_F(SortCollectionHistory, OpenAndSaveRefreshDestinationsAndPreserveSelection)
{
	const auto first = save("a/same.gqv");
	EXPECT_EQ(occurrences(first.c_str()), 1U);
	bookmark_list_set_selected_path(bookmarks, first.c_str());
	const auto second = save("b/same.gqv");
	EXPECT_EQ(occurrences(second.c_str()), 1U);
	EXPECT_STREQ(bookmark_list_get_selected_path(bookmarks), first.c_str());
	for (const auto &path : {first, second})
		{
		g_autofree gchar *label = g_strdup_printf("same (%s)", path.c_str());
		EXPECT_NE(find_widget(bookmarks, GTK_TYPE_LABEL, label), nullptr);
		}

	// A collection opened after it leaves history is discovered immediately.
	history_list_free_key("collection_history");
	bar_sort_refresh_collections(layout->bar_sort);
	EXPECT_EQ(occurrences(second.c_str()), 0U);
	g_autoptr(CollectionData) cd = collection_new(second.c_str());
	ASSERT_TRUE(collection_load(cd, second.c_str(), COLLECTION_LOAD_NONE));
	EXPECT_EQ(occurrences(second.c_str()), 1U);
}

TEST_F(SortCollectionHistory, DeduplicatesAndRetainsExplicitDestinations)
{
	const auto path = save("a/same.gqv");
	ASSERT_TRUE(bar_sort_collection_add(path.c_str()));
	g_autofree gchar *alias = g_build_filename(directory, "a", "..", "a", "same.gqv", nullptr);
	history_list_add_to_key("collection_history", alias, 0);
	bar_sort_refresh_collections(layout->bar_sort);
	EXPECT_EQ(occurrences(path.c_str()), 1U);
	history_list_free_key("collection_history");
	bar_sort_refresh_collections(layout->bar_sort);
	EXPECT_EQ(occurrences(path.c_str()), 1U);

	g_autofree gchar *missing = g_build_filename(directory, "missing.gqv", nullptr);
	history_list_add_to_key("collection_history", missing, 0);
	bar_sort_refresh_collections(layout->bar_sort);
	EXPECT_EQ(occurrences(missing), 0U);
	const auto *history = history_list_find_by_key("collection_history");
	ASSERT_NE(history, nullptr);
	EXPECT_NE(std::find(history->begin(), history->end(), missing), history->end());

	ASSERT_EQ(g_chmod(path.c_str(), 0000), 0);
	bar_sort_refresh_collections(layout->bar_sort);
	EXPECT_EQ(occurrences(path.c_str()), 0U);
	ASSERT_EQ(g_chmod(path.c_str(), 0600), 0);
	bar_sort_refresh_collections(layout->bar_sort);
	EXPECT_EQ(occurrences(path.c_str()), 1U);
}

class DirectionalReadAhead : public PartialLayoutConfig
{
protected:
	void SetUp() override
	{
		PartialLayoutConfig::SetUp();
		if (!layout) return;
		saved_image = options->image;
		options->image.enable_read_ahead = FALSE;
		options->image.read_ahead_images = 3;
		options->image.image_cache_max = 16;
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 512, 384);
		gdk_pixbuf_fill(pixels, 0x336699ff);
		for (gint i = 0; i < 8; ++i)
			{
			g_autofree gchar *path = g_strdup_printf("%s/image-%d.png", directory, i);
			ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "png", nullptr, nullptr));
			files.push_back(file_data_new_simple(path));
			}
		layout_set_path(layout, directory);
		vf_refresh(layout->vf);
		ASSERT_EQ(layout_list_count(layout), 8U);
		gtk_window_present(GTK_WINDOW(layout->window));
		const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
		while (g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			g_usleep(1000);
			}
		options->image.enable_read_ahead = TRUE;
	}

	void TearDown() override
	{
		if (layout) image_prebuffer_set(layout->image, nullptr);
		for (auto *fd : files)
			{
			g_remove(fd->path);
			file_data_unref(fd);
			}
		if (!files.empty()) options->image = saved_image;
		PartialLayoutConfig::TearDown();
	}

	bool finished()
	{
		const gint64 settled = g_get_monotonic_time() + (100 * G_TIME_SPAN_MILLISECOND);
		const gint64 deadline = g_get_monotonic_time() + (10 * G_TIME_SPAN_SECOND);
		while (g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			if (!layout->image->il && !layout->image->read_ahead_il &&
			    !layout->image->read_ahead_idle_id && !layout->image->read_ahead_queue &&
			    g_get_monotonic_time() >= settled) return true;
			g_usleep(1000);
			}
		return false;
	}

	decltype(options->image) saved_image{};
	std::vector<FileData *> files;
};

TEST_F(DirectionalReadAhead, LoadsSeveralImagesAndReversesDirection)
{
	layout_image_set_with_ahead(layout, files[3], files[4]);
	ASSERT_TRUE(finished());
	for (gint i : {4, 5, 6}) EXPECT_NE(files[i]->pixbuf, nullptr);
	EXPECT_EQ(files[2]->pixbuf, nullptr);
	layout_image_set_with_ahead(layout, files[3], files[2]);
	ASSERT_TRUE(finished());
	for (gint i : {2, 1, 0}) EXPECT_NE(files[i]->pixbuf, nullptr);
	EXPECT_EQ(image_get_fd(layout->image), files[3]);
}

TEST_F(DirectionalReadAhead, ReplacesPendingWorkAndCancelsWhenDisabled)
{
	layout_image_set_with_ahead(layout, files[3], files[4]);
	layout_image_set_with_ahead(layout, files[3], files[2]);
	ASSERT_TRUE(finished());
	EXPECT_NE(files[0]->pixbuf, nullptr);
	EXPECT_EQ(files[4]->pixbuf, nullptr);
	options->image.enable_read_ahead = FALSE;
	layout_image_set_with_ahead(layout, files[3], files[4]);
	ASSERT_TRUE(finished());
	EXPECT_EQ(files[4]->pixbuf, nullptr);
}

TEST_F(DirectionalReadAhead, StopsAtDecodedImageBudget)
{
	options->image.image_cache_max = 2;
	options->image.read_ahead_images = 7;
	layout_image_set_with_ahead(layout, files[0], files[1]);
	ASSERT_TRUE(finished());
	EXPECT_NE(files[1]->pixbuf, nullptr);
	EXPECT_NE(files[2]->pixbuf, nullptr);
	EXPECT_EQ(files[3]->pixbuf, nullptr);
	EXPECT_EQ(files[4]->pixbuf, nullptr);
}

TEST_F(DirectionalReadAhead, SelectionPreloadsOnlySelectedImages)
{
	g_autoptr(FileDataList) selected = nullptr;
	for (gint i : {1, 3, 6}) selected = g_list_append(selected, file_data_ref(files[i]));
	options->image.enable_read_ahead = FALSE;
	layout_select_none(layout);
	layout_select_list(layout, selected);
	ASSERT_TRUE(finished());
	options->image.enable_read_ahead = TRUE;
	layout_image_set_with_ahead(layout, files[3], files[6]);
	ASSERT_TRUE(finished());
	EXPECT_NE(files[6]->pixbuf, nullptr);
	EXPECT_NE(files[1]->pixbuf, nullptr);
	EXPECT_EQ(files[4]->pixbuf, nullptr);
	EXPECT_EQ(files[5]->pixbuf, nullptr);
}

TEST_F(DirectionalReadAhead, CollectionUsesDisplayedOrder)
{
	g_autoptr(CollectionData) collection = collection_new(nullptr);
	for (gint i : {6, 2, 5, 1}) ASSERT_TRUE(collection_add(collection, files[i], FALSE));
	collection->changed = FALSE;
	options->image.enable_read_ahead = FALSE;
	ASSERT_TRUE(layout_set_collection(layout, collection));
	auto sort = layout->vf->sort;
	sort.method = SORT_NONE;
	sort.ascending = TRUE;
	vf_sort_set(layout->vf, sort);
	ASSERT_TRUE(finished());
	options->image.enable_read_ahead = TRUE;
	layout_image_set_with_ahead(layout, files[6], files[2]);
	ASSERT_TRUE(finished());
	for (gint i : {2, 5, 1}) EXPECT_NE(files[i]->pixbuf, nullptr);
	EXPECT_EQ(files[7]->pixbuf, nullptr);
}

TEST_F(DirectionalReadAhead, ZeroBudgetDoesNotPreload)
{
	options->image.image_cache_max = 0;
	layout_image_set_with_ahead(layout, files[3], files[4]);
	ASSERT_TRUE(finished());
	EXPECT_EQ(files[4]->pixbuf, nullptr);
}

TEST_F(DirectionalReadAhead, PendingQueueSurvivesImageTransfer)
{
	layout_image_set_with_ahead(layout, files[3], files[4]);
	auto *other = image_new(FALSE);
	auto *window = gtk_window_new();
	gtk_window_set_child(GTK_WINDOW(window), other->widget);
	image_move_from_image(other, layout->image);
	image_move_from_image(layout->image, other);
	gtk_window_destroy(GTK_WINDOW(window));
	ASSERT_TRUE(finished());
	for (gint i : {4, 5, 6}) EXPECT_NE(files[i]->pixbuf, nullptr);
}

TEST_F(DirectionalReadAhead, ChangedFileListCancelsPendingQueue)
{
	layout_image_set_with_ahead(layout, files[3], files[4]);
	ASSERT_EQ(g_remove(files[4]->path), 0);
	vf_refresh(layout->vf);
	EXPECT_EQ(layout->image->read_ahead_queue, nullptr);
	EXPECT_EQ(layout->image->read_ahead_idle_id, 0U);
	ASSERT_TRUE(finished());
	EXPECT_EQ(files[5]->pixbuf, nullptr);
}

TEST_F(DirectionalReadAhead, ChangingSortOrderCancelsPendingQueue)
{
	layout_image_set_with_ahead(layout, files[3], files[4]);
	ASSERT_NE(layout->image->read_ahead_queue, nullptr);
	auto sort = layout->vf->sort;
	sort.ascending = !sort.ascending;
	vf_sort_set(layout->vf, sort);
	EXPECT_EQ(layout->image->read_ahead_queue, nullptr);
	EXPECT_EQ(layout->image->read_ahead_idle_id, 0U);
}

class SearchCriteriaLayout : public PartialLayoutConfig
{
protected:
	void SetUp() override
	{
		PartialLayoutConfig::SetUp();
		if (!layout) return;
		saved_positions = options->save_window_positions;
		options->save_window_positions = TRUE;
		for (const gchar *name : {"match.svg", "other.svg"})
			{
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			ASSERT_TRUE(g_file_set_contents(path, "<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'/>", -1, nullptr));
			}
		ASSERT_NO_FATAL_FAILURE(open_search());
	}

	void TearDown() override
	{
		if (search) gtk_window_close(GTK_WINDOW(search));
		search = nullptr;
		if (layout)
			{
			options->save_window_positions = saved_positions;
			for (const gchar *name : {"match.svg", "other.svg"})
				{
				g_autofree gchar *path = g_build_filename(directory, name, nullptr);
				g_remove(path);
				}
			}
		PartialLayoutConfig::TearDown();
	}

	void open_search()
	{
		search_new(layout->dir_fd, nullptr);
		auto *windows = gtk_window_get_toplevels();
		for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
			{
			g_autoptr(GtkWindow) candidate = GTK_WINDOW(g_list_model_get_item(windows, i));
			const gchar *title = gtk_window_get_title(candidate);
			if (title && g_str_has_prefix(title, _("Image search")))
				{
				search = GTK_WIDGET(candidate);
				break;
				}
			}
		ASSERT_NE(search, nullptr);
		auto *box = gtk_window_get_child(GTK_WINDOW(search));
		notebook = GTK_NOTEBOOK(gtk_widget_get_first_child(box));
		ASSERT_TRUE(GTK_IS_NOTEBOOK(notebook));
		ASSERT_EQ(gtk_notebook_get_n_pages(notebook), 2);
		criteria = gtk_notebook_get_nth_page(notebook, 0);
		results = gtk_notebook_get_nth_page(notebook, 1);
		ASSERT_TRUE(wait_layout([&]() { return gtk_widget_get_mapped(criteria); }));
	}

	GtkWidget *first_condition()
	{
		auto *box = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(criteria));
		if (GTK_IS_VIEWPORT(box)) box = gtk_viewport_get_child(GTK_VIEWPORT(box));
		for (auto *row = gtk_widget_get_first_child(box); row; row = gtk_widget_get_next_sibling(row))
			{
			auto *check = gtk_widget_get_first_child(row);
			if (GTK_IS_CHECK_BUTTON(check)) return check;
			}
		return nullptr;
	}

	GtkWidget *search = nullptr;
	GtkNotebook *notebook = nullptr;
	GtkWidget *criteria = nullptr;
	GtkWidget *results = nullptr;
	gboolean saved_positions = FALSE;
};

TEST_F(SearchCriteriaLayout, TabsPreserveConditionsAndLeaveControlsVisible)
{
	ASSERT_NE(search, nullptr);
	EXPECT_EQ(gtk_notebook_get_current_page(notebook), 0);
	auto *check = first_condition();
	ASSERT_NE(check, nullptr);
	ASSERT_TRUE(gtk_check_button_get_active(GTK_CHECK_BUTTON(check)));
	auto *summary = GTK_LABEL(gtk_notebook_get_tab_label(notebook, criteria));
	g_autofree gchar *enabled = g_strdup(gtk_label_get_text(summary));
	gtk_check_button_set_active(GTK_CHECK_BUTTON(check), FALSE);
	EXPECT_STRNE(gtk_label_get_text(summary), enabled);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(check), TRUE);
	EXPECT_STREQ(gtk_label_get_text(summary), enabled);
	auto *find = find_widget(search, GTK_TYPE_BUTTON, _("Find"));
	ASSERT_NE(find, nullptr);
	for (int page : {1, 0})
		{
		gtk_notebook_set_current_page(notebook, page);
		auto *content = gtk_notebook_get_nth_page(notebook, page);
		ASSERT_TRUE(wait_layout([&]() { return gtk_widget_get_mapped(content) && gtk_widget_get_width(content) > 0; }));
		EXPECT_GE(gtk_widget_get_width(content), gtk_widget_get_width(GTK_WIDGET(notebook)) - 10);
		graphene_rect_t bounds{};
		ASSERT_TRUE(gtk_widget_compute_bounds(find, search, &bounds));
		EXPECT_LE(bounds.origin.y + bounds.size.height, gtk_widget_get_height(search));
		EXPECT_TRUE(gtk_widget_get_mapped(find));
		EXPECT_TRUE(gtk_check_button_get_active(GTK_CHECK_BUTTON(check)));
		}
}

TEST_F(SearchCriteriaLayout, StartingSearchShowsResultsAndUpdatesMatchCount)
{
	ASSERT_NE(search, nullptr);
	auto *check = first_condition();
	ASSERT_NE(check, nullptr);
	auto *entry = find_widget(gtk_widget_get_parent(check), GTK_TYPE_ENTRY);
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), "match");
	auto *find = find_widget(search, GTK_TYPE_BUTTON, _("Find"));
	ASSERT_NE(find, nullptr);
	g_signal_emit_by_name(find, "clicked");
	EXPECT_EQ(gtk_notebook_get_current_page(notebook), 1);
	gtk_notebook_set_current_page(notebook, 0);
	auto *view = GTK_COLUMN_VIEW(find_widget(results, GTK_TYPE_COLUMN_VIEW));
	ASSERT_NE(view, nullptr);
	ASSERT_TRUE(wait_layout([&]() { return gtk_widget_get_sensitive(find) &&
	                                      g_list_model_get_n_items(G_LIST_MODEL(gtk_column_view_get_model(view))) == 1; }));
	EXPECT_EQ(gtk_notebook_get_current_page(notebook), 0);
	gtk_notebook_set_current_page(notebook, 1);
	ASSERT_TRUE(wait_layout([&]() { return find_widget(results, GTK_TYPE_LABEL, "match.svg") != nullptr; }));
	g_autofree gchar *expected = g_strdup_printf(_("Results (%d)"), 1);
	EXPECT_STREQ(gtk_label_get_text(GTK_LABEL(gtk_notebook_get_tab_label(notebook, results))), expected);
	gtk_notebook_set_current_page(notebook, 0);
	EXPECT_STREQ(gtk_editable_get_text(GTK_EDITABLE(entry)), "match");
	EXPECT_EQ(g_list_model_get_n_items(G_LIST_MODEL(gtk_column_view_get_model(view))), 1U);
	gtk_editable_set_text(GTK_EDITABLE(entry), "absent");
	g_signal_emit_by_name(find, "clicked");
	EXPECT_EQ(gtk_notebook_get_current_page(notebook), 1);
	ASSERT_TRUE(wait_layout([&]() { return gtk_widget_get_sensitive(find); }));
	EXPECT_EQ(g_list_model_get_n_items(G_LIST_MODEL(gtk_column_view_get_model(view))), 0U);
}

TEST_F(SearchCriteriaLayout, ReopeningStartsOnCriteriaAndKeepsWindowGeometry)
{
	ASSERT_NE(search, nullptr);
	gtk_window_set_default_size(GTK_WINDOW(search), 800, 600);
	ASSERT_TRUE(wait_layout([&]() { return gtk_widget_get_width(search) == 800; }));
	gtk_notebook_set_current_page(notebook, 1);
	gtk_window_close(GTK_WINDOW(search));
	search = nullptr;
	EXPECT_EQ(layout->options.search_window.width, 800);
	ASSERT_NO_FATAL_FAILURE(open_search());
	EXPECT_EQ(gtk_notebook_get_current_page(notebook), 0);
	EXPECT_EQ(gtk_widget_get_width(search), 800);
}

TEST_F(PartialLayoutConfig, SortManagerWidthSurvivesWindowResizeAndSidebarToggles)
{
	layout_bar_sort_toggle(layout);
	gtk_window_present(GTK_WINDOW(layout->window));
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled(); }));
	auto *paned = GTK_PANED(layout->sort_manager_paned);
	EXPECT_EQ(gtk_paned_get_end_child(paned), layout->bar_sort);
	EXPECT_TRUE(gtk_paned_get_wide_handle(paned));
	EXPECT_FALSE(gtk_paned_get_resize_end_child(paned));

	gtk_paned_set_position(paned, gtk_widget_get_width(GTK_WIDGET(paned)) - 340);
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled() && layout->options.sort_manager_width == 340; }));
	const int sidebar_width = gtk_widget_get_width(layout->bar_sort);
	const int window_width = gtk_widget_get_width(layout->window);
	gtk_window_set_default_size(GTK_WINDOW(layout->window), window_width + 200, 800);
	ASSERT_TRUE(wait_layout([&]() { return gtk_widget_get_width(layout->window) > window_width && sort_width_settled(); }))
		<< "Window width: " << gtk_widget_get_width(layout->window) << ", previous: " << window_width
		<< ", sidebar: " << gtk_widget_get_width(layout->bar_sort);
	EXPECT_EQ(gtk_widget_get_width(layout->bar_sort), sidebar_width);
	EXPECT_EQ(layout->options.sort_manager_width, 340);

	layout_bar_sort_toggle(layout);
	while (g_main_context_iteration(nullptr, FALSE)) {}
	EXPECT_EQ(layout->options.sort_manager_width, 340);
	layout_bar_sort_toggle(layout);
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled(); }));
	EXPECT_EQ(gtk_widget_get_width(layout->bar_sort), sidebar_width);
	layout_bar_toggle(layout);
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled() && gtk_widget_get_mapped(layout->bar); }));
	EXPECT_EQ(gtk_widget_get_width(layout->bar_sort), sidebar_width);
}

TEST_F(PartialLayoutConfig, SortManagerWidthIsSavedAndRestoredInWindowConfiguration)
{
	const gchar *names[] = {"sort_manager_width", nullptr};
	const gchar *values[] = {"360", nullptr};
	layout_update_from_config(layout, names, values);
	layout_bar_sort_toggle(layout);
	gtk_window_present(GTK_WINDOW(layout->window));
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled(); }));
	EXPECT_EQ(gtk_widget_get_width(layout->sort_manager_paned) - gtk_paned_get_position(GTK_PANED(layout->sort_manager_paned)), 360);
	const int sidebar_width = gtk_widget_get_width(layout->bar_sort);
	g_autoptr(GString) text = g_string_new(nullptr);
	RcString rc{text, 0};
	layout_write_config(layout, rc);
	EXPECT_NE(strstr(text->str, "sort_manager_width = \"360\""), nullptr);
	layout_free(layout);
	layout = nullptr;
	const gchar *restore_names[] = {"sort_manager_width", "home_path", "startup_path", nullptr};
	const gchar *restore_values[] = {"360", directory, "2", nullptr};
	layout = layout_new_from_config(restore_names, restore_values, FALSE);
	layout_bar_sort_toggle(layout);
	gtk_window_present(GTK_WINDOW(layout->window));
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled(); }));
	EXPECT_EQ(gtk_widget_get_width(layout->bar_sort), sidebar_width);
	EXPECT_EQ(layout->options.sort_manager_width, 360);
}

class SidebarRestart : public PartialLayoutConfig, public testing::WithParamInterface<int>
{
protected:
	void settle()
	{
		ASSERT_TRUE(wait_layout([&]()
			{
			return gtk_widget_get_mapped(layout->window) && gtk_widget_get_width(layout->window) > 0 &&
			       (!(GetParam() & 2) || sort_width_settled());
			}));
		const auto deadline = g_get_monotonic_time() + (300 * G_TIME_SPAN_MILLISECOND);
		while (g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			g_usleep(1000);
			}
	}

	static void collapse(GtkWidget *widget)
	{
		if (GTK_IS_EXPANDER(widget)) gtk_expander_set_expanded(GTK_EXPANDER(widget), FALSE);
		for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) collapse(child);
	}
};

TEST_P(SidebarRestart, RepeatedConfigurationRoundTripsKeepSidebarWidths)
{
	if (GetParam() & 1) layout_bar_toggle(layout);
	if (GetParam() & 2) layout_bar_sort_toggle(layout);
	if ((GetParam() & 4) && layout->bar) collapse(layout->bar);
	gtk_window_set_default_size(GTK_WINDOW(layout->window), 1228, 829);
	gtk_paned_set_position(GTK_PANED(layout->h_pane), 1000);
	gtk_window_present(GTK_WINDOW(layout->window));
	ASSERT_NO_FATAL_FAILURE(settle());
	const int initial_info = layout->bar ? gtk_widget_get_width(layout->bar) : 0;
	const int initial_sort = layout->bar_sort ? gtk_widget_get_width(layout->bar_sort) : 0;
	const int initial_window = gtk_widget_get_width(layout->window);

	for (gint cycle = 0; cycle < 5; ++cycle)
		{
		EXPECT_EQ(gtk_widget_get_width(layout->window), initial_window) << "cycle " << cycle;
		if (layout->bar)
			{
			EXPECT_EQ(gtk_widget_get_width(layout->bar), initial_info) << "cycle " << cycle;
			}
		if (layout->bar_sort)
			{
			EXPECT_EQ(gtk_widget_get_width(layout->bar_sort), initial_sort) << "cycle " << cycle;
			}
		g_autoptr(GString) text = g_string_new("<gq>");
		RcString rc{text, 0};
		layout_write_config(layout, rc);
		g_string_append(text, "</gq>");
		if ((GetParam() & 8) && cycle == 0)
			{
			// Older configurations stored only the inner divider position.
			const char *prefix = "sidebar_width = \"";
			gchar *attribute = strstr(text->str, prefix);
			ASSERT_NE(attribute, nullptr);
			gchar *end = strchr(attribute + strlen(prefix), '"');
			ASSERT_NE(end, nullptr);
			g_string_erase(text, attribute - text->str, (end - attribute) + 1);
			}
		layout_free(layout);
		layout = nullptr;
		ASSERT_TRUE(load_config_from_buf(text->str, text->len, TRUE));
		layout = layout_window_first();
		gtk_window_present(GTK_WINDOW(layout->window));
		ASSERT_TRUE(wait_layout([&]() { return gtk_widget_get_mapped(layout->window); }));
		// Startup installs the Sort Manager after plugin discovery has completed.
		if (layout->bar_sort_enabled && !layout->bar_sort) layout_bar_sort_set(layout, bar_sort_new_default(layout));
		ASSERT_NO_FATAL_FAILURE(settle());
		}
	gtk_window_set_default_size(GTK_WINDOW(layout->window), initial_window + 150, 829);
	ASSERT_TRUE(wait_layout([&]() { return gtk_widget_get_width(layout->window) > initial_window; }));
	ASSERT_NO_FATAL_FAILURE(settle());
	if (layout->bar)
		{
		EXPECT_EQ(gtk_widget_get_width(layout->bar), initial_info);
		}
	if (layout->bar_sort)
		{
		EXPECT_EQ(gtk_widget_get_width(layout->bar_sort), initial_sort);
		}
	if (GetParam() & 1)
		{
		layout_bar_toggle(layout);
		layout_bar_toggle(layout);
		ASSERT_NO_FATAL_FAILURE(settle());
		EXPECT_EQ(gtk_widget_get_width(layout->bar), initial_info);
		}
	if (GetParam() & 2)
		{
		layout_bar_sort_toggle(layout);
		layout_bar_sort_toggle(layout);
		ASSERT_NO_FATAL_FAILURE(settle());
		EXPECT_EQ(gtk_widget_get_width(layout->bar_sort), initial_sort);
		if (layout->bar)
			{
			EXPECT_EQ(gtk_widget_get_width(layout->bar), initial_info);
			}
		}
}

INSTANTIATE_TEST_SUITE_P(Configurations, SidebarRestart, testing::Values(1, 2, 3, 7, 11, 15));

TEST_F(PartialLayoutConfig, SortManagerDividerRespondsToPointerDrag)
{
	g_autofree gchar *xdotool = g_find_program_in_path("xdotool");
	if (!xdotool || !g_getenv("DISPLAY")) GTEST_SKIP() << "Requires xdotool and X11";
	layout_bar_sort_toggle(layout);
	gtk_window_present(GTK_WINDOW(layout->window));
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled(); }));
	gtk_window_set_title(GTK_WINDOW(layout->window), "Geeqie Sort Manager divider test");
	gdk_display_flush(gtk_widget_get_display(layout->window));
	graphene_rect_t start{};
	graphene_rect_t end{};
	ASSERT_TRUE(gtk_widget_compute_bounds(layout->utility_paned, layout->window, &start));
	ASSERT_TRUE(gtk_widget_compute_bounds(layout->bar_sort, layout->window, &end));
	const int x = (start.origin.x + start.size.width + end.origin.x) / 2;
	const int y = start.origin.y + (start.size.height / 2);
	g_autofree gchar *from_x = g_strdup_printf("%d", x);
	g_autofree gchar *to_x = g_strdup_printf("%d", x - 70);
	g_autofree gchar *at_y = g_strdup_printf("%d", y);
	const int width_before = gtk_widget_get_width(layout->bar_sort);
	const gchar *args[] = {xdotool, "search", "--name", "^Geeqie Sort Manager divider test$",
	                       "windowfocus", "--sync", "mousemove", "--window", "%1", from_x, at_y,
	                       "mousedown", "1", "mousemove", "--window", "%1", to_x, at_y, "mouseup", "1", nullptr};
	gint status = 0;
	ASSERT_TRUE(g_spawn_sync(nullptr, const_cast<gchar **>(args), nullptr, G_SPAWN_DEFAULT,
	                         nullptr, nullptr, nullptr, nullptr, &status, nullptr));
	ASSERT_TRUE(g_spawn_check_wait_status(status, nullptr));
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled() && gtk_widget_get_width(layout->bar_sort) >= width_before + 40; }));
	EXPECT_GT(layout->options.sort_manager_width, width_before + 40);
}

TEST_F(PartialLayoutConfig, SortManagerWidthSurvivesLayoutRebuildAndReplacement)
{
	layout_bar_sort_toggle(layout);
	gtk_window_present(GTK_WINDOW(layout->window));
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled(); }));
	gtk_paned_set_position(GTK_PANED(layout->sort_manager_paned), gtk_widget_get_width(layout->sort_manager_paned) - 350);
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled() && layout->options.sort_manager_width == 350; }));
	const int sidebar_width = gtk_widget_get_width(layout->bar_sort);
	layout_style_set(layout, 1, "123");
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled(); }));
	EXPECT_EQ(gtk_widget_get_width(layout->bar_sort), sidebar_width);
	auto *replacement = bar_sort_new_default(layout);
	layout_bar_sort_set(layout, replacement);
	gtk_widget_set_visible(layout->bar_sort, TRUE);
	ASSERT_TRUE(wait_layout([&]() { return sort_width_settled(); }));
	EXPECT_EQ(gtk_widget_get_width(layout->bar_sort), sidebar_width);
	EXPECT_EQ(layout->options.sort_manager_width, 350);
}

TEST_F(PartialLayoutConfig, HamburgerMenuFollowsFloatingToolsAndReturnsWhenHiddenOrDocked)
{
	options->hamburger_menu = TRUE;
	for (bool expand : {false, true})
		{
		options->expand_menu_toolbar = expand;
		layout_free(layout);
		layout = nullptr;
		const gchar *names[] = {"home_path", "startup_path", nullptr};
		const gchar *values[] = {directory, "2", nullptr};
		layout = layout_new_from_config(names, values, FALSE);
		ASSERT_NE(layout, nullptr);
		EXPECT_EQ(gtk_widget_get_root(layout->hamburger_menu_button), GTK_ROOT(layout->window));
		for (int repeat = 0; repeat < 3; ++repeat)
			{
			layout_tools_float_set(layout, TRUE, FALSE);
			ASSERT_NE(layout->tools, nullptr);
			EXPECT_EQ(gtk_widget_get_root(layout->hamburger_menu_button), GTK_ROOT(layout->tools));
			g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(layout->tools);
			guint menu_controllers = 0;
			for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i)
				{
				g_autoptr(GObject) controller = G_OBJECT(g_list_model_get_item(controllers, i));
				if (controller == g_object_get_data(G_OBJECT(layout->tools), "hamburger-menu-key-controller"))
					++menu_controllers;
				}
			EXPECT_EQ(menu_controllers, 1U);
			const auto deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
			while (!gtk_widget_get_mapped(layout->hamburger_menu_button) && g_get_monotonic_time() < deadline)
				{
				while (g_main_context_iteration(nullptr, FALSE)) {}
				g_usleep(1000);
				}
			ASSERT_TRUE(gtk_widget_get_mapped(layout->hamburger_menu_button));
			auto *keys = g_object_get_data(G_OBJECT(layout->tools), "hamburger-menu-key-controller");
			auto *popover = gtk_menu_button_get_popover(GTK_MENU_BUTTON(layout->hamburger_menu_button));
			gboolean handled = FALSE;
			g_signal_emit_by_name(keys, "key-pressed", GDK_KEY_F10, 0, static_cast<GdkModifierType>(0), &handled);
			EXPECT_TRUE(handled);
			EXPECT_TRUE(gtk_widget_get_visible(GTK_WIDGET(popover)));
			g_signal_emit_by_name(keys, "key-pressed", GDK_KEY_F10, 0, static_cast<GdkModifierType>(0), &handled);
			EXPECT_TRUE(handled);
			EXPECT_FALSE(gtk_widget_get_visible(GTK_WIDGET(popover)));
			layout_image_full_screen_start(layout);
			EXPECT_EQ(gtk_widget_get_root(layout->hamburger_menu_button), GTK_ROOT(layout->tools));
			layout_image_full_screen_stop(layout);
			EXPECT_EQ(gtk_widget_get_root(layout->hamburger_menu_button), GTK_ROOT(layout->tools));
			layout_tools_hide_toggle(layout);
			EXPECT_EQ(gtk_widget_get_root(layout->hamburger_menu_button), GTK_ROOT(layout->window));
			layout_tools_hide_toggle(layout);
			EXPECT_EQ(gtk_widget_get_root(layout->hamburger_menu_button), GTK_ROOT(layout->tools));
			layout_tools_float_set(layout, FALSE, FALSE);
			EXPECT_EQ(gtk_widget_get_root(layout->hamburger_menu_button), GTK_ROOT(layout->window));
			EXPECT_EQ(layout->tools, nullptr);
			}
		}
}

GtkWidget *find_scheme_dropdown(GtkWidget *widget)
{
	if (GTK_IS_DROP_DOWN(widget))
		{
		auto *model = gtk_drop_down_get_model(GTK_DROP_DOWN(widget));
		if (GTK_IS_STRING_LIST(model) && g_list_model_get_n_items(model) == 3 &&
		    g_strcmp0(gtk_string_list_get_string(GTK_STRING_LIST(model), 0), _("Follow desktop")) == 0) return widget;
		return nullptr;
		}
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		if (auto *found = find_scheme_dropdown(child)) return found;
	return nullptr;
}

class ColorSchemePreferences : public PartialLayoutConfig
{
protected:
	void SetUp() override
	{
		PartialLayoutConfig::SetUp();
		if (HasFatalFailure() || IsSkipped()) return;
		config_path = g_build_filename(get_rc_dir(), RC_FILE_NAME, nullptr);
		if (g_file_test(config_path, G_FILE_TEST_EXISTS)) GTEST_SKIP() << "Requires an unused configuration file";
		saved_scheme = options->color_scheme;
		saved_custom = options->image.use_custom_border_color;
		saved_border = options->image.border_color;
		changed = true;
		options->color_scheme = COLOR_SCHEME_LIGHT;
		color_scheme_init();
		gtk_window_present(GTK_WINDOW(layout->window));
	}

	void TearDown() override
	{
		if (get_config_window()) gtk_window_close(GTK_WINDOW(get_config_window()));
		if (changed)
			{
			color_scheme_shutdown();
			options->color_scheme = saved_scheme;
			options->image.use_custom_border_color = saved_custom;
			options->image.border_color = saved_border;
			g_unlink(config_path);
			}
		g_free(config_path);
		PartialLayoutConfig::TearDown();
	}

	static void settle()
	{
		const gint64 deadline = g_get_monotonic_time() + (100 * G_TIME_SPAN_MILLISECOND);
		while (g_get_monotonic_time() < deadline)
			{
			while (g_main_context_iteration(nullptr, FALSE)) {}
			g_usleep(1000);
			}
	}

	ColorScheme saved_scheme = COLOR_SCHEME_SYSTEM;
	gboolean saved_custom = FALSE;
	GdkRGBA saved_border{};
	gchar *config_path = nullptr;
	bool changed = false;
};

TEST_F(ColorSchemePreferences, CancelDiscardsThePendingChoice)
{
	show_config_window(layout);
	auto *dialog = get_config_window();
	auto *dropdown = find_scheme_dropdown(dialog);
	ASSERT_NE(dropdown, nullptr);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown)), COLOR_SCHEME_LIGHT);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), COLOR_SCHEME_DARK);
	EXPECT_EQ(options->color_scheme, COLOR_SCHEME_LIGHT);
	auto *cancel = find_widget(dialog, GTK_TYPE_BUTTON, _("Cancel"));
	ASSERT_NE(cancel, nullptr);
	g_signal_emit_by_name(cancel, "clicked");
	EXPECT_EQ(get_config_window(), nullptr);
	EXPECT_EQ(options->color_scheme, COLOR_SCHEME_LIGHT);
	show_config_window(layout);
	dropdown = find_scheme_dropdown(get_config_window());
	ASSERT_NE(dropdown, nullptr);
	EXPECT_EQ(gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown)), COLOR_SCHEME_LIGHT);
}

TEST_F(ColorSchemePreferences, ConfirmSavesThePreferenceWithoutReplacingASessionOverride)
{
	color_scheme_set_session(COLOR_SCHEME_DARK);
	show_config_window(layout);
	auto *dialog = get_config_window();
	auto *dropdown = find_scheme_dropdown(dialog);
	ASSERT_NE(dropdown, nullptr);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), COLOR_SCHEME_SYSTEM);
	auto *ok = find_widget(dialog, GTK_TYPE_BUTTON, "OK");
	ASSERT_NE(ok, nullptr);
	g_signal_emit_by_name(ok, "clicked");
	EXPECT_EQ(get_config_window(), nullptr);
	EXPECT_EQ(options->color_scheme, COLOR_SCHEME_SYSTEM);
	EXPECT_TRUE(g_file_test(config_path, G_FILE_TEST_EXISTS));
#if GTK_CHECK_VERSION(4, 20, 0)
	gint scheme = 0;
	g_object_get(gtk_settings_get_default(), "gtk-interface-color-scheme", &scheme, nullptr);
	EXPECT_EQ(scheme, GTK_INTERFACE_COLOR_SCHEME_DARK);
#else
	gboolean dark = FALSE;
	g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &dark, nullptr);
	EXPECT_TRUE(dark);
#endif
}

class ColorLabelPreferences : public ColorSchemePreferences
{
protected:
	void SetUp() override
	{
		ColorSchemePreferences::SetUp();
		if (HasFatalFailure() || IsSkipped()) return;
		saved_enabled = options->color_labels.enabled;
		for (int i = 0; i < COLOR_LABEL_COUNT; ++i) saved_labels.emplace_back(color_label_name(i + 1));
	}

	void TearDown() override
	{
		if (get_config_window()) gtk_window_close(GTK_WINDOW(get_config_window()));
		if (!saved_labels.empty())
			{
			gchar **names[] = {&options->color_labels.red, &options->color_labels.yellow, &options->color_labels.green,
			                  &options->color_labels.blue, &options->color_labels.purple};
			for (int i = 0; i < COLOR_LABEL_COUNT; ++i)
				{
				g_free(*names[i]);
				*names[i] = g_strdup(saved_labels[i].c_str());
				}
			options->color_labels.enabled = saved_enabled;
			}
		ColorSchemePreferences::TearDown();
	}

	static GtkWidget *label_entry(GtkWidget *widget, const gchar *text)
	{
		if (GTK_IS_ENTRY(widget) && g_strcmp0(gtk_editable_get_text(GTK_EDITABLE(widget)), text) == 0) return widget;
		for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
			{
			if (auto *entry = label_entry(child, text)) return entry;
			}
		return nullptr;
	}

	std::vector<std::string> saved_labels;
	gboolean saved_enabled = FALSE;
};

TEST_F(ColorLabelPreferences, CancelAndConfirmKeepMappingsSeparateAndSaveThem)
{
	show_config_window(layout);
	auto *dialog = get_config_window();
	auto *entry = label_entry(dialog, saved_labels[0].c_str());
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), "Approved");
	auto *cancel = find_widget(dialog, GTK_TYPE_BUTTON, _("Cancel"));
	ASSERT_NE(cancel, nullptr);
	g_signal_emit_by_name(cancel, "clicked");
	EXPECT_STREQ(color_label_name(1), saved_labels[0].c_str());
	show_config_window(layout);
	dialog = get_config_window();
	entry = label_entry(dialog, saved_labels[0].c_str());
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), "Approved");
	auto *ok = find_widget(dialog, GTK_TYPE_BUTTON, "OK");
	ASSERT_NE(ok, nullptr);
	g_signal_emit_by_name(ok, "clicked");
	EXPECT_STREQ(color_label_name(1), "Approved");
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(config_path, &contents, nullptr, nullptr));
	EXPECT_NE(std::string(contents).find("color_labels.red"), std::string::npos);
	EXPECT_NE(std::string(contents).find("Approved"), std::string::npos);
	const gchar fragment[] = "<gq><global color_labels.red='Restored' color_labels.enabled='false'/></gq>";
	ASSERT_TRUE(load_config_from_buf(fragment, sizeof(fragment) - 1, FALSE));
	EXPECT_STREQ(color_label_name(1), "Restored");
	EXPECT_FALSE(options->color_labels.enabled);
}

TEST_F(ColorSchemePreferences, AutomaticImageBackgroundUpdatesAndCustomColorIsPreserved)
{
	options->image.use_custom_border_color = FALSE;
	options->color_scheme = COLOR_SCHEME_DARK;
	color_scheme_apply();
	settle();
	const auto theme = theme_background_color(layout->window);
	EXPECT_TRUE(gdk_rgba_equal(&PIXBUF_RENDERER(layout->image->pr)->color, &theme));
	options->image.use_custom_border_color = TRUE;
	options->image.border_color = GdkRGBA{0.2F, 0.5F, 0.3F, 1.0F};
	image_background_set_color_from_options(layout->image, FALSE);
	options->color_scheme = COLOR_SCHEME_LIGHT;
	color_scheme_apply();
	settle();
	EXPECT_TRUE(gdk_rgba_equal(&PIXBUF_RENDERER(layout->image->pr)->color, &options->image.border_color));
}

TEST_F(PartialLayoutConfig, OpeningDefaultSidebarPreservesWindowOptions)
{
	ASSERT_EQ(layout->bar, nullptr);
	expect_preserved();
	layout_bar_toggle(layout);
	ASSERT_NE(layout->bar, nullptr);
	EXPECT_NE(bar_find_pane_by_id(layout->bar, PANE_EXIF, "exif"), nullptr);
	expect_preserved();
	layout_bar_toggle(layout);
	layout_bar_toggle(layout);
	expect_preserved();
}

TEST_F(PartialLayoutConfig, AddingSidebarPanePreservesWindowOptions)
{
	layout_bar_toggle(layout);
	const gchar fragment[] = "<gq><layout id='_current_'><bar>"
	                         "<pane_exif id='extra' expanded='true'>"
	                         "<entry key='Exif.Image.Artist' editable='false'/>"
	                         "</pane_exif></bar></layout></gq>";
	ASSERT_TRUE(load_config_from_buf(fragment, sizeof(fragment) - 1, FALSE));
	EXPECT_NE(bar_find_pane_by_id(layout->bar, PANE_EXIF, "extra"), nullptr);
	expect_preserved();
}

TEST_F(PartialLayoutConfig, ExplicitAttributesOverrideOnlyTheirOwnOptions)
{
	const gchar *names[] = {"id", "show_info_pixel", nullptr};
	const gchar *values[] = {"_current_", "false", nullptr};
	layout_update_from_config(layout, names, values);
	expect_preserved(false);
	layout_update_from_config(layout, nullptr, nullptr);
	expect_preserved(false);
}

TEST_F(PartialLayoutConfig, NewWindowStillUsesDefaultLayoutOptions)
{
	const gchar *names[] = {"home_path", "startup_path", nullptr};
	const gchar *values[] = {directory, "2", nullptr};
	auto *other = layout_new_from_config(names, values, FALSE);
	ASSERT_NE(other, nullptr);
	EXPECT_EQ(other->options.style, 0);
	EXPECT_STREQ(other->options.order, "123");
	EXPECT_FALSE(other->options.show_info_pixel);
	EXPECT_TRUE(other->options.animate);
	layout_free(other);
	expect_preserved();
}

} // namespace
