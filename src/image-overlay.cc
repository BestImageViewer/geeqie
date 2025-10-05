/*
 * Copyright (C) 2006 John Ellis
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

#include "image-overlay.h"

#include <algorithm>
#include <cstring>
#include <string>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <pango/pango.h>

#include "collect.h"
#include "filedata.h"
#include "histogram.h"
#include "image-load.h"
#include "image.h"
#include "img-view.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "options.h"
#include "osd.h"
#include "pixbuf-renderer.h"
#include "pixbuf-util.h"
#include "slideshow.h"
#include "typedefs.h"
#include "ui-fileops.h"

struct HistMap;

/*
 *----------------------------------------------------------------------------
 * image overlay
 *----------------------------------------------------------------------------
 */

namespace
{

struct OverlayStateData {
	ImageWindow *imd;
	ImageState changed_states;
	NotifyType notify;

	Histogram histogram;

	OsdShowFlags show;
	OverlayRendererFlags origin;

	gint ovl_info;

	gint x;
	gint y;

	gint icon_time[IMAGE_OSD_COUNT];
	gint icon_id[IMAGE_OSD_COUNT];

	guint idle_id; /* event source id */
	guint timer_id; /* event source id */
	gulong destroy_id;
};

struct OSDIcon {
	gboolean reset;	/* reset on new image */
	gint x;		/* x, y offset */
	gint y;
	gchar *key;	/* inline pixbuf */
};

const OSDIcon osd_icons[] = {
	{  TRUE,   0,   0, nullptr },			/* none */
	{  TRUE, -10, -10, nullptr },			/* auto rotated */
	{  TRUE, -10, -10, nullptr },			/* user rotated */
	{  TRUE, -40, -10, nullptr },			/* color embedded */
	{  TRUE, -70, -10, nullptr },			/* first image */
	{  TRUE, -70, -10, nullptr },			/* last image */
	{ FALSE, -70, -10, nullptr },			/* osd enabled */
	{ FALSE, 0, 0, nullptr }
};

constexpr gint HISTOGRAM_WIDTH = 256;
constexpr gint HISTOGRAM_HEIGHT = 140;

constexpr gint IMAGE_OSD_DEFAULT_DURATION = 30;

} // namespace

static void image_osd_timer_schedule(OverlayStateData *osd);

static OverlayStateData *image_get_osd_data(ImageWindow *imd)
{
	OverlayStateData *osd;

	if (!imd) return nullptr;

	g_assert(imd->pr);

	osd = static_cast<OverlayStateData *>(g_object_get_data(G_OBJECT(imd->pr), "IMAGE_OVERLAY_DATA"));
	return osd;
}

static void image_set_osd_data(ImageWindow *imd, OverlayStateData *osd)
{
	g_assert(imd);
	g_assert(imd->pr);
	g_object_set_data(G_OBJECT(imd->pr), "IMAGE_OVERLAY_DATA", osd);
}

/*
 *----------------------------------------------------------------------------
 * image histogram
 *----------------------------------------------------------------------------
 */


void image_osd_histogram_toggle_channel(ImageWindow *imd)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd) return;

	osd->histogram.toggle_channel();
	image_osd_update(imd);
}

void image_osd_histogram_toggle_mode(ImageWindow *imd)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd) return;

	osd->histogram.toggle_mode();
	image_osd_update(imd);
}

void image_osd_histogram_set_channel(ImageWindow *imd, gint chan)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd) return;

	osd->histogram.set_channel(chan);
	image_osd_update(imd);
}

void image_osd_histogram_set_mode(ImageWindow *imd, gint mode)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd) return;

	osd->histogram.set_mode(mode);
	image_osd_update(imd);
}

gint image_osd_histogram_get_channel(ImageWindow *imd)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd) return HCHAN_DEFAULT;

	return osd->histogram.get_channel();
}

gint image_osd_histogram_get_mode(ImageWindow *imd)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd) return HMODE_LINEAR;

	return osd->histogram.get_mode();
}

void image_osd_toggle(ImageWindow *imd)
{
	OsdShowFlags show;
	if (!imd) return;

	show = image_osd_get(imd);
	if (show == OSD_SHOW_NOTHING)
		{
		image_osd_set(imd, static_cast<OsdShowFlags>(OSD_SHOW_INFO | OSD_SHOW_STATUS));
		return;
		}

	if (show & OSD_SHOW_HISTOGRAM)
		{
		image_osd_set(imd, OSD_SHOW_NOTHING);
		}
	else
		{
		image_osd_set(imd, static_cast<OsdShowFlags>(show | OSD_SHOW_HISTOGRAM));
		}
}

static GdkPixbuf *image_osd_info_render(OverlayStateData *osd)
{
	GdkPixbuf *pixbuf = nullptr;
	gint width;
	gint height;
	PangoLayout *layout;
	const gchar *name;
	gboolean with_hist;
	const HistMap *histmap = nullptr;
	ImageWindow *imd = osd->imd;
	FileData *fd = image_get_fd(imd);
	PangoFontDescription *font_desc;

	if (!fd) return nullptr;

	g_autofree gchar *text = nullptr;
	name = image_get_name(imd);
	if (name)
		{
		gint n;
		gint t;
		CollectionData *cd;
		CollectInfo *info;
		OsdTemplate vars;

		cd = image_get_collection(imd, &info);
		if (cd)
			{
			t = g_list_length(cd->list);
			n = g_list_index(cd->list, info) + 1;
			if (cd->name)
				{
				if (file_extension_match(cd->name, GQ_COLLECTION_EXT))
					{
					g_autofree gchar *collection_str = remove_extension_from_path(cd->name);
					osd_template_insert(vars, "collection", collection_str);
					}
				else
					{
					osd_template_insert(vars, "collection", cd->name);
					}
				}
			else
				{
				osd_template_insert(vars, "collection", _("Untitled"));
				}
			}
		else
			{
			LayoutWindow *lw = layout_find_by_image(imd);
			if (lw)
				{
				if (lw->slideshow)
					{
					lw->slideshow->get_index_and_total(n, t);
					}
				else
					{
					n = layout_list_get_index(lw, image_get_fd(lw->image)) + 1;
					t = layout_list_count(lw);
					}
				}
			else if (!view_window_find_image(imd, n, t))
				{
				n = 1;
				t = 1;
				}

			n = std::max(n, 1);
			t = std::max(t, 1);

			osd_template_insert(vars, "collection", nullptr);
			}

		osd_template_insert(vars, "number", std::to_string(n).c_str());
		osd_template_insert(vars, "total", std::to_string(t).c_str());
		osd_template_insert(vars, "name", name);
		osd_template_insert(vars, "path", image_get_path(imd));
		osd_template_insert(vars, "date", imd->image_fd ? text_from_time(imd->image_fd->date) : "");

		g_autofree gchar *size_str = imd->image_fd ? text_from_size_abrev(imd->image_fd->size) : nullptr;
		osd_template_insert(vars, "size", size_str);

		g_autofree gchar *zoom_str = image_zoom_get_as_text(imd);
		osd_template_insert(vars, "zoom", zoom_str);

		if (!imd->unknown)
			{
			gint w;
			gint h;
			GdkPixbuf *load_pixbuf = image_loader_get_pixbuf(imd->il);

			if (imd->delay_flip &&
			    imd->il && load_pixbuf &&
			    image_get_pixbuf(imd) != load_pixbuf)
				{
				w = gdk_pixbuf_get_width(load_pixbuf);
				h = gdk_pixbuf_get_height(load_pixbuf);
				}
			else
				{
				image_get_image_size(imd, &w, &h);
				}


			osd_template_insert(vars, "width", std::to_string(w).c_str());
			osd_template_insert(vars, "height", std::to_string(h).c_str());

			g_autofree gchar *res_str = g_strdup_printf("%d × %d", w, h);
			osd_template_insert(vars, "res", res_str);
	 		}
		else
			{
			osd_template_insert(vars, "width", nullptr);
			osd_template_insert(vars, "height", nullptr);
			osd_template_insert(vars, "res", nullptr);
			}

		text = image_osd_mkinfo(options->image_overlay.template_string, imd->image_fd, vars);
	} else {
		/* When does this occur ?? */
		text = g_markup_escape_text(_("Untitled"), -1);
	}

	with_hist = (osd->show & OSD_SHOW_HISTOGRAM);
	if (with_hist)
		{
		histmap = histmap_get(imd->image_fd);
		if (!histmap)
			{
			histmap_start_idle(imd->image_fd);
			with_hist = FALSE;
			}
		}


	{
		gint active_marks = 0;
		gint mark;

		for (mark = 0; mark < FILEDATA_MARKS_SIZE; mark++)
			{
			active_marks += file_data_get_mark(fd, mark);
			}

		if (active_marks > 0)
			{
			GString *buf = g_string_sized_new(strlen(text) + 1 + (FILEDATA_MARKS_SIZE * 2));

			if (*text)
				{
				g_string_append_printf(buf, "%s\n", text);
				}

			for (mark = 0; mark < FILEDATA_MARKS_SIZE; mark++)
				{
				g_string_append_printf(buf, file_data_get_mark(fd, mark) ? " <span background='#FF00FF'>%c</span>" : " %c", '1' + (mark < 9 ? mark : -1) );
				}

			g_free(text);
			text = g_string_free(buf, FALSE);
			}

		if (with_hist)
			{
			g_autofree gchar *escaped_histogram_label = g_markup_escape_text(osd->histogram.label(), -1);
			g_autofree gchar *text2 = nullptr;
			if (*text)
				text2 = g_strdup_printf("%s\n%s", text, escaped_histogram_label);
			else
				text2 = g_steal_pointer(&escaped_histogram_label);
			std::swap(text, text2);
			}
	}

	font_desc = pango_font_description_from_string(options->image_overlay.font);
	layout = gtk_widget_create_pango_layout(imd->pr, nullptr);

	pango_layout_set_font_description(layout, font_desc);
	pango_layout_set_markup(layout, text, -1);

	pango_layout_get_pixel_size(layout, &width, &height);
	/* with empty text width is set to 0, but not height) */
	if (width == 0)
		height = 0;
	else if (height == 0)
		width = 0;
	if (width > 0) width += 10;
	if (height > 0) height += 10;

	if (with_hist)
		{
		width = std::max(width, HISTOGRAM_WIDTH + 10);
		height += HISTOGRAM_HEIGHT + 5;
		}

	if (width > 0 && height > 0)
		{
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
		pixbuf_set_rect_fill(pixbuf, 3, 3, width-6, height-6, options->image_overlay.background_red, options->image_overlay.background_green,
															options->image_overlay.background_blue, options->image_overlay.background_alpha);
		pixbuf_set_rect(pixbuf, 0, 0, width, height, 240, 240, 240, 80, 1, 1, 1, 1);
		pixbuf_set_rect(pixbuf, 1, 1, width-2, height-2, 240, 240, 240, 130, 1, 1, 1, 1);
		pixbuf_set_rect(pixbuf, 2, 2, width-4, height-4, 240, 240, 240, 180, 1, 1, 1, 1);
		pixbuf_pixel_set(pixbuf, 0, 0, 0, 0, 0, 0);
		pixbuf_pixel_set(pixbuf, width - 1, 0, 0, 0, 0, 0);
		pixbuf_pixel_set(pixbuf, 0, height - 1, 0, 0, 0, 0);
		pixbuf_pixel_set(pixbuf, width - 1, height - 1, 0, 0, 0, 0);

		if (with_hist)
			{
			gint x = 5;
			gint y = height - HISTOGRAM_HEIGHT - 5;
			gint w = width - 10;

			pixbuf_set_rect_fill(pixbuf, x, y, w, HISTOGRAM_HEIGHT, 220, 220, 220, 210);
			osd->histogram.draw(histmap, pixbuf, x, y, w, HISTOGRAM_HEIGHT);
			}
		pixbuf_draw_layout(pixbuf, layout, 5, 5,
		                   options->image_overlay.text_red, options->image_overlay.text_green, options->image_overlay.text_blue, options->image_overlay.text_alpha);
	}

	g_object_unref(G_OBJECT(layout));

	return pixbuf;
}

/**
 * @brief Create non-standard icons for the OSD
 * @param flag
 * @returns
 *
 * IMAGE_OSD_COLOR
 * \image html image-osd-color.png
 * IMAGE_OSD_FIRST
 * \image html image-osd-first.png
 * IMAGE_OSD_ICON
 * \image html image-osd-icon.png
 * IMAGE_OSD_LAST
 * \image html image-osd-last.png
 * IMAGE_OSD_ROTATE_AUTO
 * \image html image-osd-rotate-auto.png
 *
 */
static GdkPixbuf *image_osd_icon_pixbuf(ImageOSDFlag flag)
{
	static auto **icons = g_new0(GdkPixbuf *, IMAGE_OSD_COUNT);

	if (icons[flag]) return icons[flag];

	GdkPixbuf *icon = nullptr;

	if (osd_icons[flag].key)
		{
		icon = pixbuf_inline(osd_icons[flag].key);
		}

	if (!icon)
		{
		icon = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 24, 24);
		pixbuf_set_rect_fill(icon, 1, 1, 22, 22, 255, 255, 255, 200);
		pixbuf_set_rect(icon, 0, 0, 24, 24, 0, 0, 0, 128, 1, 1, 1, 1);
		switch (flag)
			{
			case IMAGE_OSD_ROTATE_AUTO:
				pixbuf_set_rect(icon, 3, 8, 11, 12,
						0, 0, 0, 255,
						3, 0, 3, 0);
				pixbuf_draw_triangle(icon, {14, 3, 6, 12},
				                     {20, 9}, {14, 15}, {14, 3},
				                     0, 0, 0, 255);
				break;
			case IMAGE_OSD_ROTATE_USER:
				break;
			case IMAGE_OSD_COLOR:
				pixbuf_set_rect_fill(icon, 3, 3, 18, 6, 200, 0, 0, 255);
				pixbuf_set_rect_fill(icon, 3, 9, 18, 6, 0, 200, 0, 255);
				pixbuf_set_rect_fill(icon, 3, 15, 18, 6, 0, 0, 200, 255);
				break;
			case IMAGE_OSD_FIRST:
				pixbuf_set_rect(icon, 3, 3, 18, 18, 0, 0, 0, 200, 3, 3, 3, 0);
				pixbuf_draw_triangle(icon, {6, 5, 12, 6},
				                     {12, 5}, {18, 11}, {6, 11},
				                     0, 0, 0, 255);
				break;
			case IMAGE_OSD_LAST:
				pixbuf_set_rect(icon, 3, 3, 18, 18, 0, 0, 0, 200, 3, 3, 0, 3);
				pixbuf_draw_triangle(icon, {6, 12, 12, 6},
				                     {12, 18}, {6, 12}, {18, 12},
				                     0, 0, 0, 255);
				break;
			case IMAGE_OSD_ICON:
				pixbuf_set_rect_fill(icon, 11, 3, 3, 12, 0, 0, 0, 255);
				pixbuf_set_rect_fill(icon, 11, 17, 3, 3, 0, 0, 0, 255);
				break;
			default:
				break;
			}
		}

	icons[flag] = icon;

	return icon;
}

static gint image_overlay_add(ImageWindow *imd, GdkPixbuf *pixbuf, gint x, gint y,
			      OverlayRendererFlags flags)
{
	return pixbuf_renderer_overlay_add(PIXBUF_RENDERER(imd->pr), pixbuf, x, y, flags);
}

static void image_overlay_set(ImageWindow *imd, gint id, GdkPixbuf *pixbuf, gint x, gint y)
{
	pixbuf_renderer_overlay_set(PIXBUF_RENDERER(imd->pr), id, pixbuf, x, y);
}

static void image_overlay_remove(ImageWindow *imd, gint id)
{
	pixbuf_renderer_overlay_remove(PIXBUF_RENDERER(imd->pr), id);
}

static void image_osd_icon_show(OverlayStateData *osd, ImageOSDFlag flag)
{
	GdkPixbuf *pixbuf;

	if (osd->icon_id[flag]) return;

	pixbuf = image_osd_icon_pixbuf(flag);
	if (!pixbuf) return;

	osd->icon_id[flag] = image_overlay_add(osd->imd, pixbuf,
					       osd_icons[flag].x, osd_icons[flag].y,
					       OVL_RELATIVE);
}

static void image_osd_icon_hide(OverlayStateData *osd, ImageOSDFlag flag)
{
	if (osd->icon_id[flag])
		{
		image_overlay_remove(osd->imd, osd->icon_id[flag]);
		osd->icon_id[flag] = 0;
		}
}

static void image_osd_icons_reset_time(OverlayStateData *osd)
{
	gint i;

	for (i = 0; i < IMAGE_OSD_COUNT; i++)
		{
		if (osd_icons[i].reset)
			{
			osd->icon_time[i] = 0;
			}
		}
}

static void image_osd_icons_update(OverlayStateData *osd)
{
	gint i;

	for (i = 0; i < IMAGE_OSD_COUNT; i++)
		{
		if (osd->icon_time[i] > 0)
			{
			image_osd_icon_show(osd, static_cast<ImageOSDFlag>(i));
			}
		else
			{
			image_osd_icon_hide(osd, static_cast<ImageOSDFlag>(i));
			}
		}
}

static void image_osd_icons_hide(OverlayStateData *osd)
{
	gint i;

	for (i = 0; i < IMAGE_OSD_COUNT; i++)
		{
		image_osd_icon_hide(osd, static_cast<ImageOSDFlag>(i));
		}
}

static void image_osd_info_show(OverlayStateData *osd, GdkPixbuf *pixbuf)
{
	if (osd->ovl_info == 0)
		{
		osd->ovl_info = image_overlay_add(osd->imd, pixbuf, osd->x, osd->y, osd->origin);
		}
	else
		{
		image_overlay_set(osd->imd, osd->ovl_info, pixbuf, osd->x, osd->y);
		}
}

static void image_osd_info_hide(OverlayStateData *osd)
{
	if (osd->ovl_info == 0) return;

	image_overlay_remove(osd->imd, osd->ovl_info);
	osd->ovl_info = 0;
}

static gboolean image_osd_update_cb(gpointer data)
{
	auto osd = static_cast<OverlayStateData *>(data);

	if (osd->show & OSD_SHOW_INFO)
		{
		/* redraw when the image or metadata was changed,
		   with histogram we have to redraw also when loading is finished */
		if (osd->changed_states & IMAGE_STATE_IMAGE || (osd->changed_states & IMAGE_STATE_LOADING && osd->show & OSD_SHOW_HISTOGRAM) || osd->notify & NOTIFY_HISTMAP || osd->notify & NOTIFY_METADATA)
			{
			GdkPixbuf *pixbuf;

			pixbuf = image_osd_info_render(osd);
			if (pixbuf)
				{
				image_osd_info_show(osd, pixbuf);
				g_object_unref(pixbuf);
				}
			else
				{
				image_osd_info_hide(osd);
				}
			}
		}
	else
		{
		image_osd_info_hide(osd);
		}

	if (osd->show & OSD_SHOW_STATUS)
		{
		if (osd->changed_states & IMAGE_STATE_IMAGE)
			image_osd_icons_reset_time(osd);

		if (osd->changed_states & IMAGE_STATE_COLOR_ADJ)
			{
			osd->icon_time[IMAGE_OSD_COLOR] = IMAGE_OSD_DEFAULT_DURATION + 1;
			image_osd_timer_schedule(osd);
			}

		if (osd->changed_states & IMAGE_STATE_ROTATE_AUTO)
			{
			gint n = 0;

			if (osd->imd->state & IMAGE_STATE_ROTATE_AUTO)
				{
				n = 1;
				if (!osd->imd->cm) n += IMAGE_OSD_DEFAULT_DURATION;
				}

			osd->icon_time[IMAGE_OSD_ROTATE_AUTO] = n;
			image_osd_timer_schedule(osd);
			}

		image_osd_icons_update(osd);
		}
	else
		{
		image_osd_icons_hide(osd);
		}

	osd->changed_states = IMAGE_STATE_NONE;
	osd->notify = static_cast<NotifyType>(0);
	osd->idle_id = 0;
	return G_SOURCE_REMOVE;
}

static void image_osd_update_schedule(OverlayStateData *osd, gboolean force)
{
	if (force) osd->changed_states = static_cast<ImageState>(osd->changed_states | IMAGE_STATE_IMAGE);

	if (!osd->idle_id)
		{
		osd->idle_id = g_idle_add_full(G_PRIORITY_HIGH, image_osd_update_cb, osd, nullptr);
		}
}

void image_osd_update(ImageWindow *imd)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd) return;

	image_osd_update_schedule(osd, TRUE);
}

static gboolean image_osd_timer_cb(gpointer data)
{
	auto osd = static_cast<OverlayStateData *>(data);
	gboolean done = TRUE;
	gboolean changed = FALSE;
	gint i;

	for (i = 0; i < IMAGE_OSD_COUNT; i++)
		{
		if (osd->icon_time[i] > 1)
			{
			osd->icon_time[i]--;
			if (osd->icon_time[i] < 2)
				{
				osd->icon_time[i] = 0;
				changed = TRUE;
				}
			else
				{
				done = FALSE;
				}
			}
		}

	if (changed) image_osd_update_schedule(osd, FALSE);

	if (done)
		{
		osd->timer_id = 0;
		return G_SOURCE_REMOVE;
		}

	return G_SOURCE_CONTINUE;
}

static void image_osd_timer_schedule(OverlayStateData *osd)
{
	if (!osd->timer_id)
		{
		osd->timer_id = g_timeout_add(100, image_osd_timer_cb, osd);
		}
}

static void image_osd_state_cb(ImageWindow *, ImageState state, gpointer data)
{
	auto osd = static_cast<OverlayStateData *>(data);

	osd->changed_states = static_cast<ImageState>(osd->changed_states | state);
	image_osd_update_schedule(osd, FALSE);
}

static void image_osd_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto osd = static_cast<OverlayStateData *>(data);

	if (((type & NOTIFY_HISTMAP) || (type & NOTIFY_METADATA)) && osd->imd && fd == osd->imd->image_fd)
		{
		DEBUG_1("Notify osd: %s %04x", fd->path, type);
		osd->notify = static_cast<NotifyType>(osd->notify | type);
		image_osd_update_schedule(osd, FALSE);
		}
}


static void image_osd_free(OverlayStateData *osd)
{
	if (!osd) return;

	if (osd->idle_id) g_source_remove(osd->idle_id);
	if (osd->timer_id) g_source_remove(osd->timer_id);

	file_data_unregister_notify_func(image_osd_notify_cb, osd);

	if (osd->imd)
		{
		image_set_osd_data(osd->imd, nullptr);
		g_signal_handler_disconnect(osd->imd->pr, osd->destroy_id);

		image_set_state_func(osd->imd, nullptr, nullptr);

		image_osd_info_hide(osd);
		image_osd_icons_hide(osd);
		}

	g_free(osd);
}

static void image_osd_destroy_cb(GtkWidget *, gpointer data)
{
	auto osd = static_cast<OverlayStateData *>(data);

	osd->imd = nullptr;
	image_osd_free(osd);
}

static void image_osd_enable(ImageWindow *imd, OsdShowFlags show)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd)
		{
		osd = g_new0(OverlayStateData, 1);
		osd->imd = imd;
		osd->show = OSD_SHOW_NOTHING;
		osd->x = options->image_overlay.x;
		osd->y = options->image_overlay.y;
		osd->origin = OVL_RELATIVE;

		osd->histogram = Histogram();

		osd->destroy_id = g_signal_connect(G_OBJECT(imd->pr), "destroy",
						   G_CALLBACK(image_osd_destroy_cb), osd);
		image_set_osd_data(imd, osd);

		image_set_state_func(osd->imd, image_osd_state_cb, osd);
		file_data_register_notify_func(image_osd_notify_cb, osd, NOTIFY_PRIORITY_LOW);
		}

	if (show & OSD_SHOW_STATUS)
		image_osd_icon(imd, IMAGE_OSD_ICON, -1);

	if (show != osd->show)
		image_osd_update_schedule(osd, TRUE);

	osd->show = show;
}

void image_osd_set(ImageWindow *imd, OsdShowFlags show)
{
	if (!imd) return;

	image_osd_enable(imd, show);
}

OsdShowFlags image_osd_get(ImageWindow *imd)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	return osd ? osd->show : OSD_SHOW_NOTHING;
}

Histogram *image_osd_get_histogram(ImageWindow *imd)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	return osd ? &osd->histogram : nullptr;
}

void image_osd_copy_status(ImageWindow *src, ImageWindow *dest)
{
	image_osd_set(dest, image_osd_get(src));

	Histogram *h_src = image_osd_get_histogram(src);
	if (!h_src) return;

	Histogram *h_dest = image_osd_get_histogram(dest);
	if (!h_dest) return;

	*h_dest = *h_src;
}

/* duration:
    0 = hide
    1 = show
   2+ = show for duration tenths of a second
   -1 = use default duration
 */
void image_osd_icon(ImageWindow *imd, ImageOSDFlag flag, gint duration)
{
	OverlayStateData *osd = image_get_osd_data(imd);

	if (!osd) return;

	if (flag >= IMAGE_OSD_COUNT) return;
	if (duration < 0) duration = IMAGE_OSD_DEFAULT_DURATION;
	if (duration > 1) duration += 1;

	osd->icon_time[flag] = duration;

	image_osd_update_schedule(osd, FALSE);
	image_osd_timer_schedule(osd);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
