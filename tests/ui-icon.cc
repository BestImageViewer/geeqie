/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <cstring>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "gtest/gtest.h"

#include "main-defines.h"
#include "pixbuf-util.h"
#include "ui-misc.h"

namespace
{

class IconFallback : public ::testing::Test
{
protected:
	void SetUp() override
	{
		static const gboolean display_available = gtk_init_check() && gdk_display_get_default();
		if (!display_available)
			{
			GTEST_SKIP() << "Requires a working GTK display";
			}
		directory = g_dir_make_tmp("geeqie-icons-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		for (const gchar *name : {"hicolor", "Adwaita", "Selected"})
			{
			g_autofree gchar *actions = g_build_filename(directory, name, "scalable", "actions", nullptr);
			ASSERT_EQ(g_mkdir_with_parents(actions, 0700), 0);
			g_autofree gchar *index = g_build_filename(directory, name, "index.theme", nullptr);
			ASSERT_TRUE(g_file_set_contents(index,
				"[Icon Theme]\nName=Test\nDirectories=scalable/actions\n"
				"[scalable/actions]\nSize=16\nType=Scalable\nMinSize=1\nMaxSize=256\nContext=Actions\n",
				-1, nullptr));
			}
		theme = gtk_icon_theme_new();
		const gchar *paths[] = {directory, nullptr};
		gtk_icon_theme_set_search_path(theme, paths);
		const gchar *resources[] = {GQ_RESOURCE_PATH_ICONS, nullptr};
		gtk_icon_theme_set_resource_path(theme, resources);
		gtk_icon_theme_set_theme_name(theme, "hicolor");
	}

	void TearDown() override
	{
		g_clear_object(&theme);
		if (!directory) return;
		for (const gchar *name : {"hicolor", "Adwaita", "Selected"})
			{
			g_autofree gchar *icon = g_build_filename(directory, name, "scalable", "actions", "object-rotate-left.svg", nullptr);
			g_unlink(icon);
			g_autofree gchar *actions = g_build_filename(directory, name, "scalable", "actions", nullptr);
			g_rmdir(actions);
			g_autofree gchar *scalable = g_build_filename(directory, name, "scalable", nullptr);
			g_rmdir(scalable);
			g_autofree gchar *index = g_build_filename(directory, name, "index.theme", nullptr);
			g_unlink(index);
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			g_rmdir(path);
			}
		g_rmdir(directory);
		g_clear_pointer(&directory, g_free);
	}

	GtkIconTheme *theme = nullptr;
	gchar *directory = nullptr;
};

TEST_F(IconFallback, BundledIconsResolveAndDecodeWithoutSystemThemes)
{
	g_auto(GStrv) files = g_resources_enumerate_children(GQ_RESOURCE_PATH_ICONS "/scalable/actions",
	                                                  G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);
	ASSERT_NE(files, nullptr);
	for (guint i = 0; files[i]; i++)
		{
		SCOPED_TRACE(files[i]);
		ASSERT_TRUE(g_str_has_prefix(files[i], "gq-fallback-"));
		g_autofree gchar *name = g_strndup(files[i] + strlen("gq-fallback-"),
		                                 strlen(files[i]) - strlen("gq-fallback-") - strlen(".svg"));
		const gboolean symbolic = g_str_has_suffix(name, "-symbolic");
		if (symbolic) name[strlen(name) - strlen("-symbolic")] = '\0';
		g_autoptr(GIcon) icon = ui_icon_new(name);
		g_autoptr(GtkIconPaintable) paintable = gtk_icon_theme_lookup_by_gicon(theme, icon, 16, 1, GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_NONE);
		ASSERT_NE(paintable, nullptr);
		g_autoptr(GFile) file = gtk_icon_paintable_get_file(paintable);
		ASSERT_NE(file, nullptr);
		g_autofree gchar *uri = g_file_get_uri(file);
		g_autofree gchar *expected = g_strconcat("resource://" GQ_RESOURCE_PATH_ICONS "/scalable/actions/", files[i], nullptr);
		EXPECT_STREQ(uri, expected);
		EXPECT_EQ(gtk_icon_paintable_is_symbolic(paintable), symbolic);
		g_autoptr(GdkPixbuf) pixbuf = icon_theme_load_pixbuf_copy(theme, name, 32, GTK_ICON_LOOKUP_NONE);
		ASSERT_NE(pixbuf, nullptr);
		EXPECT_GT(gdk_pixbuf_get_width(pixbuf), 0);
		}
}

TEST_F(IconFallback, SelectedThemeWinsAndThemeChangesKeepFallback)
{
	g_autofree gchar *path = g_build_filename(directory, "Selected", "scalable", "actions", "object-rotate-left.svg", nullptr);
	ASSERT_TRUE(g_file_set_contents(path,
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"><rect width=\"16\" height=\"16\" fill=\"red\"/></svg>",
		-1, nullptr));
	g_autoptr(GIcon) icon = ui_icon_new(GQ_ICON_ROTATE_LEFT);
	gtk_icon_theme_set_theme_name(theme, "Selected");
	g_autoptr(GtkIconPaintable) selected = gtk_icon_theme_lookup_by_gicon(theme, icon, 16, 1, GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_NONE);
	g_autoptr(GFile) file = gtk_icon_paintable_get_file(selected);
	ASSERT_NE(file, nullptr);
	g_autofree gchar *selected_path = g_file_get_path(file);
	EXPECT_STREQ(selected_path, path);

	gtk_icon_theme_set_theme_name(theme, "hicolor");
	g_autoptr(GtkIconPaintable) fallback = gtk_icon_theme_lookup_by_gicon(theme, icon, 16, 2, GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_NONE);
	g_autoptr(GFile) fallback_file = gtk_icon_paintable_get_file(fallback);
	ASSERT_NE(fallback_file, nullptr);
	EXPECT_TRUE(g_file_has_uri_scheme(fallback_file, "resource"));
	EXPECT_TRUE(gtk_icon_paintable_is_symbolic(fallback));
}

TEST_F(IconFallback, UnknownPluginUsesRunIcon)
{
	g_autoptr(GIcon) icon = ui_icon_new("geeqie-test-nonexistent-plugin");
	g_autoptr(GtkIconPaintable) paintable = gtk_icon_theme_lookup_by_gicon(theme, icon, 16, 1, GTK_TEXT_DIR_NONE, GTK_ICON_LOOKUP_NONE);
	g_autoptr(GFile) file = gtk_icon_paintable_get_file(paintable);
	ASSERT_NE(file, nullptr);
	g_autofree gchar *uri = g_file_get_uri(file);
	EXPECT_STREQ(uri, "resource://" GQ_RESOURCE_PATH_ICONS "/scalable/actions/gq-fallback-system-run-symbolic.svg");
}

TEST_F(IconFallback, FloatAndThumbnailsUseDistinctBundledArtwork)
{
	for (const gchar *name : {PIXBUF_INLINE_ICON_FLOAT, PIXBUF_INLINE_ICON_THUMB})
		{
		g_autoptr(GIcon) icon = ui_icon_new(name);
		ASSERT_TRUE(G_IS_FILE_ICON(icon));
		auto *file = g_file_icon_get_file(G_FILE_ICON(icon));
		g_autofree gchar *basename = g_file_get_basename(file);
		g_autofree gchar *light = g_strconcat(name, ".svg", nullptr);
		g_autofree gchar *dark = g_strconcat(name, "-dark.svg", nullptr);
		EXPECT_TRUE(g_str_equal(basename, light) || g_str_equal(basename, dark));
		g_autoptr(GdkPixbuf) pixbuf = icon_theme_load_pixbuf_copy(theme, name, 32, GTK_ICON_LOOKUP_NONE);
		ASSERT_NE(pixbuf, nullptr);
		}
}

TEST(IconNames, EmptyNameClearsIcon)
{
	EXPECT_EQ(ui_icon_new(nullptr), nullptr);
	EXPECT_EQ(ui_icon_new(""), nullptr);
}

} // namespace
