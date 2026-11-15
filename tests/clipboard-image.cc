/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "clipboard-image.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "gtest/gtest.h"

#include "accelerators.h"
#include "filedata.h"
#include "filefilter.h"
#include "intl.h"
#include "layout-image.h"
#include "layout.h"
#include "options.h"
#include "pixbuf-util.h"

namespace
{

bool wait_for(const std::function<bool()> &ready)
{
	const auto deadline = g_get_monotonic_time() + (10 * G_TIME_SPAN_SECOND);
	while (g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		if (ready()) return true;
		g_usleep(1000);
		}
	return false;
}

GtkWidget *find_widget(GtkWidget *widget, const std::function<bool(GtkWidget *)> &match)
{
	if (match(widget)) return widget;
	for (auto *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
		{
		if (auto *found = find_widget(child, match)) return found;
		}
	return nullptr;
}

GtkWidget *find_window(const gchar *title)
{
	auto *windows = gtk_window_get_toplevels();
	for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
		{
		g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i));
		const gchar *name = gtk_window_get_title(window);
		if (name && g_str_has_prefix(name, title)) return GTK_WIDGET(window);
		}
	return nullptr;
}

struct SaveResult
{
	bool done = false;
	bool saved = false;
	GError *error = nullptr;
	~SaveResult() { g_clear_error(&error); }
};

class ClipboardImage : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!g_getenv("DISPLAY") && !g_getenv("WAYLAND_DISPLAY")) GTEST_SKIP() << "Requires a display";
		if (!gtk_init_check() || !gdk_display_get_default()) GTEST_SKIP() << "Requires a display";
		initialized = true;
		auto *windows = gtk_window_get_toplevels();
		for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
			initial_windows = g_list_prepend(initial_windows, g_list_model_get_item(windows, i));
		g_autofree gchar *directory = g_dir_make_tmp("geeqie-clipboard-XXXXXX", nullptr);
		ASSERT_NE(directory, nullptr);
		root = directory;
		pixels = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 3, 2);
		gdk_pixbuf_fill(pixels, 0x4080c080);
		texture = pixbuf_to_texture(pixels);
	}

	void TearDown() override
	{
		if (!initialized) return;
		if (layout) layout_free(layout);
		if (changed_app)
			{
			g_application_set_default(previous_app);
			g_clear_object(&previous_app);
			}
		gdk_clipboard_set_content(gdk_display_get_clipboard(gdk_display_get_default()), nullptr);
		g_clear_object(&texture);
		g_clear_object(&pixels);
		// GTK can post a folder-loading cancellation alert after its chooser
		// closes. Drain those callbacks and close all fixture-created windows
		// so a modal alert cannot intercept input in later GUI tests.
		gint64 quiet_since = 0;
		EXPECT_TRUE(wait_for([&]()
			{
			bool found = false;
			auto *windows = gtk_window_get_toplevels();
			for (guint i = g_list_model_get_n_items(windows); i > 0; --i)
				{
				g_autoptr(GtkWindow) window = GTK_WINDOW(g_list_model_get_item(windows, i - 1));
				if (g_list_find(initial_windows, window)) continue;
				found = true;
				gtk_window_close(window);
				}
			if (found) quiet_since = 0;
			else if (!quiet_since) quiet_since = g_get_monotonic_time();
			return quiet_since && g_get_monotonic_time() - quiet_since >= 50 * G_TIME_SPAN_MILLISECOND;
			}));
		g_clear_list(&initial_windows, g_object_unref);
		std::error_code error;
		if (!root.empty()) std::filesystem::remove_all(root, error);
	}

	void open_layout()
	{
		if (!options) options = conf_options_new();
		filter_add_defaults();
		filter_rebuild();
		accel_map_load_merged();
		previous_app = g_application_get_default();
		if (previous_app) g_object_ref(previous_app);
		static GtkApplication *app = nullptr;
		if (!app)
			{
			app = gtk_application_new("org.geeqie.ClipboardImageTest", G_APPLICATION_NON_UNIQUE);
			ASSERT_TRUE(g_application_register(G_APPLICATION(app), nullptr, nullptr));
			}
		g_application_set_default(G_APPLICATION(app));
		changed_app = true;
		layout = layout_new_from_config(nullptr, nullptr, FALSE);
		ASSERT_NE(layout, nullptr);
		ASSERT_TRUE(layout_set_path(layout, root.c_str()));
	}

	std::shared_ptr<SaveResult> save(const std::string &path, GCancellable *cancellable = nullptr)
	{
		auto result = std::make_shared<SaveResult>();
		g_autoptr(GFile) file = g_file_new_for_path(path.c_str());
		clipboard_image_save_async(texture, file, cancellable, +[](GObject *source, GAsyncResult *async, gpointer data)
			{
			std::unique_ptr<std::shared_ptr<SaveResult>> owner(static_cast<std::shared_ptr<SaveResult> *>(data));
			auto &result = **owner;
			result.saved = clipboard_image_save_finish(GDK_TEXTURE(source), async, &result.error);
			result.done = true;
			}, new std::shared_ptr<SaveResult>(result));
		EXPECT_TRUE(wait_for([&]() { return result->done; }));
		return result;
	}

	void expect_pixels(const std::string &path)
	{
		g_autoptr(GError) error = nullptr;
		g_autoptr(GdkPixbuf) saved = gdk_pixbuf_new_from_file(path.c_str(), &error);
		ASSERT_NE(saved, nullptr) << (error ? error->message : "");
		ASSERT_EQ(gdk_pixbuf_get_width(saved), 3);
		ASSERT_EQ(gdk_pixbuf_get_height(saved), 2);
		ASSERT_TRUE(gdk_pixbuf_get_has_alpha(saved));
		const auto *pixel = gdk_pixbuf_get_pixels(saved);
		EXPECT_EQ(pixel[0], 0x40);
		EXPECT_EQ(pixel[1], 0x80);
		EXPECT_EQ(pixel[2], 0xc0);
		EXPECT_EQ(pixel[3], 0x80);
	}

	std::filesystem::path root;
	GdkPixbuf *pixels = nullptr;
	GdkTexture *texture = nullptr;
	LayoutWindow *layout = nullptr;
	GApplication *previous_app = nullptr;
	bool changed_app = false;
	bool initialized = false;
	GList *initial_windows = nullptr;
};

TEST_F(ClipboardImage, SavesPngWithOriginalPixelsAndTransparency)
{
	const auto path = (root / "clipboard.PNG").native();
	auto result = save(path);
	ASSERT_TRUE(result->saved) << (result->error ? result->error->message : "");
	expect_pixels(path);
}

TEST_F(ClipboardImage, RejectsWrongExtensionWithoutOverwriting)
{
	const auto path = (root / "clipboard.jpg").native();
	ASSERT_TRUE(g_file_set_contents(path.c_str(), "keep", -1, nullptr));
	auto result = save(path);
	EXPECT_FALSE(result->saved);
	EXPECT_TRUE(g_error_matches(result->error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME));
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(path.c_str(), &contents, nullptr, nullptr));
	EXPECT_STREQ(contents, "keep");
}

TEST_F(ClipboardImage, CancellationPreservesExistingDestination)
{
	const auto path = (root / "clipboard.png").native();
	ASSERT_TRUE(g_file_set_contents(path.c_str(), "keep", -1, nullptr));
	g_autoptr(GCancellable) cancellable = g_cancellable_new();
	g_cancellable_cancel(cancellable);
	auto result = save(path, cancellable);
	EXPECT_FALSE(result->saved);
	EXPECT_TRUE(g_error_matches(result->error, G_IO_ERROR, G_IO_ERROR_CANCELLED));
	g_autofree gchar *contents = nullptr;
	ASSERT_TRUE(g_file_get_contents(path.c_str(), &contents, nullptr, nullptr));
	EXPECT_STREQ(contents, "keep");
}

TEST_F(ClipboardImage, ReportsWriteFailures)
{
	auto result = save((root / "missing" / "clipboard.png").native());
	EXPECT_FALSE(result->saved);
	ASSERT_NE(result->error, nullptr);
	EXPECT_TRUE(g_error_matches(result->error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND));
}

TEST_F(ClipboardImage, ReplacesAnAcceptedDestinationWithValidPng)
{
	const auto path = (root / "clipboard.png").native();
	ASSERT_TRUE(g_file_set_contents(path.c_str(), "old data", -1, nullptr));
	ASSERT_TRUE(save(path)->saved);
	expect_pixels(path);
}

// Exercise the retained file chooser through its public API.
#ifndef SHOW_ALL_DEPRECATED_WARNINGS
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
#endif
TEST_F(ClipboardImage, ActionSavesCapturedClipboardAndSelectsTheNewFile)
{
	open_layout();
	ASSERT_NE(layout, nullptr);
	auto *clipboard = gtk_widget_get_clipboard(layout->window);
	gdk_clipboard_set_texture(clipboard, texture);
	ASSERT_TRUE(gtk_widget_activate_action(layout->window, "win.main-win-save-clipboard-image", nullptr));
	ASSERT_TRUE(wait_for([]() { return find_window(_("Save Clipboard Image")) != nullptr; }));
	auto *dialog = find_window(_("Save Clipboard Image"));
	auto *chooser = GTK_FILE_CHOOSER(find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_FILE_CHOOSER(widget); }));
	ASSERT_NE(chooser, nullptr);
	ASSERT_TRUE(wait_for([&]()
		{
		g_autoptr(GFile) folder = gtk_file_chooser_get_current_folder(chooser);
		g_autofree gchar *path = folder ? g_file_get_path(folder) : nullptr;
		return path && root == path;
		}));
	g_autofree gchar *suggested = gtk_file_chooser_get_current_name(chooser);
	EXPECT_TRUE(g_str_has_prefix(suggested, "clipboard-"));
	EXPECT_TRUE(g_str_has_suffix(suggested, ".png"));
	// The image already read must survive a change in clipboard ownership.
	gdk_clipboard_set_text(clipboard, "different clipboard content");
	gtk_file_chooser_set_current_name(chooser, "saved.png");
	g_signal_emit_by_name(dialog, "response", GTK_RESPONSE_ACCEPT);
	const auto path = (root / "saved.png").native();
	ASSERT_TRUE(wait_for([&]()
		{
		auto *fd = layout_image_get_fd(layout);
		return fd && path == fd->path;
		}));
	expect_pixels(path);
}

TEST_F(ClipboardImage, CancellingTheSaveDialogCreatesNoFile)
{
	open_layout();
	ASSERT_NE(layout, nullptr);
	gdk_clipboard_set_texture(gtk_widget_get_clipboard(layout->window), texture);
	clipboard_image_dialog_show(layout);
	ASSERT_TRUE(wait_for([]() { return find_window(_("Save Clipboard Image")) != nullptr; }));
	g_signal_emit_by_name(find_window(_("Save Clipboard Image")), "response", GTK_RESPONSE_CANCEL);
	EXPECT_TRUE(std::filesystem::is_empty(root));
}
TEST_F(ClipboardImage, AcceptedSaveFinishesSafelyAfterItsWindowCloses)
{
	open_layout();
	ASSERT_NE(layout, nullptr);
	gdk_clipboard_set_texture(gtk_widget_get_clipboard(layout->window), texture);
	clipboard_image_dialog_show(layout);
	ASSERT_TRUE(wait_for([]() { return find_window(_("Save Clipboard Image")) != nullptr; }));
	auto *dialog = find_window(_("Save Clipboard Image"));
	auto *chooser = GTK_FILE_CHOOSER(find_widget(dialog, [](GtkWidget *widget) { return GTK_IS_FILE_CHOOSER(widget); }));
	ASSERT_NE(chooser, nullptr);
	ASSERT_TRUE(wait_for([&]()
		{
		g_autoptr(GFile) folder = gtk_file_chooser_get_current_folder(chooser);
		g_autofree gchar *path = folder ? g_file_get_path(folder) : nullptr;
		return path && root == path;
		}));
	gtk_file_chooser_set_current_name(chooser, "after-close.png");
	g_signal_emit_by_name(dialog, "response", GTK_RESPONSE_ACCEPT);
	layout_free(layout);
	layout = nullptr;
	const auto path = (root / "after-close.png").native();
	ASSERT_TRUE(wait_for([&]() { return std::filesystem::exists(path); }));
	for (guint i = 0; i < 20; ++i) g_main_context_iteration(nullptr, FALSE);
	expect_pixels(path);
}

#ifndef SHOW_ALL_DEPRECATED_WARNINGS
G_GNUC_END_IGNORE_DEPRECATIONS
#endif

TEST_F(ClipboardImage, TextClipboardShowsAnImageSpecificMessage)
{
	open_layout();
	ASSERT_NE(layout, nullptr);
	gdk_clipboard_set_text(gtk_widget_get_clipboard(layout->window), "not image pixels");
	clipboard_image_dialog_show(layout);
	ASSERT_TRUE(wait_for([]() { return find_window(_("Unable to read clipboard image")) != nullptr; }));
	auto *warning = find_window(_("Unable to read clipboard image"));
	EXPECT_NE(find_widget(warning, [](GtkWidget *widget)
		{ return GTK_IS_LABEL(widget) && g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)), _("The clipboard does not contain an image.")) == 0; }), nullptr);
	EXPECT_TRUE(std::filesystem::is_empty(root));
}

TEST_F(ClipboardImage, ClosingWindowCancelsItsOpenSaveDialog)
{
	open_layout();
	ASSERT_NE(layout, nullptr);
	gdk_clipboard_set_texture(gtk_widget_get_clipboard(layout->window), texture);
	clipboard_image_dialog_show(layout);
	ASSERT_TRUE(wait_for([]() { return find_window(_("Save Clipboard Image")) != nullptr; }));
	layout_free(layout);
	layout = nullptr;
	EXPECT_EQ(find_window(_("Save Clipboard Image")), nullptr);
	EXPECT_TRUE(std::filesystem::is_empty(root));
}

TEST_F(ClipboardImage, ClosingWindowDuringClipboardReadIsSafe)
{
	open_layout();
	ASSERT_NE(layout, nullptr);
	gdk_clipboard_set_texture(gtk_widget_get_clipboard(layout->window), texture);
	clipboard_image_dialog_show(layout);
	layout_free(layout);
	layout = nullptr;
	for (guint i = 0; i < 20; ++i) g_main_context_iteration(nullptr, FALSE);
	EXPECT_EQ(find_window(_("Save Clipboard Image")), nullptr);
}

} // namespace
