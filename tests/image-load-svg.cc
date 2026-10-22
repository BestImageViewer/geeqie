/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "gtest/gtest.h"

#include <gio/gio.h>

#include "filedata.h"
#include "image-load.h"
#include "image-load-gdk.h"
#include "image-load-svgz.h"
#include "options.h"

namespace
{

constexpr gchar svg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='8'>"
                        "<rect width='16' height='8' fill='red'/></svg>";

class SvgLoader : public testing::Test
{
protected:
	void SetUp() override
	{
		if (!options) options = conf_options_new();
	}
};

void initialize(ImageLoaderBackend *backend, ImageLoader *loader)
{
	backend->init([](gpointer, gint, gint, gint, gint, gpointer) {},
	              [](gpointer, gint, gint, gpointer) {}, loader);
}

TEST_F(SvgLoader, UninitializedBackendsAreSafe)
{
	for (bool compressed : {false, true})
		{
		auto backend = compressed ? get_image_loader_backend_svgz() : get_image_loader_backend_default();
		backend->set_size(16, 8);
		EXPECT_EQ(backend->get_pixbuf(), nullptr);
		g_autofree gchar *format = backend->get_format_name();
		g_auto(GStrv) mime_types = backend->get_format_mime_types();
		ASSERT_NE(mime_types, nullptr);
		g_autoptr(GError) error = nullptr;
		gsize length = sizeof(svg) - 1;
		EXPECT_FALSE(backend->write(reinterpret_cast<const guchar *>(svg), length, length, &error));
		ASSERT_NE(error, nullptr);
		EXPECT_EQ(error->code, GDK_PIXBUF_ERROR_UNKNOWN_TYPE);
		g_clear_error(&error);
		EXPECT_FALSE(backend->close(&error));
		ASSERT_NE(error, nullptr);
		}
}

TEST_F(SvgLoader, UnavailableModuleIsReported)
{
	auto fd = FileData::new_simple("/missing/image.svg");
	ImageLoader loader{};
	loader.fd = fd;
	for (bool compressed : {false, true})
		{
		const auto unavailable = +[](GError **error) -> GdkPixbufLoader *
			{
			g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_UNKNOWN_TYPE, "SVG loader unavailable");
			return nullptr;
			};
		auto backend = compressed ? get_image_loader_backend_svgz(unavailable) : get_image_loader_backend_default(unavailable);
		initialize(backend.get(), &loader);
		backend->set_size(16, 8);
		EXPECT_EQ(backend->get_pixbuf(), nullptr);
		g_autoptr(GError) error = nullptr;
		gsize length = sizeof(svg) - 1;
		EXPECT_FALSE(backend->write(reinterpret_cast<const guchar *>(svg), length, length, &error));
		ASSERT_NE(error, nullptr);
		EXPECT_EQ(error->domain, GDK_PIXBUF_ERROR);
		EXPECT_EQ(error->code, GDK_PIXBUF_ERROR_UNKNOWN_TYPE);
		EXPECT_STREQ(error->message, "SVG loader unavailable");
		g_clear_error(&error);
		EXPECT_FALSE(backend->close(&error));
		ASSERT_NE(error, nullptr);
		}
}

TEST_F(SvgLoader, InvalidInputReportsAnError)
{
	auto fd = FileData::new_simple("/missing/image.svg");
	ImageLoader loader{};
	loader.fd = fd;
	for (bool compressed : {false, true})
		{
		auto backend = compressed ? get_image_loader_backend_svgz() : get_image_loader_backend_default();
		initialize(backend.get(), &loader);
		constexpr gchar invalid[] = "This is not SVG or gzip data";
		gsize length = sizeof(invalid) - 1;
		g_autoptr(GError) error = nullptr;
		const gboolean written = backend->write(reinterpret_cast<const guchar *>(invalid), length, length, &error);
		const gboolean closed = backend->close(error ? nullptr : &error);
		EXPECT_FALSE(written && closed);
		EXPECT_NE(error, nullptr);
		EXPECT_EQ(backend->get_pixbuf(), nullptr);
		}
}

TEST_F(SvgLoader, ValidSvgAndCompressedSvgDecode)
{
	auto fd = FileData::new_simple("/missing/image.svg");
	ImageLoader loader{};
	loader.fd = fd;
	for (bool compressed : {false, true})
		{
		g_autoptr(GBytes) bytes = nullptr;
		if (compressed)
			{
			g_autoptr(GOutputStream) memory = g_memory_output_stream_new_resizable();
			g_autoptr(GZlibCompressor) compressor = g_zlib_compressor_new(G_ZLIB_COMPRESSOR_FORMAT_GZIP, -1);
			g_autoptr(GOutputStream) output = g_converter_output_stream_new(memory, G_CONVERTER(compressor));
			ASSERT_TRUE(g_output_stream_write_all(output, svg, sizeof(svg) - 1, nullptr, nullptr, nullptr));
			ASSERT_TRUE(g_output_stream_close(output, nullptr, nullptr));
			bytes = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(memory));
			}
		else bytes = g_bytes_new_static(svg, sizeof(svg) - 1);
		gsize length;
		const auto *data = static_cast<const guchar *>(g_bytes_get_data(bytes, &length));
		auto backend = compressed ? get_image_loader_backend_svgz() : get_image_loader_backend_default();
		initialize(backend.get(), &loader);
		g_autoptr(GError) error = nullptr;
		const gboolean written = backend->write(data, length, length, &error);
		const gboolean closed = backend->close(error ? nullptr : &error);
		if (g_error_matches(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_UNKNOWN_TYPE))
			{
			GTEST_SKIP() << "Requires an SVG GdkPixbuf loader";
			}
		ASSERT_TRUE(written && closed) << (error ? error->message : "");
		GdkPixbuf *pixbuf = backend->get_pixbuf();
		ASSERT_NE(pixbuf, nullptr);
		EXPECT_EQ(gdk_pixbuf_get_width(pixbuf), 16);
		EXPECT_EQ(gdk_pixbuf_get_height(pixbuf), 8);
		}
}

} // namespace
