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

#ifndef PAN_VIEW_PAN_ITEM_H
#define PAN_VIEW_PAN_ITEM_H

#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

#include "pan-types.h"

class FileData;
struct PixbufRenderer;

void pan_item_free(PanItem *pi);

void pan_item_set_key(PanItem *pi, const gchar *key);
void pan_item_added(PanWindow *pw, PanItem *pi);
void pan_item_remove(PanWindow *pw, PanItem *pi);

// Determine sizes
void pan_item_size_by_item(PanItem *pi, PanItem *child, gint border);
void pan_item_size_coordinates(PanItem *pi, gint border, gint &w, gint &h);

// Find items
PanItem *pan_item_find_by_key(PanWindow *pw, PanItemType type, const gchar *key);
GList *pan_item_find_by_path(PanWindow *pw, PanItemType type, const gchar *path,
			     gboolean ignore_case, gboolean partial);
GList *pan_item_find_by_fd(PanWindow *pw, PanItemType type, FileData *fd,
			   gboolean ignore_case, gboolean partial);
PanItem *pan_item_find_by_coord(PanWindow *pw, PanItemType type,
				gint x, gint y, const gchar *key);

// Item box type
PanItem *pan_item_box_new(PanWindow *pw, FileData *fd, gint x, gint y, gint width, gint height,
                          gint border_size, const PanColor &base, const PanColor &bord);
void pan_item_box_shadow(PanItem *pi, gint offset, gint fade);
gboolean pan_item_box_draw(PanWindow *pw, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *pr,
                           gint x, gint y, gint width, gint height);

// Item triangle type
PanItem *pan_item_tri_new(PanWindow *pw,
                          const GdkPoint &c1, const GdkPoint &c2, const GdkPoint &c3,
                          const PanColor &color,
                          gint borders, const PanColor &border_color);
gboolean pan_item_tri_draw(PanWindow *pw, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *pr,
                           gint x, gint y, gint width, gint height);

// Item text type
PanItem *pan_item_text_new(PanWindow *pw, gint x, gint y, const gchar *text,
                           PanTextAttrType attr, PanBorderType border, const PanColor &color);
gboolean pan_item_text_draw(PanWindow *pw, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *pr,
                            gint x, gint y, gint width, gint height);

// Item thumbnail type
PanItem *pan_item_thumb_new(PanWindow *pw, FileData *fd, gint x, gint y);
gboolean pan_item_thumb_draw(PanWindow *pw, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *pr,
                             gint x, gint y, gint width, gint height);

// Item image type
PanItem *pan_item_image_new(PanWindow *pw, FileData *fd, gint x, gint y, gint w, gint h);
gboolean pan_item_image_draw(PanWindow *pw, PanItem *pi, GdkPixbuf *pixbuf, PixbufRenderer *pr,
                             gint x, gint y, gint width, gint height);

// Alignment
class PanTextAlignment
{
public:
	PanTextAlignment(PanWindow *pw, gint x, gint y, const gchar *key);
	~PanTextAlignment();

	void add(const gchar *label, const gchar *text);
	void calc(PanItem *box);

private:
	PanWindow *pw;

	struct Items
	{
		PanItem *label = nullptr;
		PanItem *text = nullptr;
	};

	std::vector<Items> columns;

	gint x;
	gint y;
	gchar *key;
};

// Cache data
void pan_cache_data_free(PanCacheData *pc);

#endif
