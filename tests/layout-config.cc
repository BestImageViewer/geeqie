/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <string>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "bar.h"
#include "filefilter.h"
#include "layout.h"
#include "layout-config.h"
#include "layout-image.h"
#include "layout-util.h"
#include "options.h"
#include "rcfile.h"

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

	LayoutWindow *layout = nullptr;
	GtkApplication *app = nullptr;
	GApplication *previous_app = nullptr;
	gchar *directory = nullptr;
	std::string id;
	gboolean saved_hamburger = FALSE;
	gboolean saved_expand = FALSE;
	bool saved_menu_settings = false;
};

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
