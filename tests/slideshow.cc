/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "slideshow.h"

#include <functional>
#include <string>

#include <cairo.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "gtest/gtest.h"

#include "accelerators.h"
#include "filedata.h"
#include "filefilter.h"
#include "fullscreen.h"
#include "histogram.h"
#include "image.h"
#include "layout-image.h"
#include "layout.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "renderer-tiles.h"

namespace
{

bool wait_until(const std::function<bool()> &predicate, gint64 timeout = 3000000)
{
	const gint64 deadline = g_get_monotonic_time() + timeout;
	do
		{
		while (g_main_context_iteration(nullptr, FALSE)) {}
		if (predicate()) return true;
		g_usleep(1000);
		}
	while (g_get_monotonic_time() < deadline);
	return false;
}

class SlideshowTransitions : public ::testing::TestWithParam<int>
{
protected:
	void SetUp() override
	{
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		saved_slideshow = options->slideshow;
		saved_read_ahead = options->image.enable_read_ahead;
		saved_image_cache_max = options->image.image_cache_max;
		options->image.enable_read_ahead = FALSE;
		options->slideshow.delay = 1;
		options->slideshow.repeat = TRUE;
		options->slideshow.random = FALSE;
		options->slideshow.transition = GTK_STACK_TRANSITION_TYPE_CROSSFADE;
		options->slideshow.transition_duration = 300;
		g_object_get(gtk_settings_get_default(), "gtk-enable-animations", &saved_animations, nullptr);
		g_object_set(gtk_settings_get_default(), "gtk-enable-animations", TRUE, nullptr);
		window = GTK_WINDOW(gtk_window_new());
		imd = image_new(FALSE);
		pr = PIXBUF_RENDERER(imd->pr);
		gtk_window_set_default_size(window, 120, 100);
		gtk_window_set_child(window, imd->widget);
		gtk_window_present(window);
		ASSERT_TRUE(wait_until([this]() { return gtk_widget_get_mapped(GTK_WIDGET(pr)); }));
	}

	void TearDown() override
	{
		if (!window) return;
		delete ss;
		gtk_window_destroy(window);
		if (layout) layout_free(layout);
		if (app)
			{
			g_application_set_default(previous_app);
			g_object_unref(app);
			}
		while (g_main_context_iteration(nullptr, FALSE)) {}
		g_object_set(gtk_settings_get_default(), "gtk-enable-animations", saved_animations, nullptr);
		options->slideshow = saved_slideshow;
		options->image.enable_read_ahead = saved_read_ahead;
		options->image.image_cache_max = saved_image_cache_max;
		if (directory)
			{
			g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
			while (const gchar *name = g_dir_read_name(dir))
				{
				g_autofree gchar *path = g_build_filename(directory, name, nullptr);
				g_unlink(path);
				}
			g_rmdir(directory);
			g_free(directory);
			}
	}

	[[nodiscard]] bool ready() const
	{
		return pr->pixbuf && !pr->loading && pr->complete && renderer_tiles_is_ready(pr->renderer);
	}

	void set_color(guint32 color)
	{
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1200, 800);
		gdk_pixbuf_fill(pixels, color);
		pixbuf_renderer_set_pixbuf(pr, pixels, 0.0);
		ASSERT_TRUE(wait_until([this]() { return ready(); }));
	}

	guint32 view_pixel(gint x = 60, gint y = 50)
	{
		auto *snapshot = gtk_snapshot_new();
		GTK_WIDGET_GET_CLASS(imd->view)->snapshot(imd->view, snapshot);
		g_autoptr(GskRenderNode) node = gtk_snapshot_free_to_node(snapshot);
		auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 120, 100);
		auto *cr = cairo_create(surface);
		gsk_render_node_draw(node, cr);
		cairo_destroy(cr);
		cairo_surface_flush(surface);
		const guint32 pixel = reinterpret_cast<guint32 *>(cairo_image_surface_get_data(surface))[(y * 120) + x];
		cairo_surface_destroy(surface);
		return pixel;
	}

	void start_show()
	{
		directory = g_dir_make_tmp("geeqie-slideshow-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		GList *files = nullptr;
		for (gint i = 0; i < 3; ++i)
			{
			g_autofree gchar *name = g_strdup_printf("%d.png", i);
			g_autofree gchar *path = g_build_filename(directory, name, nullptr);
			g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, i % 2 ? 800 : 1200, i % 2 ? 1200 : 800);
			gdk_pixbuf_fill(pixels, i % 2 ? 0x0000ffff : 0xff0000ff);
			ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "png", nullptr, nullptr));
			files = g_list_append(files, file_data_new_group(path));
			}
		// Image loading uses the current layout's animation options.
		filter_add_defaults();
		filter_rebuild();
		accel_map_load_merged();
		previous_app = g_application_get_default();
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.CrossfadeTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		app = GTK_APPLICATION(g_object_ref(test_app));
		g_application_set_default(G_APPLICATION(app));
		const gchar *names[] = {"home_path", "startup_path", nullptr};
		g_autofree gchar *startup = g_strdup_printf("%d", STARTUP_PATH_HOME);
		const gchar *values[] = {directory, startup, nullptr};
		layout = layout_new_from_config(names, values, FALSE);
		ss = SlideShow::start_from_filelist(nullptr, imd, files, [this](SlideShow *) { ss = nullptr; });
		ASSERT_NE(ss, nullptr);
		ss->pause_toggle();
		ASSERT_TRUE(wait_until([this]() { return ready(); }));
		EXPECT_FALSE(image_transition_active(imd));
		ss->pause_toggle();
	}

	GtkWindow *window = nullptr;
	LayoutWindow *layout = nullptr;
	GtkApplication *app = nullptr;
	GApplication *previous_app = nullptr;
	ImageWindow *imd = nullptr;
	PixbufRenderer *pr = nullptr;
	SlideShow *ss = nullptr;
	gchar *directory = nullptr;
	decltype(options->slideshow) saved_slideshow{};
	decltype(options->image.image_cache_max) saved_image_cache_max{};
	gboolean saved_read_ahead = FALSE;
	gboolean saved_animations = TRUE;
};

TEST_P(SlideshowTransitions, NativeTransitionLoadsAndShowsTheWholeIncomingImage)
{
	const auto type = static_cast<GtkStackTransitionType>(GetParam());
	ASSERT_NO_FATAL_FAILURE(set_color(0xff0000ff));
	g_autoptr(GdkPaintable) outgoing = image_transition_capture(imd);
	ASSERT_NE(outgoing, nullptr);
	g_autoptr(GdkPixbuf) incoming = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, GetParam() % 2 ? 1600 : 2400, GetParam() % 2 ? 2400 : 1600);
	gdk_pixbuf_fill(incoming, 0x0000ffff);
	pr->loading = TRUE;
	pixbuf_renderer_set_pixbuf(pr, incoming, 0.0);
	image_transition_from(imd, outgoing, type, 60);
	EXPECT_TRUE(GTK_IS_STACK(imd->view));
	if (type != GTK_STACK_TRANSITION_TYPE_NONE)
		{
		EXPECT_TRUE(imd->transition_pending);
		ASSERT_TRUE(wait_until([this]()
			{
			return !gtk_widget_get_mapped(imd->pr) &&
			       gtk_widget_get_width(imd->outgoing[imd->outgoing_index]) > 0;
			}));
		EXPECT_EQ(view_pixel(), 0xffff0000U);
		EXPECT_NE(gtk_stack_get_visible_child(GTK_STACK(imd->view)), imd->pr);
		}
	pr->loading = FALSE;
	if (type != GTK_STACK_TRANSITION_TYPE_NONE)
		{
		ASSERT_TRUE(wait_until([this]() { return gtk_stack_get_transition_running(GTK_STACK(imd->view)); }));
		}
	ASSERT_TRUE(wait_until([this]() { return !image_transition_active(imd) && ready(); }));
	EXPECT_EQ(gtk_stack_get_visible_child(GTK_STACK(imd->view)), imd->pr);
	EXPECT_EQ(view_pixel(), 0xff0000ffU);
	for (gint y : {pr->y_offset + 2, pr->y_offset + pr->vis_height - 3})
		for (gint x : {pr->x_offset + 2, pr->x_offset + pr->vis_width - 3}) EXPECT_EQ(view_pixel(x, y), 0xff0000ffU);
	EXPECT_EQ(imd->transition_source, 0U);
	EXPECT_EQ(gtk_picture_get_paintable(GTK_PICTURE(imd->outgoing[imd->outgoing_index])), nullptr);
}

INSTANTIATE_TEST_SUITE_P(AllGtkStackTransitions, SlideshowTransitions,
                        ::testing::Range(int(GTK_STACK_TRANSITION_TYPE_NONE),
                                         int(GTK_STACK_TRANSITION_TYPE_ROTATE_LEFT_RIGHT) + 1));

TEST_F(SlideshowTransitions, HistogramWorksWithoutPreloadingOrImageCache)
{
	options->image.image_cache_max = 0;
	ASSERT_NO_FATAL_FAILURE(start_show());
	ss->pause_toggle();
	EXPECT_FALSE(options->image.enable_read_ahead);
	for (int i = 0; i < 2; ++i)
		{
		auto *fd = image_get_fd(imd);
		ASSERT_NE(fd, nullptr);
		EXPECT_EQ(fd->pixbuf, nullptr);
		auto *source = image_get_complete_pixbuf(fd);
		ASSERT_NE(source, nullptr);
		EXPECT_EQ(gdk_pixbuf_get_width(source), gdk_pixbuf_get_width(image_get_pixbuf(imd)));
		EXPECT_EQ(gdk_pixbuf_get_height(source), gdk_pixbuf_get_height(image_get_pixbuf(imd)));
		if (!histmap_get(fd)) EXPECT_TRUE(histmap_start_idle(fd));
		ASSERT_TRUE(wait_until([fd]() { return histmap_get(fd) != nullptr; }));
		ss->next();
		ASSERT_TRUE(wait_until([this, fd]() { return image_get_fd(imd) != fd && ready(); }));
		}
}

TEST_F(SlideshowTransitions, AutomaticAdvanceAnimatesAndManualAdvanceCancels)
{
	ASSERT_NO_FATAL_FAILURE(start_show());
	ASSERT_TRUE(wait_until([this]() { return image_transition_active(imd); }));
	ASSERT_NE(ss, nullptr);
	EXPECT_EQ(imd->transition_duration, 300U);
	ss->next();
	EXPECT_FALSE(image_transition_active(imd));
	EXPECT_EQ(imd->transition_source, 0U);
	EXPECT_EQ(gtk_stack_get_visible_child(GTK_STACK(imd->view)), imd->pr);
}

TEST_F(SlideshowTransitions, StoppingCancelsPendingTransition)
{
	ASSERT_NO_FATAL_FAILURE(start_show());
	ASSERT_TRUE(wait_until([this]() { return image_transition_active(imd); }));
	delete ss;
	EXPECT_EQ(ss, nullptr);
	EXPECT_FALSE(image_transition_active(imd));
	EXPECT_EQ(imd->transition_source, 0U);
}

TEST_F(SlideshowTransitions, FinalSlideFinishesTransitionAfterShowEnds)
{
	options->slideshow.repeat = FALSE;
	ASSERT_NO_FATAL_FAILURE(start_show());
	ASSERT_TRUE(wait_until([this]() { return ss == nullptr; }));
	ASSERT_TRUE(wait_until([this]() { return !image_transition_active(imd) && ready(); }));
	EXPECT_EQ(gtk_stack_get_visible_child(GTK_STACK(imd->view)), imd->pr);
	EXPECT_EQ(imd->transition_source, 0U);
}

TEST_F(SlideshowTransitions, NoneUsesImmediateChanges)
{
	options->slideshow.transition = GTK_STACK_TRANSITION_TYPE_NONE;
	ASSERT_NO_FATAL_FAILURE(start_show());
	ASSERT_TRUE(wait_until([this]() { return ss->list_done.size() >= 2; }));
	EXPECT_FALSE(image_transition_active(imd));
}

TEST_F(SlideshowTransitions, UnmappingReleasesTransition)
{
	ASSERT_NO_FATAL_FAILURE(set_color(0xff0000ff));
	g_autoptr(GdkPaintable) outgoing = image_transition_capture(imd);
	ASSERT_NE(outgoing, nullptr);
	image_transition_from(imd, outgoing, GTK_STACK_TRANSITION_TYPE_CROSSFADE, 300);
	gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
	EXPECT_FALSE(image_transition_active(imd));
	EXPECT_EQ(imd->transition_source, 0U);
}

TEST_F(SlideshowTransitions, RespectsDisabledDesktopAnimations)
{
	ASSERT_NO_FATAL_FAILURE(set_color(0xff0000ff));
	g_autoptr(GdkPaintable) outgoing = image_transition_capture(imd);
	ASSERT_NE(outgoing, nullptr);
	g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, nullptr);
	image_transition_from(imd, outgoing, GTK_STACK_TRANSITION_TYPE_CROSSFADE, 300);
	ASSERT_TRUE(wait_until([this]() { return !image_transition_active(imd); }));
	EXPECT_EQ(gtk_stack_get_visible_child(GTK_STACK(imd->view)), imd->pr);
}

TEST_F(SlideshowTransitions, SlowDecodingGetsFullDisplayDelayWithoutTransition)
{
	options->slideshow.transition = GTK_STACK_TRANSITION_TYPE_NONE;
	ASSERT_NO_FATAL_FAILURE(start_show());
	const FileDataRef first(ss->slide_fd);
	pr->loading = TRUE;
	struct DecodeState
	{
		PixbufRenderer *renderer;
		gint64 ready_time;
	} decode{pr, 0};
	const guint source = g_timeout_add(300, +[](gpointer data) -> gboolean
		{
		auto *state = static_cast<DecodeState *>(data);
		state->renderer->loading = FALSE;
		state->ready_time = g_get_monotonic_time();
		return G_SOURCE_REMOVE;
		}, &decode);
	const bool advanced = wait_until([this, &first]() { return ss->slide_fd != first; });
	const gint64 advanced_time = g_get_monotonic_time();
	if (!decode.ready_time) g_source_remove(source);
	EXPECT_TRUE(advanced);
	ASSERT_NE(decode.ready_time, 0);
	EXPECT_GE(advanced_time - decode.ready_time, 90000);
}

TEST_F(SlideshowTransitions, CopyWhileLoadingFinishesInDestination)
{
	ASSERT_NO_FATAL_FAILURE(start_show());
	delete ss;
	g_autofree gchar *path = g_build_filename(directory, "1.png", nullptr);
	const FileDataRef file(file_data_new_group(path));
	file_data_unref(file);
	image_change_fd(imd, file, 0.0);
	ASSERT_NE(imd->il, nullptr);
	auto *copy = image_new(FALSE);
	auto *copy_window = GTK_WINDOW(gtk_window_new());
	gtk_window_set_default_size(copy_window, 120, 100);
	gtk_window_set_child(copy_window, copy->widget);
	gtk_window_present(copy_window);
	image_copy_from_image(copy, imd);
	EXPECT_TRUE(wait_until([copy]()
		{
		auto *renderer = PIXBUF_RENDERER(copy->pr);
		return !copy->il && renderer->pixbuf && !renderer->loading && renderer->complete &&
		       renderer_tiles_is_ready(renderer->renderer);
		}));
	EXPECT_EQ(copy->image_fd, static_cast<FileData *>(file));
	EXPECT_FALSE(copy->unknown);
	gtk_window_destroy(copy_window);
}

TEST_F(SlideshowTransitions, ExternalImageMatchesWholeFullscreenFrameAfterTransition)
{
	const gchar *path = g_getenv("GEEQIE_TEST_SLIDESHOW_IMAGE");
	if (!path) GTEST_SKIP() << "Set GEEQIE_TEST_SLIDESHOW_IMAGE to test a local image such as HEIC";
	ASSERT_NO_FATAL_FAILURE(start_show());
	delete ss;
	gtk_window_present(GTK_WINDOW(layout->window));
	layout_image_full_screen_start(layout);
	ASSERT_TRUE(wait_until([this]() { return gtk_window_is_fullscreen(GTK_WINDOW(layout->full_screen->window)); }, 10000000));
	auto *image = layout->full_screen->imd;
	const FileDataRef sample(file_data_new_group(path));
	file_data_unref(sample);
	auto complete = [image]()
		{
		auto *renderer = PIXBUF_RENDERER(image->pr);
		return !image->il && renderer->pixbuf && !renderer->loading && renderer->complete &&
		       renderer_tiles_is_ready(renderer->renderer) && !image_transition_active(image);
		};
	auto digest = [image]() -> std::string
		{
		auto *snapshot = gtk_snapshot_new();
		GTK_WIDGET_GET_CLASS(image->view)->snapshot(image->view, snapshot);
		g_autoptr(GskRenderNode) node = gtk_snapshot_free_to_node(snapshot);
		if (!node) return {};
		const gint width = gtk_widget_get_width(image->view);
		const gint height = gtk_widget_get_height(image->view);
		auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
		auto *cr = cairo_create(surface);
		gsk_render_node_draw(node, cr);
		cairo_destroy(cr);
		cairo_surface_flush(surface);
		g_autofree gchar *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, cairo_image_surface_get_data(surface),
		                                                   cairo_image_surface_get_stride(surface) * height);
		cairo_surface_destroy(surface);
		return hash;
		};
	auto finish_frame = []()
		{
		const gint64 deadline = g_get_monotonic_time() + 50000;
		return wait_until([deadline]() { return g_get_monotonic_time() >= deadline; });
		};
	image_change_fd(image, sample, 0.0);
	ASSERT_TRUE(wait_until(complete, 20000000));
	ASSERT_TRUE(finish_frame());
	ASSERT_FALSE(image->unknown);
	const std::string expected = digest();
	ASSERT_FALSE(expected.empty());
	g_autofree gchar *png = g_build_filename(directory, "0.png", nullptr);
	const FileDataRef previous(file_data_new_group(png));
	file_data_unref(previous);
	image_change_fd(image, previous, 0.0);
	ASSERT_TRUE(wait_until(complete));
	g_autoptr(GdkPaintable) outgoing = image_transition_capture(image);
	ASSERT_NE(outgoing, nullptr);
	image_change_fd(image, sample, 0.0);
	image_transition_from(image, outgoing, GTK_STACK_TRANSITION_TYPE_CROSSFADE, 300);
	ASSERT_TRUE(wait_until(complete, 20000000));
	ASSERT_TRUE(finish_frame());
	EXPECT_EQ(digest(), expected);
	layout_image_full_screen_stop(layout);
}

} // namespace
