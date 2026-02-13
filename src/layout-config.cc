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

#include "layout-config.h"

#include <algorithm>
#include <cstring>
#include <string>

#include <glib-object.h>

#include <config.h>

#include "compat.h"
#include "intl.h"
#include "ui-misc.h"

namespace
{

enum {
	COLUMN_TEXT = 0,
	COLUMN_KEY
};

struct LayoutStyle
{
	LayoutLocation a, b, c;
};

struct LayoutConfig
{
	std::vector<GtkWidget *> style_widgets;

	GtkWidget *listview;

	gint style;
};

constexpr gint LAYOUT_STYLE_SIZE = 48;

constexpr std::array<LayoutStyle, 4> layout_config_styles{{
	/* 1, 2, 3 */
	{ static_cast<LayoutLocation>(LAYOUT_LEFT | LAYOUT_TOP), static_cast<LayoutLocation>(LAYOUT_LEFT | LAYOUT_BOTTOM), LAYOUT_RIGHT },
	{ static_cast<LayoutLocation>(LAYOUT_LEFT | LAYOUT_TOP), static_cast<LayoutLocation>(LAYOUT_RIGHT | LAYOUT_TOP), LAYOUT_BOTTOM },
	{ LAYOUT_LEFT, static_cast<LayoutLocation>(LAYOUT_RIGHT | LAYOUT_TOP), static_cast<LayoutLocation>(LAYOUT_RIGHT | LAYOUT_BOTTOM) },
	{ LAYOUT_TOP, static_cast<LayoutLocation>(LAYOUT_LEFT | LAYOUT_BOTTOM), static_cast<LayoutLocation>(LAYOUT_RIGHT | LAYOUT_BOTTOM) }
}};

const gchar *layout_titles[] = { N_("Tools"), N_("Files"), N_("Image") };


void layout_config_destroy(LayoutConfig *lc)
{
	delete lc;
}

void layout_config_set_order(LayoutLocation l, gint n,
                             LayoutLocation &a, LayoutLocation &b, LayoutLocation &c)
{
	switch (n)
		{
		case 0:
			a = l;
			break;
		case 1:
			b = l;
			break;
		case 2:
		default:
			c = l;
			break;
		}
}

void layout_config_from_data(gint style, gint oa, gint ob, gint oc,
                             LayoutLocation &la, LayoutLocation &lb, LayoutLocation &lc)
{
	style = std::clamp<int>(style, 0, layout_config_styles.size());

	LayoutStyle ls = layout_config_styles[style];

	layout_config_set_order(ls.a, oa, la, lb, lc);
	layout_config_set_order(ls.b, ob, la, lb, lc);
	layout_config_set_order(ls.c, oc, la, lb, lc);
}

void layout_config_list_order_set(LayoutConfig *lc, gint src, gint dest)
{
	GtkListStore *store;
	GtkTreeIter iter;
	gboolean valid;
	gint n;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(lc->listview)));

	n = 0;
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
	while (valid)
		{
		if (n == dest)
			{
			gtk_list_store_set(store, &iter, COLUMN_TEXT, _(layout_titles[src]), COLUMN_KEY, src, -1);
			return;
			}
		n++;
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		}
}

gint layout_config_list_order_get(LayoutConfig *lc, gint n)
{
	GtkTreeModel *store;
	GtkTreeIter iter;
	gboolean valid;
	gint c = 0;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(lc->listview));

	valid = gtk_tree_model_get_iter_first(store, &iter);
	while (valid)
		{
		if (c == n)
			{
			gint val;
			gtk_tree_model_get(store, &iter, COLUMN_KEY, &val, -1);
			return val;
			}
		c++;
		valid = gtk_tree_model_iter_next(store, &iter);
		}
	return 0;
}

void layout_config_widget_click_cb(GtkWidget *widget, gpointer data)
{
	LayoutConfig *lc;

	lc = static_cast<LayoutConfig *>(g_object_get_data(G_OBJECT(widget), "layout_config"));

	if (lc) lc->style = GPOINTER_TO_INT(data);
}

void layout_config_table_button(GtkWidget *table, LayoutLocation l, const gchar *text)
{
	GtkWidget *button;

	gint x1;
	gint y1;
	gint x2;
	gint y2;

	x1 = 0;
	y1 = 0;
	x2 = 2;
	y2 = 2;

	if (l & LAYOUT_LEFT) x2 = 1;
	if (l & LAYOUT_RIGHT) x1 = 1;
	if (l & LAYOUT_TOP) y2 = 1;
	if (l & LAYOUT_BOTTOM) y1 = 1;

	button = gtk_button_new_with_label(text);
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_set_can_focus(button, FALSE);
	gtk_grid_attach(GTK_GRID(table), button, x1, y1, x2 - x1, y2 - y1);
	gtk_widget_show(button);
}

GtkWidget *layout_config_widget(GtkWidget *group, GtkWidget *box, gint style, LayoutConfig *lc)
{
	GtkWidget *table;
	LayoutStyle ls;

	ls = layout_config_styles[style];

	if (group)
		{
#if HAVE_GTK4
		group = gtk_toggle_button_new();
		gtk_toggle_button_set_group(button, group);
#else
		group = gtk_radio_button_new(gtk_radio_button_get_group(GTK_RADIO_BUTTON(group)));
#endif
		}
	else
		{
#if HAVE_GTK4
		group = gtk_toggle_button_new();
#else
		group = gtk_radio_button_new(nullptr);
#endif
		}
	g_object_set_data(G_OBJECT(group), "layout_config", lc);
	g_signal_connect(G_OBJECT(group), "clicked",
	                 G_CALLBACK(layout_config_widget_click_cb), GINT_TO_POINTER(style));
	gq_gtk_box_pack_start(GTK_BOX(box), group, FALSE, FALSE, 0);

	table = gtk_grid_new();

	layout_config_table_button(table, ls.a, "1");
	layout_config_table_button(table, ls.b, "2");
	layout_config_table_button(table, ls.c, "3");

	gtk_widget_set_size_request(table, LAYOUT_STYLE_SIZE, LAYOUT_STYLE_SIZE);
	gq_gtk_container_add(group, table);
	gtk_widget_show(table);

	gtk_widget_show(group);

	return group;
}

void layout_config_number_cb(GtkTreeViewColumn *, GtkCellRenderer *cell,
                    GtkTreeModel *store, GtkTreeIter *iter, gpointer)
{
	g_autoptr(GtkTreePath) tpath = gtk_tree_model_get_path(store, iter);

	gint *indices = gtk_tree_path_get_indices(tpath);
	g_object_set(cell,
	             "text", std::to_string(indices[0] + 1).c_str(),
	             NULL);
}

gchar num_to_text_char(gint n)
{
	switch (n)
		{
		case 1:
			return '2';
			break;
		case 2:
			return '3';
			break;
		default:
			break;
		}
	return '1';
}

gchar *layout_config_order_to_text(gint a, gint b, gint c)
{
	gchar *text;

	text = g_strdup("   ");

	text[0] = num_to_text_char(a);
	text[1] = num_to_text_char(b);
	text[2] = num_to_text_char(c);

	return text;
}

gint text_char_to_num(const gchar *text, gint n)
{
	if (text[n] == '3') return 2;
	if (text[n] == '2') return 1;
	return 0;
}

void layout_config_order_from_text(const gchar *text, gint &a, gint &b, gint &c)
{
	if (!text || strlen(text) < 3)
		{
		a = 0;
		b = 1;
		c = 2;
		}
	else
		{
		a = text_char_to_num(text, 0);
		b = text_char_to_num(text, 1);
		c = text_char_to_num(text, 2);
		}
}

} // namespace

void layout_config_parse(gint style, const gchar *order,
                         LayoutLocation &a, LayoutLocation &b, LayoutLocation &c)
{
	gint na;
	gint nb;
	gint nc;

	layout_config_order_from_text(order, na, nb, nc);
	layout_config_from_data(style, na, nb, nc, a, b, c);
}

void layout_config_set(GtkWidget *widget, gint style, const gchar *order)
{
	LayoutConfig *lc;

	lc = static_cast<LayoutConfig *>(g_object_get_data(G_OBJECT(widget), "layout_config"));

	if (!lc) return;

	style = std::clamp<int>(style, 0, layout_config_styles.size());
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lc->style_widgets[style]), TRUE);

	gint a;
	gint b;
	gint c;
	layout_config_order_from_text(order, a, b, c);

	layout_config_list_order_set(lc, a, 0);
	layout_config_list_order_set(lc, b, 1);
	layout_config_list_order_set(lc, c, 2);
}

gchar *layout_config_get(GtkWidget *widget, gint *style)
{
	LayoutConfig *lc;

	lc = static_cast<LayoutConfig *>(g_object_get_data(G_OBJECT(widget), "layout_config"));

	/* this should not happen */
	if (!lc) return nullptr;

	*style = lc->style;

	const gint a = layout_config_list_order_get(lc, 0);
	const gint b = layout_config_list_order_get(lc, 1);
	const gint c = layout_config_list_order_get(lc, 2);

	return layout_config_order_to_text(a, b, c);
}

GtkWidget *layout_config_new()
{
	GtkWidget *hbox;
	GtkWidget *group = nullptr;
	GtkWidget *scrolled;
	GtkListStore *store;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	auto *lc = new LayoutConfig();

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	g_object_set_data_full(G_OBJECT(box), "layout_config", lc,
	                       reinterpret_cast<GDestroyNotify>(layout_config_destroy));

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gq_gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);
	for (size_t i = 0; i < layout_config_styles.size(); i++)
		{
		group = layout_config_widget(group, hbox, i, lc);
		lc->style_widgets.push_back(group);
		}
	gtk_widget_show(hbox);

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	gq_gtk_box_pack_start(GTK_BOX(box), scrolled, FALSE, FALSE, 0);
	gtk_widget_show(scrolled);

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
	lc->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(lc->listview), FALSE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(lc->listview), FALSE);
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(lc->listview), TRUE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_set_cell_data_func(column, renderer, layout_config_number_cb, lc, nullptr);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", COLUMN_TEXT);

	gtk_tree_view_append_column(GTK_TREE_VIEW(lc->listview), column);

	for (gint i = 0; i < 3; i++)
		{
		GtkTreeIter iter;

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, COLUMN_TEXT, _(layout_titles[i]), COLUMN_KEY, i, -1);
		}

	gq_gtk_container_add(scrolled, lc->listview);
	gtk_widget_show(lc->listview);

	pref_label_new(box, _("(drag to change order)"));

	return box;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
