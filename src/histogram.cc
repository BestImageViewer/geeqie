/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#include "histogram.h"

#include <algorithm>
#include <cmath>

#include <gdk/gdk.h>
#include <glib-object.h>

#include "filedata.h"
#include "intl.h"
#include "pixbuf-util.h"

/*
 *----------------------------------------------------------------------------
 * image histogram
 *----------------------------------------------------------------------------
 */

namespace
{

constexpr gint HISTMAP_SIZE = 256;

void histogram_vgrid(const Histogram::Grid &grid, GdkPixbuf *pixbuf, GdkRectangle rect)
{
	if (grid.v == 0) return;

	float add = rect.width / static_cast<float>(grid.v);

	for (guint i = 1; i < grid.v; i++)
		{
		gint xpos = rect.x + static_cast<int>((i * add) + 0.5);

		pixbuf_draw_line(pixbuf, rect, xpos, rect.y, xpos, rect.y + rect.height,
		                 grid.color.R, grid.color.G, grid.color.B, grid.color.A);
		}
}

void histogram_hgrid(const Histogram::Grid &grid, GdkPixbuf *pixbuf, GdkRectangle rect)
{
	if (grid.h == 0) return;

	float add = rect.height / static_cast<float>(grid.h);

	for (guint i = 1; i < grid.h; i++)
		{
		gint ypos = rect.y + static_cast<int>((i * add) + 0.5);

		pixbuf_draw_line(pixbuf, rect, rect.x, ypos, rect.x + rect.width, ypos,
		                 grid.color.R, grid.color.G, grid.color.B, grid.color.A);
		}
}

} // namespace

struct HistMap {
	gulong r[HISTMAP_SIZE];
	gulong g[HISTMAP_SIZE];
	gulong b[HISTMAP_SIZE];
	gulong max[HISTMAP_SIZE];

	guint idle_id; /* event source id */
	GdkPixbuf *pixbuf;
	gint y;
};


void Histogram::set_channel(gint channel)
{
	histogram_channel = channel;
}

gint Histogram::get_channel() const
{
	return histogram_channel;
}

void Histogram::set_mode(gint mode)
{
	histogram_mode = mode;
}

gint Histogram::get_mode() const
{
	return histogram_mode;
}

void Histogram::toggle_channel()
{
	histogram_channel = (histogram_channel + 1) % HCHAN_COUNT;
}

void Histogram::toggle_mode()
{
	histogram_mode = (histogram_mode + 1) % HMODE_COUNT;
}

const gchar *Histogram::label() const
{
	const gchar *t1 = "";

	if (histogram_mode == HMODE_LOG)
		switch (histogram_channel)
			{
			case HCHAN_R:   t1 = _("Log Histogram on Red"); break;
			case HCHAN_G:   t1 = _("Log Histogram on Green"); break;
			case HCHAN_B:   t1 = _("Log Histogram on Blue"); break;
			case HCHAN_RGB: t1 = _("Log Histogram on RGB"); break;
			case HCHAN_MAX: t1 = _("Log Histogram on value"); break;
			default:
				break;
			}
	else
		switch (histogram_channel)
			{
			case HCHAN_R:   t1 = _("Linear Histogram on Red"); break;
			case HCHAN_G:   t1 = _("Linear Histogram on Green"); break;
			case HCHAN_B:   t1 = _("Linear Histogram on Blue"); break;
			case HCHAN_RGB: t1 = _("Linear Histogram on RGB"); break;
			case HCHAN_MAX: t1 = _("Linear Histogram on value"); break;
			default:
				break;
			}

	return t1;
}

static HistMap *histmap_new()
{
	auto histmap = g_new0(HistMap, 1);
	return histmap;
}

void histmap_free(HistMap *histmap)
{
	if (!histmap) return;
	if (histmap->idle_id) g_source_remove(histmap->idle_id);
	if (histmap->pixbuf) g_object_unref(histmap->pixbuf);
	g_free(histmap);
}

static gboolean histmap_read(HistMap *histmap, gboolean whole)
{
	gint w;
	gint h;
	gint i;
	gint j;
	gint srs;
	gint has_alpha;
	gint step;
	gint end_line;
	guchar *s_pix;
	GdkPixbuf *imgpixbuf = histmap->pixbuf;

	w = gdk_pixbuf_get_width(imgpixbuf);
	h = gdk_pixbuf_get_height(imgpixbuf);
	srs = gdk_pixbuf_get_rowstride(imgpixbuf);
	s_pix = gdk_pixbuf_get_pixels(imgpixbuf);
	has_alpha = gdk_pixbuf_get_has_alpha(imgpixbuf);

	if (whole)
		{
		end_line = h;
		}
	else
		{
		gint lines = 1 + (16384 / w);
		end_line = histmap->y + lines;
		end_line = std::min(end_line, h);
		}

	step = 3 + !!(has_alpha);
	for (i = histmap->y; i < end_line; i++)
		{
		guchar *sp = s_pix + (i * srs); /* 8bit */
		for (j = 0; j < w; j++)
			{
			guint max = std::max({sp[0], sp[1], sp[2]});

			histmap->r[sp[0]]++;
			histmap->g[sp[1]]++;
			histmap->b[sp[2]]++;
			histmap->max[max]++;

			sp += step;
			}
		}
	histmap->y = end_line;
	return end_line >= h;
}

const HistMap *histmap_get(FileData *fd)
{
	if (fd->histmap && !fd->histmap->idle_id) return fd->histmap; /* histmap exists and is finished */

	return nullptr;
}

static gboolean histmap_idle_cb(gpointer data)
{
	auto fd = static_cast<FileData *>(data);
	if (histmap_read(fd->histmap, FALSE))
		{
		/* finished */
		g_object_unref(fd->histmap->pixbuf); /*pixbuf is no longer needed */
		fd->histmap->pixbuf = nullptr;
		fd->histmap->idle_id = 0;
		file_data_send_notification(fd, NOTIFY_HISTMAP);
		return G_SOURCE_REMOVE;
		}
	return G_SOURCE_CONTINUE;
}

gboolean histmap_start_idle(FileData *fd)
{
	if (fd->histmap || !fd->pixbuf) return FALSE;

	fd->histmap = histmap_new();
	fd->histmap->pixbuf = g_object_ref(fd->pixbuf);
	fd->histmap->idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, histmap_idle_cb, fd, nullptr);

	return TRUE;
}


void Histogram::draw(const HistMap *histmap, GdkPixbuf *pixbuf, gint x, gint y, gint width, gint height) const
{
	if (!histmap) return;

	/** @FIXME use the coordinates correctly */
	gint i;
	gulong max = 0;
	gdouble logmax;
	gint combine = ((HISTMAP_SIZE - 1) / width) + 1;
	gint ypos = y + height;

	/* Draw the grid */
	constexpr Histogram::Grid grid{5, 3, {160, 160, 160, 250}};
	const GdkRectangle rect{x, y, width, height};
	histogram_vgrid(grid, pixbuf, rect);
	histogram_hgrid(grid, pixbuf, rect);

	/* exclude overexposed and underexposed */
	for (i = 1; i < HISTMAP_SIZE - 1; i++)
		{
		max = std::max({histmap->r[i], histmap->g[i], histmap->b[i], histmap->max[i], max});
		}

	if (max > 0)
		logmax = log(max);
	else
		logmax = 1.0;

	for (i = 0; i < width; i++)
		{
		gint j;
		glong v[4] = {0, 0, 0, 0};
		gint rplus = 0;
		gint gplus = 0;
		gint bplus = 0;
		gint ii = i * HISTMAP_SIZE / width;
		gint xpos = x + i;
		gint num_chan;

		for (j = 0; j < combine; j++)
			{
			guint p = ii + j;
			v[0] += histmap->r[p];
			v[1] += histmap->g[p];
			v[2] += histmap->b[p];
			v[3] += histmap->max[p];
			}

		for (j = 0; combine > 1 && j < 4; j++)
			v[j] /= combine;

		num_chan = (histogram_channel == HCHAN_RGB) ? 3 : 1;
		for (j = 0; j < num_chan; j++)
			{
			gint chanmax;
			if (histogram_channel == HCHAN_RGB)
				{
				chanmax = HCHAN_R;
				if (v[HCHAN_G] > v[HCHAN_R]) chanmax = HCHAN_G;
				if (v[HCHAN_B] > v[chanmax]) chanmax = HCHAN_B;
				}
			else
				{
				chanmax = histogram_channel;
				}

			    	{
				gulong pt;
				gint r = rplus;
				gint g = gplus;
				gint b = bplus;

				switch (chanmax)
					{
					case HCHAN_R: rplus = r = 255; break;
					case HCHAN_G: gplus = g = 255; break;
					case HCHAN_B: bplus = b = 255; break;
					default:
						break;
					}

				switch (histogram_channel)
					{
					case HCHAN_RGB:
						if (r == 255 && g == 255 && b == 255)
							{
							r = 0; 	b = 0; 	g = 0;
							}
						break;
					case HCHAN_R:	  	b = 0; 	g = 0; 	break;
					case HCHAN_G:   r = 0; 	b = 0;		break;
					case HCHAN_B:   r = 0;		g = 0; 	break;
					case HCHAN_MAX: r = 0; 	b = 0; 	g = 0; 	break;
					default:
						break;
					}

				if (v[chanmax] == 0)
					pt = 0;
				else if (histogram_mode == HMODE_LOG)
					pt = (static_cast<gdouble>(log(v[chanmax]))) / logmax * (height - 1);
				else
					pt = (static_cast<gdouble>(v[chanmax])) / max * (height - 1);

				pixbuf_draw_line(pixbuf, rect,
				                 xpos, ypos, xpos, ypos - pt,
				                 r, g, b, 255);
				}

			v[chanmax] = -1;
			}
		}
}

void histogram_notify_cb(FileData *fd, NotifyType type, gpointer)
{
	if ((type & NOTIFY_REREAD) && fd->histmap)
		{
		DEBUG_1("Notify histogram: %s %04x", fd->path, type);
		histmap_free(fd->histmap);
		fd->histmap = nullptr;
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
