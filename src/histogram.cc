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

#include "main.h"
#include "histogram.h"

#include "pixbuf-util.h"
#include "filedata.h"

#include <math.h>

/*
 *----------------------------------------------------------------------------
 * image histogram
 *----------------------------------------------------------------------------
 */

#define HISTMAP_SIZE 256

struct _HistMap {
	gulong r[HISTMAP_SIZE];
	gulong g[HISTMAP_SIZE];
	gulong b[HISTMAP_SIZE];
	gulong max[HISTMAP_SIZE];

	guint idle_id; /* event source id */
	GdkPixbuf *pixbuf;
	gint y;
};


Histogram *histogram_new(void)
{
	Histogram *histogram;

	histogram = g_new0(Histogram, 1);
	histogram->histogram_channel = HCHAN_DEFAULT;
	histogram->histogram_mode = 0;

	/* grid */
	histogram->vgrid = 5;
	histogram->hgrid = 3;
	histogram->grid_color.R	= 160;
	histogram->grid_color.G	= 160;
	histogram->grid_color.B	= 160;
	histogram->grid_color.A	= 250;

	return histogram;
}

void histogram_free(Histogram *histogram)
{
	g_free(histogram);
}


gint histogram_set_channel(Histogram *histogram, gint chan)
{
	if (!histogram) return 0;
	histogram->histogram_channel = chan;
	return chan;
}

gint histogram_get_channel(Histogram *histogram)
{
	if (!histogram) return 0;
	return histogram->histogram_channel;
}

gint histogram_set_mode(Histogram *histogram, gint mode)
{
	if (!histogram) return 0;
	histogram->histogram_mode = mode;
	return mode;
}

gint histogram_get_mode(Histogram *histogram)
{
	if (!histogram) return 0;
	return histogram->histogram_mode;
}

gint histogram_toggle_channel(Histogram *histogram)
{
	if (!histogram) return 0;
	return histogram_set_channel(histogram, (histogram_get_channel(histogram)+1)%HCHAN_COUNT);
}

gint histogram_toggle_mode(Histogram *histogram)
{
	if (!histogram) return 0;
	return histogram_set_mode(histogram, !histogram_get_mode(histogram));
}

const gchar *histogram_label(Histogram *histogram)
{
	const gchar *t1 = "";

	if (!histogram) return nullptr;

	if (histogram->histogram_mode)
		switch (histogram->histogram_channel)
			{
			case HCHAN_R:   t1 = _("Log Histogram on Red"); break;
			case HCHAN_G:   t1 = _("Log Histogram on Green"); break;
			case HCHAN_B:   t1 = _("Log Histogram on Blue"); break;
			case HCHAN_RGB: t1 = _("Log Histogram on RGB"); break;
			case HCHAN_MAX: t1 = _("Log Histogram on value"); break;
			}
	else
		switch (histogram->histogram_channel)
			{
			case HCHAN_R:   t1 = _("Linear Histogram on Red"); break;
			case HCHAN_G:   t1 = _("Linear Histogram on Green"); break;
			case HCHAN_B:   t1 = _("Linear Histogram on Blue"); break;
			case HCHAN_RGB: t1 = _("Linear Histogram on RGB"); break;
			case HCHAN_MAX: t1 = _("Linear Histogram on value"); break;
			}
	return t1;
}

static HistMap *histmap_new(void)
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
	gint w, h, i, j, srs, has_alpha, step, end_line;
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
		gint lines = 1 + 16384 / w;
		end_line = histmap->y + lines;
		if (end_line > h) end_line = h;
		}

	step = 3 + !!(has_alpha);
	for (i = histmap->y; i < end_line; i++)
		{
		guchar *sp = s_pix + (i * srs); /* 8bit */
		for (j = 0; j < w; j++)
			{
			guint max = sp[0];
			if (sp[1] > max) max = sp[1];
			if (sp[2] > max) max = sp[2];

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
	fd->histmap->pixbuf = fd->pixbuf;
	g_object_ref(fd->histmap->pixbuf);

	fd->histmap->idle_id = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, histmap_idle_cb, fd, nullptr);
	return TRUE;
}


static void histogram_vgrid(Histogram *histogram, GdkPixbuf *pixbuf, gint x, gint y, gint width, gint height)
{
	guint i;
	float add;

	if (histogram->vgrid == 0) return;

	add = width / static_cast<float>(histogram->vgrid);

	for (i = 1; i < histogram->vgrid; i++)
		{
		gint xpos = x + static_cast<int>(i * add + 0.5);

		pixbuf_draw_line(pixbuf, x, y, width, height, xpos, y, xpos, y + height,
				 histogram->grid_color.R,
				 histogram->grid_color.G,
				 histogram->grid_color.B,
				 histogram->grid_color.A);
		}
}

static void histogram_hgrid(Histogram *histogram, GdkPixbuf *pixbuf, gint x, gint y, gint width, gint height)
{
	guint i;
	float add;

	if (histogram->hgrid == 0) return;

	add = height / static_cast<float>(histogram->hgrid);

	for (i = 1; i < histogram->hgrid; i++)
		{
		gint ypos = y + static_cast<int>(i * add + 0.5);

		pixbuf_draw_line(pixbuf, x, y, width, height, x, ypos, x + width, ypos,
				 histogram->grid_color.R,
				 histogram->grid_color.G,
				 histogram->grid_color.B,
				 histogram->grid_color.A);
		}
}

gboolean histogram_draw(Histogram *histogram, const HistMap *histmap, GdkPixbuf *pixbuf, gint x, gint y, gint width, gint height)
{
	/** @FIXME use the coordinates correctly */
	gint i;
	gulong max = 0;
	gdouble logmax;
	gint combine = (HISTMAP_SIZE - 1) / width + 1;
	gint ypos = y + height;

	if (!histogram || !histmap) return FALSE;

	/* Draw the grid */
	histogram_vgrid(histogram, pixbuf, x, y, width, height);
	histogram_hgrid(histogram, pixbuf, x, y, width, height);

	/* exclude overexposed and underexposed */
	for (i = 1; i < HISTMAP_SIZE - 1; i++)
		{
		if (histmap->r[i] > max) max = histmap->r[i];
		if (histmap->g[i] > max) max = histmap->g[i];
		if (histmap->b[i] > max) max = histmap->b[i];
		if (histmap->max[i] > max) max = histmap->max[i];
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

		num_chan = (histogram->histogram_channel == HCHAN_RGB) ? 3 : 1;
		for (j = 0; j < num_chan; j++)
			{
			gint chanmax;
			if (histogram->histogram_channel == HCHAN_RGB)
				{
				chanmax = HCHAN_R;
				if (v[HCHAN_G] > v[HCHAN_R]) chanmax = HCHAN_G;
				if (v[HCHAN_B] > v[chanmax]) chanmax = HCHAN_B;
				}
			else
				{
				chanmax = histogram->histogram_channel;
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
					}

				switch (histogram->histogram_channel)
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
					}

				if (v[chanmax] == 0)
					pt = 0;
				else if (histogram->histogram_mode)
					pt = (static_cast<gdouble>(log(v[chanmax]))) / logmax * (height - 1);
				else
					pt = (static_cast<gdouble>(v[chanmax])) / max * (height - 1);

				pixbuf_draw_line(pixbuf,
					x, y, width, height,
					xpos, ypos, xpos, ypos - pt,
					r, g, b, 255);
				}

			v[chanmax] = -1;
			}
		}

	return TRUE;
}

void histogram_notify_cb(FileData *fd, NotifyType type, gpointer UNUSED(data))
{
	if ((type & NOTIFY_REREAD) && fd->histmap)
		{
		DEBUG_1("Notify histogram: %s %04x", fd->path, type);
		histmap_free(fd->histmap);
		fd->histmap = nullptr;
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
