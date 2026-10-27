/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <config.h>
#include <glib/gstdio.h>
#if HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

#include "accelerators.h"
#include "advanced-exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "layout.h"
#include "options.h"

#if HAVE_EXIV2
namespace
{

GtkWidget *find_widget(GtkWidget *widget, GType type, const gchar *prefix = nullptr)
{
	if (G_TYPE_CHECK_INSTANCE_TYPE(widget, type) &&
	    (!prefix || (GTK_IS_LABEL(widget) && g_str_has_prefix(gtk_label_get_text(GTK_LABEL(widget)), prefix)))) return widget;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *found = find_widget(child, type, prefix)) return found;
		}
	return nullptr;
}

class AdvancedExif : public testing::Test
{
protected:
	void SetUp() override
	{
		previous_app = g_application_get_default();
		if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		if (!options) options = conf_options_new();
		accel_map_load_merged();
		static GtkApplication *test_app = nullptr;
		if (!test_app)
			{
			test_app = gtk_application_new("org.geeqie.AdvancedExifTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(test_app), nullptr, nullptr));
			}
		g_application_set_default(G_APPLICATION(test_app));
		directory = g_dir_make_tmp("geeqie-advanced-exif-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		path = g_build_filename(directory, "metadata.png", nullptr);
		g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 16, 16);
		gdk_pixbuf_fill(pixels, 0x123456ff);
		ASSERT_TRUE(gdk_pixbuf_save(pixels, path, "png", nullptr, nullptr));
		Exiv2::XmpProperties::registerNs("https://www.geeqie.org/test/scroll/", "gqscroll");
		auto metadata = Exiv2::ImageFactory::open(path);
		metadata->readMetadata();
		for (int i = 0; i < 240; ++i)
			{
			g_autofree gchar *key = g_strdup_printf("Xmp.gqscroll.Value%03d", i);
			metadata->xmpData()[key] = value(i);
			}
		metadata->writeMetadata();
		image = FileData::new_simple(path);
		layout.options.advanced_exif_window = {0, 0, 900, 600};
		window = advanced_exif_new(&layout);
		gtk_window_set_title(GTK_WINDOW(window), "Geeqie Exif scrolling test");
		view = find_widget(window, GTK_TYPE_COLUMN_VIEW);
		ASSERT_NE(view, nullptr);
		advanced_exif_set_fd(window, image);
		settle();
	}

	void TearDown() override
	{
		if (window) gtk_window_close(GTK_WINDOW(window));
		image.reset(nullptr);
		g_application_set_default(previous_app);
		for (const auto &extra : extra_paths) g_remove(extra.c_str());
		if (path) g_remove(path);
		if (directory) g_rmdir(directory);
		g_free(path);
		g_free(directory);
	}

	std::string write_sidecar(const gchar *name, const gchar *text)
	{
		g_autofree gchar *filename = g_build_filename(directory, name, nullptr);
		extra_paths.emplace_back(filename);
		auto metadata = Exiv2::ImageFactory::create(Exiv2::ImageType::xmp, filename);
		metadata->xmpData()["Xmp.gqscroll.Value000"] = text;
		metadata->writeMetadata();
		return filename;
	}

	static std::string value(int index)
	{
		g_autofree gchar *prefix = g_strdup_printf("BEGIN-%03d-", index);
		return std::string(prefix) + "\n\t" + std::string(8192, 'x') + " Ω needle-" + std::to_string(index);
	}

	static void settle()
	{
		const gint64 deadline = g_get_monotonic_time() + (150 * G_TIME_SPAN_MILLISECOND);
		while (g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			g_usleep(1000);
			}
	}

	static void copy_value(GtkWidget *label)
	{
		g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(label);
		for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i)
			{
			g_autoptr(GObject) controller = G_OBJECT(g_list_model_get_item(controllers, i));
			if (GTK_IS_GESTURE_CLICK(controller)) g_signal_emit_by_name(controller, "released", 1, 0.0, 0.0);
			}
	}

	struct ClipboardResult
	{
		bool done = false;
		gchar *text = nullptr;
		~ClipboardResult() { g_free(text); }
	};

	static void expect_clipboard(const std::string &expected)
	{
		auto result = std::make_shared<ClipboardResult>();
		g_autoptr(GCancellable) cancel = g_cancellable_new();
		gdk_clipboard_read_text_async(gdk_display_get_primary_clipboard(gdk_display_get_default()), cancel,
			[](GObject *object, GAsyncResult *res, gpointer data)
				{
				std::unique_ptr<std::shared_ptr<ClipboardResult>> pending(static_cast<std::shared_ptr<ClipboardResult> *>(data));
				(*pending)->text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(object), res, nullptr);
				(*pending)->done = true;
				}, new std::shared_ptr<ClipboardResult>(result));
		const gint64 deadline = g_get_monotonic_time() + (2 * G_TIME_SPAN_SECOND);
		while (!result->done && g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			g_usleep(1000);
			}
		g_cancellable_cancel(cancel);
		ASSERT_TRUE(result->done);
		EXPECT_STREQ(result->text, expected.c_str());
	}

	LayoutWindow layout{};
	GtkWidget *window = nullptr;
	GtkWidget *view = nullptr;
	GApplication *previous_app = nullptr;
	gchar *directory = nullptr;
	gchar *path = nullptr;
	FileDataRef image{nullptr};
	std::vector<std::string> extra_paths;
};

TEST_F(AdvancedExif, CellsUseBoundedSingleLinePreviewsAndStableColumnWidths)
{
	auto *label = find_widget(view, GTK_TYPE_LABEL, "BEGIN-");
	ASSERT_NE(label, nullptr);
	EXPECT_LE(g_utf8_strlen(gtk_label_get_text(GTK_LABEL(label)), -1), 513);
	EXPECT_EQ(std::string(gtk_label_get_text(GTK_LABEL(label))).find_first_of("\n\r\t"), std::string::npos);
	auto *columns = gtk_column_view_get_columns(GTK_COLUMN_VIEW(view));
	for (guint i = 0; i < g_list_model_get_n_items(columns); ++i)
		{
		g_autoptr(GtkColumnViewColumn) column = GTK_COLUMN_VIEW_COLUMN(g_list_model_get_item(columns, i));
		EXPECT_GT(gtk_column_view_column_get_fixed_width(column), 0);
		EXPECT_TRUE(gtk_column_view_column_get_resizable(column));
		}
}

TEST_F(AdvancedExif, CopyAndSearchKeepValuesBeyondThePreview)
{
	auto *label = find_widget(view, GTK_TYPE_LABEL, "BEGIN-000-");
	ASSERT_NE(label, nullptr);
	copy_value(label);
	expect_clipboard(value(0));
	auto *entry = find_widget(window, GTK_TYPE_SEARCH_ENTRY);
	ASSERT_NE(entry, nullptr);
	gtk_editable_set_text(GTK_EDITABLE(entry), "needle-193");
	g_signal_emit_by_name(entry, "search-changed");
	settle();
	label = find_widget(view, GTK_TYPE_LABEL, "BEGIN-193-");
	ASSERT_NE(label, nullptr);
	copy_value(label);
	expect_clipboard(value(193));
}

TEST_F(AdvancedExif, ReloadPublishesRowsInOneBatch)
{
	auto *model = gtk_column_view_get_model(GTK_COLUMN_VIEW(view));
	guint changes = 0;
	const gulong handler = g_signal_connect(model, "items-changed",
		G_CALLBACK(+[](GListModel *, guint, guint, guint, gpointer data) { ++*static_cast<guint *>(data); }), &changes);
	advanced_exif_set_fd(window, image);
	g_signal_handler_disconnect(model, handler);
	EXPECT_EQ(changes, 1U);
	EXPECT_GE(g_list_model_get_n_items(G_LIST_MODEL(model)), 240U);
	advanced_exif_set_fd(window, nullptr);
	EXPECT_EQ(g_list_model_get_n_items(G_LIST_MODEL(model)), 0U);
}

TEST_F(AdvancedExif, GroupedJpegAndSidecarCanBeInspectedSeparately)
{
	filter_add_defaults();
	filter_rebuild();
	struct RestoreGrouping
	{
		gchar *saved = g_strdup(options->sidecar.ext);
		~RestoreGrouping() { sidecar_ext_parse(saved); g_free(saved); }
	} restore;
	sidecar_ext_parse(".jpg;.xmp");
	g_autofree gchar *jpeg_path = g_build_filename(directory, "empty.jpg", nullptr);
	extra_paths.emplace_back(jpeg_path);
	g_autoptr(GdkPixbuf) pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 16, 16);
	gdk_pixbuf_fill(pixels, 0x123456ff);
	ASSERT_TRUE(gdk_pixbuf_save(pixels, jpeg_path, "jpeg", nullptr, nullptr));
	const auto sidecar_path = write_sidecar("empty.xmp", "SIDECAR-ONLY");
	FileDataRef jpeg = FileData::new_group(jpeg_path);
	ASSERT_NE(jpeg->sidecar_files, nullptr);
	ASSERT_TRUE(filter_name_is_writable(jpeg_path));
	advanced_exif_set_fd(window, jpeg);
	settle();
	auto *source = GTK_DROP_DOWN(find_widget(window, GTK_TYPE_DROP_DOWN));
	ASSERT_NE(source, nullptr);
	ASSERT_EQ(g_list_model_get_n_items(gtk_drop_down_get_model(source)), 2U);
	EXPECT_EQ(gtk_drop_down_get_selected(source), 0U);
	auto *rows = G_LIST_MODEL(gtk_column_view_get_model(GTK_COLUMN_VIEW(view)));
	EXPECT_EQ(g_list_model_get_n_items(rows), 0U);
	gtk_drop_down_set_selected(source, 1);
	settle();
	auto *label = find_widget(view, GTK_TYPE_LABEL, "SIDECAR-ONLY");
	ASSERT_NE(label, nullptr);
	EXPECT_NE(find_widget(window, GTK_TYPE_LABEL, sidecar_path.c_str()), nullptr);
	copy_value(label);
	expect_clipboard("SIDECAR-ONLY");
	gtk_drop_down_set_selected(source, 0);
	EXPECT_EQ(g_list_model_get_n_items(rows), 0U);
}

TEST_F(AdvancedExif, BothSidecarNamingSchemesAndSourceResetAreSupported)
{
	const auto normal = write_sidecar("metadata.xmp", "NORMAL-SIDECAR");
	const auto extended = write_sidecar("metadata.png.xmp", "EXTENDED-SIDECAR");
	advanced_exif_set_fd(window, image);
	auto *source = GTK_DROP_DOWN(find_widget(window, GTK_TYPE_DROP_DOWN));
	ASSERT_NE(source, nullptr);
	ASSERT_EQ(g_list_model_get_n_items(gtk_drop_down_get_model(source)), 3U);
	gtk_drop_down_set_selected(source, 1);
	settle();
	EXPECT_NE(find_widget(view, GTK_TYPE_LABEL, "NORMAL-SIDECAR"), nullptr);
	advanced_exif_set_fd(window, image);
	EXPECT_EQ(gtk_drop_down_get_selected(source), 1U);
	gtk_drop_down_set_selected(source, 2);
	settle();
	EXPECT_NE(find_widget(view, GTK_TYPE_LABEL, "EXTENDED-SIDECAR"), nullptr);
	ASSERT_EQ(g_remove(extended.c_str()), 0);
	advanced_exif_set_fd(window, image);
	EXPECT_EQ(gtk_drop_down_get_selected(source), 0U);
	EXPECT_EQ(g_list_model_get_n_items(gtk_drop_down_get_model(source)), 2U);
	ASSERT_EQ(g_remove(normal.c_str()), 0);
	advanced_exif_set_fd(window, image);
	EXPECT_EQ(g_list_model_get_n_items(gtk_drop_down_get_model(source)), 1U);
	EXPECT_FALSE(gtk_widget_get_sensitive(GTK_WIDGET(source)));
	advanced_exif_set_fd(window, nullptr);
	EXPECT_EQ(g_list_model_get_n_items(gtk_drop_down_get_model(source)), 0U);
	EXPECT_EQ(g_list_model_get_n_items(G_LIST_MODEL(gtk_column_view_get_model(GTK_COLUMN_VIEW(view)))), 0U);
}

TEST_F(AdvancedExif, UngroupedSidecarRemainsDirectlyInspectable)
{
	const auto sidecar_path = write_sidecar("standalone.xmp", "STANDALONE-SIDECAR");
	FileDataRef sidecar = FileData::new_simple(sidecar_path.c_str());
	advanced_exif_set_fd(window, sidecar);
	settle();
	auto *source = GTK_DROP_DOWN(find_widget(window, GTK_TYPE_DROP_DOWN));
	ASSERT_NE(source, nullptr);
	EXPECT_EQ(g_list_model_get_n_items(gtk_drop_down_get_model(source)), 1U);
	EXPECT_FALSE(gtk_widget_get_sensitive(GTK_WIDGET(source)));
	EXPECT_NE(find_widget(view, GTK_TYPE_LABEL, "STANDALONE-SIDECAR"), nullptr);
	advanced_exif_set_fd(window, image);
	EXPECT_EQ(gtk_drop_down_get_selected(source), 0U);
	EXPECT_NE(find_widget(window, GTK_TYPE_LABEL, path), nullptr);
}

TEST_F(AdvancedExif, RepeatedDownInputContinuesAcrossRecycledRows)
{
	g_autofree gchar *xdotool = g_find_program_in_path("xdotool");
	if (!xdotool || !g_getenv("DISPLAY")) GTEST_SKIP() << "Requires xdotool and X11";
	auto *selection = GTK_SINGLE_SELECTION(gtk_column_view_get_model(GTK_COLUMN_VIEW(view)));
	gtk_single_selection_set_selected(selection, 0);
	gtk_column_view_scroll_to(GTK_COLUMN_VIEW(view), 0, nullptr, GTK_LIST_SCROLL_FOCUS, nullptr);
	gtk_widget_grab_focus(view);
	settle();
	struct Frames { gint64 last = 0; gint64 longest = 0; } frames;
	const guint tick = gtk_widget_add_tick_callback(view, [](GtkWidget *, GdkFrameClock *, gpointer data) -> gboolean
		{
		auto *frames = static_cast<Frames *>(data);
		const gint64 now = g_get_monotonic_time();
		if (frames->last) frames->longest = std::max(frames->longest, now - frames->last);
		frames->last = now;
		return G_SOURCE_CONTINUE;
		}, &frames, nullptr);
	g_autoptr(GSubprocess) process = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, nullptr, xdotool,
		"search", "--name", "^Geeqie Exif scrolling test$", "windowfocus", "--sync",
		"key", "--clearmodifiers", "--repeat", "180", "--delay", "25", "Down", nullptr);
	if (process)
		{
		struct ProcessResult { bool done = false; bool success = false; };
		auto result = std::make_shared<ProcessResult>();
		g_subprocess_wait_check_async(process, nullptr, [](GObject *object, GAsyncResult *res, gpointer data)
			{
			std::unique_ptr<std::shared_ptr<ProcessResult>> pending(static_cast<std::shared_ptr<ProcessResult> *>(data));
			(*pending)->success = g_subprocess_wait_check_finish(G_SUBPROCESS(object), res, nullptr);
			(*pending)->done = true;
			}, new std::shared_ptr<ProcessResult>(result));
		const gint64 deadline = g_get_monotonic_time() + (15 * G_TIME_SPAN_SECOND);
		while (!result->done && g_get_monotonic_time() < deadline)
			{
			g_main_context_iteration(nullptr, FALSE);
			g_usleep(1000);
			}
		if (!result->done)
			{
			g_subprocess_force_exit(process);
			g_subprocess_wait(process, nullptr, nullptr);
			}
		// Always release Down, including after a timeout between key press and release.
		g_autoptr(GSubprocess) release = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, nullptr,
		                                               xdotool, "keyup", "Down", nullptr);
		EXPECT_TRUE(release && g_subprocess_wait_check(release, nullptr, nullptr));
		EXPECT_TRUE(result->done);
		EXPECT_TRUE(result->success);
		}
	// Process any key events still queued after xdotool has exited.
	const gint64 input_deadline = g_get_monotonic_time() + (2 * G_TIME_SPAN_SECOND);
	while (process && gtk_single_selection_get_selected(selection) < 150 && g_get_monotonic_time() < input_deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		g_usleep(1000);
		}
	gtk_widget_remove_tick_callback(view, tick);
	ASSERT_NE(process, nullptr);
	EXPECT_GE(gtk_single_selection_get_selected(selection), 150U);
	RecordProperty("longest_frame_gap_ms", frames.longest / 1000);
}

} // namespace
#endif
