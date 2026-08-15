/*
 * Copyright (C) 2019 The Geeqie Team
 *
 * Author: Colin Clark
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

#include "search-and-run.h"

#include <config.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <pango/pango.h>

#include "actions.h"
#include "compat.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "misc.h"

namespace
{

struct SearchAndRunAction
{
	gchar *label;
	gchar *action_name;
};

struct SarData
{
	GtkWidget *window;
	GtkWidget *entry;
	GtkWidget *popover;
	GtkWidget *command_list;
	GtkWidget *command_scroller;
	std::vector<SearchAndRunAction *> actions;
	LayoutWindow *lw;
	gchar *action_name;
};

void search_and_run_action_free(SearchAndRunAction *action)
{
	if (!action) return;

	g_free(action->label);
	g_free(action->action_name);
	g_free(action);
}

gchar *action_label_from_description(const gchar *description, const gchar *target)
{
	if (!description || !description[0]) return nullptr;

	g_autofree gchar *label = nullptr;
	if (pango_parse_markup(description, -1, '_', nullptr, &label, nullptr, nullptr) && label)
		{
		return target ? g_strdup_printf("%s %s", label, target) : g_strdup(label);
		}

	return target ? g_strdup_printf("%s %s", description, target) : g_strdup(description);
}

gchar *action_accelerator_label(const gchar *action_name)
{
	auto *app = GTK_APPLICATION(g_application_get_default());
	if (!app) return nullptr;

	g_auto(GStrv) accels = gtk_application_get_accels_for_action(app, action_name);
	if (!accels || !accels[0]) return nullptr;

	guint accelerator_key = 0;
	GdkModifierType accelerator_mods = GDK_NO_MODIFIER_MASK;
	gtk_accelerator_parse(accels[0], &accelerator_key, &accelerator_mods);
	if (accelerator_key == 0) return nullptr;

	return gtk_accelerator_get_label(accelerator_key, accelerator_mods);
}

void append_action_to_list(std::vector<SearchAndRunAction *> &actions, const gchar *action_name, const gchar *description, const gchar *target)
{
	if (!action_name || !action_name[0]) return;

	g_autofree gchar *label = action_label_from_description(description, target);
	if (!label) return;

	for (const SearchAndRunAction *action : actions)
		{
		if (g_strcmp0(action->action_name, action_name) == 0) return;
		}

	g_autofree gchar *accelerator = action_accelerator_label(action_name);
	g_autofree gchar *command = accelerator
	                            ? g_strdup_printf("%s : <b>%s</b>", label, accelerator)
	                            : g_strdup(label);

	auto *action = g_new0(SearchAndRunAction, 1);
	action->label = g_steal_pointer(&command);
	action->action_name = g_strdup(action_name);

	actions.push_back(action);
}

void append_actions_from_table(std::vector<SearchAndRunAction *> &actions, const ActionDef *action_defs)
{
	for (const ActionDef *action = action_defs; action->action_name != nullptr; action++)
		{
		if (!action->description) continue;

		if (action->targets)
			{
			for (gint i = 0; action->targets[i] != nullptr; i++)
				{
				g_autofree gchar *detailed_action = g_strdup_printf("%s('%s')", action->action_name, action->targets[i]);
				append_action_to_list(actions, detailed_action, action->description, action->targets[i]);
				}
			}
		else if (!action->parameter_type)
			{
			append_action_to_list(actions, action->action_name, action->description, nullptr);
			}
		}
}

void command_list_populate(SarData *sar)
{
	const ActionDef *action_sets[] =
		{
		get_app_actions(),
		get_main_actions(),
		nullptr
		};

	for (const ActionDef **action_set = action_sets; *action_set != nullptr; action_set++)
		{
		append_actions_from_table(sar->actions, *action_set);
		}

	std::sort(sar->actions.begin(), sar->actions.end(), [](const SearchAndRunAction *a, const SearchAndRunAction *b)
		{
		return g_utf8_collate(a->label, b->label) < 0;
		});
}

void search_and_run_destroy(SarData *sar)
{
	if (!sar) return;

	if (sar->lw && sar->lw->sar_window == sar->window)
		{
		sar->lw->sar_window = nullptr;
		}

	g_signal_handlers_disconnect_by_data(sar->window, sar);
	g_signal_handlers_disconnect_by_data(sar->entry, sar);
	g_signal_handlers_disconnect_by_data(sar->command_list, sar);

	g_clear_pointer(&sar->action_name, g_free);
	for (SearchAndRunAction *action : sar->actions)
		{
		search_and_run_action_free(action);
		}

	GtkWidget *window = sar->window;
	delete sar;
	gq_gtk_widget_destroy(window);
}

GAction *lookup_action_for_detailed_name(SarData *sar, const gchar *detailed_action_name, GVariant **target)
{
	g_autofree gchar *base_name = nullptr;
	g_autoptr(GVariant) parsed_target = nullptr;

	if (!g_action_parse_detailed_name(detailed_action_name, &base_name, &parsed_target, nullptr)) return nullptr;

	GActionMap *action_map = nullptr;
	const gchar *action_name = base_name;
	if (g_str_has_prefix(base_name, "win."))
		{
		action_map = G_ACTION_MAP(sar->lw->window);
		action_name = base_name + 4;
		}
	else if (g_str_has_prefix(base_name, "app."))
		{
		action_map = G_ACTION_MAP(g_application_get_default());
		action_name = base_name + 4;
		}
	else
		{
		action_map = G_ACTION_MAP(sar->lw->window);
		}

	GAction *action = action_map ? g_action_map_lookup_action(action_map, action_name) : nullptr;
	if (!action) return nullptr;

	if (target) *target = parsed_target ? g_variant_ref(parsed_target) : nullptr;

	return action;
}

void activate_action(SarData *sar, const gchar *detailed_action_name)
{
	if (!detailed_action_name) return;

	g_autoptr(GVariant) target = nullptr;
	GAction *action = lookup_action_for_detailed_name(sar, detailed_action_name, &target);
	if (!action) return;

	const GVariantType *state_type = g_action_get_state_type(action);
	if (state_type)
		{
		if (target)
			{
			g_action_change_state(action, target);
			}
		else if (g_variant_type_equal(state_type, G_VARIANT_TYPE_BOOLEAN))
			{
			g_autoptr(GVariant) state = g_action_get_state(action);
			g_action_change_state(action, g_variant_new_boolean(!g_variant_get_boolean(state)));
			}
		else
			{
			g_action_activate(action, nullptr);
			}
		}
	else
		{
		g_action_activate(action, target);
		}
}

gboolean search_and_run_destroy_idle(gpointer data)
{
	search_and_run_destroy(static_cast<SarData *>(data));
	return G_SOURCE_REMOVE;
}

gboolean entry_activate_cb(GtkWidget *, gpointer data)
{
	auto *sar = static_cast<SarData *>(data);

	activate_action(sar, sar->action_name);
	search_and_run_destroy(sar);

	return TRUE;
}

gboolean keypress_cb(GtkEventControllerKey *, guint keyval, guint, GdkModifierType, gpointer data)
{
	auto *sar = static_cast<SarData *>(data);

	if (keyval == GDK_KEY_Escape)
		{
		search_and_run_destroy(sar);
		return TRUE;
		}

	if (keyval == GDK_KEY_Down || keyval == GDK_KEY_Up)
		{
		GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(sar->command_list));
		gint index = row ? gtk_list_box_row_get_index(row) : (keyval == GDK_KEY_Down ? -1 : 1);
		index += keyval == GDK_KEY_Down ? 1 : -1;
		row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(sar->command_list), index);
		if (row)
			{
			gtk_list_box_select_row(GTK_LIST_BOX(sar->command_list), row);

			graphene_rect_t bounds;
			if (gtk_widget_compute_bounds(GTK_WIDGET(row), sar->command_list, &bounds))
				{
				GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sar->command_scroller));
				const gdouble value = gtk_adjustment_get_value(adjustment);
				const gdouble page_size = gtk_adjustment_get_page_size(adjustment);
				const gdouble row_bottom = bounds.origin.y + bounds.size.height;
				if (bounds.origin.y < value)
					{
					gtk_adjustment_set_value(adjustment, bounds.origin.y);
					}
				else if (row_bottom > value + page_size)
					{
					gtk_adjustment_set_value(adjustment, row_bottom - page_size);
					}
				}
			gtk_widget_grab_focus(sar->entry);
			}
		return TRUE;
		}

	return FALSE;
}

gboolean action_matches(const SearchAndRunAction *action, const gchar *key)
{
	g_autofree gchar *normalized_label = g_utf8_normalize(action->label, -1, G_NORMALIZE_DEFAULT);
	g_autofree gchar *normalized_key = g_utf8_normalize(key, -1, G_NORMALIZE_DEFAULT);
	if (!normalized_label || !normalized_key) return FALSE;

	g_autofree gchar *casefold_label = g_utf8_casefold(normalized_label, -1);
	g_autofree gchar *casefold_key = g_utf8_casefold(normalized_key, -1);

	return g_strstr_len(casefold_label, -1, casefold_key) != nullptr;
}

void command_selected_cb(GtkListBox *, GtkListBoxRow *row, gpointer data)
{
	auto *sar = static_cast<SarData *>(data);
	const auto *action_name = row
	                              ? static_cast<const gchar *>(g_object_get_data(G_OBJECT(row), "action-name"))
	                              : nullptr;

	g_free(sar->action_name);
	sar->action_name = g_strdup(action_name);
}

void command_activated_cb(GtkListBox *, GtkListBoxRow *row, gpointer data)
{
	auto *sar = static_cast<SarData *>(data);
	const auto *action_name = static_cast<const gchar *>(g_object_get_data(G_OBJECT(row), "action-name"));

	activate_action(sar, action_name);
	g_idle_add(search_and_run_destroy_idle, sar);
}

void command_list_filter(SarData *sar, const gchar *key)
{
	while (GtkWidget *child = gtk_widget_get_first_child(sar->command_list))
		{
		gtk_list_box_remove(GTK_LIST_BOX(sar->command_list), child);
		}

	g_clear_pointer(&sar->action_name, g_free);
	if (!key || key[0] == '\0')
		{
		gtk_popover_popdown(GTK_POPOVER(sar->popover));
		return;
		}

	GtkListBoxRow *first_row = nullptr;
	for (const SearchAndRunAction *action : sar->actions)
		{
		if (!action_matches(action, key)) continue;

		GtkWidget *label = gtk_label_new(nullptr);
		gtk_label_set_markup(GTK_LABEL(label), action->label);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_widget_set_hexpand(label, TRUE);
		gtk_widget_set_margin_start(label, 6);
		gtk_widget_set_margin_end(label, 6);
		gtk_widget_set_margin_top(label, 4);
		gtk_widget_set_margin_bottom(label, 4);

		GtkWidget *row = gtk_list_box_row_new();
		gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
		g_object_set_data_full(G_OBJECT(row), "action-name", g_strdup(action->action_name), g_free);
		gtk_list_box_append(GTK_LIST_BOX(sar->command_list), row);
		if (!first_row) first_row = GTK_LIST_BOX_ROW(row);
		}

	if (first_row)
		{
		gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(sar->command_scroller),
		                                          gtk_widget_get_width(sar->entry));
		gtk_list_box_select_row(GTK_LIST_BOX(sar->command_list), first_row);
		gtk_popover_popup(GTK_POPOVER(sar->popover));
		}
	else
		{
		gtk_popover_popdown(GTK_POPOVER(sar->popover));
		}
}

void entry_changed_cb(GtkEditable *editable, gpointer data)
{
	command_list_filter(static_cast<SarData *>(data), gtk_editable_get_text(editable));
}

gboolean window_close_cb(GtkWidget *, gpointer data)
{
	search_and_run_destroy(static_cast<SarData *>(data));
	return TRUE;
}

} // namespace

GtkWidget *search_and_run_new(LayoutWindow *lw)
{
	auto *sar = new SarData();
	sar->lw = lw;
	command_list_populate(sar);

	sar->window = gtk_window_new();
	DEBUG_NAME(sar->window);
	gtk_window_set_title(GTK_WINDOW(sar->window), _("Search and Run command - Geeqie"));
	gtk_window_set_resizable(GTK_WINDOW(sar->window), FALSE);
	gtk_window_set_modal(GTK_WINDOW(sar->window), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(sar->window), GTK_WINDOW(lw->window));
	gtk_window_set_default_size(GTK_WINDOW(sar->window), 500, -1);
	g_signal_connect(sar->window, "close-request", G_CALLBACK(window_close_cb), sar);

	sar->entry = gtk_entry_new();
	gtk_widget_set_tooltip_text(sar->entry, _("Search for commands and run them"));
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(sar->entry), GTK_ENTRY_ICON_PRIMARY, GQ_ICON_FIND);
	gtk_window_set_child(GTK_WINDOW(sar->window), sar->entry);

	sar->command_list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(sar->command_list), GTK_SELECTION_BROWSE);
	g_signal_connect(sar->command_list, "row-selected", G_CALLBACK(command_selected_cb), sar);
	g_signal_connect(sar->command_list, "row-activated", G_CALLBACK(command_activated_cb), sar);

	sar->command_scroller = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sar->command_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(sar->command_scroller), 400);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(sar->command_scroller), TRUE);
	gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(sar->command_scroller), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sar->command_scroller), sar->command_list);

	sar->popover = gtk_popover_new();
	gtk_popover_set_autohide(GTK_POPOVER(sar->popover), FALSE);
	gtk_popover_set_child(GTK_POPOVER(sar->popover), sar->command_scroller);
	gtk_widget_set_parent(sar->popover, sar->entry);

	GtkEventController *controller = gtk_event_controller_key_new();
	g_signal_connect(controller, "key-pressed", G_CALLBACK(keypress_cb), sar);
	gtk_widget_add_controller(sar->entry, controller);
	g_signal_connect(sar->entry, "activate", G_CALLBACK(entry_activate_cb), sar);
	g_signal_connect(sar->entry, "changed", G_CALLBACK(entry_changed_cb), sar);

	gtk_window_present(GTK_WINDOW(sar->window));
	gtk_widget_grab_focus(sar->entry);

	return sar->window;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
