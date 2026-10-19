/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <glib-object.h>
#include <glib.h>

#include <config.h>

#include "filedata.h"
#include "image-load.h"
#include "options.h"

#if HAVE_JPEG
#include "image-load-jpeg.h"
#endif

namespace
{

#if HAVE_JPEG
TEST(ImageLoader, ConfigurableJpegDecodeMemoryLimit)
{
	if (!options) options = conf_options_new();
	struct RestoreLimit
	{
		gint value = options->image.jpeg_decode_memory_limit;
		~RestoreLimit() { options->image.jpeg_decode_memory_limit = value; }
	} restore_limit;

	g_autoptr(GdkPixbuf) source = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 1024, 512);
	gdk_pixbuf_fill(source, 0x204060ff);
	g_autofree gchar *jpeg = nullptr;
	gsize length = 0;
	ASSERT_TRUE(gdk_pixbuf_save_to_buffer(source, &jpeg, &length, "jpeg", nullptr, nullptr));

	// The decoded image requires 1.5 MiB. Changing the limit must affect new loads.
	for (const gint limit : {1, 2, 1})
		{
		options->image.jpeg_decode_memory_limit = limit;
		auto loader = get_image_loader_backend_jpeg();
		loader->init([](gpointer, gint, gint, gint, gint, gpointer) {},
		             [](gpointer, gint, gint, gpointer) {}, nullptr);
		g_autoptr(GError) error = nullptr;
		gsize chunk_size = length;
		const gboolean loaded = loader->write(reinterpret_cast<const guchar *>(jpeg), chunk_size, length, &error);
		if (limit == 1)
			{
			EXPECT_FALSE(loaded);
			ASSERT_NE(error, nullptr);
			EXPECT_EQ(error->domain, GDK_PIXBUF_ERROR);
			EXPECT_EQ(error->code, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY);
			}
		else
			{
			EXPECT_TRUE(loaded);
			EXPECT_EQ(error, nullptr);
			ASSERT_NE(loader->get_pixbuf(), nullptr);
			EXPECT_EQ(gdk_pixbuf_get_width(loader->get_pixbuf()), 1024);
			EXPECT_EQ(gdk_pixbuf_get_height(loader->get_pixbuf()), 512);
			}
		}
}
#endif

TEST(ImageLoader, CancelPendingStart)
{
	auto *fd = file_data_new_simple("/nonexistent/geeqie-pending-image.jpg");
	auto *il = image_loader_new(fd);
	file_data_unref(fd);
	ASSERT_TRUE(image_loader_start_deferred(il));
	EXPECT_FALSE(il->thread);
	EXPECT_EQ(il->mapped_file, nullptr);
	const guint source_id = il->idle_id;
	EXPECT_NE(g_main_context_find_source_by_id(nullptr, source_id), nullptr);
	EXPECT_FALSE(image_loader_start_deferred(il));
	image_loader_free(il);
	EXPECT_EQ(g_main_context_find_source_by_id(nullptr, source_id), nullptr);
}

TEST(ImageLoader, DeferredStartReportsFailure)
{
	auto *fd = file_data_new_simple("/nonexistent/geeqie-pending-image.jpg");
	auto *il = image_loader_new(fd);
	file_data_unref(fd);
	gboolean failed = FALSE;
	g_signal_connect(il, "error", G_CALLBACK(+[](ImageLoader *, gpointer data)
		{
		*static_cast<gboolean *>(data) = TRUE;
		}), &failed);
	ASSERT_TRUE(image_loader_start_deferred(il));
	const gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
	while (!failed && g_get_monotonic_time() < deadline)
		{
		g_main_context_iteration(nullptr, FALSE);
		}
	EXPECT_TRUE(failed);
	EXPECT_EQ(il->idle_id, 0U);
	image_loader_free(il);
}

} // namespace
