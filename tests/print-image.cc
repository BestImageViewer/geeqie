/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <set>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "filedata.h"
#include "img-view.h"
#include "layout.h"
#include "main-defines.h"
#include "options.h"

namespace
{

bool contains_action(GMenuModel *menu, const gchar *name)
{
	for (gint i = 0; i < g_menu_model_get_n_items(menu); ++i)
		{
		g_autofree gchar *action = nullptr;
		if (g_menu_model_get_item_attribute(menu, i, G_MENU_ATTRIBUTE_ACTION, "s", &action) && g_strcmp0(action, name) == 0) return true;
		for (const gchar *link : {G_MENU_LINK_SECTION, G_MENU_LINK_SUBMENU})
			{
			g_autoptr(GMenuModel) child = g_menu_model_get_item_link(menu, i, link);
			if (child && contains_action(child, name)) return true;
			}
		}
	return false;
}

TEST(ImagePrint, BothContextMenusExposePrint)
{
	g_autoptr(GtkBuilder) main_builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-layout-image.ui");
	g_autoptr(GtkBuilder) standalone_builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/menu-img-view.ui");
	EXPECT_TRUE(contains_action(G_MENU_MODEL(gtk_builder_get_object(main_builder, "menu-layout-image")), "win.layout-image-print"));
	EXPECT_TRUE(contains_action(G_MENU_MODEL(gtk_builder_get_object(standalone_builder, "menu-image")), "win.image-win-print"));
}

std::set<GtkWidget *> toplevels()
{
	std::set<GtkWidget *> result;
	g_autoptr(GList) windows = gtk_window_list_toplevels();
	for (GList *work = windows; work; work = work->next) result.insert(GTK_WIDGET(work->data));
	return result;
}

GtkWidget *new_window(const std::set<GtkWidget *> &before)
{
	for (auto *window : toplevels()) if (!before.count(window)) return window;
	return nullptr;
}

class StandalonePrinting : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		previous_app = g_application_get_default();
		static GtkApplication *app = nullptr;
		if (!app)
			{
			app = gtk_application_new("org.geeqie.ImagePrintTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
			}
		g_application_set_default(G_APPLICATION(app));
		app_changed = true;
		accel_map_load_merged();
		directory = g_dir_make_tmp("geeqie-image-print-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "image.png", nullptr);
		g_autoptr(GdkPixbuf) image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 32, 32);
		gdk_pixbuf_fill(image, 0x4080c0ff);
		ASSERT_TRUE(gdk_pixbuf_save(image, path, "png", nullptr, nullptr));
		g_autofree gchar *startup = g_strdup_printf("%d", STARTUP_PATH_HOME);
		const gchar *names[] = {"home_path", "startup_path", nullptr};
		const gchar *values[] = {directory, startup, nullptr};
		layout = layout_new_from_config(names, values, FALSE);
		ASSERT_NE(layout, nullptr);
		auto fd = FileData::new_simple(path);
		const auto before = toplevels();
		view_window_new(fd);
		normal = new_window(before);
		ASSERT_NE(normal, nullptr);
	}

	void TearDown() override
	{
		if (print_options) gtk_window_close(GTK_WINDOW(print_options));
		if (normal) gtk_window_close(GTK_WINDOW(normal));
		if (layout) layout_free(layout);
		if (app_changed) g_application_set_default(previous_app);
		if (path) g_remove(path);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(directory);
	}

	GtkWidget *normal = nullptr;
	LayoutWindow *layout = nullptr;
	GtkWidget *print_options = nullptr;
	GApplication *previous_app = nullptr;
	gchar *directory = nullptr;
	gchar *path = nullptr;
	bool app_changed = false;
};

TEST_F(StandalonePrinting, NormalAndFullscreenActionsOpenPrintOptions)
{
	GtkWidget *parent = normal;
	for (bool fullscreen : {false, true})
		{
		if (fullscreen)
			{
			const auto before = toplevels();
			ASSERT_TRUE(gtk_widget_activate_action(normal, "win.image-win-fullscreen", nullptr));
			parent = new_window(before);
			ASSERT_NE(parent, nullptr);
			}
		const auto before = toplevels();
		ASSERT_TRUE(gtk_widget_activate_action(parent, "win.image-win-print", nullptr));
		print_options = new_window(before);
		ASSERT_NE(print_options, nullptr);
		EXPECT_EQ(GTK_WIDGET(gtk_window_get_transient_for(GTK_WINDOW(print_options))), parent);
		EXPECT_TRUE(toplevels().count(parent));
		gtk_window_close(GTK_WINDOW(print_options));
		print_options = nullptr;
		}
}

} // namespace
