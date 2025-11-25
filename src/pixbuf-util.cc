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

#include "pixbuf-util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

#include <cairo.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <pango/pangocairo.h>

#include <config.h>

#include "compat-deprecated.h"
#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "main-defines.h"
#include "ui-fileops.h"

namespace
{

using GetAlpha = std::function<guint8(gint, gint)>;

struct PixbufInline
{
	const gchar *key;
	const gchar *data;
};

constexpr PixbufInline inline_pixbuf_data[] = {
	{ PIXBUF_INLINE_ARCHIVE,                "gq-icon-archive-file" },
	{ PIXBUF_INLINE_BROKEN,                 "gq-icon-broken" },
	{ PIXBUF_INLINE_COLLECTION,             "gq-icon-collection" },
	{ PIXBUF_INLINE_ICON_180,               "gq-icon-rotate-180" },
	{ PIXBUF_INLINE_ICON_BOOK,              "gq-icon-book" },
	{ PIXBUF_INLINE_ICON_CONFIG,            "gq-icon-config" },
	{ PIXBUF_INLINE_ICON_DRAW_RECTANGLE,    "gq-icon-draw-rectangle" },
	{ PIXBUF_INLINE_ICON_EXIF,              "gq-icon-exif" },
	{ PIXBUF_INLINE_ICON_EXPOSURE,          "gq-icon-exposure" },
	{ PIXBUF_INLINE_ICON_FLOAT,             "gq-icon-float" },
	{ PIXBUF_INLINE_ICON,                   "gqview-icon" },
	{ PIXBUF_INLINE_ICON_GRAYSCALE,         "gq-icon-grayscale" },
	{ PIXBUF_INLINE_ICON_HEIF,              "gq-icon-heic" },
	{ PIXBUF_INLINE_ICON_HIDETOOLS,         "gq-icon-hidetools" },
	{ PIXBUF_INLINE_ICON_MAINTENANCE,       "gq-icon-maintenance" },
	{ PIXBUF_INLINE_ICON_MARKS,             "gq-icon-marks" },
	{ PIXBUF_INLINE_ICON_MOVE,              "gq-icon-move" },
	{ PIXBUF_INLINE_ICON_ORIGINAL,          "gq-icon-original" },
	{ PIXBUF_INLINE_ICON_PANORAMA,          "gq-icon-panorama" },
	{ PIXBUF_INLINE_ICON_PDF,               "gq-icon-pdf" },
	{ PIXBUF_INLINE_ICON_PROPERTIES,        "gq-icon-properties" },
	{ PIXBUF_INLINE_ICON_RENAME,            "gq-icon-rename" },
	{ PIXBUF_INLINE_ICON_SELECT_ALL,        "gq-icon-select-all" },
	{ PIXBUF_INLINE_ICON_SELECT_INVERT,     "gq-icon-select-invert" },
	{ PIXBUF_INLINE_ICON_SELECT_NONE,       "gq-icon-select-none" },
	{ PIXBUF_INLINE_ICON_SELECT_RECTANGLE,  "gq-icon-select-rectangle" },
	{ PIXBUF_INLINE_ICON_SORT,              "gq-icon-sort" },
	{ PIXBUF_INLINE_ICON_THUMB,             "gq-icon-thumb" },
	{ PIXBUF_INLINE_ICON_TOOLS,             "gq-icon-tools" },
	{ PIXBUF_INLINE_ICON_VIEW,              "gq-icon-view" },
	{ PIXBUF_INLINE_ICON_ZOOMFILLHOR,       "gq-icon-zoomfillhor" },
	{ PIXBUF_INLINE_ICON_ZOOMFILLVERT,      "gq-icon-zoomfillvert" },
	{ PIXBUF_INLINE_LOGO,                   "geeqie-logo" },
	{ PIXBUF_INLINE_METADATA,               "gq-icon-metadata" },
	{ PIXBUF_INLINE_SCROLLER,               "gq-scroller" },
	{ PIXBUF_INLINE_SPLIT_PANE_SYNC,        "gq-icon-split-pane-sync" },
	{ PIXBUF_INLINE_UNKNOWN,                "gq-icon-unknown" },
	{ PIXBUF_INLINE_VIDEO,                  "gq-icon-video" },
};

constexpr gint ROTATE_BUFFER_WIDTH = 48;
constexpr gint ROTATE_BUFFER_HEIGHT = 48;

// Intersects the clip region with the pixbuf. r is that intersecting region.
gboolean pixbuf_clip_region(const GdkPixbuf *pb, GdkRectangle clip, GdkRectangle &r)
{
	gint pw = gdk_pixbuf_get_width(pb);
	gint ph = gdk_pixbuf_get_height(pb);
	const GdkRectangle pb_rect{0, 0, pw, ph};

	return gdk_rectangle_intersect(&pb_rect, &clip, &r);
}

/*
 * Fills rectangular region of pixbuf defined by
 * corners `(x1, y1)` and `(x2, y2)` from `rect`
 * with colors red (r), green (g), blue (b)
 * applying alpha (a) from `get_alpha` function.
 */
void pixbuf_draw_rect_fill(guchar *p_pix, gint prs, gboolean has_alpha,
                           GdkRectangle rect,
                           guint8 r, guint8 g, guint8 b,
                           const GetAlpha &get_alpha)
{
	const gint x1 = rect.x;
	const gint y1 = rect.y;
	const gint x2 = rect.x + rect.width;
	const gint y2 = rect.y + rect.height;
	const gint p_step = has_alpha ? 4 : 3;

	for (gint y = y1; y < y2; y++)
		{
		guchar *pp = p_pix + (y * prs) + (x1 * p_step);

		for (gint x = x1; x < x2; x++)
			{
			guint8 a = get_alpha(x, y);

			pp[0] = (r * a + pp[0] * (256-a)) >> 8;
			pp[1] = (g * a + pp[1] * (256-a)) >> 8;
			pp[2] = (b * a + pp[2] * (256-a)) >> 8;
			pp += p_step;
			}
		}
}

} // namespace

/*
 *-----------------------------------------------------------------------------
 * png save
 *-----------------------------------------------------------------------------
 */

gboolean pixbuf_to_file_as_png(GdkPixbuf *pixbuf, const gchar *filename)
{
	gboolean ret;

	if (!pixbuf || !filename) return FALSE;

	g_autoptr(GError) error = nullptr;
	ret = gdk_pixbuf_save(pixbuf, filename, "png", &error,
			      "tEXt::Software", GQ_APPNAME " " VERSION, NULL);

	if (error)
		{
		log_printf("Error saving png file: %s\n", error->message);
		}

	return ret;
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf from inline
 *-----------------------------------------------------------------------------
 */

GdkPixbuf *pixbuf_inline(const gchar *key)
{
	GInputStream *in_stream;

	if (!key) return nullptr;

	GtkSettings *settings = gtk_settings_get_default();
	g_autofree gchar *theme_name = nullptr;
	g_object_get(settings, "gtk-theme-name", &theme_name, nullptr);
	g_autofree gchar *theme_name_lc = g_ascii_strdown(theme_name, -1);
	gboolean dark = g_str_has_suffix(theme_name_lc, "dark");

	const auto it = std::find_if(std::cbegin(inline_pixbuf_data), std::cend(inline_pixbuf_data),
	                             [key](const PixbufInline &pi){ return strcmp(pi.key, key) == 0; });
	if (it == std::cend(inline_pixbuf_data))
		{
		log_printf("warning: inline pixbuf key \"%s\" not found.\n", key);
		return nullptr;
		}

	const auto get_input_stream = [](const gchar *data, gboolean dark, GError **error) -> GInputStream *
	{
		g_autofree gchar *file_name = g_strconcat(data, dark ? "-dark" : "", ".png", nullptr);
		g_autofree gchar *path = g_build_filename(GQ_RESOURCE_PATH_ICONS, file_name, nullptr);
		return g_resources_open_stream(path, G_RESOURCE_LOOKUP_FLAGS_NONE, error);
	};

	g_autoptr(GError) error = nullptr;
	in_stream = get_input_stream(it->data, dark, &error);
	if (error && dark)
		{
		g_error_free(error);
		error = nullptr;
		in_stream = get_input_stream(it->data, FALSE, &error);
		}

	if (error)
		{
		log_printf("warning: inline pixbuf error: %s", error->message);
		g_object_unref(in_stream);
		return nullptr;
		}

	GdkPixbuf *icon_pixbuf = gdk_pixbuf_new_from_stream(in_stream, nullptr, &error);
	g_object_unref(in_stream);

	if (error)
		{
		log_printf("warning: inline pixbuf error: %s", error->message);
		return nullptr;
		}

	return icon_pixbuf;
}

static void register_stock_icon(const gchar *key, GdkPixbuf *pixbuf)
{
	static GtkIconFactory *icon_factory = []()
	{
		GtkIconFactory *icon_factory = gq_gtk_icon_factory_new();
		gq_gtk_icon_factory_add_default(icon_factory);
		return icon_factory;
	}();

	GtkIconSet *icon_set = gq_gtk_icon_set_new_from_pixbuf(pixbuf);
	gq_gtk_icon_factory_add(icon_factory, key, icon_set);
}


void pixbuf_inline_register_stock_icons()
{
	for (const PixbufInline &pi : inline_pixbuf_data)
		{
		g_autoptr(GdkPixbuf) pixbuf = pixbuf_inline(pi.key);
		register_stock_icon(pi.key, pixbuf);
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
			g_autofree gchar *icon2 = remove_extension_from_path(icon);
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
			}
		}

	if (!pixbuf) return FALSE;

	register_stock_icon(key, pixbuf);
	return TRUE;
}

gboolean pixbuf_scale_aspect(gint req_w, gint req_h,
                             gint old_w, gint old_h,
                             gint &new_w, gint &new_h)
{
	auto ratio_w = static_cast<gdouble>(req_w) / old_w;
	auto ratio_h = static_cast<gdouble>(req_h) / old_h;

	if (ratio_w < ratio_h)
		{
		new_w = req_w;
		new_h = std::max<gint>(ratio_w * old_h, 1);
		}
	else
		{
		new_w = std::max<gint>(ratio_h * old_w, 1);
		new_h = req_h;
		}

	return (new_w != old_w || new_h != old_h);
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
			gint nw;
			gint nh;

			if (pixbuf_scale_aspect(requested_width, requested_height,
			                        w, h, nw, nh))
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
 * pixbuf rotation
 *-----------------------------------------------------------------------------
 */

static void pixbuf_copy_block_rotate(guchar *src, gint src_row_stride, gint x, gint y,
				     guchar *dest, gint dest_row_stride, gint w, gint h,
				     gint bytes_per_pixel, gboolean counter_clockwise)
{
	gint i;
	gint j;
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

/*
 * Returns a copy of pixbuf src rotated 90 degrees clockwise or 90 counterclockwise
 *
 */
GdkPixbuf *pixbuf_copy_rotate_90(GdkPixbuf *src, gboolean counter_clockwise)
{
	GdkPixbuf *dest;
	gboolean has_alpha;
	gint sw;
	gint sh;
	gint srs;
	gint dw;
	gint dh;
	gint drs;
	guchar *s_pix;
	guchar *d_pix;
	gint i;
	gint j;
	gint a;
	GdkPixbuf *buffer;
	guchar *b_pix;
	gint brs;
	gint w;
	gint h;

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
		w = std::min(ROTATE_BUFFER_WIDTH, sh - i);
		for (j = 0; j < sw; j += ROTATE_BUFFER_HEIGHT)
			{
			gint x;
			gint y;

			h = std::min(ROTATE_BUFFER_HEIGHT, sw - j);
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
	gint w;
	gint h;
	gint srs;
	gint drs;
	guchar *s_pix;
	guchar *d_pix;
	guchar *sp;
	guchar *dp;
	gint i;
	gint j;
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

/**
 * @brief Composites the fill color with the existing contents of the pixbuf,
 *        within the specified region, with a proportion set by the alpha (`a`)
 *        parameter.
 * @param pb The `GdkPixbuf` to paint into.
 * @param rect The specified region.
 * @param r,g,b Fill color.
 * @param a The alpha to use for compositing. a=255 is solid (fully the new
 *          color).  a=0 is tranparent (fully the original contents).
 */
void pixbuf_draw_rect_fill(GdkPixbuf *pb,
                           GdkRectangle rect,
                           gint r, gint g, gint b, gint a)
{
	gboolean has_alpha;
	gint pw;
	gint ph;
	gint prs;
	guchar *p_pix;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (rect.x < 0 || rect.x + rect.width > pw) return;
	if (rect.y < 0 || rect.y + rect.height > ph) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	const auto get_a = [a](gint, gint){ return a; };

	// TODO(xsdg): Should we do anything about a potential
	// existing alpha value here?

	pixbuf_draw_rect_fill(p_pix, prs, has_alpha, rect, r, g, b, get_a);
}

/**
 * @brief Fills the specified region of the pixbuf with the specified color.
 * @param pb The `GdkPixbuf` to paint into.
 * @param x,y Coordinates of the top-left corner of the first region.
 * @param w,h Extent of the first region.
 * @param r,g,b,a Fill color and alpha.
 */
void pixbuf_set_rect_fill(GdkPixbuf *pb,
			  gint x, gint y, gint w, gint h,
			  gint r, gint g, gint b, gint a)
{
	gboolean has_alpha;
	gint pw;
	gint ph;
	gint prs;
	guchar *p_pix;
	guchar *pp;
	gint i;
	gint j;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (x < 0 || x + w > pw) return;
	if (y < 0 || y + h > ph) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	const gint p_step = has_alpha ? 4 : 3;

	for (i = 0; i < h; i++)
		{
		pp = p_pix + (y + i) * prs + (x * p_step);
		for (j = 0; j < w; j++)
			{
			*pp = r; pp++;
			*pp = g; pp++;
			*pp = b; pp++;
			if (has_alpha) { *pp = a; pp++; }
			}
		}
}

/**
 * @brief Draws a rectangular stroke of the specified stroke width and color
 *        around the specified region of the pixbuf.
 * @param pb The `GdkPixbuf` to paint into.
 * @param x,y Coordinates of the top-left corner of the region.
 * @param w,h Extent of the region.
 * @param r,g,b,a Line color and alpha.
 * @param left_width Stroke width of the left edge of the rectangle.
 * @param right_width Stroke width of the right edge of the rectangle.
 * @param top_width Stroke width of the top edge of the rectangle.
 * @param bottom_width Stroke width of the bottom edge of the rectangle.
 */
void pixbuf_set_rect(GdkPixbuf *pb,
		     gint x, gint y, gint w, gint h,
		     gint r, gint g, gint b, gint a,
		     gint left_width, gint right_width, gint top_width, gint bottom_width)
{
	// TODO(xsdg): This function has multiple off-by-one errors.  Would be
	// much easier to read (and implement correctly) with temporaries to
	// translate from (x, y, w, h) coordinates to (x1, y1, x2, y2).
	pixbuf_set_rect_fill(pb,
			     x + left_width, y, w - left_width - right_width, top_width,
			     r, g, b ,a);
	pixbuf_set_rect_fill(pb,
			     x + w - right_width, y, right_width, h,
			     r, g, b ,a);
	pixbuf_set_rect_fill(pb,
			     x + left_width, y + h - bottom_width, w - left_width - right_width, bottom_width,
			     r, g, b ,a);
	pixbuf_set_rect_fill(pb,
			     x, y, left_width, h,
			     r, g, b ,a);
}

/**
 * @brief Sets the specified pixel of the pixbuf to the specified color.
 * @param pb The `GdkPixbuf` to paint into.
 * @param x,y Coordinates of the pixel to set.
 * @param r,g,b,a Color and alpha.
 */
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
	gint sw;
	gint sh;
	gint srs;
	gboolean s_alpha;
	gint s_step;
	guchar *s_pix;
	gint dw;
	gint dh;
	gint drs;
	gboolean d_alpha;
	gint d_step;
	guchar *d_pix;

	guchar *sp;
	guchar *dp;
	gint i;
	gint j;

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
				dp[0] = (r * asub + dp[0] * (256-asub)) >> 8;
				asub = a * sp[1] / 255;
				dp[1] = (g * asub + dp[1] * (256-asub)) >> 8;
				asub = a * sp[2] / 255;
				dp[2] = (b * asub + dp[2] * (256-asub)) >> 8;

				if (d_alpha) dp[3] = std::max<guchar>(dp[3], a * ((sp[0] + sp[1] + sp[2]) / 3) / 255);
				}

			sp += s_step;
			dp += d_step;
			}
		}
}

void pixbuf_draw_layout(GdkPixbuf *pixbuf, PangoLayout *layout,
                        gint x, gint y,
                        guint8 r, guint8 g, guint8 b, guint8 a)
{
	GdkPixbuf *buffer;
	gint w;
	gint h;
	gint sx;
	gint sy;
	gint dw;
	gint dh;
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

/**
 * @brief Computes the bounding box for the specified triangle.
 * @param[in] c1 Coordinates of the first corner of the triangle.
 * @param[in] c2 Coordinates of the second corner of the triangle.
 * @param[in] c3 Coordinates of the third corner of the triangle.
 * @return The computed bounding box.
 */
GdkRectangle util_triangle_bounding_box(GdkPoint c1, GdkPoint c2, GdkPoint c3)
{
	GdkRectangle bounding_box;

	gint x_max;
	std::tie(bounding_box.x, x_max) = std::minmax({c1.x, c2.x, c3.x});
	bounding_box.width = x_max - bounding_box.x;

	gint y_max;
	std::tie(bounding_box.y, y_max) = std::minmax({c1.y, c2.y, c3.y});
	bounding_box.height = y_max - bounding_box.y;

	return bounding_box;
}

/**
 * @brief Draws a filled triangle of the specified color into the pixbuf, constrained
 *        to the specified clip region.
 * @param pb The `GdkPixbuf` to paint into.
 * @param clip Clipping region.
 * @param c1 Coordinates of the first corner of the triangle.
 * @param c2 Coordinates of the second corner of the triangle.
 * @param c3 Coordinates of the third corner of the triangle.
 * @param r,g,b,a Color and alpha.
 */
void pixbuf_draw_triangle(GdkPixbuf *pb, GdkRectangle clip,
                          GdkPoint c1, GdkPoint c2, GdkPoint c3,
                          guint8 r, guint8 g, guint8 b, guint8 a)
{
	gboolean has_alpha;
	gint prs;
	gint fx2;
	gint fy2;
	guchar *p_pix;
	guchar *pp;
	gint p_step;
	gboolean middle = FALSE;

	if (!pb) return;

	GdkRectangle pb_rect;
	if (!pixbuf_clip_region(pb, clip, pb_rect)) return;

	// Determine the bounding box for the triangle.
	GdkRectangle tri_rect = util_triangle_bounding_box(c1, c2, c3);

	// And now clip the triangle bounding box to the pixbuf clipping region.
	GdkRectangle f;
	if (!gdk_rectangle_intersect(&pb_rect, &tri_rect, &f)) return;

	fx2 = f.x + f.width;
	fy2 = f.y + f.height;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	p_step = (has_alpha) ? 4 : 3;

	// Ensure that points are ordered by increasing y coordinate.
	std::vector<GdkPoint> v{c1, c2, c3};
	std::sort(v.begin(), v.end(), [](const GdkPoint &l, const GdkPoint &r){ return l.y < r.y; });

	const auto get_slope = [](GdkPoint start, GdkPoint end)
	{
		gdouble slope = end.y - start.y;
		if (slope) slope = static_cast<gdouble>(end.x - start.x) / slope;
		return slope;
	};

	gdouble slope1 = get_slope(v[0], v[1]);
	GdkPoint slope1_start = v[0];
	const gdouble slope2 = get_slope(v[0], v[2]);
	const GdkPoint &slope2_start = v[0];

	for (gint y = f.y; y < fy2; y++)
		{
		if (!middle && y > v[1].y)
			{
			slope1 = get_slope(v[1], v[2]);
			slope1_start = v[1];

			middle = TRUE;
			}

		gint x1 = slope1_start.x + (slope1 * (y - slope1_start.y) + 0.5);
		gint x2 = slope2_start.x + (slope2 * (y - slope2_start.y) + 0.5);

		if (x1 > x2)
			{
			std::swap(x1, x2);
			}

		x1 = std::clamp(x1, f.x, fx2);
		x2 = std::clamp(x2, f.x, fx2);

		pp = p_pix + y * prs + x1 * p_step;

		while (x1 < x2)
			{
			pp[0] = (r * a + pp[0] * (256-a)) >> 8;
			pp[1] = (g * a + pp[1] * (256-a)) >> 8;
			pp[2] = (b * a + pp[2] * (256-a)) >> 8;
			pp += p_step;

			x1++;
			}
		}
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf drawing (line)
 *-----------------------------------------------------------------------------
 */

/**
 * @brief Clips the specified line segment to the specified clipping region.
 * @param[in] clip_x,clip_y Coordinates of the top-left corner of the clipping region.
 * @param[in] clip_w,clip_h Extent of the clipping region.
 * @param[in] x1,y1 Coordinates of the first point of the line segment.
 * @param[in] x2,y2 Coordinates of the second point of the line segment.
 * @param[out] rx1,ry1 Computed coordinates of the first point of the clipped line segment.
 * @param[out] rx2,ry2 Computed coordinates of the second point of the clipped line segment.
 * @retval FALSE The line segment lies outside of the clipping region.
 * @retval TRUE The clip operation was performed, and the output params were set.
 */
static gboolean util_clip_line(gdouble clip_x, gdouble clip_y, gdouble clip_w, gdouble clip_h,
                               gdouble x1, gdouble y1, gdouble x2, gdouble y2,
                               gdouble &rx1, gdouble &ry1, gdouble &rx2, gdouble &ry2)
{
	gboolean flip = FALSE;
	gdouble d;

	// Normalize: Line endpoint 1 must be farther left.
	if (x1 > x2)
		{
		std::swap(x1, x2);
		std::swap(y1, y2);
		flip = TRUE;
		}

	// Ensure the line horizontally overlaps with the clip region.
	if (x2 < clip_x || x1 > clip_x + clip_w) return FALSE;

	// Ensure the line vertically overlaps with the clip region.
	// Note that a line can both horizontally and vertically overlap with
	// clipping region, while still being outside of the clipping region.  That
	// case is detected further below.
	if (y1 < y2)
		{
		if (y2 < clip_y || y1 > clip_y + clip_h) return FALSE;
		}
	else
		{
		if (y1 < clip_y || y2 > clip_y + clip_h) return FALSE;
		}

	d = x2 - x1;
	// TODO(xsdg): Either use ints here, or define a reasonable epsilon to do the
	// right thing if -epsilon < d < 0.  We already guaranteed above that x2 >= x1.
	if (d > 0.0)
		{
		gdouble slope;

		slope = (y2 - y1) / d;
		// If needed, project (x1, y1) to be horizontally within the clip
		// region, while maintaining the line's slope and y-offset.
		if (x1 < clip_x)
			{
			y1 = y1 + slope * (clip_x - x1);
			x1 = clip_x;
			}
		// Likewise with (x2, y2).
		if (x2 > clip_x + clip_w)
			{
			y2 = y2 + slope * (clip_x + clip_w - x2);
			x2 = clip_x + clip_w;
			}
		}

	// Check that any horizontal projections didn't cause the line segment to
	// no longer vertically overlap with the clip region.
	if (y1 < y2)
		{
		if (y2 < clip_y || y1 > clip_y + clip_h) return FALSE;
		}
	else
		{
		if (y1 < clip_y || y2 > clip_y + clip_h) return FALSE;

		// Re-normalize: line endpoint 1 must be farther up.
		std::swap(x1, x2);
		std::swap(y1, y2);
		flip = !flip;
		}

	d = y2 - y1;
	if (d > 0.0)
		{
		gdouble slope;

		slope = (x2 - x1) / d;
		// If needed, project (x1, y1) to be vertically within the clip
		// region, while maintaining the line's slope and x-offset.
		if (y1 < clip_y)
			{
			x1 = x1 + slope * (clip_y - y1);
			y1 = clip_y;
			}
		// Likewise with (x2, y2).
		if (y2 > clip_y + clip_h)
			{
			x2 = x2 + slope * (clip_y + clip_h - y2);
			y2 = clip_y + clip_h;
			}
		}

	// Set the output params, accounting for any flips that might have
	// happened during normalization.
	if (flip)
		{
		rx1 = x2;
		ry1 = y2;
		rx2 = x1;
		ry2 = y1;
		}
	else
		{
		rx1 = x1;
		ry1 = y1;
		rx2 = x2;
		ry2 = y2;
		}

	return TRUE;
}

/**
 * @brief Draws the sub-segment of the specified line segment that lies within the
 *        clip region into the pixbuf.
 * @param pb The `GdkPixbuf` to paint into.
 * @param clip Clipping region.
 * @param x1,y1 Coordinates of the first point of the line segment.
 * @param x2,y2 Coordinates of the second point of the line segment.
 * @param r,g,b,a Color and alpha.
 */
void pixbuf_draw_line(GdkPixbuf *pb, GdkRectangle clip,
                      gint x1, gint y1, gint x2, gint y2,
                      guint8 r, guint8 g, guint8 b, guint8 a)
{
	gboolean has_alpha;
	gint prs;
	gdouble rx1;
	gdouble ry1;
	gdouble rx2;
	gdouble ry2;
	guchar *p_pix;
	gint p_step;
	gdouble slope;
	gdouble x;
	gdouble y;
	gint px;
	gint py;

	if (!pb) return;

	GdkRectangle pb_rect;
	if (!pixbuf_clip_region(pb, clip, pb_rect)) return;

	// Clips the specified line segment to the intersecting region from above.
	if (!util_clip_line(pb_rect.x, pb_rect.y, pb_rect.width, pb_rect.height,
	                    x1, y1, x2, y2,
	                    rx1, ry1, rx2, ry2)) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	p_step = (has_alpha) ? 4 : 3;

	const auto fill_pixel = [pb_rect, p_pix, prs, p_step, r, g, b, a](gint x, gint y)
	{
		if (x < pb_rect.x || x >= pb_rect.x + pb_rect.width ||
		    y < pb_rect.y || y >= pb_rect.y + pb_rect.height) return;

		guchar *pp = p_pix + (y * prs) + (x * p_step);
		pp[0] = (r * a + pp[0] * (256-a)) >> 8;
		pp[1] = (g * a + pp[1] * (256-a)) >> 8;
		pp[2] = (b * a + pp[2] * (256-a)) >> 8;
	};

	// We draw the clipped line segment along the longer axis first, and
	// allow the shorter axis to follow.  This is because our raster line segment
	// will contain max(rx2-rx1, ry2-ry1) pixels, and the pixels along the
	// shorter axis may not advance for each cycle (the line is not anti-aliased).
	if (fabs(rx2 - rx1) > fabs(ry2 - ry1))
		{
		if (rx1 > rx2)
			{
			std::swap(rx1, rx2);
			std::swap(ry1, ry2);
			}

		slope = rx2 - rx1;
		if (slope != 0.0) slope = (ry2 - ry1) / slope;
		for (x = rx1; x < rx2; x += 1.0)
			{
			px = static_cast<gint>(x + 0.5);
			py = static_cast<gint>(ry1 + ((x - rx1) * slope) + 0.5);

			fill_pixel(px, py);
			}
		}
	else
		{
		if (ry1 > ry2)
			{
			std::swap(rx1, rx2);
			std::swap(ry1, ry2);
			}

		slope = ry2 - ry1;
		if (slope != 0.0) slope = (rx2 - rx1) / slope;
		for (y = ry1; y < ry2; y += 1.0)
			{
			px = static_cast<gint>(rx1 + ((y - ry1) * slope) + 0.5);
			py = static_cast<gint>(y + 0.5);

			fill_pixel(px, py);
			}
		}
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf drawing (fades and shadows)
 *-----------------------------------------------------------------------------
 */

/**
 * @brief Composites a horizontal or vertical linear gradient into the rectangular
 *        region `fade_rect`.
 * @param p_pix The pixel buffer to paint into.
 * @param prs The pixel row stride (how many pixels per row of the buffer).
 * @param has_alpha TRUE if the p_pix representation is rgba.  FALSE if just rgb.
 * @param s The "center" of the gradient, along the axis defined by `vertical`.
 *          Note that if the center is not along an edge, the gradient will be
 *          symmetric about the center.
 * @param vertical When `TRUE`, the gradient color will vary vertically.  When `FALSE`,
 *                 horizontally.
 * @param border The maximum extent of the gradient, in pixels.
 * @param fade_rect The region.
 * @param r,g,b Base color of the gradient.
 * @param a The peak alpha value when compositing the gradient.  The alpha varies
 *          from this value down to 0 (fully transparent).  Note that any alpha
 *          value associated with the original pixel is unmodified.
 */
static void pixbuf_draw_fade_linear(guchar *p_pix, gint prs, gboolean has_alpha,
                                    gint s, gboolean vertical, gint border,
                                    GdkRectangle fade_rect,
                                    guint8 r, guint8 g, guint8 b, guint8 a)
{
	const auto get_a = [s, vertical, border, a](gint x, gint y)
	{
		gint coord = vertical ? x : y;
		gint distance = std::min(border, abs(coord - s));
		return a - (a * distance / border);
	};

	pixbuf_draw_rect_fill(p_pix, prs, has_alpha, fade_rect, r, g, b, get_a);
}

/**
 * @brief Composites a radial gradient into the rectangular region `fade_rect`.
 * @param p_pix The pixel buffer to paint into.
 * @param prs The pixel row stride (how many pixels per row of the buffer).
 * @param has_alpha TRUE if the p_pix representation is rgba.  FALSE if just rgb.
 * @param sx,sy The coordinates of the center of the gradient.
 * @param border The max radius, in pixels, of the gradient.  Pixels farther away
 *               from the center than this will be unaffected.
 * @param fade_rect The region.
 * @param r,g,b Base color of the gradient.
 * @param a The peak alpha value when compositing the gradient.  The alpha varies
 *          from this value down to 0 (fully transparent).  Note that any alpha
 *          value associated with the original pixel is unmodified.
 */
static void pixbuf_draw_fade_radius(guchar *p_pix, gint prs, gboolean has_alpha,
                                    gint sx, gint sy, gint border,
                                    GdkRectangle fade_rect,
                                    guint8 r, guint8 g, guint8 b, guint8 a)
{
	const auto get_a = [sx, sy, border, a](gint x, gint y)
	{
		gint radius = std::min(border, static_cast<gint>(hypot(x - sx, y - sy)));
		return a - (a * radius / border);
	};

	pixbuf_draw_rect_fill(p_pix, prs, has_alpha, fade_rect, r, g, b, get_a);
}

/**
 * @brief Composites a "shaded" region of the specified color and with the
 *        specified size and border gradient width into the clip region of the
 *        specified pixbuf.
 * @param pb The `GdkPixbuf` to paint into.
 * @param clip Clipping region.
 * @param x,y Coordinates of the top-left corner of the shaded region.
 * @param w,h Extent of the shaded region.
 * @param border The thickness, in pixels, of the gradient border around the
 *        fully-shaded region.
 * @param r,g,b Shadow base color.
 * @param a The max shadow composition fraction.  Note that any alpha value of the
 *          original pixel will remain untouched.
 */
void pixbuf_draw_shadow(GdkPixbuf *pb, GdkRectangle clip,
                        gint x, gint y, gint w, gint h, gint border,
                        guint8 r, guint8 g, guint8 b, guint8 a)
{
	gint has_alpha;
	gint prs;
	guchar *p_pix;

	if (!pb) return;

	GdkRectangle pb_rect;
	if (!pixbuf_clip_region(pb, clip, pb_rect)) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	// Composites the specified color into the rectangle specified by x, y, w, h,
	// as contracted by `border` pixels, with a composition fraction that's defined
	// by the supplied `a` parameter.
	const GdkRectangle contracted_rect{x + border, y + border, w - (border * 2), h - (border * 2)};
	GdkRectangle f;
	if (gdk_rectangle_intersect(&contracted_rect, &pb_rect, &f))
		{
		pixbuf_draw_rect_fill(pb, f, r, g, b, a);
		}

	if (border < 1) return;

	// Draws linear gradients along each of the 4 edges.
	const auto draw_fade_linear_if_intersect = [&pb_rect, p_pix, prs, has_alpha, border, r, g, b, a](GdkRectangle rect, gint s, gboolean vertical)
	{
		GdkRectangle fade_rect;
		if (!gdk_rectangle_intersect(&rect, &pb_rect, &fade_rect)) return;

		pixbuf_draw_fade_linear(p_pix, prs, has_alpha,
		                        s, vertical, border,
		                        fade_rect,
		                        r, g, b, a);
	};

	draw_fade_linear_if_intersect({x, y + border, border, h - (border * 2)}, x + border, TRUE);
	draw_fade_linear_if_intersect({x + w - border, y + border, border, h - (border * 2)}, x + w - border, TRUE);
	draw_fade_linear_if_intersect({x + border, y, w - (border * 2), border}, y + border, FALSE);
	draw_fade_linear_if_intersect({x + border, y + h - border, w - (border * 2), border}, y + h - border, FALSE);

	// Draws radial gradients at each of the 4 corners.
	const auto draw_fade_radius_if_intersect = [&pb_rect, p_pix, prs, has_alpha, border, r, g, b, a](GdkRectangle rect, gint sx, gint sy)
	{
		GdkRectangle fade_rect;
		if (!gdk_rectangle_intersect(&rect, &pb_rect, &fade_rect)) return;

		pixbuf_draw_fade_radius(p_pix, prs, has_alpha,
		                        sx, sy, border,
		                        fade_rect,
		                        r, g, b, a);
	};

	draw_fade_radius_if_intersect({x, y, border, border}, x + border, y + border);
	draw_fade_radius_if_intersect({x + w - border, y, border, border}, x + w - border, y + border);
	draw_fade_radius_if_intersect({x, y + h - border, border, border}, x + border, y + h - border);
	draw_fade_radius_if_intersect({x + w - border, y + h - border, border, border}, x + w - border, y + h - border);
}


/*
 *-----------------------------------------------------------------------------
 * pixbuf color alterations
 *-----------------------------------------------------------------------------
 */

/**
 * @brief Sets the r, g, and b values for each pixel within the specified region
 *        of the pixbuf the average of the original values for that pixel.
 * @param pb The `GdkPixbuf` to paint into.
 * @param x,y Coordinates of the top-left corner of the region.
 * @param w,h Extent of the region.
 */
void pixbuf_desaturate_rect(GdkPixbuf *pb,
			    gint x, gint y, gint w, gint h)
{
	gboolean has_alpha;
	gint pw;
	gint ph;
	gint prs;
	guchar *p_pix;
	guchar *pp;
	gint i;
	gint j;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (x < 0 || x + w > pw) return;
	if (y < 0 || y + h > ph) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	const gint p_step = has_alpha ? 4 : 3;

	for (i = 0; i < h; i++)
		{
		pp = p_pix + (y + i) * prs + (x * p_step);
		for (j = 0; j < w; j++)
			{
			guint8 grey;

			grey = (pp[0] + pp[1] + pp[2]) / 3;
			pp[0] = grey;
			pp[1] = grey;
			pp[2] = grey;
			pp += p_step;
			}
		}
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf highlight under/over exposure
 *-----------------------------------------------------------------------------
 */

/**
 * @brief Sets each full-black `(0, 0, 0)` or full-white `(255, 255, 255)` pixel in the
 *        specified pixbuf region to full-red `(255, 0, 0)`.  Does not change alpha.
 * @param pb The `GdkPixbuf` to paint into.
 * @param x,y Coordinates of the top-left corner of the first region.
 * @param w,h Extent of the first region.
 */
 void pixbuf_highlight_overunderexposed(GdkPixbuf *pb, gint x, gint y, gint w, gint h)
{
	gboolean has_alpha;
	gint pw;
	gint ph;
	gint prs;
	guchar *p_pix;
	guchar *pp;
	gint i;
	gint j;

	if (!pb) return;

	pw = gdk_pixbuf_get_width(pb);
	ph = gdk_pixbuf_get_height(pb);

	if (x < 0 || x + w > pw) return;
	if (y < 0 || y + h > ph) return;

	has_alpha = gdk_pixbuf_get_has_alpha(pb);
	prs = gdk_pixbuf_get_rowstride(pb);
	p_pix = gdk_pixbuf_get_pixels(pb);

	const gint p_step = has_alpha ? 4 : 3;

	for (i = 0; i < h; i++)
		{
		pp = p_pix + (y + i) * prs + (x * p_step);
		for (j = 0; j < w; j++)
			{
			if (pp[0] == 255 || pp[1] == 255 || pp[2] == 255 || pp[0] == 0 || pp[1] == 0 || pp[2] == 0)
				{
				pp[0] = 255;
				pp[1] = 0;
				pp[2] = 0;
				}
			pp += p_step;
			}
		}
}

/*
 *-----------------------------------------------------------------------------
 * pixbuf ignore alpha
 *-----------------------------------------------------------------------------
*/

/**
 * @brief Sets the alpha channel to 255 (fully opaque) for every pixel in the specified
 *        pixbuf region.
 * @param pb The `GdkPixbuf` to paint into.
 * @param x,y Coordinates of the top-left corner of the first region.
 * @param w,h Extent of the first region.
 */
void pixbuf_ignore_alpha_rect(GdkPixbuf *pb,
			      gint x, gint y, gint w, gint h)
{
   gboolean has_alpha;
   gint pw;
   gint ph;
   gint prs;
   guchar *p_pix;
   guchar *pp;
   gint i;
   gint j;

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
