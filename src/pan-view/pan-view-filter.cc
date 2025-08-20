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

#include "pan-view-filter.h"

#include <cstddef>

#include <glib-object.h>

#include "compat.h"
#include "filedata.h"
#include "intl.h"
#include "main-defines.h"
#include "metadata.h"
#include "misc.h"
#include "pan-types.h"
#include "pan-view.h"
#include "ui-misc.h"
#include "ui-tabcomp.h"

namespace
{

enum PanViewFilterMode {
	PAN_VIEW_FILTER_REQUIRE,
	PAN_VIEW_FILTER_EXCLUDE,
	PAN_VIEW_FILTER_INCLUDE,
	PAN_VIEW_FILTER_GROUP
};

struct PanViewFilterElement
{
	PanViewFilterMode mode;
	gchar *keyword;
	GRegex *kw_regex;
};

struct PanFilterCallbackState
{
	PanWindow *pw;
	GList *filter_element;
};

void pan_view_filter_element_free(PanViewFilterElement *filter_element)
{
	if (!filter_element) return;

	g_free(filter_element->keyword);
	if (filter_element->kw_regex) g_regex_unref(filter_element->kw_regex);
	g_free(filter_element);
}

void pan_filter_callback_state_free(PanFilterCallbackState *cb_state)
{
	if (!cb_state) return;

	g_list_free_full(cb_state->filter_element, reinterpret_cast<GDestroyNotify>(pan_view_filter_element_free));
	g_free(cb_state);
}

void pan_filter_kw_button_cb(GtkButton *widget, gpointer data)
{
	auto cb_state = static_cast<PanFilterCallbackState *>(data);
	PanWindow *pw = cb_state->pw;
	PanViewFilterUi *ui = pw->filter_ui;

	ui->filter_elements = g_list_remove_link(ui->filter_elements, cb_state->filter_element);
	widget_remove_from_parent(GTK_WIDGET(widget));
	pan_filter_callback_state_free(cb_state);

	gtk_label_set_text(GTK_LABEL(pw->filter_ui->filter_label), _("Removed keyword…"));
	pan_layout_update(pw);
}

void pan_filter_activate_cb(const gchar *text, gpointer data)
{
	GtkWidget *kw_button;
	auto pw = static_cast<PanWindow *>(data);
	PanViewFilterUi *ui = pw->filter_ui;
	GtkTreeIter iter;

	if (!text) return;

	// Get all relevant state and reset UI.
	GtkTreeModel *filter_mode_model = gtk_combo_box_get_model(GTK_COMBO_BOX(ui->filter_mode_combo));
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(ui->filter_mode_combo), &iter);
	gq_gtk_entry_set_text(GTK_ENTRY(ui->filter_entry), "");
	tab_completion_append_to_history(ui->filter_entry, text);

	// Add new filter element.
	auto element = g_new0(PanViewFilterElement, 1);
	gtk_tree_model_get(filter_mode_model, &iter, 0, &element->mode, -1);
	element->keyword = g_strdup(text);
	if (g_strcmp0(text, g_regex_escape_string(text, -1)))
		{
		// It's an actual regex, so compile
		element->kw_regex = g_regex_new(text, static_cast<GRegexCompileFlags>(G_REGEX_ANCHORED | G_REGEX_OPTIMIZE), G_REGEX_MATCH_ANCHORED, nullptr);
		}
	ui->filter_elements = g_list_append(ui->filter_elements, element);

	// Get the short version of the mode value.
	g_autofree gchar *short_mode = nullptr;
	gtk_tree_model_get(filter_mode_model, &iter, 2, &short_mode, -1);

	// Create the button.
	/** @todo (xsdg): Use MVC so that the button list is an actual representation of the GList */
	g_autofree gchar *label = g_strdup_printf("(%s) %s", short_mode, text);
	kw_button = gtk_button_new_with_label(label);

	gq_gtk_box_pack_start(GTK_BOX(ui->filter_kw_hbox), kw_button, FALSE, FALSE, 0);
	gtk_widget_show(kw_button);

	auto cb_state = g_new0(PanFilterCallbackState, 1);
	cb_state->pw = pw;
	cb_state->filter_element = g_list_last(ui->filter_elements);

	g_signal_connect(G_OBJECT(kw_button), "clicked",
	                 G_CALLBACK(pan_filter_kw_button_cb), cb_state);

	pan_layout_update(pw);
}

void pan_filter_ui_replace_filter_button_arrow(PanViewFilterUi *ui, const gchar *new_icon_name)
{
	GtkWidget *parent = gtk_widget_get_parent(ui->filter_button_arrow);

	gtk_container_remove(GTK_CONTAINER(parent), ui->filter_button_arrow);
	ui->filter_button_arrow = gtk_image_new_from_icon_name(new_icon_name, GTK_ICON_SIZE_BUTTON);

	gq_gtk_box_pack_start(GTK_BOX(parent), ui->filter_button_arrow, FALSE, FALSE, 0);
	gtk_box_reorder_child(GTK_BOX(parent), ui->filter_button_arrow, 0);

	gtk_widget_show(ui->filter_button_arrow);
};

void pan_filter_toggle_cb(GtkWidget *button, gpointer data)
{
	auto *pw = static_cast<PanWindow *>(data);
	PanViewFilterUi *ui = pw->filter_ui;

	gboolean visible = gtk_widget_get_visible(ui->filter_box);
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)) == visible) return;

	gtk_widget_set_visible(ui->filter_box, !visible);

	if (visible)
		{
		pan_filter_ui_replace_filter_button_arrow(ui, GQ_ICON_PAN_UP);
		}
	else
		{
		pan_filter_ui_replace_filter_button_arrow(ui, GQ_ICON_PAN_DOWN);

		gtk_widget_grab_focus(ui->filter_entry);
		}
}

void pan_filter_toggle_button_cb(GtkWidget *, gpointer data)
{
	auto pw = static_cast<PanWindow *>(data);
	PanViewFilterUi *ui = pw->filter_ui;

	gint old_classes = ui->filter_classes;
	ui->filter_classes = 0;

	for (gint i = 0; i < FILE_FORMAT_CLASSES; i++)
	{
		ui->filter_classes |= gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->filter_check_buttons[i])) ? 1 << i : 0;
	}

	if (ui->filter_classes != old_classes)
		pan_layout_update(pw);
}

gchar *pan_view_list_find_kw_pattern(GList *haystack, const PanViewFilterElement *filter)
{
	GList *found_elem;

	if (filter->kw_regex)
		{
		// regex compile succeeded; attempt regex match.
		static const auto regex_cmp = [](gconstpointer data, gconstpointer user_data)
		{
			const auto *keyword = static_cast<const gchar *>(data);
			const auto *kw_regex = static_cast<const GRegex *>(user_data);
			return g_regex_match(kw_regex, keyword, static_cast<GRegexMatchFlags>(0), nullptr) ? 0 : 1;
		};
		found_elem = g_list_find_custom(haystack, filter->kw_regex, regex_cmp);
		}
	else
		{
		// regex compile failed; fall back to exact string match.
		found_elem = g_list_find_custom(haystack, filter->keyword, reinterpret_cast<GCompareFunc>(g_strcmp0));
		}

	return found_elem ? static_cast<gchar *>(found_elem->data) : nullptr;
}

} // namespace

PanViewFilterUi *pan_filter_ui_new(PanWindow *pw)
{
	auto ui = g_new0(PanViewFilterUi, 1);
	GtkWidget *combo;
	GtkWidget *hbox;

	/* Since we're using the GHashTable as a HashSet (in which key and value pointers
	 * are always identical), specifying key _and_ value destructor callbacks will
	 * cause a double-free.
	 */
	{
		GtkTreeIter iter;
		g_autoptr(GtkListStore) filter_mode_model = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
		gtk_list_store_append(filter_mode_model, &iter);
		gtk_list_store_set(filter_mode_model, &iter,
				   0, PAN_VIEW_FILTER_REQUIRE, 1, _("Require"), 2, _("R"), -1);
		gtk_list_store_append(filter_mode_model, &iter);
		gtk_list_store_set(filter_mode_model, &iter,
				   0, PAN_VIEW_FILTER_EXCLUDE, 1, _("Exclude"), 2, _("E"), -1);
		gtk_list_store_append(filter_mode_model, &iter);
		gtk_list_store_set(filter_mode_model, &iter,
				   0, PAN_VIEW_FILTER_INCLUDE, 1, _("Include"), 2, _("I"), -1);
		gtk_list_store_append(filter_mode_model, &iter);
		gtk_list_store_set(filter_mode_model, &iter,
				   0, PAN_VIEW_FILTER_GROUP, 1, _("Group"), 2, _("G"), -1);

		ui->filter_mode_combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(filter_mode_model));
		gtk_widget_set_focus_on_click(ui->filter_mode_combo, FALSE);
		gtk_combo_box_set_active(GTK_COMBO_BOX(ui->filter_mode_combo), 0);

		GtkCellRenderer *render = gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(ui->filter_mode_combo), render, TRUE);
		gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(ui->filter_mode_combo), render, "text", 1, NULL);
	}

	// Build the actual filter UI.
	ui->filter_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_spacer(ui->filter_box, 0);
	pref_label_new(ui->filter_box, _("Keyword Filter:"));

	gq_gtk_box_pack_start(GTK_BOX(ui->filter_box), ui->filter_mode_combo, FALSE, FALSE, 0);
	gtk_widget_show(ui->filter_mode_combo);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gq_gtk_box_pack_start(GTK_BOX(ui->filter_box), hbox, TRUE, TRUE, 0);
	gtk_widget_show(hbox);

	combo = tab_completion_new_with_history(&ui->filter_entry, "", "pan_view_filter", -1,
						pan_filter_activate_cb, pw);
	gq_gtk_box_pack_start(GTK_BOX(hbox), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);

	ui->filter_label = gtk_label_new("");/** @todo (xsdg): Figure out whether it's useful to keep this label around. */

	ui->filter_kw_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gq_gtk_box_pack_start(GTK_BOX(hbox), ui->filter_kw_hbox, TRUE, TRUE, 0);
	gtk_widget_show(ui->filter_kw_hbox);

	// Build the spin-button to show/hide the filter UI.
	ui->filter_button = gtk_toggle_button_new();
	gtk_button_set_relief(GTK_BUTTON(ui->filter_button), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click(ui->filter_button, FALSE);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	gq_gtk_container_add(GTK_WIDGET(ui->filter_button), hbox);
	gtk_widget_show(hbox);
	ui->filter_button_arrow = gtk_image_new_from_icon_name(GQ_ICON_PAN_UP, GTK_ICON_SIZE_BUTTON);
	gq_gtk_box_pack_start(GTK_BOX(hbox), ui->filter_button_arrow, FALSE, FALSE, 0);
	gtk_widget_show(ui->filter_button_arrow);
	pref_label_new(hbox, _("Filter"));

	g_signal_connect(G_OBJECT(ui->filter_button), "clicked",
			 G_CALLBACK(pan_filter_toggle_cb), pw);

	// Add check buttons for filtering by image class
	for (gint i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		ui->filter_check_buttons[i] = gtk_check_button_new_with_label(_(format_class_list[i]));
		gq_gtk_box_pack_start(GTK_BOX(ui->filter_box), ui->filter_check_buttons[i], FALSE, FALSE, 0);
		gtk_widget_show(ui->filter_check_buttons[i]);
		}

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->filter_check_buttons[FORMAT_CLASS_IMAGE]), TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->filter_check_buttons[FORMAT_CLASS_RAWIMAGE]), TRUE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->filter_check_buttons[FORMAT_CLASS_VIDEO]), TRUE);
	ui->filter_classes = (1 << FORMAT_CLASS_IMAGE) | (1 << FORMAT_CLASS_RAWIMAGE) | (1 << FORMAT_CLASS_VIDEO);

	// Connecting the signal before setting the state causes segfault as pw is not yet prepared
	for (GtkWidget *filter_check_button : ui->filter_check_buttons)
		g_signal_connect(GTK_TOGGLE_BUTTON(filter_check_button), "toggled", G_CALLBACK(pan_filter_toggle_button_cb), pw);

	return ui;
}

void pan_filter_ui_destroy(PanViewFilterUi *ui)
{
	if (!ui) return;

	g_list_free_full(ui->filter_elements, reinterpret_cast<GDestroyNotify>(pan_view_filter_element_free));
	g_free(ui);
}

gboolean pan_filter_fd_list(GList **fd_list, GList *filter_elements, gint filter_classes)
{
	GList *work;
	gboolean modified = FALSE;
	GHashTable *seen_kw_table = nullptr;

	if (!fd_list || !*fd_list) return modified;

	// seen_kw_table is only valid in this scope, so don't take ownership of any strings.
	if (filter_elements)
		seen_kw_table = g_hash_table_new_full(g_str_hash, g_str_equal, nullptr, nullptr);

	work = *fd_list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		GList *last_work = work;
		work = work->next;

		gboolean should_reject = FALSE;

		if (!((1 << fd -> format_class) & filter_classes))
			{
			should_reject = TRUE;
			}
		else if (filter_elements)
			{
			/** @todo (xsdg): OPTIMIZATION Do the search inside of metadata.cc to avoid a bunch of string list copies. */
			GList *img_keywords = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);
			gchar *group_kw = nullptr; // group_kw references an item from img_keywords.

			/** @todo (xsdg): OPTIMIZATION Determine a heuristic for when to linear-search the keywords list, and when to build a hash table for the image's keywords. */
			GList *filter_element = filter_elements;

			while (filter_element)
				{
				auto filter = static_cast<PanViewFilterElement *>(filter_element->data);
				filter_element = filter_element->next;
				gchar *found_kw = pan_view_list_find_kw_pattern(img_keywords, filter);
				gboolean has_kw = !!found_kw;

				switch (filter->mode)
					{
					case PAN_VIEW_FILTER_REQUIRE:
						should_reject |= !has_kw;
						break;
					case PAN_VIEW_FILTER_EXCLUDE:
						should_reject |= has_kw;
						break;
					case PAN_VIEW_FILTER_INCLUDE:
						if (has_kw) should_reject = FALSE;
						break;
					case PAN_VIEW_FILTER_GROUP:
						if (has_kw)
							{
							if (g_hash_table_contains(seen_kw_table, found_kw))
								{
								should_reject = TRUE;
								}
							else if (group_kw == nullptr)
								{
								group_kw = found_kw;
								}
							}
						break;
					}
				}
			g_list_free_full(img_keywords, g_free);

			if (!should_reject && group_kw != nullptr)
				g_hash_table_add(seen_kw_table, group_kw); // @FIXME group_kw points to freed memory
			}

		if (should_reject)
			{
			*fd_list = g_list_delete_link(*fd_list, last_work);
			modified = TRUE;
			}
		}

	if (filter_elements)
		g_hash_table_destroy(seen_kw_table);

	return modified;
}
