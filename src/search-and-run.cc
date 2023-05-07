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

#include "main.h"
#include "search-and-run.h"

#include "ui-misc.h"
#include "window.h"

enum {
	SAR_LABEL,
	SAR_ACTION,
	SAR_COUNT
};

typedef struct _SarData SarData;
struct _SarData
{
	GtkWidget *window;
	GtkWidget *vbox;
	GtkWidget *entry_box;
	GtkEntryCompletion *completion;
	GtkListStore *command_store;
	GtkAction *action;
	LayoutWindow *lw;
	gboolean match_found;
};

static gint sort_iter_compare_func (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer UNUSED(data))
{
	gint ret = 0;
	gchar *label1, *label2;

	gtk_tree_model_get(model, a, SAR_LABEL, &label1, -1);
	gtk_tree_model_get(model, b, SAR_LABEL, &label2, -1);

	if (label1 == NULL || label2 == NULL)
		{
		if (label1 == NULL && label2 == NULL)
			{
			ret = 0;
			}
		else
			{
			ret = (label1 == NULL) ? -1 : 1;
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
	GList *groups, *actions;
	GtkAction *action;
	const gchar *accel_path;
	GtkAccelKey key;
	GtkTreeIter iter;
	GtkTreeSortable *sortable;
	gchar *label, *tooltip;
	gchar *label2, *tooltip2;
	GString *new_command;
	gchar *existing_command;
	gboolean iter_found;
	gboolean duplicate_command;
	gchar *accel;

	sar->command_store = gtk_list_store_new(SAR_COUNT, G_TYPE_STRING, G_TYPE_OBJECT);

	sortable = GTK_TREE_SORTABLE(sar->command_store);
	gtk_tree_sortable_set_sort_func(sortable, SAR_LABEL, sort_iter_compare_func, NULL, NULL);

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
				if (g_strrstr(accel_path, ".desktop") != NULL || tooltip != NULL)
					{
					if (pango_parse_markup(label, -1, '_', NULL, &label2, NULL, NULL) && label2)
						{
						g_free(label);
						label = label2;
						}
					if (tooltip)
						{
						if (pango_parse_markup(tooltip, -1, '_', NULL, &tooltip2, NULL, NULL) && label2)
							{
							g_free(tooltip);
							tooltip = tooltip2;
							}
						}

					new_command = g_string_new(NULL);
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
	SarData *sar = static_cast<SarData *>(data);

	sar->lw->sar_window = NULL;
	gtk_widget_destroy(sar->window);

	return TRUE;
}

static gboolean entry_box_activate_cb(GtkWidget *UNUSED(widget), gpointer data)
{
	SarData *sar = static_cast<SarData *>(data);

	if (sar->action)
		{
		gtk_action_activate(sar->action);
		}

	search_and_run_destroy(sar);

	return TRUE;
}

static gboolean keypress_cb(GtkWidget *UNUSED(widget), GdkEventKey *event, gpointer data)
{
	SarData *sar = static_cast<SarData *>(data);
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
			sar->action = NULL;
		}

	return ret;
}

static gboolean match_selected_cb(GtkEntryCompletion *UNUSED(widget), GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	SarData *sar = static_cast<SarData *>(data);

	gtk_tree_model_get(GTK_TREE_MODEL(model), iter, SAR_ACTION, &sar->action, -1);

	if (sar->action)
		{
		gtk_action_activate(sar->action);
		}

	g_idle_add((GSourceFunc)search_and_run_destroy, sar);

	return TRUE;
}

static gboolean match_func(GtkEntryCompletion *completion, const gchar *key, GtkTreeIter *iter, gpointer data)
{
	SarData *sar = static_cast<SarData *>(data);
	gboolean ret = FALSE;
	gchar *normalized;
	GtkTreeModel *model;
	GtkAction *action;
	gchar *label;
	GString *reg_exp_str;
	GRegex *reg_exp;
	GError *error = NULL;

	model = gtk_entry_completion_get_model(completion);
	gtk_tree_model_get(GTK_TREE_MODEL(model), iter, SAR_LABEL, &label, -1);
	gtk_tree_model_get(GTK_TREE_MODEL(model), iter, SAR_ACTION, &action, -1);

	normalized = g_utf8_normalize(label, -1, G_NORMALIZE_DEFAULT);

	reg_exp_str = g_string_new("\\b(\?=.*:)");
	reg_exp_str = g_string_append(reg_exp_str, key);

	reg_exp = g_regex_new(reg_exp_str->str, G_REGEX_CASELESS, GRegexMatchFlags(0), &error);
	if (error)
		{
		log_printf("Error: could not compile regular expression %s\n%s\n", reg_exp_str->str, error->message);
		g_error_free(error);
		error = NULL;
		reg_exp = g_regex_new("", GRegexCompileFlags(0), GRegexMatchFlags(0), NULL);
		}

	ret = g_regex_match(reg_exp, normalized, GRegexMatchFlags(0), NULL);

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
	GdkGeometry geometry;

	sar = g_new0(SarData, 1);
	sar->lw = lw;
	sar->window = window_new(GTK_WINDOW_TOPLEVEL, "sar_window", NULL, NULL, _("Search and Run command"));
	DEBUG_NAME(sar->window);

	geometry.min_width = 500;
	geometry.max_width = 1500;
	geometry.min_height = 10;
	geometry.max_height = 10;
	gtk_window_set_geometry_hints(GTK_WINDOW(sar->window), NULL, &geometry, static_cast<GdkWindowHints>(GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE));

	gtk_window_set_resizable(GTK_WINDOW(sar->window), TRUE);

	sar->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_container_add(GTK_CONTAINER(sar->window), sar->vbox);
	gtk_widget_show(sar->vbox);

	sar->entry_box = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(sar->vbox), sar->entry_box, FALSE, FALSE, 0);
	gtk_widget_show(sar->entry_box);
	gtk_entry_set_icon_from_stock(GTK_ENTRY(sar->entry_box), GTK_ENTRY_ICON_PRIMARY, GTK_STOCK_FIND);
	gtk_widget_show(sar->vbox);
	gtk_widget_set_tooltip_text(sar->entry_box, "Search for commands and run them");
	g_signal_connect(G_OBJECT(sar->entry_box), "key_press_event", G_CALLBACK(keypress_cb), sar);
	g_signal_connect(G_OBJECT(sar->entry_box), "activate", G_CALLBACK(entry_box_activate_cb), sar);

	command_store_populate(sar);

	sar->completion = gtk_entry_completion_new();
	gtk_entry_set_completion(GTK_ENTRY(sar->entry_box), sar->completion);
	gtk_entry_completion_set_inline_completion(sar->completion, FALSE);
	gtk_entry_completion_set_inline_selection(sar->completion, FALSE);
	gtk_entry_completion_set_minimum_key_length(sar->completion, 1);
	gtk_entry_completion_set_match_func(sar->completion, match_func, sar, NULL);
	g_signal_connect(G_OBJECT(sar->completion), "match-selected", G_CALLBACK(match_selected_cb), sar);
	gtk_entry_completion_set_model(sar->completion, GTK_TREE_MODEL(sar->command_store));
	gtk_entry_completion_set_text_column(sar->completion, SAR_LABEL);

	gtk_widget_show(sar->window);
	return sar->window;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
