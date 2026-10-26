/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <initializer_list>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "image.h"
#include "main.h"
#include "options.h"
#include "rcfile.h"

namespace
{

TEST(ColorScheme, ParsesOnlySupportedCommandLineValues)
{
	ColorScheme scheme = COLOR_SCHEME_SYSTEM;
	EXPECT_TRUE(color_scheme_parse("system", scheme));
	EXPECT_EQ(scheme, COLOR_SCHEME_SYSTEM);
	EXPECT_TRUE(color_scheme_parse("light", scheme));
	EXPECT_EQ(scheme, COLOR_SCHEME_LIGHT);
	EXPECT_TRUE(color_scheme_parse("dark", scheme));
	EXPECT_EQ(scheme, COLOR_SCHEME_DARK);
	for (const gchar *invalid : std::initializer_list<const gchar *>{nullptr, "", "auto", "Dark", "blue"})
		{
		EXPECT_FALSE(color_scheme_parse(invalid, scheme));
		EXPECT_EQ(scheme, COLOR_SCHEME_DARK);
		}
}

class ColorSchemeGtk : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		saved = options->color_scheme;
		initialized = true;
		color_scheme_shutdown();
		color_scheme_init();
	}

	void TearDown() override
	{
		if (!initialized) return;
		color_scheme_shutdown();
		options->color_scheme = saved;
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

	static gint current_scheme()
	{
#if GTK_CHECK_VERSION(4, 20, 0)
		gint scheme = 0;
		g_object_get(gtk_settings_get_default(), "gtk-interface-color-scheme", &scheme, nullptr);
		return scheme == GTK_INTERFACE_COLOR_SCHEME_DARK ? COLOR_SCHEME_DARK :
		       scheme == GTK_INTERFACE_COLOR_SCHEME_LIGHT ? COLOR_SCHEME_LIGHT : COLOR_SCHEME_SYSTEM;
#else
		gboolean dark = FALSE;
		g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &dark, nullptr);
		return dark ? COLOR_SCHEME_DARK : COLOR_SCHEME_LIGHT;
#endif
	}

	ColorScheme saved = COLOR_SCHEME_SYSTEM;
	bool initialized = false;
};

TEST_F(ColorSchemeGtk, SessionOverrideDoesNotChangeTheSavedPreference)
{
	options->color_scheme = COLOR_SCHEME_SYSTEM;
	color_scheme_apply();
	const gint desktop_scheme = current_scheme();
	options->color_scheme = COLOR_SCHEME_LIGHT;
	color_scheme_apply();
	EXPECT_EQ(current_scheme(), COLOR_SCHEME_LIGHT);
	color_scheme_set_session(COLOR_SCHEME_DARK);
	EXPECT_EQ(current_scheme(), COLOR_SCHEME_DARK);
	EXPECT_EQ(options->color_scheme, COLOR_SCHEME_LIGHT);
	color_scheme_apply();
	EXPECT_EQ(current_scheme(), COLOR_SCHEME_DARK);
	options->color_scheme = COLOR_SCHEME_DARK;
	color_scheme_set_session(COLOR_SCHEME_LIGHT);
	EXPECT_EQ(current_scheme(), COLOR_SCHEME_LIGHT);
	EXPECT_EQ(options->color_scheme, COLOR_SCHEME_DARK);
	color_scheme_set_session(COLOR_SCHEME_SYSTEM);
	EXPECT_EQ(current_scheme(), desktop_scheme);
}

TEST_F(ColorSchemeGtk, ExplicitChoiceSurvivesDesktopNotifications)
{
	options->color_scheme = COLOR_SCHEME_LIGHT;
	color_scheme_apply();
#if GTK_CHECK_VERSION(4, 20, 0)
	g_object_set(gtk_settings_get_default(), "gtk-interface-color-scheme", GTK_INTERFACE_COLOR_SCHEME_DARK, nullptr);
#else
	g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, nullptr);
#endif
	EXPECT_EQ(current_scheme(), COLOR_SCHEME_LIGHT);
}

TEST_F(ColorSchemeGtk, LightAndDarkChangeTheRenderedTheme)
{
	GtkWidget *window = gtk_window_new();
	gtk_window_present(GTK_WINDOW(window));
	options->color_scheme = COLOR_SCHEME_LIGHT;
	color_scheme_apply();
	settle();
	const auto light = theme_background_color(window);
	options->color_scheme = COLOR_SCHEME_DARK;
	color_scheme_apply();
	settle();
	const auto dark = theme_background_color(window);
	EXPECT_GT(light.red + light.green + light.blue, dark.red + dark.green + dark.blue + 0.5);
	gtk_window_destroy(GTK_WINDOW(window));
}

TEST_F(ColorSchemeGtk, ApplyingPreferencesAfterAnImageWindowClosesIsSafe)
{
	g_autoptr(GtkWidget) window = gtk_window_new();
	g_object_ref_sink(window);
	auto *image = image_new(FALSE);
	g_autoptr(GtkWidget) retained = GTK_WIDGET(g_object_ref_sink(image->widget));
	gtk_window_set_child(GTK_WINDOW(window), retained);
	image_attach_window(image, window, nullptr, nullptr, FALSE);
	gtk_window_destroy(GTK_WINDOW(window));
	g_clear_object(&window);
	EXPECT_EQ(image->top_window, nullptr);
	image_options_sync();
}

TEST_F(ColorSchemeGtk, SavedSchemeRoundTripsAndInvalidValuesAreClamped)
{
	g_autofree gchar *directory = g_dir_make_tmp("geeqie-color-scheme-XXXXXX", nullptr);
	ASSERT_NE(directory, nullptr);
	g_autofree gchar *path = g_build_filename(directory, "geeqierc.xml", nullptr);
	options->color_scheme = COLOR_SCHEME_DARK;
	EXPECT_TRUE(save_config_to_file(path, options, nullptr));
	options->color_scheme = COLOR_SCHEME_LIGHT;
	EXPECT_TRUE(load_config_from_file(path, FALSE));
	EXPECT_EQ(options->color_scheme, COLOR_SCHEME_DARK);
	const gchar invalid[] = "<gq><global color_scheme='99'/></gq>";
	EXPECT_TRUE(load_config_from_buf(invalid, sizeof(invalid) - 1, FALSE));
	EXPECT_EQ(options->color_scheme, COLOR_SCHEME_DARK);
	g_unlink(path);
	g_rmdir(directory);
}

} // namespace
