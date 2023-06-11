/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
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
 */

#include "main.h"
#include "pixbuf-util.h"
#include "exif.h"
#include "ui-fileops.h"

#include <math.h>


/*
 *-----------------------------------------------------------------------------
 * png save
 *-----------------------------------------------------------------------------
 */

gboolean pixbuf_to_file_as_png(GdkPixbuf *pixbuf, const gchar *filename)
{
	GError *error = nullptr;
	gboolean ret;

	if (!pixbuf || !filename) return FALSE;

	ret = gdk_pixbuf_save(pixbuf, filename, "png", &error,
			      "tEXt::Software", GQ_APPNAME " " VERSION, NULL);

	if (error)
		{
		log_printf("Error saving png file: %s\n", error->message);
		g_error_free(error);
		}

	return ret;
}

/*
 *-----------------------------------------------------------------------------
 * jpeg save
 *-----------------------------------------------------------------------------
 */

//gboolean pixbuf_to_file_as_jpg(GdkPixbuf *pixbuf, const gchar *filename, gint quality)
//{
	//GError *error = NULL;
	//gchar *qbuf;
	//gboolean ret;

	//if (!pixbuf || !filename) return FALSE;

	//if (quality == -1) quality = 75;
	//if (quality < 1 || quality > 100)
		//{
		//log_printf("Jpeg not saved, invalid quality %d\n", quality);
		//return FALSE;
		//}

	//qbuf = g_strdup_printf("%d", quality);
	//ret = gdk_pixbuf_save(pixbuf, filename, "jpeg", &error, "quality", qbuf, NULL);
	//g_free(qbuf);

	//if (error)
		//{
		//log_printf("Error saving jpeg to %s\n%s\n", filename, error->message);
		//g_error_free(error);
		//}

	//return ret;
//}

/*
 *-----------------------------------------------------------------------------
 * pixbuf from inline
 *-----------------------------------------------------------------------------
 */

struct PixbufInline
{
	const gchar *key;
	const gchar *data;
};

static PixbufInline inline_pixbuf_data[] = {
	{ PIXBUF_INLINE_ARCHIVE,	"gq-archive-file.png" },
	{ PIXBUF_INLINE_BROKEN,	"gq-sheet-broken.png" },
	{ PIXBUF_INLINE_COLLECTION,	"gq-collection.png" },
	{ PIXBUF_INLINE_ICON,	"gqview-icon.png" },
	{ PIXBUF_INLINE_ICON_180,	"gq-icon-rotate-180.png" },
	{ PIXBUF_INLINE_ICON_BOOK,	"gq-icon-book.png" },
	{ PIXBUF_INLINE_ICON_CCW,	"gq-icon-rotate-counter-clockwise.png" },
	{ PIXBUF_INLINE_ICON_CONFIG,	"gq-icon-config.png" },
	{ PIXBUF_INLINE_ICON_CW,	"gq-icon-rotate-clockwise.png" },
	{ PIXBUF_INLINE_ICON_DRAW_RECTANGLE,	"gq-icon-draw-rectangle.png" },
	{ PIXBUF_INLINE_ICON_EXIF,	"gq-icon-exif.png" },
	{ PIXBUF_INLINE_ICON_EXPOSURE,	"gq-icon-exposure.png" },
	{ PIXBUF_INLINE_ICON_FILE_FILTER,	"gq-icon-file-filter.png" },
	{ PIXBUF_INLINE_ICON_FLIP,	"gq-icon-flip.png" },
	{ PIXBUF_INLINE_ICON_FLOAT,	"gq-icon-float.png" },
	{ PIXBUF_INLINE_ICON_GRAYSCALE,	"gq-icon-grayscale.png" },
	{ PIXBUF_INLINE_ICON_HEIF,	"gq-icon-heic.png" },
	{ PIXBUF_INLINE_ICON_HIDETOOLS,		"gq-icon-hidetools.png" },
	{ PIXBUF_INLINE_ICON_INFO,	"gq-icon-info.png" },
	{ PIXBUF_INLINE_ICON_MAINTENANCE,	"gq-icon-maintenance.png" },
	{ PIXBUF_INLINE_ICON_MARKS,	"gq-icon-marks.png" },
	{ PIXBUF_INLINE_ICON_MIRROR,	"gq-icon-mirror.png" },
	{ PIXBUF_INLINE_ICON_MOVE,	"gq-icon-move.png" },
	{ PIXBUF_INLINE_ICON_ORIGINAL,	"gq-icon-original.png" },
	{ PIXBUF_INLINE_ICON_PANORAMA,	"gq-icon-panorama.png" },
	{ PIXBUF_INLINE_ICON_PDF,	"gq-icon-pdf.png" },
	{ PIXBUF_INLINE_ICON_RENAME,	"gq-icon-rename.png" },
	{ PIXBUF_INLINE_ICON_SELECT_ALL,	"gq-icon-select-all.png" },
	{ PIXBUF_INLINE_ICON_SELECT_INVERT,	"gq-icon-select-invert.png" },
	{ PIXBUF_INLINE_ICON_SELECT_NONE,	"gq-icon-select-none.png" },
	{ PIXBUF_INLINE_ICON_SELECT_RECTANGLE,	"gq-icon-select-rectangle.png" },
	{ PIXBUF_INLINE_ICON_SORT,	"gq-icon-sort.png" },
	{ PIXBUF_INLINE_ICON_THUMB,	"gq-icon-thumb.png" },
	{ PIXBUF_INLINE_ICON_TOOLS,		"gq-icon-tools.png" },
	{ PIXBUF_INLINE_ICON_TRASH,	"gq-icon-trash.png" },
	{ PIXBUF_INLINE_ICON_VIEW,	"gq-icon-view.png" },
	{ PIXBUF_INLINE_ICON_ZOOMFILLHOR,	"gq-icon-zoomfillhor.png" },
	{ PIXBUF_INLINE_ICON_ZOOMFILLVERT,	"gq-icon-zoomfillvert.png" },
	{ PIXBUF_INLINE_LOGO,	"geeqie-logo.png" },
	{ PIXBUF_INLINE_METADATA,	"gq-sheet-metadata.png" },
	{ PIXBUF_INLINE_SCROLLER,	"gq-scroller.png" },
	{ PIXBUF_INLINE_SPLIT_PANE_SYNC, "gq-icon-split-pane-sync.png" },
	{ PIXBUF_INLINE_UNKNOWN,	"gq-sheet-unknown.png" },
	{ PIXBUF_INLINE_VIDEO,	"gq-sheet-video.png" },
	{ nullptr, nullptr }
};

GdkPixbuf *pixbuf_inline(const gchar *key)
{
	GError *error = nullptr;
	GInputStream *in_stream;
	GdkPixbuf *icon_pixbuf;
	gchar *path;
	gint i;

	if (!key) return nullptr;

	i = 0;
	while (inline_pixbuf_data[i].key)
		{
		if (strcmp(inline_pixbuf_data[i].key, key) == 0)
			{
			path = g_build_filename(GQ_RESOURCE_PATH_ICONS, inline_pixbuf_data[i].data, NULL);

			in_stream = g_resources_open_stream(path, G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
			g_free(path);

			if (error)
				{
				log_printf("warning: inline pixbuf error: %s", error->message);
				g_error_free(error);
				g_object_unref(in_stream);
				return nullptr;
				}

			icon_pixbuf = gdk_pixbuf_new_from_stream(in_stream, nullptr, &error);
			g_object_unref(in_stream);

			if (error)
				{
				log_printf("warning: inline pixbuf error: %s", error->message);
				g_error_free(error);
				return nullptr;
				}

			return icon_pixbuf;
			}
		i++;
		}

	log_printf("warning: inline pixbuf key \"%s\" not found.\n", key);

	return nullptr;
}

static void register_stock_icon(const gchar *key, GdkPixbuf *pixbuf)
{
	static GtkIconFactory *icon_factory = nullptr;
	GtkIconSet *icon_set;

	if (!icon_factory)
		{
		icon_factory = gtk_icon_factory_new();
		gtk_icon_factory_add_default(icon_factory);
		}

	icon_set = gtk_icon_set_new_from_pixbuf(pixbuf);
	gtk_icon_factory_add(icon_factory, key, icon_set);
}


void pixbuf_inline_register_stock_icons()
{
	gint i;

	i = 0;
	while (inline_pixbuf_data[i].key)
		{
		register_stock_icon(inline_pixbuf_data[i].key, pixbuf_inline(inline_pixbuf_data[i].key));
		i++;
		}
}

gboolean register_theme_icon_as_stock(const gchar *key, const gchar *icon)
{
	GtkIconTheme *icon_theme;
	GdkPixbuf *pixbuf;
	GError *error = nullptr;

	icon_theme = gtk_icon_theme_get_default();

	if (gtk_icon_theme_has_icon(icon_theme, key)) return FALSE;

	pixbuf = gtk_icon_theme_load_icon(icon_theme,
                           icon, /* icon name */
                           64, /* size */
                           static_cast<GtkIconLookupFlags>(0),  /* flags */
                           &error);
	if (!pixbuf)
		{
		if (error)
			{
			DEBUG_1("Couldn't load icon %s: %s", icon, error->message);
			g_error_free(error);
			error = nullptr;
			}

		if (strchr(icon, '.'))
			{
			/* try again without extension */
			gchar *icon2 = remove_extension_from_path(icon);
			pixbuf = gtk_icon_theme_load_icon(icon_theme,
		                           icon2, /* icon name */
		                           64, /* size */
		                           static_cast<GtkIconLookupFlags>(0),  /* flags */
		                           &error);
			if (error)
				{
				DEBUG_1("Couldn't load icon %s: %s", icon2, error->message);
				g_error_free(error);
				error = nullptr;

				/* try as an absolute path */
				pixbuf = gdk_pixbuf_new_from_file(icon, &error);
				if (error)
					{
					DEBUG_1("Couldn't load icon as absolute path %s: %s", icon, error->message);
					g_error_free(error);
					}
				}
			g_free(icon2);
			}
		}

	if (!pixbuf) return FALSE;

	register_stock_icon(key, pixbuf);
	return TRUE;
}

gboolean pixbuf_scale_aspect(gint req_w, gint req_h,
			     gint old_w, gint old_h,
			     gint *new_w, gint *new_h)
{
	if ((static_cast<gdouble>(req_w) / old_w) < (static_cast<gdouble>(req_h) / old_h))
		{
		*new_w = req_w;
		*new_h = static_cast<gdouble>(*new_w) / old_w * old_h;
		if (*new_h < 1) *new_h = 1;
		}
	else
		{
		*new_h = req_h;
		*new_w = static_cast<gdouble>(*new_h) / old_h * old_w;
		if (*new_w < 1) *new_w = 1;
		}

	return (*new_w != old_w || *new_h != old_h);
}

GdkPixbuf *pixbuf_fallback(FileData *fd, gint requested_width, gint requested_height)
{
	GdkPixbuf *pixbuf;

	switch (fd->format_class)
		{
		case FORMAT_CLASS_UNKNOWN:
			pixbuf = pixbuf_inline(PIXBUF_INLINE_UNKNOWN);
			break;
		case FORMAT_CLASS_META:
			pixbuf = pixbuf_inline(PIXBUF_INLINE_METADATA);
			break;
		case FORMAT_CLASS_VIDEO:
			pixbuf = pixbuf_inline(PIXBUF_INLINE_VIDEO);
			break;
		case FORMAT_CLASS_COLLECTION:
			pixbuf = pixbuf_inline(PIXBUF_INLINE_COLLECTION);
			break;
		case FORMAT_CLASS_DOCUMENT:
			pixbuf = pixbuf_inline(PIXBUF_INLINE_ICON_PDF);
			break;
		case FORMAT_CLASS_ARCHIVE:
			pixbuf = pixbuf_inline(PIXBUF_INLINE_ARCHIVE);
			break;
		default:
			pixbuf = pixbuf_inline(PIXBUF_INLINE_BROKEN);
		}

	if (requested_width && requested_height)
		{
		gint w = gdk_pixbuf_get_width(pixbuf);
		gint h = gdk_pixbuf_get_height(pixbuf);

		if (w > requested_width || h > requested_height)
			{
			gint nw, nh;

			if (pixbuf_scale_aspect(requested_width, requested_height,
							  w, h, &nw, &nh))
				{
				GdkPixbuf *tmp;

				tmp = pixbuf;
				pixbuf = gdk_pixbuf_scale_simple(tmp, nw, nh, GDK_INTERP_TILES);
				g_object_unref(G_OBJECT(tmp));
				}
			}
		}
	return pixbuf;
}


/*
 *-----------------------------------------------------------------------------
 * misc utils
 *-----------------------------------------------------------------------------
 */

gboolean util_clip_region(gint x, gint y, gint w, gint h,
			  gint clip_x, gint clip_y, gint clip_w, gint clip_h,
			  gint *rx, gint *ry, gint *rw, gint *rh)
{
	if (clip_x + clip_w <= x ||
	    clip_x >= x + w ||
	    clip_y + clip_h <= y ||
	    clip_y >= y + h)
		{
		return FALSE;
		}

	*rx = MAX(x, clip_x);
	*rw = MIN((x + w), (clip_x + clip_w)) - *rx;

	*ry = MAX(y, clip_y);
	*rh = MIN((y + h), (clip_y + clip_h)) - *ry;

	return TRUE;
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf rotation
 *-----------------------------------------------------------------------------
 */

static void pixbuf_copy_block_rotate(guchar *src, gint src_row_stride, gint x, gint y,
				     guchar *dest, gint dest_row_stride, gint w, gint h,
				     gint bytes_per_pixel, gboolean counter_clockwise)
{
	gint i, j;
	guchar *sp;
	guchar *dp;

	for (i = 0; i < h; i++)
		{
		sp = src + ((i + y) * src_row_stride) + (x * bytes_per_pixel);
		for (j = 0; j < w; j++)
			{
			if (counter_clockwise)
				{
				dp = dest + ((w - j - 1) * dest_row_stride) + (i * bytes_per_pixel);
				}
			else
				{
				dp = dest + (j * dest_row_stride) + ((h - i - 1) * bytes_per_pixel);
				}
			*(dp++) = *(sp++);	/* r */
			*(dp++) = *(sp++);	/* g */
			*(dp++) = *(sp++);	/* b */
			if (bytes_per_pixel == 4) *(dp) = *(sp++);	/* a */
			}
		}

}

static void pixbuf_copy_block(guchar *src, gint src_row_stride, gint w, gint h,
			      guchar *dest, gint dest_row_stride, gint x, gint y, gint bytes_per_pixel)
{
	gint i;
	guchar *sp;
	guchar *dp;

	for (i = 0; i < h; i++)
		{
		sp = src + (i * src_row_stride);
		dp = dest + ((y + i) * dest_row_stride) + (x * bytes_per_pixel);
		memcpy(dp, sp, w * bytes_per_pixel);
		}
}

#define ROTATE_BUFFER_WIDTH 48
#define ROTATE_BUFFER_HEIGHT 48

/*
 * Returns a copy of pixbuf src rotated 90 degrees clockwise or 90 counterclockwise
 *
 */
GdkPixbuf *pixbuf_copy_rotate_90(GdkPixbuf *src, gboolean counter_clockwise)
{
	GdkPixbuf *dest;
	gboolean has_alpha;
	gint sw, sh, srs;
	gint dw, dh, drs;
	guchar *s_pix;
	guchar *d_pix;
	gint i, j;
	gint a;
	GdkPixbuf *buffer;
	guchar *b_pix;
	gint brs;
	gint w, h;

	if (!src) return nullptr;

	sw = gdk_pixbuf_get_width(src);
	sh = gdk_pixbuf_get_height(src);
	has_alpha = gdk_pixbuf_get_has_alpha(src);
	srs = gdk_pixbuf_get_rowstride(src);
	s_pix = gdk_pixbuf_get_pixels(src);

	dw = sh;
	dh = sw;
	dest = gdk_pixbuf_new(GDK_COLORSPACE_RGB, has_alpha, 8, dw, dh);
	drs = gdk_pixbuf_get_rowstride(dest);
	d_pix = gdk_pixbuf_get_pixels(dest);

	a = (has_alpha ? 4 : 3);

	buffer = gdk_pixbuf_new(GDK_COLORSPACE_RGB, has_alpha, 8,
				ROTATE_BUFFER_WIDTH, ROTATE_BUFFER_HEIGHT);
	b_pix = gdk_pixbuf_get_pixels(buffer);
	brs = gdk_pixbuf_get_rowstride(buffer);

	for (i = 0; i < sh; i+= ROTATE_BUFFER_WIDTH)
		{
		w = MIN(ROTATE_BUFFER_WIDTH, (sh - i));
		for (j = 0; j < sw; j += ROTATE_BUFFER_HEIGHT)
			{
			gint x, y;

			h = MIN(ROTATE_BUFFER_HEIGHT, (sw - j));
			pixbuf_copy_block_rotate(s_pix, srs, j, i,
						 b_pix, brs, h, w,
						 a, counter_clockwise);

			if (counter_clockwise)
				{
				x = i;
				y = sw - h - j;
				}
			else
				{
				x = sh - w - i;
				y = j;
				}
			pixbuf_copy_block(b_pix, brs, w, h,
					  d_pix, drs, x, y, a);
			}
		}

	g_object_unref(buffer);

#if 0
	/* this is the simple version of rotation (roughly 2-4x slower) */

	for (i = 0; i < sh; i++)
		{
		sp = s_pix + (i * srs);
		for (j = 0; j < sw; j++)
			{
			if (counter_clockwise)
				{
				dp = d_pix + ((dh - j - 1) * drs) + (i * a);
				}
			else
				{
				dp = d_pix + (j * drs) + ((dw - i - 1) * a);
				}

			*(dp++) = *(sp++);	/* r */
			*(dp++) = *(sp++);	/* g */
			*(dp++) = *(sp++);	/* b */
			if (has_alpha) *(dp) = *(sp++);	/* a */
			}
		}
#endif

	return dest;
}

/*
 * Returns a copy of pixbuf mirrored and or flipped.
 * TO do a 180 degree rotations set both mirror and flipped TRUE
 * if mirror and flip are FALSE, result is a simple copy.
 */
GdkPixbuf *pixbuf_copy_mirror(GdkPixbuf *src, gboolean mirror, gboolean flip)
{
	GdkPixbuf *dest;
	gboolean has_alpha;
	gint w, h, srs;
	gint drs;
	guchar *s_pix;
	guchar *d_pix;
	guchar *sp;
	guchar *dp;
	gint i, j;
	gint a;

	if (!src) return nullptr;

	w = gdk_pixbuf_get_width(src);
	h = gdk_pixbuf_get_height(src);
	has_alpha = gdk_pixbuf_get_has_alpha(src);
	srs = gdk_pixbuf_get_rowstride(src);
	s_pix = gdk_pixbuf_get_pixels(src);

	dest = gdk_pixbuf_new(GDK_COLORSPACE_RGB, has_alpha, 8, w, h);
	drs = gdk_pixbuf_get_rowstride(dest);
	d_pix = gdk_pixbuf_get_pixels(dest);

	a = has_alpha ? 4 : 3;

	for (i = 0; i < h; i++)
		{
		sp = s_pix + (i * srs);
		if (flip)
			{
			dp = d_pix + ((h - i - 1) * drs);
			}
		else
			{
			dp = d_pix + (i * drs);
			}
		if (mirror)
			{
			dp += (w - 1) * a;
			for (j = 0; j < w; j++)
				{
				*(dp++) = *(sp++);	/* r */
				*(dp++) = *(sp++);	/* g */
				*(dp++) = *(sp++);	/* b */
				if (has_alpha) *(dp) = *(sp++);	/* a */
				dp -= (a + 3);
				}
			}
		else
			{
			for (j = 0; j < w; j++)
				{
				*(dp++) = *(sp++);	/* r */
				*(dp++) = *(sp++);	/* g */
				*(dp++) = *(sp++);	/* b */
				if (has_alpha) *(dp++) = *(sp++);	/* a */
				}
			}
		}

	return dest;
}

GdkPixbuf *pixbuf_apply_orientation(GdkPixbuf *pixbuf, gint orientation)
{
	GdkPixbuf *dest;
	GdkPixbuf *tmp = nullptr;

	switch (orientation)
		{
		case EXIF_ORIENTATION_TOP_LEFT:
			dest = gdk_pixbuf_copy(pixbuf);
			break;
		case EXIF_ORIENTATION_TOP_RIGHT:
			/* mirrored */
			dest = pixbuf_copy_mirror(pixbuf, TRUE, FALSE);
			break;
		case EXIF_ORIENTATION_BOTTOM_RIGHT:
			/* upside down */
			dest = pixbuf_copy_mirror(pixbuf, TRUE, TRUE);
			break;
		case EXIF_ORIENTATION_BOTTOM_LEFT:
			/* flipped */
			dest = pixbuf_copy_mirror(pixbuf, FALSE, TRUE);
			break;
		case EXIF_ORIENTATION_LEFT_TOP:
			tmp = pixbuf_copy_mirror(pixbuf, FALSE, TRUE);
			dest = pixbuf_copy_rotate_90(tmp, FALSE);
			break;
		case EXIF_ORIENTATION_RIGHT_TOP:
			/* rotated -90 (270) */
			dest = pixbuf_copy_rotate_90(pixbuf, FALSE);
			break;
		case EXIF_ORIENTATION_RIGHT_BOTTOM:
			tmp = pixbuf_copy_mirror(pixbuf, FALSE, TRUE);
			dest = pixbuf_copy_rotate_90(tmp, TRUE);
			break;
		case EXIF_ORIENTATION_LEFT_BOTTOM:
			/* rotated 90 */
			dest = pixbuf_copy_rotate_90(pixbuf, TRUE);
			break;
		default:
			dest = gdk_pixbuf_copy(pixbuf);
			break;
		}
	if (tmp) g_object_unref(tmp);
	return dest;

}


/*
 *-----------------------------------------------------------------------------
 * pixbuf drawing (rectangles)
 *-----------------------------------------------------------------------------
 */

/*
 * Fills region of pixbuf at x,y over w,h
 * with colors red (r), green (g), blue (b)
 * applying alpha (a), use a=255 for solid.
 */
void pixbuf_draw_rect_fill(GdkPixbuf *pb,
			   gint x, gint y, gint w, gint h,
			   gint r, gint g, gint b, gint a)
{
	gboolean has_alpha;
	gint pw, ph, prs;
	guchar *p_pix;
	guchar *pp;
	gint i, j;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (x < 0 || x + w > pw) return;
	if (y < 0 || y + h > ph) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	for (i = 0; i < h; i++)
		{
		pp = p_pix + (y + i) * prs + (x * (has_alpha ? 4 : 3));
		for (j = 0; j < w; j++)
			{
			*pp = (r * a + *pp * (256-a)) >> 8;
			pp++;
			*pp = (g * a + *pp * (256-a)) >> 8;
			pp++;
			*pp = (b * a + *pp * (256-a)) >> 8;
			pp++;
			if (has_alpha) pp++;
			}
		}
}

//void pixbuf_draw_rect(GdkPixbuf *pb,
		      //gint x, gint y, gint w, gint h,
		      //gint r, gint g, gint b, gint a,
		      //gint left, gint right, gint top, gint bottom)
//{
	//pixbuf_draw_rect_fill(pb, x + left, y, w - left - right, top,
			      //r, g, b ,a);
	//pixbuf_draw_rect_fill(pb, x + w - right, y, right, h,
			      //r, g, b ,a);
	//pixbuf_draw_rect_fill(pb, x + left, y + h - bottom, w - left - right, bottom,
			      //r, g, b ,a);
	//pixbuf_draw_rect_fill(pb, x, y, left, h,
			      //r, g, b ,a);
//}

void pixbuf_set_rect_fill(GdkPixbuf *pb,
			  gint x, gint y, gint w, gint h,
			  gint r, gint g, gint b, gint a)
{
	gboolean has_alpha;
	gint pw, ph, prs;
	guchar *p_pix;
	guchar *pp;
	gint i, j;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (x < 0 || x + w > pw) return;
	if (y < 0 || y + h > ph) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	for (i = 0; i < h; i++)
		{
		pp = p_pix + (y + i) * prs + (x * (has_alpha ? 4 : 3));
		for (j = 0; j < w; j++)
			{
			*pp = r; pp++;
			*pp = g; pp++;
			*pp = b; pp++;
			if (has_alpha) { *pp = a; pp++; }
			}
		}
}

void pixbuf_set_rect(GdkPixbuf *pb,
		     gint x, gint y, gint w, gint h,
		     gint r, gint g, gint b, gint a,
		     gint left, gint right, gint top, gint bottom)
{
	pixbuf_set_rect_fill(pb, x + left, y, w - left - right, top,
			     r, g, b ,a);
	pixbuf_set_rect_fill(pb, x + w - right, y, right, h,
			     r, g, b ,a);
	pixbuf_set_rect_fill(pb, x + left, y + h - bottom, w - left - right, bottom,
			     r, g, b ,a);
	pixbuf_set_rect_fill(pb, x, y, left, h,
			     r, g, b ,a);
}

void pixbuf_pixel_set(GdkPixbuf *pb, gint x, gint y, gint r, gint g, gint b, gint a)
{
	guchar *buf;
	gboolean has_alpha;
	gint rowstride;
	guchar *p;

	if (x < 0 || x >= gdk_pixbuf_get_width(pb) ||
	    y < 0 || y >= gdk_pixbuf_get_height(pb)) return;

	buf = gdk_pixbuf_get_pixels(pb);
	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	rowstride = gdk_pixbuf_get_rowstride(pb);

	p = buf + (y * rowstride) + (x * (has_alpha ? 4 : 3));
	*p = r; p++;
	*p = g; p++;
	*p = b; p++;
	if (has_alpha) *p = a;
}


/*
 *-----------------------------------------------------------------------------
 * pixbuf text rendering
 *-----------------------------------------------------------------------------
 */

static void pixbuf_copy_font(GdkPixbuf *src, gint sx, gint sy,
			     GdkPixbuf *dest, gint dx, gint dy,
			     gint w, gint h,
			     guint8 r, guint8 g, guint8 b, guint8 a)
{
	gint sw, sh, srs;
	gboolean s_alpha;
	gint s_step;
	guchar *s_pix;
	gint dw, dh, drs;
	gboolean d_alpha;
	gint d_step;
	guchar *d_pix;

	guchar *sp;
	guchar *dp;
	gint i, j;

	if (!src || !dest) return;

	sw = gdk_pixbuf_get_width(src);
	sh = gdk_pixbuf_get_height(src);

	if (sx < 0 || sx + w > sw) return;
	if (sy < 0 || sy + h > sh) return;

	dw = gdk_pixbuf_get_width(dest);
	dh = gdk_pixbuf_get_height(dest);

	if (dx < 0 || dx + w > dw) return;
	if (dy < 0 || dy + h > dh) return;

	s_alpha = gdk_pixbuf_get_has_alpha(src);
	d_alpha = gdk_pixbuf_get_has_alpha(dest);
	srs = gdk_pixbuf_get_rowstride(src);
	drs = gdk_pixbuf_get_rowstride(dest);
	s_pix = gdk_pixbuf_get_pixels(src);
	d_pix = gdk_pixbuf_get_pixels(dest);

	s_step = (s_alpha) ? 4 : 3;
	d_step = (d_alpha) ? 4 : 3;

	for (i = 0; i < h; i++)
		{
		sp = s_pix + (sy + i) * srs + sx * s_step;
		dp = d_pix + (dy + i) * drs + dx * d_step;
		for (j = 0; j < w; j++)
			{
			if (*sp)
				{
				guint8 asub;

				asub = a * sp[0] / 255;
				*dp = (r * asub + *dp * (256-asub)) >> 8;
				dp++;
				asub = a * sp[1] / 255;
				*dp = (g * asub + *dp * (256-asub)) >> 8;
				dp++;
				asub = a * sp[2] / 255;
				*dp = (b * asub + *dp * (256-asub)) >> 8;
				dp++;

				if (d_alpha)
					{
					*dp = MAX(*dp, a * ((sp[0] + sp[1] + sp[2]) / 3) / 255);
					dp++;
					}
				}
			else
				{
				dp += d_step;
				}

			sp += s_step;
			}
		}
}

void pixbuf_draw_layout(GdkPixbuf *pixbuf, PangoLayout *layout, GtkWidget *UNUSED(widget),
			gint x, gint y,
			guint8 r, guint8 g, guint8 b, guint8 a)
{
	GdkPixbuf *buffer;
	gint w, h;
	gint sx, sy;
	gint dw, dh;
	cairo_surface_t *source;
	cairo_t *cr;

	pango_layout_get_pixel_size(layout, &w, &h);
	if (w < 1 || h < 1) return;

	source = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);

	cr = cairo_create (source);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle (cr, 0, 0, w, h);
	cairo_fill (cr);
	cairo_set_source_rgb(cr, 1, 1, 1);
	pango_cairo_show_layout (cr, layout);
	cairo_destroy (cr);

	buffer = gdk_pixbuf_new_from_data (cairo_image_surface_get_data (source),
	                                   GDK_COLORSPACE_RGB,
	                                   cairo_image_surface_get_format (source) == CAIRO_FORMAT_ARGB32,
	                                   8,
	                                   cairo_image_surface_get_width (source),
	                                   cairo_image_surface_get_height (source),
	                                   cairo_image_surface_get_stride (source),
	                                   nullptr,
	                                   nullptr);

	sx = 0;
	sy = 0;
	dw = gdk_pixbuf_get_width(pixbuf);
	dh = gdk_pixbuf_get_height(pixbuf);

	if (x < 0)
		{
		w += x;
		sx = -x;
		x = 0;
		}

	if (y < 0)
		{
		h += y;
		sy = -y;
		y = 0;
		}

	if (x + w > dw)	w = dw - x;
	if (y + h > dh) h = dh - y;

	pixbuf_copy_font(buffer, sx, sy,
			 pixbuf, x, y, w, h,
			 r, g, b, a);

	g_object_unref(buffer);
	cairo_surface_destroy(source);
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf drawing (triangle)
 *-----------------------------------------------------------------------------
 */

void util_clip_triangle(gint x1, gint y1, gint x2, gint y2, gint x3, gint y3,
			gint *rx, gint *ry, gint *rw, gint *rh)
{
	gint tx, ty, tw, th;

	tx = MIN(x1, x2);
	tx = MIN(tx, x3);
	ty = MIN(y1, y2);
	ty = MIN(ty, y3);
	tw = MAX(abs(x1 - x2), abs(x2 - x3));
	tw = MAX(tw, abs(x3 - x1));
	th = MAX(abs(y1 - y2), abs(y2 - y3));
	th = MAX(th, abs(y3 - y1));

	*rx = tx;
	*ry = ty;
	*rw = tw;
	*rh = th;
}

void pixbuf_draw_triangle(GdkPixbuf *pb,
			  gint clip_x, gint clip_y, gint clip_w, gint clip_h,
			  gint x1, gint y1, gint x2, gint y2, gint x3, gint y3,
			  guint8 r, guint8 g, guint8 b, guint8 a)
{
	gboolean has_alpha;
	gint pw, ph, prs;
	gint rx, ry, rw, rh;
	gint tx, ty, tw, th;
	gint fx1, fy1;
	gint fx2, fy2;
	gint fw, fh;
	guchar *p_pix;
	guchar *pp;
	gint p_step;
	gdouble slope1, slope2;
	gint slope1_x, slope1_y;
	gint y;
	gint t;
	gboolean middle = FALSE;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (!util_clip_region(0, 0, pw, ph,
			      clip_x, clip_y, clip_w, clip_h,
			      &rx, &ry, &rw, &rh)) return;

	util_clip_triangle(x1, y1, x2, y2, x3, y3,
			   &tx, &ty, &tw, &th);

	if (!util_clip_region(rx, ry, rw, rh,
			      tx, ty, tw, th,
			      &fx1, &fy1, &fw, &fh)) return;
	fx2 = fx1 + fw;
	fy2 = fy1 + fh;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	p_step = (has_alpha) ? 4 : 3;

	if (y1 > y2)
		{
		t = x1; x1 = x2; x2 = t;
		t = y1; y1 = y2; y2 = t;
		}
	if (y2 > y3)
		{
		t = x2; x2 = x3; x3 = t;
		t = y2; y2 = y3; y3 = t;
		}
	if (y1 > y2)
		{
		t = x1; x1 = x2; x2 = t;
		t = y1; y1 = y2; y2 = t;
		}

	slope1 = static_cast<gdouble>(y2 - y1);
	if (slope1) slope1 = static_cast<gdouble>(x2 - x1) / slope1;
	slope1_x = x1;
	slope1_y = y1;
	slope2 = static_cast<gdouble>(y3 - y1);
	if (slope2) slope2 = static_cast<gdouble>(x3 - x1) / slope2;

	for (y = fy1; y < fy2; y++)
		{
		gint xa, xb;

		if (!middle && y > y2)
			{
			slope1 = static_cast<gdouble>(y3 - y2);
			if (slope1) slope1 = static_cast<gdouble>(x3 - x2) / slope1;
			slope1_x = x2;
			slope1_y = y2;

			middle = TRUE;
			}

		xa = slope1_x + (slope1 * (y - slope1_y) + 0.5);
		xb = x1 + (slope2 * (y - y1) + 0.5);

		if (xa > xb)
			{
			t = xa; xa = xb; xb = t;
			}

		xa = CLAMP(xa, fx1, fx2);
		xb = CLAMP(xb, fx1, fx2);

		pp = p_pix + y * prs + xa * p_step;

		while (xa < xb)
			{
			*pp = (r * a + *pp * (256-a)) >> 8;
			pp++;
			*pp = (g * a + *pp * (256-a)) >> 8;
			pp++;
			*pp = (b * a + *pp * (256-a)) >> 8;
			pp++;
			if (has_alpha) pp++;

			xa++;
			}
		}
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf drawing (line)
 *-----------------------------------------------------------------------------
 */

static gboolean util_clip_line(gdouble clip_x, gdouble clip_y, gdouble clip_w, gdouble clip_h,
			       gdouble x1, gdouble y1, gdouble x2, gdouble y2,
			       gdouble *rx1, gdouble *ry1, gdouble *rx2, gdouble *ry2)
{
	gboolean flip = FALSE;
	gdouble d;

	if (x1 > x2)
		{
		gdouble t;

		t = x1; x1 = x2; x2 = t;
		t = y1; y1 = y2; y2 = t;
		flip = TRUE;
		}

	if (x2 < clip_x || x1 > clip_x + clip_w) return FALSE;

	if (y1 < y2)
		{
		if (y2 < clip_y || y1 > clip_y + clip_h) return FALSE;
		}
	else
		{
		if (y1 < clip_y || y2 > clip_y + clip_h) return FALSE;
		}

	d = x2 - x1;
	if (d > 0.0)
		{
		gdouble slope;

		slope = (y2 - y1) / d;
		if (x1 < clip_x)
			{
			y1 = y1 + slope * (clip_x - x1);
			x1 = clip_x;
			}
		if (x2 > clip_x + clip_w)
			{
			y2 = y2 + slope * (clip_x + clip_w - x2);
			x2 = clip_x + clip_w;
			}
		}

	if (y1 < y2)
		{
		if (y2 < clip_y || y1 > clip_y + clip_h) return FALSE;
		}
	else
		{
		gdouble t;

		if (y1 < clip_y || y2 > clip_y + clip_h) return FALSE;

		t = x1; x1 = x2; x2 = t;
		t = y1; y1 = y2; y2 = t;
		flip = !flip;
		}

	d = y2 - y1;
	if (d > 0.0)
		{
		gdouble slope;

		slope = (x2 - x1) / d;
		if (y1 < clip_y)
			{
			x1 = x1 + slope * (clip_y - y1);
			y1 = clip_y;
			}
		if (y2 > clip_y + clip_h)
			{
			x2 = x2 + slope * (clip_y + clip_h - y2);
			y2 = clip_y + clip_h;
			}
		}

	if (flip)
		{
		*rx1 = x2;
		*ry1 = y2;
		*rx2 = x1;
		*ry2 = y1;
		}
	else
		{
		*rx1 = x1;
		*ry1 = y1;
		*rx2 = x2;
		*ry2 = y2;
		}

	return TRUE;
}

void pixbuf_draw_line(GdkPixbuf *pb,
		      gint clip_x, gint clip_y, gint clip_w, gint clip_h,
		      gint x1, gint y1, gint x2, gint y2,
		      guint8 r, guint8 g, guint8 b, guint8 a)
{
	gboolean has_alpha;
	gint pw, ph, prs;
	gint rx, ry, rw, rh;
	gdouble rx1, ry1, rx2, ry2;
	guchar *p_pix;
	guchar *pp;
	gint p_step;
	gdouble slope;
	gdouble x, y;
	gint px, py;
	gint cx1, cy1, cx2, cy2;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (!util_clip_region(0, 0, pw, ph,
			      clip_x, clip_y, clip_w, clip_h,
			      &rx, &ry, &rw, &rh)) return;
	if (!util_clip_line(static_cast<gdouble>(rx), static_cast<gdouble>(ry), static_cast<gdouble>(rw), static_cast<gdouble>(rh),
			    static_cast<gdouble>(x1), static_cast<gdouble>(y1), static_cast<gdouble>(x2), static_cast<gdouble>(y2),
			    &rx1, &ry1, &rx2, &ry2)) return;

	cx1 = rx;
	cy1 = ry;
	cx2 = rx + rw;
	cy2 = ry + rh;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	p_step = (has_alpha) ? 4 : 3;

	if (fabs(rx2 - rx1) > fabs(ry2 - ry1))
		{
		if (rx1 > rx2)
			{
			gdouble t;
			t = rx1; rx1 = rx2; rx2 = t;
			t = ry1; ry1 = ry2; ry2 = t;
			}

		slope = rx2 - rx1;
		if (slope != 0.0) slope = (ry2 - ry1) / slope;
		for (x = rx1; x < rx2; x += 1.0)
			{
			px = static_cast<gint>(x + 0.5);
			py = static_cast<gint>(ry1 + (x - rx1) * slope + 0.5);

			if (px >=  cx1 && px < cx2 && py >= cy1 && py < cy2)
				{
				pp = p_pix + py * prs + px * p_step;
				*pp = (r * a + *pp * (256-a)) >> 8;
				pp++;
				*pp = (g * a + *pp * (256-a)) >> 8;
				pp++;
				*pp = (b * a + *pp * (256-a)) >> 8;
				}
			}
		}
	else
		{
		if (ry1 > ry2)
			{
			gdouble t;
			t = rx1; rx1 = rx2; rx2 = t;
			t = ry1; ry1 = ry2; ry2 = t;
			}

		slope = ry2 - ry1;
		if (slope != 0.0) slope = (rx2 - rx1) / slope;
		for (y = ry1; y < ry2; y += 1.0)
			{
			px = static_cast<gint>(rx1 + (y - ry1) * slope + 0.5);
			py = static_cast<gint>(y + 0.5);

			if (px >=  cx1 && px < cx2 && py >= cy1 && py < cy2)
				{
				pp = p_pix + py * prs + px * p_step;
				*pp = (r * a + *pp * (256-a)) >> 8;
				pp++;
				*pp = (g * a + *pp * (256-a)) >> 8;
				pp++;
				*pp = (b * a + *pp * (256-a)) >> 8;
				}
			}
		}
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf drawing (fades and shadows)
 *-----------------------------------------------------------------------------
 */

static void pixbuf_draw_fade_linear(guchar *p_pix, gint prs, gboolean has_alpha,
				    gint s, gboolean vertical, gint border,
				    gint x1, gint y1, gint x2, gint y2,
				    guint8 r, guint8 g, guint8 b, guint8 a)
{
	guchar *pp;
	gint p_step;
	guint8 n = a;
	gint i, j;

	p_step = (has_alpha) ? 4 : 3;
	for (j = y1; j < y2; j++)
		{
		pp = p_pix + j * prs + x1 * p_step;
		if (!vertical) n = a - a * abs(j - s) / border;
		for (i = x1; i < x2; i++)
			{
			if (vertical) n = a - a * abs(i - s) / border;
			*pp = (r * n + *pp * (256-n)) >> 8;
			pp++;
			*pp = (g * n + *pp * (256-n)) >> 8;
			pp++;
			*pp = (b * n + *pp * (256-n)) >> 8;
			pp++;
			if (has_alpha) pp++;
			}
		}
}

static void pixbuf_draw_fade_radius(guchar *p_pix, gint prs, gboolean has_alpha,
				    gint sx, gint sy, gint border,
				    gint x1, gint y1, gint x2, gint y2,
				    guint8 UNUSED(r), guint8 g, guint8 b, guint8 a)
{
	guchar *pp;
	gint p_step;
	gint i, j;

	p_step = (has_alpha) ? 4 : 3;
	for (j = y1; j < y2; j++)
		{
		pp = p_pix + j * prs + x1 * p_step;
		for (i = x1; i < x2; i++)
			{
			guint8 n;
			gint r;

			r = MIN(border, (gint)sqrt((i-sx)*(i-sx) + (j-sy)*(j-sy)));
			n = a - a * r / border;
			*pp = (r * n + *pp * (256-n)) >> 8;
			pp++;
			*pp = (g * n + *pp * (256-n)) >> 8;
			pp++;
			*pp = (b * n + *pp * (256-n)) >> 8;
			pp++;
			if (has_alpha) pp++;
			}
		}
}

void pixbuf_draw_shadow(GdkPixbuf *pb,
			gint clip_x, gint clip_y, gint clip_w, gint clip_h,
			gint x, gint y, gint w, gint h, gint border,
			guint8 r, guint8 g, guint8 b, guint8 a)
{
	gint has_alpha;
	gint pw, ph, prs;
	gint rx, ry, rw, rh;
	gint fx, fy, fw, fh;
	guchar *p_pix;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (!util_clip_region(0, 0, pw, ph,
			      clip_x, clip_y, clip_w, clip_h,
			      &rx, &ry, &rw, &rh)) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	if (util_clip_region(x + border, y + border, w - border * 2, h - border * 2,
			     rx, ry, rw, rh,
			     &fx, &fy, &fw, &fh))
		{
		pixbuf_draw_rect_fill(pb, fx, fy, fw, fh, r, g, b, a);
		}

	if (border < 1) return;

	if (util_clip_region(x, y + border, border, h - border * 2,
			     rx, ry, rw, rh,
			     &fx, &fy, &fw, &fh))
		{
		pixbuf_draw_fade_linear(p_pix, prs, has_alpha,
					x + border, TRUE, border,
					fx, fy, fx + fw, fy + fh,
					r, g, b, a);
		}
	if (util_clip_region(x + w - border, y + border, border, h - border * 2,
			     rx, ry, rw, rh,
			     &fx, &fy, &fw, &fh))
		{
		pixbuf_draw_fade_linear(p_pix, prs, has_alpha,
					x + w - border, TRUE, border,
					fx, fy, fx + fw, fy + fh,
					r, g, b, a);
		}
	if (util_clip_region(x + border, y, w - border * 2, border,
			     rx, ry, rw, rh,
			     &fx, &fy, &fw, &fh))
		{
		pixbuf_draw_fade_linear(p_pix, prs, has_alpha,
					y + border, FALSE, border,
					fx, fy, fx + fw, fy + fh,
					r, g, b, a);
		}
	if (util_clip_region(x + border, y + h - border, w - border * 2, border,
			     rx, ry, rw, rh,
			     &fx, &fy, &fw, &fh))
		{
		pixbuf_draw_fade_linear(p_pix, prs, has_alpha,
					y + h - border, FALSE, border,
					fx, fy, fx + fw, fy + fh,
					r, g, b, a);
		}
	if (util_clip_region(x, y, border, border,
			     rx, ry, rw, rh,
			     &fx, &fy, &fw, &fh))
		{
		pixbuf_draw_fade_radius(p_pix, prs, has_alpha,
					x + border, y + border, border,
					fx, fy, fx + fw, fy + fh,
					r, g, b, a);
		}
	if (util_clip_region(x + w - border, y, border, border,
			     rx, ry, rw, rh,
			     &fx, &fy, &fw, &fh))
		{
		pixbuf_draw_fade_radius(p_pix, prs, has_alpha,
					x + w - border, y + border, border,
					fx, fy, fx + fw, fy + fh,
					r, g, b, a);
		}
	if (util_clip_region(x, y + h - border, border, border,
			     rx, ry, rw, rh,
			     &fx, &fy, &fw, &fh))
		{
		pixbuf_draw_fade_radius(p_pix, prs, has_alpha,
					x + border, y + h - border, border,
					fx, fy, fx + fw, fy + fh,
					r, g, b, a);
		}
	if (util_clip_region(x + w - border, y + h - border, border, border,
			     rx, ry, rw, rh,
			     &fx, &fy, &fw, &fh))
		{
		pixbuf_draw_fade_radius(p_pix, prs, has_alpha,
					x + w - border, y + h - border, border,
					fx, fy, fx + fw, fy + fh,
					r, g, b, a);
		}
}


/*
 *-----------------------------------------------------------------------------
 * pixbuf color alterations
 *-----------------------------------------------------------------------------
 */

void pixbuf_desaturate_rect(GdkPixbuf *pb,
			    gint x, gint y, gint w, gint h)
{
	gboolean has_alpha;
	gint pw, ph, prs;
	guchar *p_pix;
	guchar *pp;
	gint i, j;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (x < 0 || x + w > pw) return;
	if (y < 0 || y + h > ph) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	for (i = 0; i < h; i++)
		{
		pp = p_pix + (y + i) * prs + (x * (has_alpha ? 4 : 3));
		for (j = 0; j < w; j++)
			{
			guint8 grey;

			grey = (pp[0] + pp[1] + pp[2]) / 3;
			*pp = grey;
			pp++;
			*pp = grey;
			pp++;
			*pp = grey;
			pp++;
			if (has_alpha) pp++;
			}
		}
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf highlight under/over exposure *-----------------------------------------------------------------------------
 */
void pixbuf_highlight_overunderexposed(GdkPixbuf *pb, gint x, gint y, gint w, gint h)
{
	gboolean has_alpha;
	gint pw, ph, prs;
	guchar *p_pix;
	guchar *pp;
	gint i, j;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (x < 0 || x + w > pw) return;
	if (y < 0 || y + h > ph) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	for (i = 0; i < h; i++)
		{
		pp = p_pix + (y + i) * prs + (x * (has_alpha ? 4 : 3));
		for (j = 0; j < w; j++)
			{
			if (pp[0] == 255 || pp[1] == 255 || pp[2] == 255 || pp[0] == 0 || pp[1] == 0 || pp[2] == 0)
				{
				*pp = 255;
				pp++;
				*pp = 0;
				pp++;
				*pp = 0;
				pp++;
				if (has_alpha) pp++;
				}
			else
				{
				pp = pp + 3;
				if (has_alpha) pp++;
				}
			}
		}
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf ignore alpha
 *-----------------------------------------------------------------------------
*/
void pixbuf_ignore_alpha_rect(GdkPixbuf *pb,
                 gint x, gint y, gint w, gint h)
{
   gboolean has_alpha;
   gint pw, ph, prs;
   guchar *p_pix;
   guchar *pp;
   gint i, j;

   if (!pb) return;

   pw = gdk_pixbuf_get_width(pb);
   ph = gdk_pixbuf_get_height(pb);

   if (x < 0 || x + w > pw) return;
   if (y < 0 || y + h > ph) return;

   has_alpha = gdk_pixbuf_get_has_alpha(pb);
   if (!has_alpha) return;

   prs = gdk_pixbuf_get_rowstride(pb);
   p_pix = gdk_pixbuf_get_pixels(pb);

   for (i = 0; i < h; i++)
       {
       pp = p_pix + (y + i) * prs + (x * 4 );
       for (j = 0; j < w; j++)
           {
           pp[3] = 0xff;
           pp+=4;
           }
       }
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
