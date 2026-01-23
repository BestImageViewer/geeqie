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

#include "bar-keywords.h"

#if HAVE_GTK4
#else

#include <array>
#include <cstdio>
#include <string>

#include <gdk/gdk.h>
#include <glib-object.h>

#include "bar.h"
#include "compat.h"
#include "dnd.h"
#include "filedata.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "rcfile.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"
#include "ui-utildlg.h"

namespace
{

GtkListStore *keyword_store = nullptr;

void bar_pane_keywords_changed(GtkTextBuffer *buffer, gpointer data);

void autocomplete_keywords_list_load(const gchar *path);
gboolean autocomplete_keywords_list_save(const gchar *path);
gboolean autocomplete_activate_cb(GtkWidget *widget, gpointer data);

/*
 *-------------------------------------------------------------------
 * keyword / comment utils
 *-------------------------------------------------------------------
 */

GList *keyword_list_pull_selected(GtkWidget *text_widget)
{
	g_autofree gchar *text = text_widget_text_pull_selected(text_widget);

	return string_to_keywords_list(text);
}

/* the "changed" signal should be blocked before calling this */
void keyword_list_push(GtkWidget *textview, GList *list)
{
	GtkTextBuffer *buffer;
	GtkTextIter start;
	GtkTextIter end;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
	gtk_text_buffer_get_bounds(buffer, &start, &end);
	gtk_text_buffer_delete(buffer, &start, &end);

	while (list)
		{
		auto word = static_cast<const gchar *>(list->data);
		GtkTextIter iter;

		gtk_text_buffer_get_end_iter(buffer, &iter);
		if (word) gtk_text_buffer_insert(buffer, &iter, word, -1);
		gtk_text_buffer_get_end_iter(buffer, &iter);
		gtk_text_buffer_insert(buffer, &iter, "\n", -1);

		list = list->next;
		}
}


/*
 *-------------------------------------------------------------------
 * info bar
 *-------------------------------------------------------------------
 */


enum {
	FILTER_KEYWORD_COLUMN_TOGGLE = 0,
	FILTER_KEYWORD_COLUMN_MARK,
	FILTER_KEYWORD_COLUMN_NAME,
	FILTER_KEYWORD_COLUMN_IS_KEYWORD,
	FILTER_KEYWORD_COLUMN_COUNT
};

GType filter_keyword_column_types[] = {G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN};

struct PaneKeywordsData
{
	PaneData pane;
	GtkWidget *widget;

	GtkWidget *keyword_view;
	GtkWidget *keyword_treeview;

	GtkTreePath *click_tpath;

	gboolean expand_checked;
	gboolean collapse_unchecked;
	gboolean hide_unchecked;

	guint idle_id; /* event source id */
	FileData *fd;
	gchar *key;
	gint height;

	GList *expanded_rows;

	GtkWidget *autocomplete;
};

struct ConfDialogData
{
	PaneKeywordsData *pkd;
	GtkTreePath *click_tpath;

	/* dialog parts */
	GenericDialog *gd;
	GtkWidget *edit_widget;
	gboolean is_keyword;

	gboolean edit_existing;
};


void bar_pane_keywords_write(PaneKeywordsData *pkd)
{
	GList *list;

	if (!pkd->fd) return;

	list = keyword_list_pull(pkd->keyword_view);

	metadata_write_list(pkd->fd, KEYWORD_KEY, list);

	g_list_free_full(list, g_free);
}

gboolean bar_keyword_tree_expand_if_set_cb(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	gboolean set;

	gtk_tree_model_get(model, iter, FILTER_KEYWORD_COLUMN_TOGGLE, &set, -1);

	if (set && !gtk_tree_view_row_expanded(GTK_TREE_VIEW(pkd->keyword_treeview), path))
		{
		gtk_tree_view_expand_to_path(GTK_TREE_VIEW(pkd->keyword_treeview), path);
		}
	return FALSE;
}

gboolean bar_keyword_tree_collapse_if_unset_cb(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	gboolean set;

	gtk_tree_model_get(model, iter, FILTER_KEYWORD_COLUMN_TOGGLE, &set, -1);

	if (!set && gtk_tree_view_row_expanded(GTK_TREE_VIEW(pkd->keyword_treeview), path))
		{
		gtk_tree_view_collapse_row(GTK_TREE_VIEW(pkd->keyword_treeview), path);
		}
	return FALSE;
}

void bar_keyword_tree_sync(PaneKeywordsData *pkd)
{
	GtkTreeModel *model;

	GtkTreeModel *keyword_tree;
	GList *keywords;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

	keywords = keyword_list_pull(pkd->keyword_view);
	keyword_show_set_in(GTK_TREE_STORE(keyword_tree), model, keywords);
	if (pkd->hide_unchecked) keyword_hide_unset_in(GTK_TREE_STORE(keyword_tree), model, keywords);
	g_list_free_full(keywords, g_free);

	gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(model));

	if (pkd->expand_checked) gtk_tree_model_foreach(model, bar_keyword_tree_expand_if_set_cb, pkd);
	if (pkd->collapse_unchecked) gtk_tree_model_foreach(model, bar_keyword_tree_collapse_if_unset_cb, pkd);
}

void bar_pane_keywords_update(PaneKeywordsData *pkd)
{
	GList *keywords = nullptr;
	GList *orig_keywords = nullptr;
	GList *work1;
	GList *work2;
	GtkTextBuffer *keyword_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pkd->keyword_view));

	keywords = metadata_read_list(pkd->fd, KEYWORD_KEY, METADATA_PLAIN);
	orig_keywords = keyword_list_pull(pkd->keyword_view);

	/* compare the lists */
	work1 = keywords;
	work2 = orig_keywords;

	while (work1 && work2)
		{
		if (strcmp(static_cast<const gchar *>(work1->data), static_cast<const gchar *>(work2->data)) != 0) break;
		work1 = work1->next;
		work2 = work2->next;
		}

	if (work1 || work2) /* lists differs */
		{
		g_signal_handlers_block_by_func(keyword_buffer, (gpointer)bar_pane_keywords_changed, pkd);
		keyword_list_push(pkd->keyword_view, keywords);
		bar_keyword_tree_sync(pkd);
		g_signal_handlers_unblock_by_func(keyword_buffer, (gpointer)bar_pane_keywords_changed, pkd);
		}
	g_list_free_full(keywords, g_free);
	g_list_free_full(orig_keywords, g_free);
}

void bar_pane_keywords_set_fd(GtkWidget *pane, FileData *fd)
{
	PaneKeywordsData *pkd;

	pkd = static_cast<PaneKeywordsData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pkd) return;

	file_data_unref(pkd->fd);
	pkd->fd = file_data_ref(fd);

	bar_pane_keywords_update(pkd);
}

void bar_keyword_tree_get_expanded_cb(GtkTreeView *keyword_treeview, GtkTreePath *path,  gpointer data)
{
	auto expanded = static_cast<GList **>(data);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar *path_string;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(keyword_treeview));
	gtk_tree_model_get_iter(model, &iter, path);

	path_string = gtk_tree_model_get_string_from_iter(model, &iter);

	*expanded = g_list_append(*expanded, path_string);
}

void bar_pane_keywords_entry_write_config(gchar *entry, GString *outstr, gint indent)
{
	struct {
		gchar *path;
	} expand;

	expand.path = entry;

	WRITE_NL(); WRITE_STRING("<expanded ");
	WRITE_CHAR(expand, path);
	WRITE_STRING("/>");
}

void bar_pane_keywords_write_config(GtkWidget *pane, GString *outstr, gint indent)
{
	PaneKeywordsData *pkd;
	GList *path_expanded = nullptr;
	gint w;
	gint h;

	pkd = static_cast<PaneKeywordsData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pkd) return;

	gtk_widget_get_size_request(pane, &w, &h);
	pkd->height = h;

	WRITE_NL(); WRITE_STRING("<pane_keywords ");
	write_char_option(outstr, "id", pkd->pane.id);
	write_char_option(outstr, "title", gtk_label_get_text(GTK_LABEL(pkd->pane.title)));
	WRITE_BOOL(pkd->pane, expanded);
	WRITE_CHAR(*pkd, key);
	WRITE_INT(*pkd, height);
	WRITE_STRING(">");
	indent++;

	gtk_tree_view_map_expanded_rows(GTK_TREE_VIEW(pkd->keyword_treeview),
								(bar_keyword_tree_get_expanded_cb), &path_expanded);

	GList *work = g_list_first(path_expanded);
	while (work)
		{
		bar_pane_keywords_entry_write_config(static_cast<gchar *>(work->data), outstr, indent);
		work = work->next;
		}
	g_list_free_full(path_expanded, g_free);

	indent--;
	WRITE_NL();
	WRITE_STRING("</pane_keywords>");
}

gint bar_pane_keywords_event(GtkWidget *bar, GdkEvent *event)
{
	PaneKeywordsData *pkd;

	pkd = static_cast<PaneKeywordsData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!pkd) return FALSE;

	if (gtk_widget_has_focus(pkd->keyword_view)) return gtk_widget_event(pkd->keyword_view, event);

	if (gtk_widget_has_focus(pkd->autocomplete))
		{
		return gtk_widget_event(pkd->autocomplete, event);
		}
	return FALSE;
}

void bar_pane_keywords_keyword_toggle(GtkCellRendererToggle *, const gchar *path, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean active;
	GList *list;
	GtkTreeIter child_iter;
	GtkTreeModel *keyword_tree;

	GtkTextBuffer *keyword_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pkd->keyword_view));

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));

	g_autoptr(GtkTreePath) tpath = gtk_tree_path_new_from_string(path);
	gtk_tree_model_get_iter(model, &iter, tpath);

	gtk_tree_model_get(model, &iter, FILTER_KEYWORD_COLUMN_TOGGLE, &active, -1);
	active = (!active);


	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));
	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &child_iter, &iter);

	list = keyword_list_pull(pkd->keyword_view);
	if (active)
		keyword_tree_set(keyword_tree, &child_iter, &list);
	else
		keyword_tree_reset(keyword_tree, &child_iter, &list);

	g_signal_handlers_block_by_func(keyword_buffer, (gpointer)bar_pane_keywords_changed, pkd);
	keyword_list_push(pkd->keyword_view, list);
	g_list_free_full(list, g_free);
	g_signal_handlers_unblock_by_func(keyword_buffer, (gpointer)bar_pane_keywords_changed, pkd);

	/* call this just once in the end */
	bar_pane_keywords_changed(keyword_buffer, pkd);
}

void bar_pane_keywords_filter_modify(GtkTreeModel *model, GtkTreeIter *iter, GValue *value, gint column, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));
	GtkTreeIter child_iter;

	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &child_iter, iter);

	memset(value, 0, sizeof (GValue));

	switch (column)
		{
		case FILTER_KEYWORD_COLUMN_TOGGLE:
			{
			GList *keywords = keyword_list_pull(pkd->keyword_view);
			gboolean set = keyword_tree_is_set(keyword_tree, &child_iter, keywords);
			g_list_free_full(keywords, g_free);

			g_value_init(value, G_TYPE_BOOLEAN);
			g_value_set_boolean(value, set);
			break;
			}
		case FILTER_KEYWORD_COLUMN_MARK:
			gtk_tree_model_get_value(keyword_tree, &child_iter, KEYWORD_COLUMN_MARK, value);
			break;
		case FILTER_KEYWORD_COLUMN_NAME:
			gtk_tree_model_get_value(keyword_tree, &child_iter, KEYWORD_COLUMN_NAME, value);
			break;
		case FILTER_KEYWORD_COLUMN_IS_KEYWORD:
			gtk_tree_model_get_value(keyword_tree, &child_iter, KEYWORD_COLUMN_IS_KEYWORD, value);
			break;
		default:
			break;
		}
}

gboolean bar_pane_keywords_filter_visible(GtkTreeModel *keyword_tree, GtkTreeIter *iter, gpointer data)
{
	auto filter = static_cast<GtkTreeModel *>(data);

	return !keyword_is_hidden_in(keyword_tree, iter, filter);
}

template<gboolean append>
void bar_pane_keywords_set_selection_cb(GtkWidget *, gpointer data)
{
	auto *pkd = static_cast<PaneKeywordsData *>(data);
	GList *keywords = nullptr;
	GList *work;

	keywords = keyword_list_pull_selected(pkd->keyword_view);

	g_autoptr(FileDataList) list = layout_selection_list(pkd->pane.lw);
	list = file_data_process_groups_in_selection(list, FALSE, nullptr);

	const auto func = append ? metadata_append_list : metadata_write_list;

	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;

		func(fd, KEYWORD_KEY, keywords);
		}

	g_list_free_full(keywords, g_free);
}

void bar_pane_keywords_populate_popup_cb(GtkTextView *, GtkWidget *menu, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);

	menu_item_add_divider(menu);
	menu_item_add_icon(menu, _("Add selected keywords to selected files"), GQ_ICON_ADD,
	                   G_CALLBACK(bar_pane_keywords_set_selection_cb<TRUE>), pkd);
	menu_item_add_icon(menu, _("Replace existing keywords in selected files with selected keywords"), GQ_ICON_REPLACE,
	                   G_CALLBACK(bar_pane_keywords_set_selection_cb<FALSE>), pkd);
}


void bar_pane_keywords_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE | NOTIFY_METADATA)) && fd == pkd->fd)
		{
		DEBUG_1("Notify pane_keywords: %s %04x", fd->path, type);
		bar_pane_keywords_update(pkd);
		}
}

gboolean bar_pane_keywords_changed_idle_cb(gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);

	bar_pane_keywords_write(pkd);
	bar_keyword_tree_sync(pkd);
	pkd->idle_id = 0;
	return G_SOURCE_REMOVE;
}

void bar_pane_keywords_changed(GtkTextBuffer *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);

	if (pkd->idle_id) return;
	/* higher prio than redraw */
	pkd->idle_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE, bar_pane_keywords_changed_idle_cb, pkd, nullptr);
}


/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */

constexpr std::array<GtkTargetEntry, 2> bar_pane_keywords_drag_types{{
	{ const_cast<gchar *>(TARGET_APP_KEYWORD_PATH_STRING), GTK_TARGET_SAME_WIDGET, TARGET_APP_KEYWORD_PATH },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
}};

constexpr std::array<GtkTargetEntry, 2> bar_pane_keywords_drop_types{{
	{ const_cast<gchar *>(TARGET_APP_KEYWORD_PATH_STRING), GTK_TARGET_SAME_WIDGET, TARGET_APP_KEYWORD_PATH },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
}};

#if !HAVE_GTK4
void bar_pane_keywords_dnd_get(GtkWidget *tree_view, GdkDragContext *,
				     GtkSelectionData *selection_data, guint info,
				     guint, gpointer)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeIter child_iter;
	GtkTreeModel *keyword_tree;

	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));

        if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));
	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &child_iter, &iter);

	switch (info)
		{
		case TARGET_APP_KEYWORD_PATH:
			{
			GList *path = keyword_tree_get_path(keyword_tree, &child_iter);
			gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data),
					       8, reinterpret_cast<const guchar *>(&path), sizeof(path));
			break;
			}

		case TARGET_TEXT_PLAIN:
		default:
			{
			g_autofree gchar *name = keyword_get_name(keyword_tree, &child_iter);
			gtk_selection_data_set_text(selection_data, name, -1);
			}
			break;
		}
}

void bar_pane_keywords_dnd_begin(GtkWidget *tree_view, GdkDragContext *context, gpointer)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeIter child_iter;
	GtkTreeModel *keyword_tree;

	GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));

	if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));
	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &child_iter, &iter);

	g_autofree gchar *name = keyword_get_name(keyword_tree, &child_iter);

	dnd_set_drag_label(tree_view, context, name);
}

void bar_pane_keywords_dnd_end(GtkWidget *, GdkDragContext *, gpointer)
{
}


gboolean bar_pane_keywords_dnd_can_move(GtkTreeModel *keyword_tree, GtkTreeIter *src_kw_iter, GtkTreeIter *dest_kw_iter)
{
	GtkTreeIter parent;

	if (dest_kw_iter && keyword_same_parent(keyword_tree, src_kw_iter, dest_kw_iter))
		{
		return TRUE; /* reordering of siblings is ok */
		}
	if (!dest_kw_iter && !gtk_tree_model_iter_parent(keyword_tree, &parent, src_kw_iter))
		{
		return TRUE; /* reordering of top-level siblings is ok */
		}

	g_autofree gchar *src_name = keyword_get_name(keyword_tree, src_kw_iter);
	return !keyword_exists(keyword_tree, nullptr, dest_kw_iter, src_name, FALSE, nullptr);
}

gboolean bar_pane_keywords_dnd_skip_existing(GtkTreeModel *keyword_tree, GtkTreeIter *dest_kw_iter, GList **keywords)
{
	/* we have to find at least one keyword that does not already exist as a sibling of dest_kw_iter */
	GList *work = *keywords;
	while (work)
		{
		auto keyword = static_cast<gchar *>(work->data);
		if (keyword_exists(keyword_tree, nullptr, dest_kw_iter, keyword, FALSE, nullptr))
			{
			GList *next = work->next;
			g_free(keyword);
			*keywords = g_list_delete_link(*keywords, work);
			work = next;
			}
		else
			{
			work = work->next;
			}
		}
	return !!*keywords;
}

void bar_pane_keywords_dnd_receive(GtkWidget *tree_view, GdkDragContext *,
					  gint x, gint y,
					  GtkSelectionData *selection_data, guint info,
					  guint, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *model;

	GtkTreeModel *keyword_tree;
	gboolean src_valid = FALSE;
	GList *new_keywords = nullptr;
	GList *work;

	/* iterators for keyword_tree */
	GtkTreeIter src_kw_iter;
	GtkTreeIter dest_kw_iter;
	GtkTreeIter new_kw_iter;

	g_signal_stop_emission_by_name(tree_view, "drag_data_received");

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view));
	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

	g_autoptr(GtkTreePath) tpath = nullptr;
	GtkTreeViewDropPosition pos;
	gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(tree_view), x, y, &tpath, &pos);
	gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(tree_view), nullptr, pos);

	switch (info)
		{
		case TARGET_APP_KEYWORD_PATH:
			{
			auto path = static_cast<GList *>(*reinterpret_cast<const gpointer *>(gtk_selection_data_get_data(selection_data)));
			src_valid = keyword_tree_get_iter(keyword_tree, &src_kw_iter, path);
			g_list_free_full(path, g_free);
			break;
			}
		default:
			new_keywords = string_to_keywords_list(reinterpret_cast<const gchar *>(gtk_selection_data_get_data(selection_data)));
			break;
		}

	if (tpath)
		{
		GtkTreeIter dest_iter;
		gtk_tree_model_get_iter(model, &dest_iter, tpath);
		gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &dest_kw_iter, &dest_iter);

		if (src_valid && gtk_tree_store_is_ancestor(GTK_TREE_STORE(keyword_tree), &src_kw_iter, &dest_kw_iter))
			{
			/* can't move to it's own child */
			return;
			}

		if (src_valid && keyword_equal(keyword_tree, &src_kw_iter, &dest_kw_iter))
			{
			/* can't move to itself */
			return;
			}

		if ((pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE || pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER) &&
		    !gtk_tree_model_iter_has_child(keyword_tree, &dest_kw_iter))
			{
			/* the node has no children, all keywords can be added */
			gtk_tree_store_append(GTK_TREE_STORE(keyword_tree), &new_kw_iter, &dest_kw_iter);
			}
		else
			{
			if (src_valid && !bar_pane_keywords_dnd_can_move(keyword_tree, &src_kw_iter, &dest_kw_iter))
				{
				/* the keyword can't be moved if the same name already exist */
				return;
				}
			if (new_keywords && !bar_pane_keywords_dnd_skip_existing(keyword_tree, &dest_kw_iter, &new_keywords))
				{
				/* the keywords can't be added if the same name already exist */
				return;
				}

			switch (pos)
				{
				case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
				case GTK_TREE_VIEW_DROP_BEFORE:
					gtk_tree_store_insert_before(GTK_TREE_STORE(keyword_tree), &new_kw_iter, nullptr, &dest_kw_iter);
					break;
				case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
				case GTK_TREE_VIEW_DROP_AFTER:
					gtk_tree_store_insert_after(GTK_TREE_STORE(keyword_tree), &new_kw_iter, nullptr, &dest_kw_iter);
					break;
				}
			}

		}
	else
		{
		if (src_valid && !bar_pane_keywords_dnd_can_move(keyword_tree, &src_kw_iter, nullptr))
			{
			/* the keyword can't be moved if the same name already exist */
			return;
			}
		if (new_keywords && !bar_pane_keywords_dnd_skip_existing(keyword_tree, nullptr, &new_keywords))
			{
			/* the keywords can't be added if the same name already exist */
			return;
			}
		gtk_tree_store_append(GTK_TREE_STORE(keyword_tree), &new_kw_iter, nullptr);
		}


	if (src_valid)
		{
		keyword_move_recursive(GTK_TREE_STORE(keyword_tree), &new_kw_iter, &src_kw_iter);
		}

	work = new_keywords;
	while (work)
		{
		auto keyword = static_cast<gchar *>(work->data);
		keyword_set(GTK_TREE_STORE(keyword_tree), &new_kw_iter, keyword, TRUE);
		work = work->next;

		if (work)
			{
			GtkTreeIter add;
			gtk_tree_store_insert_after(GTK_TREE_STORE(keyword_tree), &add, nullptr, &new_kw_iter);
			new_kw_iter = add;
			}
		}
	g_list_free_full(new_keywords, g_free);
	bar_keyword_tree_sync(pkd);
}

gint bar_pane_keywords_dnd_motion(GtkWidget *tree_view, GdkDragContext *context,
					gint x, gint y, guint time, gpointer)
{
	g_autoptr(GtkTreePath) tpath = nullptr;
	GtkTreeViewDropPosition pos;
	gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(tree_view), x, y, &tpath, &pos);
	if (tpath)
		{
		GtkTreeModel *model;
		GtkTreeIter dest_iter;
		model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view));
                gtk_tree_model_get_iter(model, &dest_iter, tpath);
		if (pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE && gtk_tree_model_iter_has_child(model, &dest_iter))
			pos = GTK_TREE_VIEW_DROP_BEFORE;

		if (pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER && gtk_tree_model_iter_has_child(model, &dest_iter))
			pos = GTK_TREE_VIEW_DROP_AFTER;
		}

	gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(tree_view), tpath, pos);

	if (tree_view == gtk_drag_get_source_widget(context))
		gdk_drag_status(context, GDK_ACTION_MOVE, time);
	else
		gdk_drag_status(context, GDK_ACTION_COPY, time);

	return TRUE;
}
#endif

/*
 *-------------------------------------------------------------------
 * edit dialog
 *-------------------------------------------------------------------
 */

void bar_pane_keywords_edit_destroy_cb(GtkWidget *, gpointer data)
{
	auto cdd = static_cast<ConfDialogData *>(data);
	gtk_tree_path_free(cdd->click_tpath);
	g_free(cdd);
}


void bar_pane_keywords_edit_ok_cb(GenericDialog *, gpointer data)
{
	auto cdd = static_cast<ConfDialogData *>(data);
	PaneKeywordsData *pkd = cdd->pkd;
	GtkTreeModel *model;

	GtkTreeModel *keyword_tree;
	GtkTreeIter kw_iter;

	gboolean have_dest = FALSE;

	GList *keywords;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

        if (cdd->click_tpath)
		{
		GtkTreeIter iter;
		if (gtk_tree_model_get_iter(model, &iter, cdd->click_tpath))
			{
			gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &kw_iter, &iter);
			have_dest = TRUE;
			}
		}

	if (cdd->edit_existing && !have_dest) return;

	keywords = keyword_list_pull(cdd->edit_widget);

	if (cdd->edit_existing)
		{
		if (keywords && keywords->data && /* there should be one keyword */
		    !keyword_exists(keyword_tree, nullptr, &kw_iter, static_cast<const gchar *>(keywords->data), TRUE, nullptr))
			{
			keyword_set(GTK_TREE_STORE(keyword_tree), &kw_iter, static_cast<const gchar *>(keywords->data), cdd->is_keyword);
			}
		}
	else
		{
		GList *work = keywords;
		gboolean append_to = FALSE;

		while (work)
			{
			GtkTreeIter add;
			if (keyword_exists(keyword_tree, nullptr, (have_dest || append_to) ? &kw_iter : nullptr, static_cast<const gchar *>(work->data), FALSE, nullptr))
				{
				work = work->next;
				continue;
				}
			if (have_dest)
				{
				gtk_tree_store_append(GTK_TREE_STORE(keyword_tree), &add, &kw_iter);
				}
			else if (append_to)
				{
				gtk_tree_store_insert_after(GTK_TREE_STORE(keyword_tree), &add, nullptr, &kw_iter);
				}
			else
				{
				gtk_tree_store_append(GTK_TREE_STORE(keyword_tree), &add, nullptr);
				append_to = TRUE;
				kw_iter = add;
				}
			keyword_set(GTK_TREE_STORE(keyword_tree), &add, static_cast<const gchar *>(work->data), cdd->is_keyword);
			work = work->next;
			}
		}
	g_list_free_full(keywords, g_free);
}

template<gboolean is_keyword>
void bar_pane_keywords_conf_set_is_keyword_cb(GtkWidget *, gpointer data)
{
	auto cdd = static_cast<ConfDialogData *>(data);
	cdd->is_keyword = is_keyword;
}


/*
 *-------------------------------------------------------------------
 * popup menu
 *-------------------------------------------------------------------
 */

template<gboolean edit_existing>
void bar_pane_keywords_edit_dialog_cb(GtkWidget *, gpointer data)
{
	auto *pkd = static_cast<PaneKeywordsData *>(data);
	ConfDialogData *cdd;
	GenericDialog *gd;
	GtkWidget *table;
	GtkWidget *group;
	GtkWidget *button;

	g_autofree gchar *name = nullptr;
	gboolean is_keyword = TRUE;

	if (edit_existing && pkd->click_tpath)
		{
		GtkTreeModel *model;
		GtkTreeIter iter;
		model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));

		if (gtk_tree_model_get_iter(model, &iter, pkd->click_tpath))
			{
			gtk_tree_model_get(model, &iter,
			                   FILTER_KEYWORD_COLUMN_NAME, &name,
			                   FILTER_KEYWORD_COLUMN_IS_KEYWORD, &is_keyword,
			                   -1);
			}
		else
			{
			return;
			}
		}

	if (edit_existing && !name) return;

	cdd = g_new0(ConfDialogData, 1);
	cdd->pkd =pkd;
	cdd->click_tpath = pkd->click_tpath;
	pkd->click_tpath = nullptr;
	cdd->edit_existing = edit_existing;

	cdd->gd = gd = generic_dialog_new(name ? _("Edit keyword") : _("New keyword"), "keyword_edit",
	                                  pkd->widget, TRUE,
	                                  generic_dialog_dummy_cb, cdd);
	g_signal_connect(G_OBJECT(gd->dialog), "destroy",
			 G_CALLBACK(bar_pane_keywords_edit_destroy_cb), cdd);


	generic_dialog_add_message(gd, nullptr, name ? _("Configure keyword") : _("New keyword"), nullptr, FALSE);

	generic_dialog_add_button(gd, GQ_ICON_OK, "OK",
				  bar_pane_keywords_edit_ok_cb, TRUE);

	table = pref_table_new(gd->vbox, 3, 1, FALSE, TRUE);
	pref_table_label(table, 0, 0, _("Keyword:"), GTK_ALIGN_END);
	cdd->edit_widget = gtk_entry_new();
	gtk_widget_set_size_request(cdd->edit_widget, 300, -1);
	if (name) gq_gtk_entry_set_text(GTK_ENTRY(cdd->edit_widget), name);
	gq_gtk_grid_attach_default(GTK_GRID(table), cdd->edit_widget, 1, 2, 0, 1);
	/* here could eventually be a text view instead of entry */
	generic_dialog_attach_default(gd, cdd->edit_widget);
	gtk_widget_show(cdd->edit_widget);

	group = pref_group_new(gd->vbox, FALSE, _("Keyword type:"), GTK_ORIENTATION_VERTICAL);

	button = pref_radiobutton_new(group, nullptr, _("Active keyword"), is_keyword,
	                              G_CALLBACK(bar_pane_keywords_conf_set_is_keyword_cb<TRUE>), cdd);
	button = pref_radiobutton_new(group, button, _("Helper"), !is_keyword,
	                              G_CALLBACK(bar_pane_keywords_conf_set_is_keyword_cb<FALSE>), cdd);

	cdd->is_keyword = is_keyword;

	gtk_widget_grab_focus(cdd->edit_widget);

	gtk_widget_show(gd->dialog);
}

void bar_pane_keywords_connect_mark_cb(GtkWidget *menu_widget, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);

	GtkTreeModel *model;
	GtkTreeIter iter;

	GtkTreeModel *keyword_tree;
	GtkTreeIter kw_iter;

	gint mark = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_widget), "mark")) - 1;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

        if (!pkd->click_tpath) return;
        if (!gtk_tree_model_get_iter(model, &iter, pkd->click_tpath)) return;

	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &kw_iter, &iter);

	meta_data_connect_mark_with_keyword(keyword_tree, &kw_iter, mark);
}

void bar_pane_keywords_disconnect_marks_ok_cb(GenericDialog *, gpointer)
{
	keyword_tree_disconnect_marks();
}

void bar_pane_keywords_disconnect_marks_cb(GtkWidget *menu_widget, gpointer data)
{
	GenericDialog *gd = generic_dialog_new(_("Marks Keywords"), "marks_keywords",
	                                       menu_widget, TRUE,
	                                       generic_dialog_dummy_cb, data);
	generic_dialog_add_message(gd, GQ_ICON_DIALOG_WARNING,
				_("Disconnect all Marks Keywords connections?"), _("This will disconnect all Marks Keywords connections"), TRUE);
	generic_dialog_add_button(gd, GQ_ICON_OK, "OK", bar_pane_keywords_disconnect_marks_ok_cb, TRUE);

	gtk_widget_show(gd->dialog);
}

void bar_pane_keywords_delete_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *model;
	GtkTreeIter iter;

	GtkTreeModel *keyword_tree;
	GtkTreeIter kw_iter;

        if (!pkd->click_tpath) return;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

        if (!gtk_tree_model_get_iter(model, &iter, pkd->click_tpath)) return;
	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &kw_iter, &iter);

	keyword_delete(GTK_TREE_STORE(keyword_tree), &kw_iter);
}

void bar_pane_keywords_hide_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *model;
	GtkTreeIter iter;

	GtkTreeModel *keyword_tree;
	GtkTreeIter kw_iter;

        if (!pkd->click_tpath) return;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

        if (!gtk_tree_model_get_iter(model, &iter, pkd->click_tpath)) return;
	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &kw_iter, &iter);

	keyword_hide_in(GTK_TREE_STORE(keyword_tree), &kw_iter, model);
}

void bar_pane_keywords_show_all_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *model;

	GtkTreeModel *keyword_tree;

	g_list_free_full(pkd->expanded_rows, g_free);
	pkd->expanded_rows = nullptr;
	gtk_tree_view_map_expanded_rows(GTK_TREE_VIEW(pkd->keyword_treeview),
								(bar_keyword_tree_get_expanded_cb), &pkd->expanded_rows);

	pkd->hide_unchecked = FALSE;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

	keyword_show_all_in(GTK_TREE_STORE(keyword_tree), model);

	if (!pkd->collapse_unchecked) gtk_tree_view_expand_all(GTK_TREE_VIEW(pkd->keyword_treeview));
	bar_keyword_tree_sync(pkd);
}

void bar_pane_keywords_revert_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GList *work;
	gchar *path;

	gtk_tree_view_collapse_all(GTK_TREE_VIEW(pkd->keyword_treeview));

	work = pkd->expanded_rows;
	while (work)
		{
		path = static_cast<gchar *>(work->data);
		g_autoptr(GtkTreePath) tree_path = gtk_tree_path_new_from_string(path);
		gtk_tree_view_expand_to_path(GTK_TREE_VIEW(pkd->keyword_treeview), tree_path);
		work = work->next;
		}

	bar_keyword_tree_sync(pkd);
}

void bar_pane_keywords_expand_checked_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *model;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	gtk_tree_model_foreach(model, bar_keyword_tree_expand_if_set_cb, pkd);
}

void bar_pane_keywords_collapse_all_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);

	g_list_free_full(pkd->expanded_rows, g_free);
	pkd->expanded_rows = nullptr;
	gtk_tree_view_map_expanded_rows(GTK_TREE_VIEW(pkd->keyword_treeview),
								(bar_keyword_tree_get_expanded_cb), &pkd->expanded_rows);

	gtk_tree_view_collapse_all(GTK_TREE_VIEW(pkd->keyword_treeview));

	bar_keyword_tree_sync(pkd);
}

void bar_pane_keywords_revert_hidden_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *model;
	GtkTreeModel *keyword_tree;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

	keyword_revert_hidden_in(GTK_TREE_STORE(keyword_tree), model);

	bar_keyword_tree_sync(pkd);
}

void bar_pane_keywords_collapse_unchecked_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *model;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	gtk_tree_model_foreach(model, bar_keyword_tree_collapse_if_unset_cb, pkd);
}

void bar_pane_keywords_hide_unchecked_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeModel *model;

	GtkTreeModel *keyword_tree;
	GList *keywords;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));

	keywords = keyword_list_pull(pkd->keyword_view);
	keyword_hide_unset_in(GTK_TREE_STORE(keyword_tree), model, keywords);
	g_list_free_full(keywords, g_free);
	bar_keyword_tree_sync(pkd);
}

void bar_pane_keywords_expand_checked_toggle_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	pkd->expand_checked = !pkd->expand_checked;
	bar_keyword_tree_sync(pkd);
}

void bar_pane_keywords_collapse_unchecked_toggle_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	pkd->collapse_unchecked = !pkd->collapse_unchecked;
	bar_keyword_tree_sync(pkd);
}

void bar_pane_keywords_hide_unchecked_toggle_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	pkd->hide_unchecked = !pkd->hide_unchecked;
	bar_keyword_tree_sync(pkd);
}

/**
 * @brief Callback for adding selected keyword to all selected images.
 */
void bar_pane_keywords_add_to_selected_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTreeIter iter; /* This is the iter which initial holds the current keyword */
	GtkTreeIter child_iter;
	GtkTreeModel *model;
	GtkTreeModel *keyword_tree;
	GList *list;
	GList *work;
	GList *keywords = nullptr;

	GtkTextBuffer *keyword_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pkd->keyword_view));

	/* Acquire selected keyword */
	if (pkd->click_tpath)
		{
		gboolean is_keyword = TRUE;

		model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));
	        if (!gtk_tree_model_get_iter(model, &iter, pkd->click_tpath)) return;
		gtk_tree_model_get(model, &iter, FILTER_KEYWORD_COLUMN_IS_KEYWORD, &is_keyword, -1);
		if (!is_keyword) return;
		}
	else
		return;

	keyword_tree = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model));
	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &child_iter, &iter);

	list = keyword_list_pull(pkd->keyword_view); /* Get the left keyword view */

	/* Now set the current image */
	keyword_tree_set(keyword_tree, &child_iter, &list);

	keyword_list_push(pkd->keyword_view, list); /* Set the left keyword view */
	g_list_free_full(list, g_free);

	bar_pane_keywords_changed(keyword_buffer, pkd); /* Get list of all keywords in the hierarchy */

	gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model), &child_iter, &iter);
	keywords = keyword_tree_get(keyword_tree, &child_iter);

	list = layout_selection_list(pkd->pane.lw);
	work = list;
	while (work)
		{
		auto fd = static_cast<FileData *>(work->data);
		work = work->next;
		metadata_append_list(fd, KEYWORD_KEY, keywords);
		}
	file_data_list_free(list);
	g_list_free_full(keywords, g_free);
}

void bar_pane_keywords_menu_popup(GtkWidget *, PaneKeywordsData *pkd, gint x, gint y)
{
	GtkWidget *menu;
	GtkWidget *item;
	GtkWidget *submenu;
	GtkTreeViewDropPosition pos;

	if (pkd->click_tpath) gtk_tree_path_free(pkd->click_tpath);
	pkd->click_tpath = nullptr;
	gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(pkd->keyword_treeview), x, y, &pkd->click_tpath, &pos);

	menu = popup_menu_short_lived();

	menu_item_add_icon(menu, _("New keyword"), GQ_ICON_NEW,
	                   G_CALLBACK(bar_pane_keywords_edit_dialog_cb<FALSE>), pkd);

	menu_item_add_divider(menu);

	if (pkd->click_tpath)
		{
		/* for the entry */
		GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(pkd->keyword_treeview));

		GtkTreeIter iter;
		gtk_tree_model_get_iter(model, &iter, pkd->click_tpath);

		g_autofree gchar *name = nullptr;
		g_autofree gchar *mark = nullptr;
		gboolean is_keyword;
		gtk_tree_model_get(model, &iter,
		                   FILTER_KEYWORD_COLUMN_NAME, &name,
		                   FILTER_KEYWORD_COLUMN_MARK, &mark,
		                   FILTER_KEYWORD_COLUMN_IS_KEYWORD, &is_keyword,
		                   -1);

		if (is_keyword)
			{
			g_autofree gchar *text = g_strdup_printf(_("Add \"%s\" to all selected images"), name);
			menu_item_add_icon(menu, text, GQ_ICON_ADD, G_CALLBACK(bar_pane_keywords_add_to_selected_cb), pkd);
			}
		menu_item_add_divider(menu);

		g_autofree gchar *hide_text = g_strdup_printf(_("Hide \"%s\""), name);
		menu_item_add(menu, hide_text, G_CALLBACK(bar_pane_keywords_hide_cb), pkd);

		submenu = gtk_menu_new();
		for (gint i = 0; i < FILEDATA_MARKS_SIZE; i++)
			{
			g_autofree gchar *text = g_strdup_printf(_("Mark %d"), 1 + (i < 9 ? i : -1));
			item = menu_item_add(submenu, text, G_CALLBACK(bar_pane_keywords_connect_mark_cb), pkd);
			g_object_set_data(G_OBJECT(item), "mark", GINT_TO_POINTER(i + 1));
			}

		if (is_keyword)
			{
			g_autofree gchar *text = g_strdup_printf(_("Connect \"%s\" to mark"), name);
			item = menu_item_add(menu, text, nullptr, nullptr);
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
			}
		menu_item_add_divider(menu);

		g_autofree gchar *edit_text = g_strdup_printf(_("Edit \"%s\""), name);
		menu_item_add_icon(menu, edit_text, GQ_ICON_EDIT,
		                   G_CALLBACK(bar_pane_keywords_edit_dialog_cb<TRUE>), pkd);

		g_autofree gchar *delete_text = g_strdup_printf(_("Remove \"%s\""), name);
		menu_item_add_icon(menu, delete_text, GQ_ICON_DELETE, G_CALLBACK(bar_pane_keywords_delete_cb), pkd);

		if (mark && mark[0])
			{
			g_autofree gchar *text = g_strdup_printf(_("Disconnect \"%s\" from mark %s"), name, mark);
			menu_item_add_icon(menu, text, GQ_ICON_DELETE, G_CALLBACK(bar_pane_keywords_connect_mark_cb), pkd);
			}

		if (is_keyword)
			{
			g_autofree gchar *text = g_strdup_printf("%s", _("Disconnect all Mark Keyword connections"));
			menu_item_add_icon(menu, text, GQ_ICON_DELETE, G_CALLBACK(bar_pane_keywords_disconnect_marks_cb), pkd);
			}
		menu_item_add_divider(menu);
		}
	/* for the pane */


	menu_item_add(menu, _("Expand checked"), G_CALLBACK(bar_pane_keywords_expand_checked_cb), pkd);
	menu_item_add(menu, _("Collapse unchecked"), G_CALLBACK(bar_pane_keywords_collapse_unchecked_cb), pkd);
	menu_item_add(menu, _("Hide unchecked"), G_CALLBACK(bar_pane_keywords_hide_unchecked_cb), pkd);
	menu_item_add(menu, _("Revert all hidden"), G_CALLBACK(bar_pane_keywords_revert_hidden_cb), pkd);
	menu_item_add_divider(menu);
	menu_item_add(menu, _("Show all"), G_CALLBACK(bar_pane_keywords_show_all_cb), pkd);
	menu_item_add(menu, _("Collapse all"), G_CALLBACK(bar_pane_keywords_collapse_all_cb), pkd);
	menu_item_add(menu, _("Revert"), G_CALLBACK(bar_pane_keywords_revert_cb), pkd);
	menu_item_add_divider(menu);

	submenu = gtk_menu_new();
	item = menu_item_add(menu, _("On any change"), nullptr, nullptr);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);

	menu_item_add_check(submenu, _("Expand checked"), pkd->expand_checked, G_CALLBACK(bar_pane_keywords_expand_checked_toggle_cb), pkd);
	menu_item_add_check(submenu, _("Collapse unchecked"), pkd->collapse_unchecked, G_CALLBACK(bar_pane_keywords_collapse_unchecked_toggle_cb), pkd);
	menu_item_add_check(submenu, _("Hide unchecked"), pkd->hide_unchecked, G_CALLBACK(bar_pane_keywords_hide_unchecked_toggle_cb), pkd);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

gboolean bar_pane_keywords_menu_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	if (bevent->button == GDK_BUTTON_SECONDARY)
		{
		bar_pane_keywords_menu_popup(widget, pkd, bevent->x, bevent->y);
		return TRUE;
		}
	return FALSE;
}

/*
 *-------------------------------------------------------------------
 * init
 *-------------------------------------------------------------------
 */

void bar_pane_keywords_destroy(gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);

	g_autofree gchar *path = g_build_filename(get_rc_dir(), "keywords", NULL);
	autocomplete_keywords_list_save(path);

	g_list_free_full(pkd->expanded_rows, g_free);
	if (pkd->click_tpath) gtk_tree_path_free(pkd->click_tpath);
	if (pkd->idle_id) g_source_remove(pkd->idle_id);
	file_data_unregister_notify_func(bar_pane_keywords_notify_cb, pkd);

	file_data_unref(pkd->fd);
	g_free(pkd->key);

	g_free(pkd);
}


GtkWidget *bar_pane_keywords_new(const gchar *id, const gchar *title, const gchar *key, gboolean expanded, gint height)
{
	PaneKeywordsData *pkd;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkTextBuffer *buffer;
	GtkTreeModel *store;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;
	GtkTreeIter iter;
	GtkEntryCompletion *completion;

	pkd = g_new0(PaneKeywordsData, 1);

	pkd->pane.pane_set_fd = bar_pane_keywords_set_fd;
	pkd->pane.pane_event = bar_pane_keywords_event;
	pkd->pane.pane_write_config = bar_pane_keywords_write_config;
	pkd->pane.title = bar_pane_expander_title(title);
	pkd->pane.id = g_strdup(id);
	pkd->pane.type = PANE_KEYWORDS;

	pkd->pane.expanded = expanded;

	pkd->height = height;
	pkd->key = g_strdup(key);

	pkd->expand_checked = TRUE;
	pkd->expanded_rows = nullptr;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	gq_gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	pkd->widget = vbox;
	g_object_set_data_full(G_OBJECT(pkd->widget), "pane_data", pkd, bar_pane_keywords_destroy);
	gtk_widget_set_size_request(pkd->widget, -1, height);
	gtk_widget_show(hbox);

	GtkWidget *scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(hbox), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);

	pkd->keyword_view = gtk_text_view_new();
	gq_gtk_container_add(scrolled, pkd->keyword_view);
	g_signal_connect(G_OBJECT(pkd->keyword_view), "populate-popup",
			 G_CALLBACK(bar_pane_keywords_populate_popup_cb), pkd);
	gtk_widget_show(pkd->keyword_view);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pkd->keyword_view));
	g_signal_connect(G_OBJECT(buffer), "changed",
			 G_CALLBACK(bar_pane_keywords_changed), pkd);

	if (options->show_predefined_keyword_tree)
		{
		scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
		gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
						GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		gq_gtk_box_pack_start(GTK_BOX(hbox), scrolled, TRUE, TRUE, 0);
		gtk_widget_show(scrolled);
		}

	pkd->autocomplete = gtk_entry_new();
	gq_gtk_box_pack_end(GTK_BOX(vbox), pkd->autocomplete, FALSE, FALSE, 0);
	gtk_widget_show(pkd->autocomplete);
	gtk_widget_show(vbox);
	gtk_widget_set_tooltip_text(pkd->autocomplete, _("Keyword autocomplete"));

	g_autofree gchar *path = g_build_filename(get_rc_dir(), "keywords", NULL);
	autocomplete_keywords_list_load(path);

	completion = gtk_entry_completion_new();
	gtk_entry_set_completion(GTK_ENTRY(pkd->autocomplete), completion);
	gtk_entry_completion_set_inline_completion(completion, TRUE);
	gtk_entry_completion_set_inline_selection(completion, TRUE);
	g_object_unref(completion);

	gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(keyword_store));
	gtk_entry_completion_set_text_column(completion, 0);

	g_signal_connect(G_OBJECT(pkd->autocomplete), "activate",
			 G_CALLBACK(autocomplete_activate_cb), pkd);

	GtkTreeStore *keyword_tree = keyword_tree_get_or_new();
	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(keyword_tree), &iter))
		{
		/* keyword tree is empty - fill with defaults */
		keyword_tree_set_default(keyword_tree);
		}

	store = gtk_tree_model_filter_new(GTK_TREE_MODEL(keyword_tree), nullptr);

	gtk_tree_model_filter_set_modify_func(GTK_TREE_MODEL_FILTER(store),
					      FILTER_KEYWORD_COLUMN_COUNT,
					      filter_keyword_column_types,
					      bar_pane_keywords_filter_modify,
					      pkd,
					      nullptr);
	gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(store),
					       bar_pane_keywords_filter_visible,
					       store,
					       nullptr);

	pkd->keyword_treeview = gtk_tree_view_new_with_model(store);
	g_object_unref(store);

	gtk_widget_set_size_request(pkd->keyword_treeview, -1, 400);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(pkd->keyword_treeview), FALSE);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);

	gtk_tree_view_column_add_attribute(column, renderer, "text", FILTER_KEYWORD_COLUMN_MARK);

	gtk_tree_view_append_column(GTK_TREE_VIEW(pkd->keyword_treeview), column);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	renderer = gtk_cell_renderer_toggle_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, renderer, "active", FILTER_KEYWORD_COLUMN_TOGGLE);
	gtk_tree_view_column_add_attribute(column, renderer, "visible", FILTER_KEYWORD_COLUMN_IS_KEYWORD);
	g_signal_connect(G_OBJECT(renderer), "toggled",
			 G_CALLBACK(bar_pane_keywords_keyword_toggle), pkd);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", FILTER_KEYWORD_COLUMN_NAME);

	gtk_tree_view_append_column(GTK_TREE_VIEW(pkd->keyword_treeview), column);
	gtk_tree_view_set_expander_column(GTK_TREE_VIEW(pkd->keyword_treeview), column);

	gq_gtk_drag_source_set(pkd->keyword_treeview,
	                    static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
	                    bar_pane_keywords_drag_types.data(), bar_pane_keywords_drag_types.size(),
	                    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));

	gq_drag_g_signal_connect(G_OBJECT(pkd->keyword_treeview), "drag_data_get",
			 G_CALLBACK(bar_pane_keywords_dnd_get), pkd);

	gq_drag_g_signal_connect(G_OBJECT(pkd->keyword_treeview), "drag_begin",
			 G_CALLBACK(bar_pane_keywords_dnd_begin), pkd);
	gq_drag_g_signal_connect(G_OBJECT(pkd->keyword_treeview), "drag_end",
			 G_CALLBACK(bar_pane_keywords_dnd_end), pkd);

	gq_gtk_drag_dest_set(pkd->keyword_treeview,
	                  static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP),
	                  bar_pane_keywords_drop_types.data(), bar_pane_keywords_drop_types.size(),
	                  static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));

	gq_drag_g_signal_connect(G_OBJECT(pkd->keyword_treeview), "drag_data_received",
			 G_CALLBACK(bar_pane_keywords_dnd_receive), pkd);

	gq_drag_g_signal_connect(G_OBJECT(pkd->keyword_treeview), "drag_motion",
			 G_CALLBACK(bar_pane_keywords_dnd_motion), pkd);

	g_signal_connect(G_OBJECT(pkd->keyword_treeview), "button_release_event",
			 G_CALLBACK(bar_pane_keywords_menu_cb), pkd);

	if (options->show_predefined_keyword_tree)
		{
		gq_gtk_container_add(scrolled, pkd->keyword_treeview);
		gtk_widget_show(pkd->keyword_treeview);
		}

	file_data_register_notify_func(bar_pane_keywords_notify_cb, pkd, NOTIFY_PRIORITY_LOW);

	return pkd->widget;
}

/*
 *-----------------------------------------------------------------------------
 * Autocomplete keywords
 *-----------------------------------------------------------------------------
 */

gboolean autocomplete_activate_cb(GtkWidget *, gpointer data)
{
	auto pkd = static_cast<PaneKeywordsData *>(data);
	GtkTextBuffer *buffer;
	GtkTextIter iter;
	GtkTreeIter iter_t;
	gchar *kw_split;
	gboolean valid;
	gboolean found = FALSE;
	gchar *string;

	g_autofree gchar *entry_text = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(pkd->autocomplete)));
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(pkd->keyword_view));

	kw_split = strtok(entry_text, ",");
	while (kw_split != nullptr)
		{
		g_autofree gchar *kw_cr = g_strconcat(kw_split, "\n", NULL);
		g_strchug(kw_cr);
		gtk_text_buffer_get_end_iter(buffer, &iter);
		gtk_text_buffer_insert(buffer, &iter, kw_cr, -1);

		kw_split = strtok(nullptr, ",");
		}

	g_free(entry_text);
	entry_text = g_strdup(gq_gtk_entry_get_text(GTK_ENTRY(pkd->autocomplete)));

	gq_gtk_entry_set_text(GTK_ENTRY(pkd->autocomplete), "");

	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(keyword_store), &iter_t);
	while (valid)
		{
		gtk_tree_model_get (GTK_TREE_MODEL(keyword_store), &iter_t, 0, &string, -1);
		if (g_strcmp0(entry_text, string) == 0)
			{
			found = TRUE;
			break;
			}
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(keyword_store), &iter_t);
		}

	if (!found)
		{
		gtk_list_store_append (keyword_store, &iter_t);
		gtk_list_store_set(keyword_store, &iter_t, 0, entry_text, -1);
		}

	return FALSE;
}

gint autocomplete_sort_iter_compare_func (GtkTreeModel *model,
									GtkTreeIter *a,
									GtkTreeIter *b,
									gpointer)
{
	return gq_gtk_tree_iter_utf8_collate(model, a, b, 0);
}

void autocomplete_keywords_list_load(const gchar *path)
{
	gchar s_buf[1024];
	gint len;
	GtkTreeIter iter;
	GtkTreeSortable *sortable;

	if (keyword_store) return;
	keyword_store = gtk_list_store_new(1, G_TYPE_STRING);

	sortable = GTK_TREE_SORTABLE(keyword_store);
	gtk_tree_sortable_set_sort_func(sortable, 0, autocomplete_sort_iter_compare_func,
												GINT_TO_POINTER(0), nullptr);

	gtk_tree_sortable_set_sort_column_id(sortable, 0, GTK_SORT_ASCENDING);

	g_autofree gchar *pathl = path_from_utf8(path);
	g_autoptr(FILE) f = fopen(pathl, "r");

	if (!f)
		{
		log_printf("Warning: keywords file %s not loaded", pathl);
		return;
		}

	/* first line must start with Keywords comment */
	if (!fgets(s_buf, sizeof(s_buf), f) ||
	    strncmp(s_buf, "#Keywords", 9) != 0)
		{
		log_printf("Warning: keywords file %s not loaded", pathl);
		return;
		}

	while (fgets(s_buf, sizeof(s_buf), f))
		{
		if (s_buf[0]=='#') continue;

		len = strlen(s_buf);
		if( s_buf[len-1] == '\n' )
			{
			s_buf[len-1] = 0;
			}
		gtk_list_store_append (keyword_store, &iter);
		gtk_list_store_set(keyword_store, &iter, 0, g_strdup(s_buf), -1);
		}
}

gboolean autocomplete_keywords_list_save(const gchar *path)
{
	g_autofree gchar *pathl = path_from_utf8(path);

	g_autoptr(GString) gstring = g_string_new("#Keywords list\n");

	const auto keyword_save = [](GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer data)
	{
		g_autofree gchar *string = nullptr;
		gtk_tree_model_get(model, iter, 0, &string, -1);

		g_string_append_printf(static_cast<GString *>(data), "%s\n", string);

		return FALSE;
	};
	gtk_tree_model_foreach(GTK_TREE_MODEL(keyword_store), keyword_save, gstring);

	g_string_append(gstring, "#end\n");

	return secure_save(pathl, gstring->str, -1);
}

} // namespace
#endif
/*
 *-------------------------------------------------------------------
 * init
 *-------------------------------------------------------------------
 */

GtkWidget *bar_pane_keywords_new_from_config(const gchar **attribute_names, const gchar **attribute_values)
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return nullptr;
#else
	g_autofree gchar *id = g_strdup("keywords");
	g_autofree gchar *title = nullptr;
	g_autofree gchar *key = g_strdup(COMMENT_KEY);
	gboolean expanded = TRUE;
	gint height = 200;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("id", id)) continue;
		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_CHAR_FULL("key", key)) continue;
		if (READ_BOOL_FULL("expanded", expanded)) continue;
		if (READ_INT_FULL("height", height)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	options->info_keywords.height = height;
	bar_pane_translate_title(PANE_KEYWORDS, id, &title);
	return bar_pane_keywords_new(id, title, key, expanded, height);
#endif
}

void bar_pane_keywords_update_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values)
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return;
#else
	PaneKeywordsData *pkd;

	pkd = static_cast<PaneKeywordsData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pkd) return;

	g_autofree gchar *title = nullptr;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_CHAR_FULL("key", pkd->key)) continue;
		if (READ_BOOL_FULL("expanded", pkd->pane.expanded)) continue;
		if (READ_CHAR_FULL("id", pkd->pane.id)) continue;

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}

	if (title)
		{
		bar_pane_translate_title(PANE_KEYWORDS, pkd->pane.id, &title);
		gtk_label_set_text(GTK_LABEL(pkd->pane.title), title);
		}

	bar_update_expander(pane);
	bar_pane_keywords_update(pkd);
#endif
}


void bar_pane_keywords_entry_add_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values)
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return;
#else
	PaneKeywordsData *pkd;
	gchar *path = nullptr;

	pkd = static_cast<PaneKeywordsData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!pkd) return;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("path", path))
			{
			g_autoptr(GtkTreePath) tree_path = gtk_tree_path_new_from_string(path);
			gtk_tree_view_expand_to_path(GTK_TREE_VIEW(pkd->keyword_treeview), tree_path);
			pkd->expanded_rows = g_list_append(pkd->expanded_rows, g_strdup(path));
			continue;
			}

		config_file_error((std::string("Unknown attribute: ") + option + " = " + value).c_str());
		}
#endif
}

/*
 *-------------------------------------------------------------------
 * keyword / comment utils
 *-------------------------------------------------------------------
 */

GList *keyword_list_pull(GtkWidget *text_widget)
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return nullptr;
#else
	g_autofree gchar *text = text_widget_text_pull(text_widget);

	return string_to_keywords_list(text);
#endif
}

GList *keyword_list_get()
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return nullptr;
#else
	GList *ret_list = nullptr;
	gchar *string;
	GtkTreeIter iter;
	gboolean valid;

	if (keyword_store)
		{
		valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(keyword_store), &iter);

		while (valid)
			{
			gtk_tree_model_get (GTK_TREE_MODEL(keyword_store), &iter, 0, &string, -1);
			ret_list = g_list_append(ret_list, string);
			valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(keyword_store), &iter);
			}
		}

	return ret_list;
#endif
}

void keyword_list_set(GList *keyword_list)
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return;
#else
	GtkTreeIter  iter;

	if (!keyword_list) return;

	if (keyword_store)
		{
		gtk_list_store_clear(keyword_store);
		}
	else
		{
		keyword_store = gtk_list_store_new(1, G_TYPE_STRING);
		}

	while (keyword_list)
		{
		gtk_list_store_append (keyword_store, &iter);
		gtk_list_store_set(keyword_store, &iter, 0, keyword_list->data, -1);

		keyword_list = keyword_list->next;
		}
#endif
}

gboolean bar_keywords_autocomplete_focus(LayoutWindow *lw)
{
#if HAVE_GTK4
/* @FIXME GTK4 stub */
	return FALSE;
#else
	GtkWidget *pane = bar_find_pane_by_id(lw->bar, PANE_KEYWORDS, "keywords");
	if (!pane)
		{
		GApplication *app = g_application_get_default();

		g_autoptr(GNotification) notification = g_notification_new("Geeqie");

		g_notification_set_title(notification, _("Keyword Autocomplete"));
		g_notification_set_body(notification, _("The Info Sidebar has not yet been opened"));
		g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_NORMAL);
		g_notification_set_default_action(notification, "app.null");

		g_application_send_notification(G_APPLICATION(app), "keyword-autocomplete-notification", notification);
		return FALSE;
		}

	g_autoptr(GList) children = gq_gtk_widget_get_children(GTK_WIDGET(pane));
	const GList *last_child = g_list_last(children);

	gboolean is_focused = (gtk_window_get_focus(GTK_WINDOW(lw->window)) == last_child->data);
	if (!is_focused)
		{
		gtk_widget_grab_focus(static_cast<GtkWidget *>(last_child->data));
		}

	return is_focused;
#endif
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
