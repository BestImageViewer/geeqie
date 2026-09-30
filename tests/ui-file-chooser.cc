/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <functional>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "gtest/gtest.h"

#include "options.h"
#include "ui-file-chooser.h"

namespace
{

GtkWidget *find_widget(GtkWidget *widget, const std::function<bool(GtkWidget *)> &match)
{
	if (match(widget)) return widget;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *found = find_widget(child, match)) return found;
		}
	return nullptr;
}

bool wait_for(const std::function<bool()> &ready)
{
	for (int i = 0; i < 2000; ++i)
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		if (ready()) return true;
		g_usleep(1000);
		}
	return false;
}

TEST(FileChooserDestination, BreadcrumbClearsChildButExplicitSelectionRemains)
{
	if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
	if (!options) options = conf_options_new();
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-destination-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *child_path = g_build_filename(directory, "a", nullptr);
	ASSERT_EQ(g_mkdir(child_path, 0700), 0);
	g_autoptr(GFile) folder = g_file_new_for_path(directory);
	g_autoptr(GFile) child = g_file_new_for_path(child_path);
	auto *parent = GTK_WINDOW(gtk_window_new());
	gtk_window_present(parent);
	for (const int selection_mode : {0, 1, 2})
		{
		GFile *result = nullptr;
		FileDialogData data{};
		data.action = FileDialogAction::SELECT_FOLDER;
		data.parent = parent;
		data.filename = child_path;
		data.data = &result;
		data.callback = [](GFile *file, gpointer output)
			{
			*static_cast<GFile **>(output) = file ? G_FILE(g_object_ref(file)) : nullptr;
			};
		file_dialog_show(data);
		auto *windows = gtk_window_get_toplevels();
		GtkWidget *dialog = nullptr;
		for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
			{
			g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
			if (GTK_IS_DIALOG(window)) dialog = GTK_WIDGET(window);
			}
		ASSERT_NE(dialog, nullptr);
		auto *chooser_widget = find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_FILE_CHOOSER(widget); });
		ASSERT_NE(chooser_widget, nullptr);
		auto *chooser = GTK_FILE_CHOOSER(chooser_widget);
		G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		ASSERT_TRUE(wait_for([&]()
			{
			g_autoptr(GFile) current = gtk_file_chooser_get_current_folder(chooser);
			return current && g_file_equal(current, child);
			}));
		auto *path_bar = find_widget(chooser_widget, [](GtkWidget *widget)
			{ return g_signal_lookup("path-clicked", G_OBJECT_TYPE(widget)) != 0; });
		ASSERT_NE(path_bar, nullptr);
		g_signal_emit_by_name(path_bar, "path-clicked", folder, child, FALSE);
		ASSERT_TRUE(wait_for([&]()
			{
			g_autoptr(GFile) current = gtk_file_chooser_get_current_folder(chooser);
			g_autoptr(GFile) selected = gtk_file_chooser_get_file(chooser);
			return current && selected && g_file_equal(current, folder) && g_file_equal(selected, folder);
			}));
		if (selection_mode != 0)
			{
			if (selection_mode == 1)
				{
				auto *view = find_widget(chooser_widget, [](GtkWidget *widget) { return GTK_IS_COLUMN_VIEW(widget); });
				ASSERT_NE(view, nullptr);
				auto *model = gtk_column_view_get_model(GTK_COLUMN_VIEW(view));
				guint child_index = 0;
				ASSERT_TRUE(wait_for([&]()
					{
					for (guint i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(model)); ++i)
						{
						g_autoptr(GObject) item = G_OBJECT(g_list_model_get_item(G_LIST_MODEL(model), i));
						if (G_IS_FILE_INFO(item) && g_strcmp0(g_file_info_get_name(G_FILE_INFO(item)), "a") == 0)
							{
							child_index = i;
							return true;
							}
						}
					return false;
					}));
				g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(view);
				for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i)
					{
					g_autoptr(GtkEventController) controller = GTK_EVENT_CONTROLLER(g_list_model_get_item(controllers, i));
					if (GTK_IS_GESTURE_CLICK(controller) && gtk_event_controller_get_propagation_phase(controller) == GTK_PHASE_CAPTURE)
						g_signal_emit_by_name(controller, "pressed", 1, 10.0, 10.0);
					}
				gtk_widget_grab_focus(view);
				ASSERT_TRUE(gtk_selection_model_select_item(model, child_index, TRUE));
				}
			else
				g_signal_emit_by_name(chooser_widget, "location-popup", child_path);
			ASSERT_TRUE(wait_for([&]()
				{
				g_autoptr(GFile) selected = gtk_file_chooser_get_file(chooser);
				return selected && g_file_equal(selected, child);
				}));
			}
		g_autofree gchar *destination = g_strdup_printf("Destination: %s", selection_mode ? child_path : directory);
		ASSERT_TRUE(wait_for([&]()
			{
			return find_widget(dialog, [&](GtkWidget *widget)
				{
				return GTK_IS_LABEL(widget) && g_str_equal(gtk_label_get_text(GTK_LABEL(widget)), destination);
				}) != nullptr;
			}));
		gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
		G_GNUC_END_IGNORE_DEPRECATIONS
		ASSERT_NE(result, nullptr);
		EXPECT_TRUE(g_file_equal(result, selection_mode ? child : folder));
		g_object_unref(result);
		}
	gtk_window_destroy(parent);
	g_rmdir(child_path);
	g_rmdir(directory);
}

} // namespace
