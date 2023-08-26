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

#ifndef PIXBUF_UTIL_H
#define PIXBUF_UTIL_H

struct FileData;

gboolean pixbuf_to_file_as_png (GdkPixbuf *pixbuf, const gchar *filename);

void pixbuf_inline_register_stock_icons();
gboolean register_theme_icon_as_stock(const gchar *key, const gchar *icon);

GdkPixbuf *pixbuf_inline(const gchar *key);
GdkPixbuf *pixbuf_fallback(FileData *fd, gint requested_width, gint requested_height);

gboolean pixbuf_scale_aspect(gint req_w, gint req_h, gint old_w, gint old_h, gint *new_w, gint *new_h);

#define PIXBUF_INLINE_ARCHIVE		"gq-archive-file"
#define PIXBUF_INLINE_BROKEN		"gq-sheet-broken"
#define PIXBUF_INLINE_COLLECTION	"gq-collection"
#define PIXBUF_INLINE_ICON		"gqview-icon"
#define PIXBUF_INLINE_ICON_180	"gq-icon-rotate-180"
#define PIXBUF_INLINE_ICON_BOOK		"gq-icon-book"
#define PIXBUF_INLINE_ICON_CONFIG	"gq-icon-config"
#define PIXBUF_INLINE_ICON_DRAW_RECTANGLE	"gq-icon-draw-rectangle"
#define PIXBUF_INLINE_ICON_EXIF		"gq-icon-exif"
#define PIXBUF_INLINE_ICON_EXPOSURE		"gq-icon-exposure"
#define PIXBUF_INLINE_ICON_FILE_FILTER	"gq-icon-file-filter"
#define PIXBUF_INLINE_ICON_FLOAT	"gq-icon-float"
#define PIXBUF_INLINE_ICON_GRAYSCALE	"gq-icon-grayscale"
#define PIXBUF_INLINE_ICON_HEIF		"gq-icon-heic"
#define PIXBUF_INLINE_ICON_HIDETOOLS	"gq-icon-hidetools"
#define PIXBUF_INLINE_ICON_MAINTENANCE	"gq-icon-maintenance"
#define PIXBUF_INLINE_ICON_MARKS	"gq-icon-marks"
#define PIXBUF_INLINE_ICON_MOVE		"gq-icon-move"
#define PIXBUF_INLINE_ICON_ORIGINAL	"gq-icon-original"
#define PIXBUF_INLINE_ICON_PANORAMA		"gq-icon-panorama"
#define PIXBUF_INLINE_ICON_PDF		"gq-icon-pdf"
#define PIXBUF_INLINE_ICON_RENAME	"gq-icon-rename"
#define PIXBUF_INLINE_ICON_SELECT_ALL	"gq-icon-select-all"
#define PIXBUF_INLINE_ICON_SELECT_INVERT	"gq-icon-select-invert"
#define PIXBUF_INLINE_ICON_SELECT_NONE	"gq-icon-select-none"
#define PIXBUF_INLINE_ICON_SELECT_RECTANGLE	"gq-icon-select-rectangle"
#define PIXBUF_INLINE_ICON_SORT		"gq-icon-sort"
#define PIXBUF_INLINE_ICON_THUMB	"gq-icon-thumb"
#define PIXBUF_INLINE_ICON_TOOLS	"gq-icon-tools"
#define PIXBUF_INLINE_ICON_TRASH	"gq-icon-trash"
#define PIXBUF_INLINE_ICON_VIEW		"gq-icon-view"
#define PIXBUF_INLINE_ICON_ZOOMFILLHOR	"gq-icon-zoomfillhor"
#define PIXBUF_INLINE_ICON_ZOOMFILLVERT	"gq-icon-zoomfillvert"
#define PIXBUF_INLINE_LOGO		"geeqie-logo"
#define PIXBUF_INLINE_METADATA		"gq-sheet-metadata"
#define PIXBUF_INLINE_SCROLLER		"gq-scroller"
#define PIXBUF_INLINE_SPLIT_PANE_SYNC	"gq-icon-split-pane-sync"
#define PIXBUF_INLINE_UNKNOWN		"gq-sheet-unknown"
#define PIXBUF_INLINE_VIDEO			"gq-sheet-video"

GdkPixbuf *pixbuf_copy_rotate_90(GdkPixbuf *src, gboolean counter_clockwise);
GdkPixbuf *pixbuf_copy_mirror(GdkPixbuf *src, gboolean mirror, gboolean flip);
GdkPixbuf* pixbuf_apply_orientation(GdkPixbuf *pixbuf, gint orientation);

void pixbuf_draw_rect_fill(GdkPixbuf *pb,
			   gint x, gint y, gint w, gint h,
			   gint r, gint g, gint b, gint a);

void pixbuf_set_rect_fill(GdkPixbuf *pb,
			  gint x, gint y, gint w, gint h,
			  gint r, gint g, gint b, gint a);

void pixbuf_set_rect(GdkPixbuf *pb,
		     gint x, gint y, gint w, gint h,
		     gint r, gint g, gint b, gint a,
		     gint left, gint right, gint top, gint bottom);

void pixbuf_pixel_set(GdkPixbuf *pb, gint x, gint y, gint r, gint g, gint b, gint a);


void pixbuf_draw_layout(GdkPixbuf *pixbuf, PangoLayout *layout, GtkWidget *widget,
			gint x, gint y,
			guint8 r, guint8 g, guint8 b, guint8 a);


void pixbuf_draw_triangle(GdkPixbuf *pb,
			  gint clip_x, gint clip_y, gint clip_w, gint clip_h,
			  gint x1, gint y1, gint x2, gint y2, gint x3, gint y3,
			  guint8 r, guint8 g, guint8 b, guint8 a);

void pixbuf_draw_line(GdkPixbuf *pb,
		      gint clip_x, gint clip_y, gint clip_w, gint clip_h,
		      gint x1, gint y1, gint x2, gint y2,
		      guint8 r, guint8 g, guint8 b, guint8 a);

void pixbuf_draw_shadow(GdkPixbuf *pb,
			gint clip_x, gint clip_y, gint clip_w, gint clip_h,
			gint x, gint y, gint w, gint h, gint border,
			guint8 r, guint8 g, guint8 b, guint8 a);

void pixbuf_desaturate_rect(GdkPixbuf *pb,
			    gint x, gint y, gint w, gint h);
void pixbuf_highlight_overunderexposed(GdkPixbuf *pb,
			    gint x, gint y, gint w, gint h);
void pixbuf_ignore_alpha_rect(GdkPixbuf *pb,
                 gint x, gint y, gint w, gint h);

/* clipping utils */

gboolean util_clip_region(gint x, gint y, gint w, gint h,
		          gint clip_x, gint clip_y, gint clip_w, gint clip_h,
		          gint *rx, gint *ry, gint *rw, gint *rh);
void util_clip_triangle(gint x1, gint y1, gint x2, gint y2, gint x3, gint y3,
			gint *rx, gint *ry, gint *rw, gint *rh);


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
