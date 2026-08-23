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

#include "intl.h"
#include "layout.h"
#include "misc.h"
#include "ui-misc.h"

namespace
{

struct LayoutStyle
{
	LayoutLocation a, b, c;
};

struct LayoutConfigItem
{
	GObject parent_instance;
	gchar *title;
	gint key;
};

struct LayoutConfigItemClass
{
	GObjectClass parent_class;
};

struct LayoutConfig
{
	std::vector<GtkWidget *> style_widgets;

	GtkWidget *list_view{};
	GListStore *store{};

	gint style{};

	~LayoutConfig()
	{
		g_clear_object(&store);
	}
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

constexpr gchar LAYOUT_CONFIG_LIST_ITEM_DATA[] = "layout-config-list-item-data";
constexpr gchar LAYOUT_CONFIG_NUMBER_LABEL_DATA[] = "layout-config-number-label-data";
constexpr gchar LAYOUT_CONFIG_TITLE_LABEL_DATA[] = "layout-config-title-label-data";

G_DEFINE_TYPE(LayoutConfigItem, layout_config_item, G_TYPE_OBJECT)

void layout_config_item_finalize(GObject *object)
{
	auto *item = reinterpret_cast<LayoutConfigItem *>(object);
	g_free(item->title);

	G_OBJECT_CLASS(layout_config_item_parent_class)->finalize(object);
}

void layout_config_item_class_init(LayoutConfigItemClass *item_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(item_class);
	object_class->finalize = layout_config_item_finalize;
}

void layout_config_item_init(LayoutConfigItem *)
{
}

LayoutConfigItem *layout_config_item_new(gint key)
{
	auto *item = reinterpret_cast<LayoutConfigItem *>(g_object_new(layout_config_item_get_type(), nullptr));
	item->title = g_strdup(_(layout_titles[key]));
	item->key = key;

	return item;
}

gint layout_config_list_order_get(LayoutConfig *lc, gint n)
{
	g_autoptr(GObject) object = static_cast<GObject *>(g_list_model_get_item(G_LIST_MODEL(lc->store), n));
	if (!object) return 0;

	return reinterpret_cast<LayoutConfigItem *>(object)->key;
}

void layout_config_widget_click_cb(GtkWidget *widget, gpointer data)
{
	LayoutConfig *lc;

	lc = static_cast<LayoutConfig *>(g_object_get_data(G_OBJECT(widget), "layout_config"));

	if (lc && gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)))
		{
		lc->style = GPOINTER_TO_INT(data);
		}
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
	gtk_widget_set_focusable(button, FALSE);
	gtk_grid_attach(GTK_GRID(table), button, x1, y1, x2 - x1, y2 - y1);
}

GtkWidget *layout_config_widget(GtkWidget *group, GtkWidget *box, gint style, LayoutConfig *lc)
{
	GtkWidget *table;
	LayoutStyle ls;

	ls = layout_config_styles[style];

	if (group)
		{
		GtkWidget *sibling = group;
		group = gtk_check_button_new();
		gtk_check_button_set_group(GTK_CHECK_BUTTON(group), GTK_CHECK_BUTTON(sibling));
		}
	else
		{
		group = gtk_check_button_new();
		}

	g_object_set_data(G_OBJECT(group), "layout_config", lc);
	g_signal_connect(G_OBJECT(group), "toggled",
	                 G_CALLBACK(layout_config_widget_click_cb), GINT_TO_POINTER(style));
	gtk_box_append(GTK_BOX(box), group);

	table = gtk_grid_new();

	layout_config_table_button(table, ls.a, "1");
	layout_config_table_button(table, ls.b, "2");
	layout_config_table_button(table, ls.c, "3");

	gtk_widget_set_size_request(table, LAYOUT_STYLE_SIZE, LAYOUT_STYLE_SIZE);
	gtk_check_button_set_child(GTK_CHECK_BUTTON(group), table);


	return group;
}

void layout_config_number_update(GtkListItem *list_item, GParamSpec *, gpointer data)
{
	GtkWidget *label = GTK_WIDGET(data);
	const guint position = gtk_list_item_get_position(list_item);

	if (position == GTK_INVALID_LIST_POSITION)
		{
		gtk_label_set_text(GTK_LABEL(label), "");
		return;
		}

	gtk_label_set_text(GTK_LABEL(label), std::to_string(position + 1).c_str());
}

GdkContentProvider *layout_config_drag_prepare(GtkDragSource *source, gdouble, gdouble, gpointer)
{
	GtkWidget *row_widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(source));
	auto *list_item = static_cast<GtkListItem *>(g_object_get_data(G_OBJECT(row_widget), LAYOUT_CONFIG_LIST_ITEM_DATA));
	auto *item = static_cast<LayoutConfigItem *>(gtk_list_item_get_item(list_item));

	return item ? gdk_content_provider_new_typed(layout_config_item_get_type(), item) : nullptr;
}

gboolean layout_config_drop(GtkDropTarget *target, const GValue *value, gdouble, gdouble y, gpointer data)
{
	auto *lc = static_cast<LayoutConfig *>(data);
	auto *source_item = static_cast<LayoutConfigItem *>(g_value_get_object(value));
	if (!source_item) return FALSE;

	GtkWidget *row_widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(target));
	auto *target_list_item = static_cast<GtkListItem *>(g_object_get_data(G_OBJECT(row_widget), LAYOUT_CONFIG_LIST_ITEM_DATA));
	const guint target_position = gtk_list_item_get_position(target_list_item);
	if (target_position == GTK_INVALID_LIST_POSITION) return FALSE;

	const guint count = g_list_model_get_n_items(G_LIST_MODEL(lc->store));
	guint source_position = GTK_INVALID_LIST_POSITION;
	for (guint position = 0; position < count; position++)
		{
		g_autoptr(GObject) object = static_cast<GObject *>(g_list_model_get_item(G_LIST_MODEL(lc->store), position));
		if (object == G_OBJECT(source_item))
			{
			source_position = position;
			break;
			}
		}
	if (source_position == GTK_INVALID_LIST_POSITION) return FALSE;

	guint insert_position = target_position;
	if (y >= gtk_widget_get_height(row_widget) / 2.0) insert_position++;
	if (source_position < insert_position) insert_position--;
	if (source_position == insert_position) return TRUE;

	g_object_ref(source_item);
	g_list_store_remove(lc->store, source_position);
	g_list_store_insert(lc->store, insert_position, source_item);
	g_object_unref(source_item);

	return TRUE;
}

void layout_config_factory_setup(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer data)
{
	auto *lc = static_cast<LayoutConfig *>(data);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	GtkWidget *number_label = gtk_label_new(nullptr);
	GtkWidget *title_label = gtk_label_new(nullptr);

	gtk_widget_set_size_request(number_label, 16, -1);
	gtk_label_set_xalign(GTK_LABEL(number_label), 1.0);
	gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
	gtk_widget_set_hexpand(title_label, TRUE);
	gtk_box_append(GTK_BOX(row), number_label);
	gtk_box_append(GTK_BOX(row), title_label);
	g_object_set_data(G_OBJECT(row), LAYOUT_CONFIG_LIST_ITEM_DATA, list_item);
	g_object_set_data(G_OBJECT(row), LAYOUT_CONFIG_NUMBER_LABEL_DATA, number_label);
	g_object_set_data(G_OBJECT(row), LAYOUT_CONFIG_TITLE_LABEL_DATA, title_label);
	g_signal_connect(list_item, "notify::position", G_CALLBACK(layout_config_number_update), number_label);

	GtkDragSource *drag_source = gtk_drag_source_new();
	gtk_drag_source_set_actions(drag_source, GDK_ACTION_MOVE);
	g_signal_connect(drag_source, "prepare", G_CALLBACK(layout_config_drag_prepare), nullptr);
	gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(drag_source));

	GtkDropTarget *drop_target = gtk_drop_target_new(layout_config_item_get_type(), GDK_ACTION_MOVE);
	g_signal_connect(drop_target, "drop", G_CALLBACK(layout_config_drop), lc);
	gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(drop_target));

	gtk_list_item_set_child(list_item, row);
}

void layout_config_factory_bind(GtkSignalListItemFactory *, GtkListItem *list_item, gpointer)
{
	GtkWidget *row = gtk_list_item_get_child(list_item);
	auto *number_label = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(row), LAYOUT_CONFIG_NUMBER_LABEL_DATA));
	auto *title_label = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(row), LAYOUT_CONFIG_TITLE_LABEL_DATA));
	auto *item = static_cast<LayoutConfigItem *>(gtk_list_item_get_item(list_item));

	layout_config_number_update(list_item, nullptr, number_label);
	gtk_label_set_text(GTK_LABEL(title_label), item->title);
}

gint text_char_to_num(gchar c)
{
	if (c == '3') return 2;
	if (c == '2') return 1;
	return 0;
}

std::tuple<int, int, int> layout_config_order_from_text(const gchar *text)
{
	if (!text || strlen(text) < 3) return { 0, 1, 2 };

	const int a = text_char_to_num(text[0]);
	const int b = text_char_to_num(text[1]);
	const int c = text_char_to_num(text[2]);

	return { a, b, c };
}

void layout_config_list_append(GListStore *store, gint n)
{
	LayoutConfigItem *item = layout_config_item_new(n);
	g_list_store_append(store, item);
	g_object_unref(item);
}

} // namespace

void layout_config_parse(gint style, const gchar *order,
                         LayoutLocation &a, LayoutLocation &b, LayoutLocation &c)
{
	const auto [oa, ob, oc] = layout_config_order_from_text(order);

	style = std::clamp<int>(style, 0, layout_config_styles.size());
	LayoutStyle ls = layout_config_styles[style];

	LayoutLocation *lls[] = { &a, &b, &c };
	*lls[oa] = ls.a;
	*lls[ob] = ls.b;
	*lls[oc] = ls.c;
}

gchar *layout_config_get(GtkWidget *widget, gint *style)
{
	LayoutConfig *lc;

	lc = static_cast<LayoutConfig *>(g_object_get_data(G_OBJECT(widget), "layout_config"));

	/* this should not happen */
	if (!lc) return nullptr;

	*style = lc->style;

	const gint a = layout_config_list_order_get(lc, 0) + 1;
	const gint b = layout_config_list_order_get(lc, 1) + 1;
	const gint c = layout_config_list_order_get(lc, 2) + 1;

	return g_strdup_printf("%d", (100 * a) + (10 * b) + c);
}

GtkWidget *layout_config_new(gint style, const gchar *order)
{
	GtkWidget *hbox;
	GtkWidget *group = nullptr;
	GtkWidget *scrolled;

	auto *lc = new LayoutConfig();

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	g_object_set_data_full(G_OBJECT(box), "layout_config", lc, delete_cb<LayoutConfig>);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_box_append(GTK_BOX(box), hbox);
	for (size_t i = 0; i < layout_config_styles.size(); i++)
		{
		group = layout_config_widget(group, hbox, i, lc);
		lc->style_widgets.push_back(group);
		}
	style = std::clamp<int>(style, 0, layout_config_styles.size() - 1);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(lc->style_widgets[style]), TRUE);

	scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	gtk_box_append(GTK_BOX(box), scrolled);

	lc->store = g_list_store_new(layout_config_item_get_type());
	GtkSelectionModel *selection = GTK_SELECTION_MODEL(gtk_no_selection_new(G_LIST_MODEL(g_object_ref(lc->store))));
	GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
	g_signal_connect(factory, "setup", G_CALLBACK(layout_config_factory_setup), lc);
	g_signal_connect(factory, "bind", G_CALLBACK(layout_config_factory_bind), nullptr);
	lc->list_view = gtk_list_view_new(selection, factory);

	const auto [a, b, c] = layout_config_order_from_text(order);
	layout_config_list_append(lc->store, a);
	layout_config_list_append(lc->store, b);
	layout_config_list_append(lc->store, c);

	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), lc->list_view);

	pref_label_new(box, _("(drag to change order)"));

	return box;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
