// SPDX-License-Identifier: GPL-2.0-or-later

#define _GNU_SOURCE

#include <dlfcn.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib.h>
#include <png.h>

typedef GdkPixbuf *(*GdkPixbufNewFromStreamFunc)(GInputStream *, GCancellable *, GError **);

static void free_pixels(guchar *pixels, gpointer)
{
	g_free(pixels);
}

static gboolean read_stream(GInputStream *stream, GCancellable *cancellable, GByteArray **bytes, GError **error)
{
	g_autoptr(GByteArray) buffer = g_byte_array_new();
	guint8 chunk[8192];

	for (;;)
		{
		gssize count = g_input_stream_read(stream, chunk, sizeof(chunk), cancellable, error);
		if (count < 0) return FALSE;
		if (count == 0) break;
		g_byte_array_append(buffer, chunk, (guint)count);
		}

	*bytes = g_steal_pointer(&buffer);
	return TRUE;
}

static GdkPixbuf *load_png_from_memory(const guint8 *data, gsize size, GError **error)
{
	png_image image = {0};
	image.version = PNG_IMAGE_VERSION;

	if (png_image_begin_read_from_memory(&image, data, size) == 0)
		{
		g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE, "%s", image.message);
		return NULL;
		}

	image.format = PNG_FORMAT_RGBA;

	const png_alloc_size_t rowstride = PNG_IMAGE_ROW_STRIDE(image);
	g_autofree guchar *pixels = g_try_malloc(PNG_IMAGE_SIZE(image));
	if (!pixels)
		{
		png_image_free(&image);
		g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY, "Not enough memory to decode PNG");
		return NULL;
		}

	if (png_image_finish_read(&image, NULL, pixels, 0, NULL) == 0)
		{
		g_set_error(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED, "%s", image.message);
		png_image_free(&image);
		return NULL;
		}

	return gdk_pixbuf_new_from_data(g_steal_pointer(&pixels),
	                                GDK_COLORSPACE_RGB,
	                                TRUE,
	                                8,
	                                (int)image.width,
	                                (int)image.height,
	                                (int)rowstride,
	                                free_pixels,
	                                NULL);
}

GdkPixbuf *gdk_pixbuf_new_from_stream(GInputStream *stream, GCancellable *cancellable, GError **error)
{
	static GdkPixbufNewFromStreamFunc real_func = NULL;
	g_autoptr(GByteArray) bytes = NULL;

	if (!real_func)
		{
		real_func = (GdkPixbufNewFromStreamFunc)dlsym(RTLD_NEXT, "gdk_pixbuf_new_from_stream");
		}

	if (!read_stream(stream, cancellable, &bytes, error))
		{
		return NULL;
		}

	if (bytes->len >= 8 && png_sig_cmp(bytes->data, 0, 8) == 0)
		{
		return load_png_from_memory(bytes->data, bytes->len, error);
		}

	if (!real_func)
		{
		g_set_error_literal(error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_UNKNOWN_TYPE, "Original gdk_pixbuf_new_from_stream not available");
		return NULL;
		}

	g_autoptr(GInputStream) copy = g_memory_input_stream_new_from_bytes(g_byte_array_free_to_bytes(g_steal_pointer(&bytes)));
	return real_func(copy, cancellable, error);
}
