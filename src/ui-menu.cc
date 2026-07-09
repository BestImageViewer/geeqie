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

#include "ui-menu.h"

#include <glib/gi18n.h>
#include <pango/pango.h>

#include "editors.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "options.h"

/*
 *-----------------------------------------------------------------------------
 * menu items
 *-----------------------------------------------------------------------------
 */

static void menu_item_add_accelerator(GtkWidget *, GtkWidget *)
{
	/* Temporary GTK4 compatibility stub. */
}

static GtkWidget *menu_item_label_new(const gchar *text, gboolean use_mnemonic)
{
	GtkWidget *label = use_mnemonic ? gtk_label_new_with_mnemonic(text) : gtk_label_new(text);

	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_widget_set_halign(label, GTK_ALIGN_FILL);

	return label;
}

static GtkWidget *menu_item_button_new(const gchar *text, gboolean use_mnemonic)
{
	GtkWidget *item = gtk_button_new();
	GtkWidget *label = menu_item_label_new(text, use_mnemonic);

	gtk_button_set_child(GTK_BUTTON(item), label);
	gtk_button_set_has_frame(GTK_BUTTON(item), FALSE);
	gtk_widget_add_css_class(item, "flat");
	gtk_widget_set_hexpand(item, TRUE);
	gtk_widget_set_halign(item, GTK_ALIGN_FILL);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), item);

	return item;
}

static GtkWidget *menu_item_check_new(const gchar *text)
{
	GtkWidget *item = gtk_check_button_new();
	GtkWidget *label = menu_item_label_new(text, TRUE);

	gtk_check_button_set_child(GTK_CHECK_BUTTON(item), label);
	gtk_widget_add_css_class(item, "flat");
	gtk_widget_set_hexpand(item, TRUE);
	gtk_widget_set_halign(item, GTK_ALIGN_FILL);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), item);

	return item;
}

static void menu_item_finish(GtkWidget *menu, GtkWidget *item, GCallback func, gpointer data)
{
	auto *popover = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(menu), "gq-popover"));

	if (func && GTK_IS_CHECK_BUTTON(item))
		{
		g_signal_connect(G_OBJECT(item), "toggled", func, data);
		if (popover)
			{
			g_signal_connect_swapped(G_OBJECT(item), "toggled", G_CALLBACK(gtk_popover_popdown), popover);
			}
		}
	else if (func && GTK_IS_BUTTON(item))
		{
		g_signal_connect(G_OBJECT(item), "clicked", func, data);
		if (popover)
			{
			g_signal_connect_swapped(G_OBJECT(item), "clicked", G_CALLBACK(gtk_popover_popdown), popover);
			}
		}
	if (GTK_IS_BOX(menu))
		{
		gtk_box_append(GTK_BOX(menu), item);
		}
	gtk_widget_show(item);
}

GtkWidget *popover_item_add(GtkWidget *menu, const gchar *label,
			 GCallback func, gpointer data)
{
	GtkWidget *item;

	item = menu_item_button_new(label, TRUE);

	menu_item_add_accelerator(menu, item);

	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *popover_item_add_stock(GtkWidget *menu, const gchar *label, const gchar *stock_id,
			       GCallback func, gpointer data)
{
	GtkWidget *item;
	(void)stock_id;

	/* Temporary GTK4 stub: stock images are dropped until this path is ported. */
	item = menu_item_button_new(label, TRUE);

	menu_item_add_accelerator(menu, item);

	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *popover_item_add_icon(GtkWidget *menu, const gchar *label, const gchar *icon_name,
			       GCallback func, gpointer data)
{
	GtkWidget *item;
	(void)icon_name;

	/* Temporary GTK4 stub: icons are dropped until this path is ported. */
	item = menu_item_button_new(label, TRUE);

	menu_item_add_accelerator(menu, item);

	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *popover_item_add_sensitive(GtkWidget *menu, const gchar *label, gboolean sensitive,
				   GCallback func, gpointer data)
{
	GtkWidget *item;

	item = popover_item_add(menu, label, func, data);
	gtk_widget_set_sensitive(item, sensitive);

	return item;
}

GtkWidget *popover_item_add_icon_sensitive(GtkWidget *menu, const gchar *label, const gchar *icon_name, gboolean sensitive,
					 GCallback func, gpointer data)
{
	GtkWidget *item;

	item = popover_item_add_icon(menu, label, icon_name, func, data);
	gtk_widget_set_sensitive(item, sensitive);

	return item;
}

GtkWidget *popover_item_add_check(GtkWidget *menu, const gchar *label, gboolean active,
			       GCallback func, gpointer data)
{
	GtkWidget *item;

	item = menu_item_check_new(label);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(item), active);

	menu_item_add_accelerator(menu, item);

	menu_item_finish(menu, item, func, data);

	return item;
}

GtkWidget *popover_item_add_radio(GtkWidget *menu, const gchar *label, gpointer item_data, gboolean active,
			       GCallback func, gpointer data)
{
	GtkWidget *item = popover_item_add_check(menu, label, active, func, data);

	if (item_data) g_object_set_data(G_OBJECT(item), "menu_item_radio_data", item_data);

	return item;
}

gpointer popover_item_radio_get_data(GtkWidget *menu_item)
{
	return g_object_get_data(G_OBJECT(menu_item), "menu_item_radio_data");
}

void popover_item_add_divider(GtkWidget *menu)
{
	GtkWidget *item = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
	menu_item_finish(menu, item, nullptr, nullptr);
}

/**
 * @brief Use to avoid mnemonics, for example filenames
 */
GtkWidget *popover_item_add_simple(GtkWidget *menu, const gchar *label,
				GCallback func, gpointer data)
{
	GtkWidget *item = menu_item_button_new(label, FALSE);
	menu_item_finish(menu, item, func, data);

	return item;
}

/*
 *-----------------------------------------------------------------------------
 * popup menus
 *-----------------------------------------------------------------------------
 */

GtkWidget *popover_box_new(GtkWidget *parent, gdouble x, gdouble y)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	if (parent)
		{
		GtkWidget *popover = gtk_popover_new();
		g_object_set_data(G_OBJECT(box), "gq-popover", popover);
		gtk_popover_set_child(GTK_POPOVER(popover), box);
		popover_set_parent(popover, parent);
		if (x >= 0 && y >= 0)
			{
			GdkRectangle pointing_to{
				static_cast<int>(x),
				static_cast<int>(y),
				1, 1
			};
			gtk_popover_set_pointing_to(GTK_POPOVER(popover), &pointing_to);
			}
		popover_popup(popover);
		}

	return box;
}

GtkWidget *popover_parent_new(GtkWidget *child)
{
#if HAVE_GTK4_22
	GtkWidget *popover_parent = gtk_popover_bin_new();
	gtk_popover_bin_set_child(GTK_POPOVER_BIN(popover_parent), child);

	return popover_parent;
#else
	return child;
#endif
}

void popover_set_parent(GtkWidget *popover, GtkWidget *parent)
{
#if HAVE_GTK4_22
	if (GTK_IS_POPOVER_BIN(parent))
		{
		gtk_popover_bin_set_popover(GTK_POPOVER_BIN(parent), popover);
		return;
		}
#endif

	gtk_widget_set_parent(popover, parent);
}

void popover_popup(GtkWidget *popover)
{
#if HAVE_GTK4_22
	GtkWidget *parent = gtk_widget_get_parent(popover);
	if (parent && GTK_IS_POPOVER_BIN(parent))
		{
		gtk_popover_bin_popup(GTK_POPOVER_BIN(parent));
		return;
		}
#endif

	gtk_popover_present(GTK_POPOVER(popover));
	gtk_popover_popup(GTK_POPOVER(popover));
}

GtkWidget *popup_menu(GMenu *menu_model, GtkWidget *window)
{
	GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu_model));
	popover_set_parent(popover, window);
	gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
	popover_popup(popover);

	return popover;
}

GtkWidget *popup_menu_at(GMenu *menu_model, GtkWidget *parent, gdouble x, gdouble y)
{
	GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu_model));
	GdkRectangle pointing_to{
		static_cast<int>(x),
		static_cast<int>(y),
		1, 1
	};

	popover_set_parent(popover, parent);
	gtk_popover_set_pointing_to(GTK_POPOVER(popover), &pointing_to);
	gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
	popover_popup(popover);

	return popover;
}

static GMenuItem *menu_item_clone_with_new_label(GMenuModel *model, int index, const char *new_label)
{
	g_autoptr(GMenuItem) item = g_menu_item_new(nullptr, nullptr);

	/* Copy all attributes, replacing only "label" */
	g_autoptr(GMenuAttributeIter) attr_iter = g_menu_model_iterate_item_attributes(model, index);
	if (attr_iter)
		{
		const char *name = nullptr;
		GVariant *value = nullptr;

		while (g_menu_attribute_iter_get_next(attr_iter, &name, &value))
			{
			if (g_strcmp0(name, G_MENU_ATTRIBUTE_LABEL) == 0)
				{
				g_menu_item_set_attribute(item, G_MENU_ATTRIBUTE_LABEL, "s", new_label);
				}
			else
				{
				g_menu_item_set_attribute_value(item, name, value);
				}

			g_variant_unref(value);
			}
		}
	else
		{
		/* No attributes at all: at least set the label */
		g_menu_item_set_attribute(item, G_MENU_ATTRIBUTE_LABEL, "s", new_label);
		}

	/* If there was no label attribute originally, add it now */
	g_autofree char *existing_label = nullptr;
	g_menu_model_get_item_attribute(model, index, G_MENU_ATTRIBUTE_LABEL, "s", &existing_label);
	if (!existing_label)
		{
		g_menu_item_set_attribute(item, G_MENU_ATTRIBUTE_LABEL, "s", new_label);
		}

	/* Copy all links (submenu, section, etc.) */
	g_autoptr(GMenuLinkIter) link_iter = g_menu_model_iterate_item_links(model, index);
	if (link_iter)
		{
		const char *name = nullptr;
		GMenuModel *link_model = nullptr;

		while (g_menu_link_iter_get_next(link_iter, &name, &link_model))
			{
			g_menu_item_set_link(item, name, link_model);
			g_object_unref(link_model);
			}
		}

	return g_steal_pointer(&item);
}

bool menu_item_include_ellipsis(GMenuModel *model, const char *action)
{
	if (!model || !action) return FALSE;

	const int n = g_menu_model_get_n_items(model);

	for (int i = 0; i < n; i++)
		{
		g_autofree char *action_i = nullptr;
		g_autofree char *label_i = nullptr;

		g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_ACTION, "s", &action_i);
		g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label_i);

		if (action_i && g_strcmp0(action_i, action) == 0)
			{
			if (!label_i) return TRUE;
			if (g_str_has_suffix(label_i, "…")) return TRUE;

			GMenu *menu = G_MENU(model);
			g_autofree char *new_label = g_strconcat(label_i, "…", nullptr);
			g_autoptr(GMenuItem) new_item = menu_item_clone_with_new_label(model, i, new_label);

			g_menu_remove(menu, i);
			g_menu_insert_item(menu, i, new_item);

			return TRUE;
			}

		GMenuModel *section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
		if (section)
			{
			const bool found = menu_item_include_ellipsis(section, action);
			g_object_unref(section);
			if (found) return TRUE;
			}

		GMenuModel *submenu = g_menu_model_get_item_link(model, i, G_MENU_LINK_SUBMENU);
		if (submenu)
			{
			const bool found = menu_item_include_ellipsis(submenu, action);
			g_object_unref(submenu);
			if (found) return TRUE;
			}
		}

	return FALSE;
}

void plugins_menu_populate(GMenu *plugins_menu, const char *action, GList *fd_list)
{
	EditorsList editors_list = editor_list_get();

	for (EditorDescription *ed : editors_list)
		{
		if (fd_list && editor_errors(editor_command_parse(ed, fd_list, FALSE, nullptr)))
			{
			continue;
			}

		const char *label = (ed->name && *ed->name) ? ed->name : ed->key;
		const char *icon_name = (ed->icon && *ed->icon) ? ed->icon : GQ_ICON_MISSING_IMAGE;

		g_autoptr(GMenuItem) item = g_menu_item_new(label, nullptr);

		g_menu_item_set_action_and_target_value( item, action, g_variant_new_string(ed->key)
		);

		g_autoptr(GIcon) icon = g_themed_icon_new(icon_name);
		g_menu_item_set_icon(item, icon);

		g_menu_append_item(plugins_menu, item);
		}
}

void color_profiles_menu_populate(LayoutWindow *lw, const gchar *action)
{
	const struct
		{
		gint index;
		const gchar *label;
		}
	builtin_profiles[] =
		{
			{ 0, _("Input 0: sRGB") },
			{ 1, _("Input 1: AdobeRGB compatible") },
		};

	GMenu *color_profiles_menu = G_MENU(gtk_builder_get_object(lw->builder, "color-profiles-menu"));
	g_menu_remove_all(color_profiles_menu);

	GAction *image_profile_action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), "main-win-use-image-profile");
	gboolean use_image_profile = g_variant_get_boolean(g_action_get_state(G_ACTION(image_profile_action)));

	for (const auto &profile : builtin_profiles)
		{
		g_autoptr(GMenuItem) item = g_menu_item_new(profile.label, nullptr);

		g_menu_item_set_action_and_target_value(
		    item,
		    action,
		    g_variant_new_int32(profile.index));

		g_menu_append_item(color_profiles_menu, item);
		}

	for (gint i = 0; i < 4; i++)
		{
		if (!options->color_profile.input_file[i] || options->color_profile.input_file[i][0] == '\0')
			{
			continue;
			}

		g_autofree gchar *label = nullptr;

		if (!options->color_profile.input_name[i] || options->color_profile.input_name[i][0] == '\0')
			{
			label = g_strdup_printf(_("Input %d: %s"), i + 2, options->color_profile.input_file[i]);
			}
		else
			{
			label = g_strdup_printf(_("Input %d: %s"), i + 2, options->color_profile.input_name[i]);
			}

		g_autoptr(GMenuItem) item = g_menu_item_new(label, nullptr);

		g_menu_item_set_action_and_target_value(item, action, g_variant_new_int32(i + 2));

		GAction *menu_action = g_action_map_lookup_action(G_ACTION_MAP(lw->window), action + 4);
		g_simple_action_set_enabled(G_SIMPLE_ACTION(menu_action), !use_image_profile);

		g_menu_append_item(color_profiles_menu, item);
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
