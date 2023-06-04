/*
 * Copyright (C) 20019 - The Geeqie Team
 *
 * Author: Colin Clark
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * 
 * Derived from:
 * 
 * GdkPixbuf library - PSD image loader
 *
 * Copyright (C) 2008 Jan Dudek
 *
 * Authors: Jan Dudek <jd@jandudek.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * TODO
 * - report errors from parse_psd_header
 * - other color modes (CMYK at least)
 * - i18n
 */

#include "main.h"

#include "image-load.h"
#include "image-load-psd.h"

typedef struct _ImageLoaderPSD ImageLoaderPSD;
struct _ImageLoaderPSD {
	ImageLoaderBackendCbAreaUpdated area_updated_cb;
	ImageLoaderBackendCbSize size_cb;
	ImageLoaderBackendCbAreaPrepared area_prepared_cb;
	gpointer data;
	GdkPixbuf *pixbuf;
	guint requested_width;
	guint requested_height;
	gboolean abort;
};

typedef struct
{
	guchar  signature[4];  /* file ID, always "8BPS" */
	guint16 version;       /* version number, always 1 */
	guchar  resetved[6];
	guint16 channels;      /* number of color channels (1-24) */
	guint32 rows;          /* height of image in pixels (1-30000) */
	guint32 columns;       /* width of image in pixels (1-30000) */
	guint16 depth;         /* number of bits per channel (1, 8, 16 or 32) */
	guint16 color_mode;    /* color mode as defined below */
} PsdHeader;

#define PSD_HEADER_SIZE 26

typedef enum
{
	PSD_MODE_MONO = 0,
	PSD_MODE_GRAYSCALE = 1,
	PSD_MODE_INDEXED = 2,
	PSD_MODE_RGB = 3,
	PSD_MODE_CMYK = 4,
	PSD_MODE_MULTICHANNEL = 7,
	PSD_MODE_DUOTONE = 8,
	PSD_MODE_LAB = 9,
} PsdColorMode;

typedef enum
{
	PSD_COMPRESSION_NONE = 0,
	PSD_COMPRESSION_RLE = 1
} PsdCompressionType;

typedef enum
{
	PSD_STATE_HEADER,
	PSD_STATE_COLOR_MODE_BLOCK,
	PSD_STATE_RESOURCES_BLOCK,
	PSD_STATE_LAYERS_BLOCK,
	PSD_STATE_COMPRESSION,
	PSD_STATE_LINES_LENGTHS,
	PSD_STATE_CHANNEL_DATA,
	PSD_STATE_DONE
} PsdReadState;

typedef struct
{
	PsdReadState       state;
	
	GdkPixbuf*                  pixbuf;
	gpointer                    user_data;

	guchar*            buffer;
	guint              bytes_read;
	guint32            bytes_to_skip;
	gboolean           bytes_to_skip_known;

	guint32            width;
	guint32            height;
	guint16            channels;
	guint16            depth;
	guint16            depth_bytes;
	PsdColorMode       color_mode;
	PsdCompressionType compression;

	guchar**           ch_bufs;       /* channels buffers */
	guint              curr_ch;       /* current channel */
	guint              curr_row;
	guint              pos;
	guint16*           lines_lengths;
	gboolean           finalized;
} PsdContext;


static guint16
read_uint16 (guchar* buf)
{
	return (buf[0] << 8) | buf[1];
}

static guint32
read_uint32 (guchar* buf)
{
	return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}


/*
 * Parse Psdheader from buffer
 *
 * str is expected to be at least PSD_HEADER_SIZE long
 */
static PsdHeader
psd_parse_header (guchar* str)
{
	PsdHeader hd;
	
	memcpy(hd.signature, str, 4);
	hd.version = read_uint16(str + 4);
	hd.channels = read_uint16(str + 12);
	hd.rows = read_uint32(str + 14);
	hd.columns = read_uint32(str + 18);
	hd.depth = read_uint16(str + 22);
	hd.color_mode = read_uint16(str + 24);

	return hd;
}

/*
 * Attempts to read bytes_needed bytes from data and stores them in buffer.
 *
 * Returns true if there were enough bytes and false otherwise
 * (which means we need to call feed_buffer again)
 */
static gboolean
feed_buffer (guchar*        buffer,
             guint*         bytes_read,
             const guchar** data,
             guint*         size,
             guint          bytes_needed)
{
	guint how_many = bytes_needed - *bytes_read;
	if (how_many > *size) {
		how_many = *size;
	}
	memcpy(buffer + *bytes_read, *data, how_many);
	*bytes_read += how_many;
	*data += how_many;
	*size -= how_many;
	return (*bytes_read == bytes_needed);
}

/*
 * Attempts to read size of the block and then skip this block.
 *
 * Returns true when finishes consuming block data, otherwise false
 * (false means we need to call skip_block again)
 */
static gboolean
skip_block (PsdContext* context, const guchar** data, guint* size)
{
	if (!context->bytes_to_skip_known) {
		context->bytes_read = 0;
		if (feed_buffer(context->buffer, &context->bytes_read, data, size, 4)) {
			context->bytes_to_skip = read_uint32(context->buffer);
			context->bytes_to_skip_known = TRUE;
		} else {
			return FALSE;
		}
	}
	if (*size < context->bytes_to_skip) {
		*data += *size;
		context->bytes_to_skip -= *size;
		*size = 0;
		return FALSE;
	} else {
		*size -= context->bytes_to_skip;
		*data += context->bytes_to_skip;
		return TRUE;
	}
}

/*
 * Decodes RLE-compressed data
 */
static void
decompress_line(const guchar* src, guint line_length, guchar* dest)
{
	guint16 bytes_read = 0;
	int k;
	while (bytes_read < line_length) {
		gchar byte = src[bytes_read];
		++bytes_read;
	
		if (byte == -128) {
			continue;
		} else if (byte > -1) {
			gint count = byte + 1;
		
			/* copy next count bytes */
			for (k = 0; k < count; ++k) {
				*dest = src[bytes_read];
				++dest;
				++bytes_read;
			}
		} else {
			gint count = -byte + 1;
		
			/* copy next byte count times */
			guchar next_byte = src[bytes_read];
			++bytes_read; 
			for (k = 0; k < count; ++k) {
				*dest = next_byte;
				++dest;
			}
		}
	}
}

static void
reset_context_buffer(PsdContext* ctx)
{
	ctx->bytes_read = 0;
	ctx->bytes_to_skip = 0;
	ctx->bytes_to_skip_known = FALSE;
}

static void free_context(PsdContext *ctx)
{
	g_free(ctx->buffer);
	g_free(ctx->lines_lengths);
	if (ctx->ch_bufs) {
		int i;
		for (i = 0; i < ctx->channels; i++) {
			g_free(ctx->ch_bufs[i]);
		}
	}
	g_free(ctx);
}

static gboolean image_loader_psd_load(gpointer loader, const guchar *buf, gsize count, GError **UNUSED(error))
{
	auto ld = (ImageLoaderPSD *) loader;
	auto  ctx = g_new0(PsdContext, 1);
	guint i;
	guint32 j;
	guint size = count;

	ctx->state = PSD_STATE_HEADER;

	/* we'll allocate larger buffer once we know image size */
	ctx->buffer = static_cast<guchar *>(g_malloc(PSD_HEADER_SIZE));
	reset_context_buffer(ctx);

	ctx->ch_bufs = NULL;
	ctx->curr_ch = 0;
	ctx->curr_row = 0;
	ctx->pos = 0;
	ctx->lines_lengths = NULL;
	ctx->finalized = FALSE;

	while (size > 0) {
		switch (ctx->state) {
			case PSD_STATE_HEADER:
				if (feed_buffer(
						ctx->buffer, &ctx->bytes_read,
						&buf, &size, PSD_HEADER_SIZE))
				{
					PsdHeader hd = psd_parse_header(ctx->buffer);

					ctx->width = hd.columns;
					ctx->height = hd.rows;
					ctx->channels = hd.channels;
					ctx->depth = hd.depth;
					ctx->depth_bytes = (ctx->depth/8 > 0 ? ctx->depth/8 : 1);
					ctx->color_mode = static_cast<PsdColorMode>(hd.color_mode);
					
					if (ctx->color_mode != PSD_MODE_RGB
					    && ctx->color_mode != PSD_MODE_GRAYSCALE
					    && ctx->color_mode != PSD_MODE_CMYK
					    && ctx->color_mode != PSD_MODE_DUOTONE
					) {
						log_printf("warning: psd - Unsupported color mode\n");
						free_context(ctx);
						return FALSE;
					}
					
					if (ctx->depth != 8 && ctx->depth != 16) {
						log_printf("warning: psd - Unsupported color depth\n");
						free_context(ctx);
						return FALSE;
					}

					/* we need buffer that can contain one channel data for one
					   row in RLE compressed format. 2*width should be enough */
					g_free(ctx->buffer);
					ctx->buffer = static_cast<guchar *>(g_malloc(ctx->width * 2 * ctx->depth_bytes));
					
					/* this will be needed for RLE decompression */
					ctx->lines_lengths =
						static_cast<guint16 *>(g_malloc(2 * ctx->channels * ctx->height));
					
					ctx->pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB,
						FALSE, 8, ctx->width, ctx->height);

					if (ctx->lines_lengths == NULL || ctx->buffer == NULL ||
						ctx->pixbuf == NULL)
					{
						log_printf("warning: Insufficient memory to load PSD image file\n");
						free_context(ctx);
						return FALSE;
					}
					
					/* create separate buffers for each channel */
					ctx->ch_bufs = static_cast<guchar **>(g_malloc(sizeof(guchar*) * ctx->channels));
					for (i = 0; i < ctx->channels; i++) {
						ctx->ch_bufs[i] =
							static_cast<guchar *>(g_malloc(ctx->width*ctx->height*ctx->depth_bytes));

						if (ctx->ch_bufs[i] == NULL) {
						log_printf("warning: Insufficient memory to load PSD image file\n");
						free_context(ctx);
						return FALSE;
						}	
					}

					ctx->state = PSD_STATE_COLOR_MODE_BLOCK;
					reset_context_buffer(ctx);
				}
				break;
			case PSD_STATE_COLOR_MODE_BLOCK:
				if (skip_block(ctx, &buf, &size)) {
					ctx->state = PSD_STATE_RESOURCES_BLOCK;
					reset_context_buffer(ctx);
				}
				break;
			case PSD_STATE_RESOURCES_BLOCK:
				if (skip_block(ctx, &buf, &size)) {
					ctx->state = PSD_STATE_LAYERS_BLOCK;
					reset_context_buffer(ctx);
				}
				break;
			case PSD_STATE_LAYERS_BLOCK:
				if (skip_block(ctx, &buf, &size)) {
					ctx->state = PSD_STATE_COMPRESSION;
					reset_context_buffer(ctx);
				}
				break;
			case PSD_STATE_COMPRESSION:
				if (feed_buffer(ctx->buffer, &ctx->bytes_read, &buf, &size, 2))
				{
					ctx->compression = static_cast<PsdCompressionType>(read_uint16(ctx->buffer));

					if (ctx->compression == PSD_COMPRESSION_RLE) {
						ctx->state = PSD_STATE_LINES_LENGTHS;
						reset_context_buffer(ctx);
					} else if (ctx->compression == PSD_COMPRESSION_NONE) {
						ctx->state = PSD_STATE_CHANNEL_DATA;
						reset_context_buffer(ctx);
					} else {
						log_printf("warning: psd - Unsupported compression type\n");
						return FALSE;
					}
				}
				break;
			case PSD_STATE_LINES_LENGTHS:
				if (feed_buffer(
						(guchar*) ctx->lines_lengths, &ctx->bytes_read, &buf,
						 &size,	2 * ctx->height * ctx->channels))
				{
					/* convert from different endianness */
					for (i = 0; i < ctx->height * ctx->channels; i++) {
						ctx->lines_lengths[i] = read_uint16(
							(guchar*) &ctx->lines_lengths[i]);
					}
					ctx->state = PSD_STATE_CHANNEL_DATA;
					reset_context_buffer(ctx);
				}
				break;
			case PSD_STATE_CHANNEL_DATA:
				{
					guint line_length = ctx->width * ctx->depth_bytes;
					if (ctx->compression == PSD_COMPRESSION_RLE) {
						line_length = ctx->lines_lengths[
							ctx->curr_ch * ctx->height + ctx->curr_row];
					}
					
					if (feed_buffer(ctx->buffer, &ctx->bytes_read, &buf, &size,
							line_length))
					{
						if (ctx->compression == PSD_COMPRESSION_RLE) {
							decompress_line(ctx->buffer, line_length,
								ctx->ch_bufs[ctx->curr_ch] + ctx->pos
							);
						} else {
							memcpy(ctx->ch_bufs[ctx->curr_ch] + ctx->pos,
								ctx->buffer, line_length);
						}
						
						ctx->pos += ctx->width * ctx->depth_bytes;
						++ctx->curr_row;
					
						if (ctx->curr_row >= ctx->height) {
							++ctx->curr_ch;
							ctx->curr_row = 0;
							ctx->pos = 0;
							if (ctx->curr_ch >= ctx->channels) {
								ctx->state = PSD_STATE_DONE;
							}
						}
						
						reset_context_buffer(ctx);
					}
				}
				break;
			case PSD_STATE_DONE:
			default:
				size = 0;
				break;
		}
	}
	
	if (ctx->state == PSD_STATE_DONE && !ctx->finalized) {
		/* convert or copy channel buffers to our GdkPixbuf */
		guchar* pixels = gdk_pixbuf_get_pixels(ctx->pixbuf);
		guint b = ctx->depth_bytes;

		if (ctx->color_mode == PSD_MODE_RGB) {
			for (i = 0; i < ctx->height; i++) {
				for (j = 0; j < ctx->width; j++) {
					pixels[3*j+0] = ctx->ch_bufs[0][ctx->width*i*b + j*b];
					pixels[3*j+1] = ctx->ch_bufs[1][ctx->width*i*b + j*b];
					pixels[3*j+2] = ctx->ch_bufs[2][ctx->width*i*b + j*b];
				}
				pixels += gdk_pixbuf_get_rowstride(ctx->pixbuf);
			}
		} else if (ctx->color_mode == PSD_MODE_GRAYSCALE ||
		           ctx->color_mode == PSD_MODE_DUOTONE)
		{
			for (i = 0; i < ctx->height; i++) {
				for (j = 0; j < ctx->width; j++) {
					pixels[3*j+0] = pixels[3*j+1] = pixels[3*j+2] =
						ctx->ch_bufs[0][ctx->width*i*b + j*b];
				}
				pixels += gdk_pixbuf_get_rowstride(ctx->pixbuf);
			}
		} else if (ctx->color_mode == PSD_MODE_CMYK) {
			/* unfortunately, this doesn't work 100% correctly...
			   CMYK-RGB conversion distorts colors significantly  */
		
			guchar* pixels = gdk_pixbuf_get_pixels(ctx->pixbuf);
			for (i = 0; i < ctx->height; i++) {
				for (j = 0; j < ctx->width; j++) {
					double c = 1.0 -
						(double) ctx->ch_bufs[0][ctx->width*i + j] / 255.0;
					double m = 1.0 -
						(double) ctx->ch_bufs[1][ctx->width*i + j] / 255.0;
					double y = 1.0 -
						(double) ctx->ch_bufs[2][ctx->width*i + j] / 255.0;
					double k = 1.0 -
						(double) ctx->ch_bufs[3][ctx->width*i + j] / 255.0;
					
					pixels[3*j+0] = (1.0 - (c * (1.0 - k) + k)) * 255.0;
					pixels[3*j+1] = (1.0 - (m * (1.0 - k) + k)) * 255.0;
					pixels[3*j+2] = (1.0 - (y * (1.0 - k) + k)) * 255.0;
				}
				pixels += gdk_pixbuf_get_rowstride(ctx->pixbuf);
			}
		}
		ctx->finalized = TRUE;
		ld->pixbuf = ctx->pixbuf;
		ld->area_updated_cb(loader, 0, 0, ctx->width, ctx->height, ld->data);
		free_context(ctx);

		return TRUE;
	}

	free_context(ctx);
	return FALSE;
}

/* ------- Geeqie ------------ */

static gpointer image_loader_psd_new(ImageLoaderBackendCbAreaUpdated area_updated_cb, ImageLoaderBackendCbSize size_cb, ImageLoaderBackendCbAreaPrepared area_prepared_cb, gpointer data)
{
	auto loader = g_new0(ImageLoaderPSD, 1);
	loader->area_updated_cb = area_updated_cb;
	loader->size_cb = size_cb;
	loader->area_prepared_cb = area_prepared_cb;
	loader->data = data;
	return (gpointer) loader;
}

static void image_loader_psd_set_size(gpointer loader, int width, int height)
{
	auto ld = (ImageLoaderPSD *) loader;
	ld->requested_width = width;
	ld->requested_height = height;
}

static GdkPixbuf* image_loader_psd_get_pixbuf(gpointer loader)
{
	auto ld = (ImageLoaderPSD *) loader;
	return ld->pixbuf;
}

static gchar* image_loader_psd_get_format_name(gpointer UNUSED(loader))
{
	return g_strdup("psd");
}

static gchar** image_loader_psd_get_format_mime_types(gpointer UNUSED(loader))
{
	static const gchar *mime[] = {"application/psd", NULL};
	return g_strdupv(const_cast<gchar **>(mime));
}

static gboolean image_loader_psd_close(gpointer UNUSED(loader), GError **UNUSED(error))
{
	return TRUE;
}

static void image_loader_psd_abort(gpointer loader)
{
	auto ld = (ImageLoaderPSD *) loader;
	ld->abort = TRUE;
}

static void image_loader_psd_free(gpointer loader)
{
	auto ld = (ImageLoaderPSD *) loader;
	if (ld->pixbuf) g_object_unref(ld->pixbuf);
	g_free(ld);
}

void image_loader_backend_set_psd(ImageLoaderBackend *funcs)
{
	funcs->loader_new = image_loader_psd_new;
	funcs->set_size = image_loader_psd_set_size;
	funcs->load = image_loader_psd_load;
	funcs->write = NULL;
	funcs->get_pixbuf = image_loader_psd_get_pixbuf;
	funcs->close = image_loader_psd_close;
	funcs->abort = image_loader_psd_abort;
	funcs->free = image_loader_psd_free;
	funcs->get_format_name = image_loader_psd_get_format_name;
	funcs->get_format_mime_types = image_loader_psd_get_format_mime_types;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
