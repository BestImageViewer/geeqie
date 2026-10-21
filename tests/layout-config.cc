/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <gtk/gtk.h>

#include "layout-config.h"

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

} // namespace
