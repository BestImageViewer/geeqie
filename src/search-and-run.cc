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

enum {
	SAR_LABEL,
	SAR_ACTION,
	SAR_COUNT
};

struct SearchAndRunAction
{
	gchar *label;
	gchar *action_name;
};

struct SarData
{
	GtkWidget *window;
	GtkWidget *entry;
	GtkEntryCompletion *completion;
	GtkListStore *command_store;
	LayoutWindow *lw;
	gchar *action_name;
	gboolean match_found;
};

void search_and_run_action_free(SearchAndRunAction *action)
{
	if (!action) return;

	g_free(action->label);
	g_free(action->action_name);
	g_free(action);
}

gint sort_iter_compare_func(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer)
{
	return gq_gtk_tree_iter_utf8_collate(model, a, b, SAR_LABEL);
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
	GdkModifierType accelerator_mods = static_cast<GdkModifierType>(0);
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

void command_store_populate(SarData *sar)
{
	const ActionDef *action_sets[] =
		{
		get_app_actions(),
		get_main_actions(),
		nullptr
		};

	std::vector<SearchAndRunAction *> actions;

	for (const ActionDef **action_set = action_sets; *action_set != nullptr; action_set++)
		{
		append_actions_from_table(actions, *action_set);
		}

	std::sort(actions.begin(), actions.end(), [](const SearchAndRunAction *a, const SearchAndRunAction *b)
		{
		return g_utf8_collate(a->label, b->label) < 0;
		});

	GtkTreeSortable *sortable = GTK_TREE_SORTABLE(sar->command_store);
	gtk_tree_sortable_set_sort_func(sortable, SAR_LABEL, sort_iter_compare_func, nullptr, nullptr);
	gtk_tree_sortable_set_sort_column_id(sortable, SAR_LABEL, GTK_SORT_ASCENDING);

	for (SearchAndRunAction *action : actions)
		{
		GtkTreeIter iter;
		gtk_list_store_append(sar->command_store, &iter);
		gtk_list_store_set(sar->command_store, &iter,
		                   SAR_LABEL, action->label,
		                   SAR_ACTION, action->action_name,
		                   -1);
		}

	for (SearchAndRunAction *action : actions)
		{
		search_and_run_action_free(action);
		}
}

void search_and_run_destroy(SarData *sar)
{
	if (!sar) return;

	if (sar->lw && sar->lw->sar_window == sar->window)
		{
		sar->lw->sar_window = nullptr;
		}

	g_clear_pointer(&sar->action_name, g_free);
	g_clear_object(&sar->completion);
	g_clear_object(&sar->command_store);

	GtkWidget *window = sar->window;
	g_free(sar);
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

	if (keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter)
		{
		sar->match_found = FALSE;
		g_clear_pointer(&sar->action_name, g_free);
		}

	return FALSE;
}

gboolean match_selected_cb(GtkEntryCompletion *, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	auto *sar = static_cast<SarData *>(data);
	g_autofree gchar *action_name = nullptr;

	gtk_tree_model_get(model, iter, SAR_ACTION, &action_name, -1);
	activate_action(sar, action_name);

	g_idle_add(search_and_run_destroy_idle, sar);

	return TRUE;
}

gboolean match_func(GtkEntryCompletion *completion, const gchar *key, GtkTreeIter *iter, gpointer data)
{
	GtkTreeModel *model = gtk_entry_completion_get_model(completion);

	g_autofree gchar *label = nullptr;
	g_autofree gchar *action_name = nullptr;
	gtk_tree_model_get(model, iter, SAR_LABEL, &label, SAR_ACTION, &action_name, -1);

	if (!label) return FALSE;

	g_autofree gchar *normalized_label = g_utf8_normalize(label, -1, G_NORMALIZE_DEFAULT);
	g_autofree gchar *normalized_key = g_utf8_normalize(key, -1, G_NORMALIZE_DEFAULT);
	if (!normalized_label || !normalized_key) return FALSE;

	g_autofree gchar *casefold_label = g_utf8_casefold(normalized_label, -1);
	g_autofree gchar *casefold_key = g_utf8_casefold(normalized_key, -1);

	if (!g_strstr_len(casefold_label, -1, casefold_key)) return FALSE;

	auto *sar = static_cast<SarData *>(data);
	if (!sar->match_found)
		{
		g_free(sar->action_name);
		sar->action_name = g_steal_pointer(&action_name);
		sar->match_found = TRUE;
		}

	return TRUE;
}

gboolean window_close_cb(GtkWidget *, gpointer data)
{
	search_and_run_destroy(static_cast<SarData *>(data));
	return TRUE;
}

} // namespace

GtkWidget *search_and_run_new(LayoutWindow *lw)
{
	auto *sar = g_new0(SarData, 1);
	sar->lw = lw;

	sar->command_store = gtk_list_store_new(SAR_COUNT, G_TYPE_STRING, G_TYPE_STRING);
	command_store_populate(sar);

	sar->completion = gtk_entry_completion_new();
	gtk_entry_completion_set_model(sar->completion, GTK_TREE_MODEL(sar->command_store));
	gtk_entry_completion_set_text_column(sar->completion, SAR_LABEL);
	gtk_entry_completion_set_match_func(sar->completion, match_func, sar, nullptr);

	GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(sar->completion), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(sar->completion), renderer, "markup", SAR_LABEL, NULL);
	g_signal_connect(sar->completion, "match-selected", G_CALLBACK(match_selected_cb), sar);

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
	gtk_entry_set_completion(GTK_ENTRY(sar->entry), sar->completion);
	gtk_window_set_child(GTK_WINDOW(sar->window), sar->entry);

	GtkEventController *controller = gtk_event_controller_key_new();
	g_signal_connect(controller, "key-pressed", G_CALLBACK(keypress_cb), sar);
	gtk_widget_add_controller(sar->entry, controller);
	g_signal_connect(sar->entry, "activate", G_CALLBACK(entry_activate_cb), sar);

	gtk_widget_show(sar->entry);
	gtk_widget_show(sar->window);
	gtk_widget_grab_focus(sar->entry);

	return sar->window;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
