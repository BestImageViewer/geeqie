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

#include <cstddef>

#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <pango/pango.h>

#include "compat.h"
#include "debug.h"
#include "layout.h"
#include "main-defines.h"

enum {
	SAR_LABEL,
	SAR_ACTION,
	SAR_COUNT
};

struct SarData
{
	GtkWidget *window;
	GtkListStore *command_store;
	GtkAction *action;
	LayoutWindow *lw;
	gboolean match_found;
	GtkBuilder *builder;
};

static gint sort_iter_compare_func (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer)
{
	gint ret = 0;
	gchar *label1;
	gchar *label2;

	gtk_tree_model_get(model, a, SAR_LABEL, &label1, -1);
	gtk_tree_model_get(model, b, SAR_LABEL, &label2, -1);

	if (label1 == nullptr || label2 == nullptr)
		{
		if (label1 == nullptr && label2 == nullptr)
			{
			ret = 0;
			}
		else
			{
			ret = (label1 == nullptr) ? -1 : 1;
			}
		}
	else
		{
		ret = g_utf8_collate(label1, label2);
		}

	g_free(label1);
	g_free(label2);

	return ret;
}

static void command_store_populate(SarData* sar)
{
	GList *groups;
	GList *actions;
	GtkAction *action;
	const gchar *accel_path;
	GtkAccelKey key;
	GtkTreeIter iter;
	GtkTreeSortable *sortable;
	gchar *label;
	gchar *tooltip;
	gchar *label2;
	gchar *tooltip2;
	GString *new_command;
	gchar *existing_command;
	gboolean iter_found;
	gboolean duplicate_command;
	gchar *accel;

	sar->command_store = GTK_LIST_STORE(gtk_builder_get_object(sar->builder, "command_store"));
	sortable = GTK_TREE_SORTABLE(sar->command_store);
	gtk_tree_sortable_set_sort_func(sortable, SAR_LABEL, sort_iter_compare_func, nullptr, nullptr);

	gtk_tree_sortable_set_sort_column_id(sortable, SAR_LABEL, GTK_SORT_ASCENDING);

	groups = gtk_ui_manager_get_action_groups(sar->lw->ui_manager);
	while (groups)
		{
		actions = gtk_action_group_list_actions(GTK_ACTION_GROUP(groups->data));
		while (actions)
			{
			action = GTK_ACTION(actions->data);
			accel_path = gtk_action_get_accel_path(action);
			if (accel_path && gtk_accel_map_lookup_entry(accel_path, &key))
				{
				g_object_get(action, "tooltip", &tooltip, "label", &label, NULL);
				accel = gtk_accelerator_get_label(key.accel_key, key.accel_mods);

				/* menu items with no tooltip are placeholders */
				if (g_strrstr(accel_path, ".desktop") != nullptr || tooltip != nullptr)
					{
					if (pango_parse_markup(label, -1, '_', nullptr, &label2, nullptr, nullptr) && label2)
						{
						g_free(label);
						label = label2;
						}
					if (tooltip)
						{
						if (pango_parse_markup(tooltip, -1, '_', nullptr, &tooltip2, nullptr, nullptr) && label2)
							{
							g_free(tooltip);
							tooltip = tooltip2;
							}
						}

					new_command = g_string_new(nullptr);
					if (g_strcmp0(label, tooltip) == 0)
						{
						g_string_append_printf(new_command, "%s : %s",label, accel);
						}
					else
						{
						g_string_append_printf(new_command, "%s - %s : %s",label, tooltip, accel);
						}

					iter_found = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(sar->command_store), &iter);
					duplicate_command = FALSE;

					while (iter_found)
						{
						gtk_tree_model_get(GTK_TREE_MODEL(sar->command_store), &iter, SAR_LABEL, &existing_command, -1);
						if (g_strcmp0(new_command->str, existing_command) == 0)
							{
							g_free(existing_command);
							duplicate_command = TRUE;
							break;
							}
						g_free(existing_command);
						iter_found = gtk_tree_model_iter_next(GTK_TREE_MODEL(sar->command_store), &iter);
						}

					if (!duplicate_command )
						{
						gtk_list_store_append(sar->command_store, &iter);
						gtk_list_store_set(sar->command_store, &iter,
								SAR_LABEL, new_command->str,
								SAR_ACTION, action,
								-1);
						}
					g_free(label);
					g_free(tooltip);
					g_free(accel);
					g_string_free(new_command, TRUE);
					}
				}
			actions = actions->next;
			}
		groups = groups->next;
		}
}

static gboolean search_and_run_destroy(gpointer data)
{
	auto sar = static_cast<SarData *>(data);

	sar->lw->sar_window = nullptr;
	g_object_unref(gtk_builder_get_object(sar->builder, "completion"));
	g_object_unref(gtk_builder_get_object(sar->builder, "command_store"));
	gq_gtk_widget_destroy(sar->window);
	g_free(sar);

	return G_SOURCE_REMOVE;
}

static gboolean entry_box_activate_cb(GtkWidget *, gpointer data)
{
	auto sar = static_cast<SarData *>(data);

	if (sar->action)
		{
		gtk_action_activate(sar->action);
		}

	search_and_run_destroy(sar);

	return TRUE;
}

static gboolean keypress_cb(GtkWidget *, GdkEventKey *event, gpointer data)
{
	auto sar = static_cast<SarData *>(data);
	gboolean ret = FALSE;

	switch (event->keyval)
		{
		case GDK_KEY_Escape:
			search_and_run_destroy(sar);
			ret = TRUE;
			break;
		case GDK_KEY_Return:
			break;
		default:
			sar->match_found = FALSE;
			sar->action = nullptr;
		}

	return ret;
}

static gboolean match_selected_cb(GtkEntryCompletion *, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	auto sar = static_cast<SarData *>(data);

	gtk_tree_model_get(GTK_TREE_MODEL(model), iter, SAR_ACTION, &sar->action, -1);

	if (sar->action)
		{
		gtk_action_activate(sar->action);
		}

	g_idle_add(static_cast<GSourceFunc>(search_and_run_destroy), sar);

	return TRUE;
}

static gboolean match_func(GtkEntryCompletion *completion, const gchar *key, GtkTreeIter *iter, gpointer data)
{
	auto sar = static_cast<SarData *>(data);
	gboolean ret = FALSE;
	gchar *normalized;
	GtkTreeModel *model;
	GtkAction *action;
	gchar *label;
	GString *reg_exp_str;
	GRegex *reg_exp;
	GError *error = nullptr;

	model = gtk_entry_completion_get_model(completion);
	gtk_tree_model_get(GTK_TREE_MODEL(model), iter, SAR_LABEL, &label, -1);
	gtk_tree_model_get(GTK_TREE_MODEL(model), iter, SAR_ACTION, &action, -1);

	normalized = g_utf8_normalize(label, -1, G_NORMALIZE_DEFAULT);

	reg_exp_str = g_string_new("\\b(\?=.*:)");
	reg_exp_str = g_string_append(reg_exp_str, key);

	reg_exp = g_regex_new(reg_exp_str->str, G_REGEX_CASELESS, static_cast<GRegexMatchFlags>(0), &error);
	if (error)
		{
		log_printf("Error: could not compile regular expression %s\n%s\n", reg_exp_str->str, error->message);
		g_error_free(error);
		error = nullptr;
		reg_exp = g_regex_new("", static_cast<GRegexCompileFlags>(0), static_cast<GRegexMatchFlags>(0), nullptr);
		}

	ret = g_regex_match(reg_exp, normalized, static_cast<GRegexMatchFlags>(0), nullptr);

	if (sar->match_found == FALSE && ret == TRUE)
		{
		sar->action = action;
		sar->match_found = TRUE;
		}

	g_regex_unref(reg_exp);
	g_string_free(reg_exp_str, TRUE);
	g_free(normalized);

	return ret;
}

GtkWidget *search_and_run_new(LayoutWindow *lw)
{
	SarData *sar;

	sar = g_new0(SarData, 1);
	sar->lw = lw;

	sar->builder = gtk_builder_new_from_resource(GQ_RESOURCE_PATH_UI "/search-and-run.ui");
	command_store_populate(sar);

	sar->window = GTK_WIDGET(gtk_builder_get_object(sar->builder, "search_and_run"));
	DEBUG_NAME(sar->window);

	gtk_entry_completion_set_match_func(GTK_ENTRY_COMPLETION(gtk_builder_get_object(sar->builder, "completion")), match_func, sar, nullptr);

	g_signal_connect(G_OBJECT(gtk_builder_get_object(sar->builder, "completion")), "match-selected", G_CALLBACK(match_selected_cb), sar);
	g_signal_connect(G_OBJECT(gtk_builder_get_object(sar->builder, "entry")), "key_press_event", G_CALLBACK(keypress_cb), sar);
	g_signal_connect(G_OBJECT(gtk_builder_get_object(sar->builder, "entry")), "activate", G_CALLBACK(entry_box_activate_cb), sar);

	gtk_widget_show(sar->window);

	return sar->window;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
