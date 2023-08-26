/*
 * Copyright (C) 2004 John Ellis
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

#ifndef BAR_H
#define BAR_H

struct FileData;
struct LayoutWindow;

enum PaneType {
	PANE_UNDEF = 0,
	PANE_COMMENT,
	PANE_EXIF,
	PANE_HISTOGRAM,
	PANE_KEYWORDS,
	PANE_GPS,
	PANE_RATING
};

struct PaneData {
	void (*pane_set_fd)(GtkWidget *pane, FileData *fd); /**< filled in by pane */
	void (*pane_notify_selection)(GtkWidget *pane, gint count); /**< filled in by pane */
	gint (*pane_event)(GtkWidget *pane, GdkEvent *event); /**< filled in by pane */
	void (*pane_write_config)(GtkWidget *pane, GString *outstr, gint indent); /**< filled in by pane */
	GtkWidget *title; /**< filled in by pane */
	gboolean expanded; /**< filled in by pane */
	gchar *id; /**< filled in by pane */
	PaneType type; /**< filled in by pane */

	GtkWidget *bar; /**< filled in by bar */
	LayoutWindow *lw; /**< filled in by bar */
};




GtkWidget *bar_new(LayoutWindow *lw);
GtkWidget *bar_new_from_config(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values);
GtkWidget *bar_update_from_config(GtkWidget *bar, const gchar **attribute_names, const gchar **attribute_values, LayoutWindow *lw, gboolean startup);

void bar_close(GtkWidget *bar);

void bar_write_config(GtkWidget *bar, GString *outstr, gint indent);

void bar_populate_default(GtkWidget *bar);

void bar_add(GtkWidget *bar, GtkWidget *pane);
GtkWidget *bar_find_pane_by_id(GtkWidget *bar, PaneType type, const gchar *id);

void bar_clear(GtkWidget *bar);

void bar_set_fd(GtkWidget *bar, FileData *fd);
void bar_notify_selection(GtkWidget *bar, gint count);
gboolean bar_event(GtkWidget *bar, GdkEvent *event);

GtkWidget *bar_pane_expander_title(const gchar *title);
void bar_update_expander(GtkWidget *pane);
gboolean bar_pane_translate_title(PaneType type, const gchar *id, gchar **title);

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
