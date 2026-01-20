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

#include "view-dir-tree.h"

#include <unistd.h>

#include <cstdlib>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>

#include "compat-deprecated.h"
#include "filedata.h"
#include "layout.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"
#include "ui-tree-edit.h"
#include "view-dir.h"

namespace
{

struct ViewDirInfoTree
{
	guint drop_expand_id; /**< event source id */
	gint busy_ref;
};

#define VDTREE(_vd_) ((ViewDirInfoTree *)((_vd_)->info))


struct PathData
{
	gchar *name;
	FileData *node;
};

void path_data_free(PathData *pd)
{
	if (!pd) return;

	g_free(pd->name);
	g_free(pd);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(PathData, path_data_free)

} // namespace

static void vdtree_row_expanded(GtkTreeView *treeview, GtkTreeIter *iter, GtkTreePath *tpath, gpointer data);


/*
 *----------------------------------------------------------------------------
 * utils
 *----------------------------------------------------------------------------
 */

static void set_cursor(GtkWidget *widget, gint cursor_type)
{
	if (!widget) return;

	widget_set_cursor(widget, cursor_type);
	deprecated_gdk_flush();
}

static void vdtree_busy_push(ViewDir *vd)
{
	if (VDTREE(vd)->busy_ref == 0) set_cursor(vd->view, GDK_WATCH);
	VDTREE(vd)->busy_ref++;
}

static void vdtree_busy_pop(ViewDir *vd)
{
	if (VDTREE(vd)->busy_ref == 1) set_cursor(vd->view, -1);
	if (VDTREE(vd)->busy_ref > 0) VDTREE(vd)->busy_ref--;
}

gboolean vdtree_find_row(ViewDir *vd, FileData *fd, GtkTreeIter *iter, GtkTreeIter *parent)
{
	GtkTreeModel *store;
	gboolean valid;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	if (parent)
		{
		valid = gtk_tree_model_iter_children(store, iter, parent);
		}
	else
		{
		valid = gtk_tree_model_get_iter_first(store, iter);
		}
	while (valid)
		{
		NodeData *nd;
		GtkTreeIter found;

		gtk_tree_model_get(GTK_TREE_MODEL(store), iter, DIR_COLUMN_POINTER, &nd, -1);
		if (nd->fd == fd) return TRUE;

		if (vdtree_find_row(vd, fd, &found, iter))
			{
			memcpy(iter, &found, sizeof(found));
			return TRUE;
			}

		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), iter);
		}

	return FALSE;
}

static void vdtree_icon_set_by_iter(ViewDir *vd, GtkTreeIter *iter, GdkPixbuf *pixbuf)
{
	GtkTreeModel *store;
	GdkPixbuf *old;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	gtk_tree_model_get(store, iter, DIR_COLUMN_ICON, &old, -1);
	if (old != vd->pf->deny)
		{
		gtk_tree_store_set(GTK_TREE_STORE(store), iter, DIR_COLUMN_ICON, pixbuf, -1);
		}
}

static void vdtree_expand_by_iter(ViewDir *vd, GtkTreeIter *iter, gboolean expand)
{
	GtkTreeModel *store;
	GtkTreePath *tpath;
	NodeData *nd;
	FileData *fd = nullptr;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	tpath = gtk_tree_model_get_path(store, iter);

	if (expand)
		{
		/* block signal handler, icon is set here, the caller of vdtree_expand_by_iter must make sure
		   that the iter is populated */
		g_signal_handlers_block_by_func(G_OBJECT(vd->view), (gpointer)vdtree_row_expanded, vd);
		gtk_tree_view_expand_row(GTK_TREE_VIEW(vd->view), tpath, FALSE);
		gtk_tree_model_get(store, iter, DIR_COLUMN_POINTER, &nd, -1);
		fd = (nd) ? nd->fd : nullptr;

		if (fd && islink(fd->path))
			{
			vdtree_icon_set_by_iter(vd, iter, vd->pf->link);
			}
		else
			{
			vdtree_icon_set_by_iter(vd, iter, vd->pf->open);
			}

		g_signal_handlers_unblock_by_func(G_OBJECT(vd->view), (gpointer)vdtree_row_expanded, vd);
		}
	else
		{
		/* signal handler vdtree_row_collapsed is called, it updates the icon */
		gtk_tree_view_collapse_row(GTK_TREE_VIEW(vd->view), tpath);
		}
	gtk_tree_path_free(tpath);
}

static void vdtree_expand_by_data(ViewDir *vd, FileData *fd, gboolean expand)
{
	GtkTreeIter iter;

	if (vd_find_row(vd, fd, &iter))
		{
		vdtree_expand_by_iter(vd, &iter, expand);
		}
}

static void vdtree_node_free(NodeData *nd)
{
	if (!nd) return;

	if (nd->fd) file_data_unref(nd->fd);
	g_free(nd);
}

/*
 *----------------------------------------------------------------------------
 * dnd
 *----------------------------------------------------------------------------
 */

static gboolean vdtree_dnd_drop_expand_cb(gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	GtkTreeIter iter;

	if (vd->drop_fd && vd_find_row(vd, vd->drop_fd, &iter))
		{
		vdtree_populate_path_by_iter(vd, &iter, FALSE, vd->dir_fd);
		vdtree_expand_by_data(vd, vd->drop_fd, TRUE);
		}

	VDTREE(vd)->drop_expand_id = 0;
	return G_SOURCE_REMOVE;
}

static void vdtree_dnd_drop_expand_cancel(ViewDir *vd)
{
	g_clear_handle_id(&(VDTREE(vd)->drop_expand_id), g_source_remove);
}

static void vdtree_dnd_drop_expand(ViewDir *vd)
{
	vdtree_dnd_drop_expand_cancel(vd);
	VDTREE(vd)->drop_expand_id = g_timeout_add(1000, vdtree_dnd_drop_expand_cb, vd);
}

/*
 *----------------------------------------------------------------------------
 * parts lists
 *----------------------------------------------------------------------------
 */

static GList *parts_list(const gchar *path, const ViewDir *vd)
{
	if (*path != G_DIR_SEPARATOR) return nullptr;

	GList *list = nullptr;

	GtkTreeModel *store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	GtkTreeIter iter;
	gboolean valid = gtk_tree_model_get_iter_first(store, &iter);

	g_auto(GStrv) parts = g_strsplit(path, G_DIR_SEPARATOR_S, -1);

	for (guint i = 0; parts[i]; i++)
		{
		const gchar *name = (i == 0) ? G_DIR_SEPARATOR_S : parts[i];
		if (*name == '\0') continue;

		FileData *fd = nullptr;
		while (valid && !fd)
			{
			NodeData *nd;

			gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &nd, -1);
			if (nd->fd && strcmp(nd->fd->name, name) == 0)
				{
				fd = nd->fd;
				}
			else
				{
				valid = gtk_tree_model_iter_next(store, &iter);
				}
			}

		auto *pd = g_new0(PathData, 1);
		pd->name = g_strdup(name);
		pd->node = fd;

		list = g_list_prepend(list, pd);

		if (fd)
			{
			GtkTreeIter parent;
			memcpy(&parent, &iter, sizeof(parent));
			valid = gtk_tree_model_iter_children(store, &iter, &parent);
			}
		}

	return g_list_reverse(list);
}


/*
 *----------------------------------------------------------------------------
 * node traversal, management
 *----------------------------------------------------------------------------
 */

static gboolean vdtree_find_iter_by_data(ViewDir *vd, GtkTreeIter *parent, NodeData *nd, GtkTreeIter *iter)
{
	GtkTreeModel *store;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	if (!nd || !gtk_tree_model_iter_children(store, iter, parent)) return -1;
	do	{
		NodeData *cnd;

		gtk_tree_model_get(store, iter, DIR_COLUMN_POINTER, &cnd, -1);
		if (cnd == nd) return TRUE;
		} while (gtk_tree_model_iter_next(store, iter));

	return FALSE;
}

static NodeData *vdtree_find_iter_by_name(ViewDir *vd, GtkTreeIter *parent, const gchar *name, GtkTreeIter *iter)
{
	GtkTreeModel *store;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	if (!name || !gtk_tree_model_iter_children(store, iter, parent)) return nullptr;
	do	{
		NodeData *nd;

		gtk_tree_model_get(store, iter, DIR_COLUMN_POINTER, &nd, -1);
		if (nd && strcmp(nd->fd->name, name) == 0) return nd;
		} while (gtk_tree_model_iter_next(store, iter));

	return nullptr;
}

static NodeData *vdtree_find_iter_by_fd(ViewDir *vd, GtkTreeIter *parent, FileData *fd, GtkTreeIter *iter)
{
	GtkTreeModel *store;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	if (!fd || !gtk_tree_model_iter_children(store, iter, parent)) return nullptr;
	do	{
		NodeData *nd;

		gtk_tree_model_get(store, iter, DIR_COLUMN_POINTER, &nd, -1);
		if (nd && nd->fd == fd) return nd;
		} while (gtk_tree_model_iter_next(store, iter));

	return nullptr;
}

static void vdtree_add_by_data(ViewDir *vd, FileData *fd, GtkTreeIter *parent)
{
	GtkTreeStore *store;
	GtkTreeIter child;
	GdkPixbuf *pixbuf;
	GtkTreeIter empty;

	if (!fd) return;

	if (access_file(fd->path, R_OK | X_OK))
		{
		if (islink(fd->path))
			{
			pixbuf = vd->pf->link;
			}
		else if (!access_file(fd->path, W_OK) )
			{
			pixbuf = vd->pf->read_only;
			}
		else
			{
			pixbuf = vd->pf->close;
			}
		}
	else
		{
		pixbuf = vd->pf->deny;
		}

	auto nd = g_new0(NodeData, 1);
	nd->fd = fd;
	nd->version = fd->version;
	nd->expanded = FALSE;
	nd->last_update = time(nullptr);

	g_autofree gchar *link = nullptr;
	if (islink(fd->path))
		{
		link = realpath(fd->path, nullptr);
		}

	store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view)));
	gtk_tree_store_append(store, &child, parent);
	gtk_tree_store_set(store, &child, DIR_COLUMN_POINTER, nd,
					 DIR_COLUMN_ICON, pixbuf,
					 DIR_COLUMN_NAME, nd->fd->name,
					 DIR_COLUMN_LINK, link,
					 DIR_COLUMN_COLOR, FALSE, -1);

	/* all nodes are created with an "empty" node, so that the expander is shown
	 * this is removed when the child is populated */
	auto end = g_new0(NodeData, 1);
	end->fd = nullptr;
	end->expanded = TRUE;

	gtk_tree_store_append(store, &empty, &child);
	gtk_tree_store_set(store, &empty, DIR_COLUMN_POINTER, end,
					  DIR_COLUMN_NAME, "empty", -1);

	if (parent)
		{
		NodeData *pnd;
		GtkTreePath *tpath;

		gtk_tree_model_get(GTK_TREE_MODEL(store), parent, DIR_COLUMN_POINTER, &pnd, -1);
		tpath = gtk_tree_model_get_path(GTK_TREE_MODEL(store), parent);
		if (options->tree_descend_subdirs &&
		    gtk_tree_view_row_expanded(GTK_TREE_VIEW(vd->view), tpath) &&
		    !nd->expanded)
			{
			vdtree_populate_path_by_iter(vd, &child, FALSE, vd->dir_fd);
			}
		gtk_tree_path_free(tpath);
		}
}

gboolean vdtree_populate_path_by_iter(ViewDir *vd, GtkTreeIter *iter, gboolean force, FileData *target_fd)
{
	GtkTreeModel *store;
	GList *list;
	GList *work;
	GList *old;
	time_t current_time;
	GtkTreeIter child;
	NodeData *nd;
	gboolean add_hidden = FALSE;

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	gtk_tree_model_get(store, iter, DIR_COLUMN_POINTER, &nd, -1);

	if (!nd) return FALSE;

	current_time = time(nullptr);

	if (nd->expanded)
		{
		if (!nd->fd || !isdir(nd->fd->path))
			{
			if (vd->click_fd == nd->fd) vd->click_fd = nullptr;
			if (vd->drop_fd == nd->fd) vd->drop_fd = nullptr;
			gtk_tree_store_remove(GTK_TREE_STORE(store), iter);
			vdtree_node_free(nd);
			return FALSE;
			}
		if (!force && current_time - nd->last_update < 2)
			{
			DEBUG_1("Too frequent update of %s", nd->fd->path);
			return TRUE;
			}
		file_data_check_changed_files(nd->fd); /* make sure we have recent info */
		}

	/* when hidden files are not enabled, and the user enters a hidden path,
	 * allow the tree to display that path by specifically inserting the hidden entries
	 */
	if (!options->file_filter.show_hidden_files &&
	    target_fd &&
	    strncmp(nd->fd->path, target_fd->path, strlen(nd->fd->path)) == 0)
		{
		gint n;

		n = strlen(nd->fd->path);
		if (target_fd->path[n] == G_DIR_SEPARATOR && target_fd->path[n+1] == '.')
			add_hidden = TRUE;
		}

	if (nd->expanded && (!force && !add_hidden) && nd->fd->version == nd->version)
		return TRUE;

	vdtree_busy_push(vd);

	filelist_read(nd->fd, nullptr, &list);

	if (add_hidden)
		{
		gint n;

		n = strlen(nd->fd->path) + 1;

		while (target_fd->path[n] != '\0' && target_fd->path[n] != G_DIR_SEPARATOR) n++;

		g_autofree gchar *name8 = g_strndup(target_fd->path, n);

		if (isdir(name8))
			{
			list = g_list_prepend(list, file_data_new_dir(name8));
			}
		}

	old = nullptr;
	if (gtk_tree_model_iter_children(store, &child, iter))
		{
		do	{
			NodeData *cnd;

			gtk_tree_model_get(store, &child, DIR_COLUMN_POINTER, &cnd, -1);
			old = g_list_prepend(old, cnd);
			} while (gtk_tree_model_iter_next(store, &child));
		}

	work = list;
	while (work)
		{
		FileData *fd;

		fd = static_cast<FileData *>(work->data);
		work = work->next;

		if (strcmp(fd->name, ".") == 0 || strcmp(fd->name, "..") == 0)
			{
			file_data_unref(fd);
			}
		else
			{
			NodeData *cnd;

			cnd = vdtree_find_iter_by_fd(vd, iter, fd, &child);
			if (cnd)
				{
				if (cnd->expanded && cnd->version != fd->version)
					{
					vdtree_populate_path_by_iter(vd, &child, FALSE, target_fd);
					}

				gtk_tree_store_set(GTK_TREE_STORE(store), &child, DIR_COLUMN_NAME, fd->name, -1);

				g_autofree gchar *link = nullptr;
				if (islink(fd->path))
					{
					link = realpath(fd->path, nullptr);
					}

				gtk_tree_store_set(GTK_TREE_STORE(store), &child, DIR_COLUMN_LINK, link, -1);

				cnd->version = fd->version;
				old = g_list_remove(old, cnd);
				file_data_unref(fd);
				}
			else
				{
				vdtree_add_by_data(vd, fd, iter);
				}
			}
		}

	work = old;
	while (work)
		{
		auto cnd = static_cast<NodeData *>(work->data);
		work = work->next;

		if (vd->click_fd == cnd->fd) vd->click_fd = nullptr;
		if (vd->drop_fd == cnd->fd) vd->drop_fd = nullptr;

		if (vdtree_find_iter_by_data(vd, iter, cnd, &child))
			{
			gtk_tree_store_remove(GTK_TREE_STORE(store), &child);
			vdtree_node_free(cnd);
			}
		}

	g_list_free(old);
	g_list_free(list);

	vdtree_busy_pop(vd);

	nd->expanded = TRUE;
	nd->last_update = current_time;

	return TRUE;
}

FileData *vdtree_populate_path(ViewDir *vd, FileData *target_fd, gboolean expand, gboolean force)
{
	if (!target_fd) return nullptr;

	vdtree_busy_push(vd);

	g_autolist(PathData) list = parts_list(target_fd->path, vd);

	for (GList *work = list; work; work = work->next)
		{
		auto pd = static_cast<PathData *>(work->data);
		if (pd->node == nullptr)
			{
			if (work == list)
				{
				/* should not happen */
				log_printf("vdtree warning, root node not found\n");
				vdtree_busy_pop(vd);
				return nullptr;
				}

			auto *parent_pd = static_cast<PathData *>(work->prev->data);
			GtkTreeIter parent_iter;
			GtkTreeIter iter;
			NodeData *nd;

			if (!vd_find_row(vd, parent_pd->node, &parent_iter) ||
			    !vdtree_populate_path_by_iter(vd, &parent_iter, force, target_fd) ||
			    (nd = vdtree_find_iter_by_name(vd, &parent_iter, pd->name, &iter)) == nullptr)
				{
				log_printf("vdtree warning, aborted at %s\n", parent_pd->name);
				vdtree_busy_pop(vd);
				return nullptr;
				}

			pd->node = nd->fd;

			if (pd->node)
				{
				if (expand)
					{
					vdtree_expand_by_iter(vd, &parent_iter, TRUE);
					vdtree_expand_by_iter(vd, &iter, TRUE);
					}
				vdtree_populate_path_by_iter(vd, &iter, force, target_fd);
				}
			}
		else
			{
			GtkTreeIter iter;

			if (vd_find_row(vd, pd->node, &iter))
				{
				if (expand)
					{
					vdtree_expand_by_iter(vd, &iter, TRUE);
					}
				vdtree_populate_path_by_iter(vd, &iter, force, target_fd);
				}
			}
		}

	FileData *fd = nullptr;

	if (list)
		{
		GList *last = g_list_last(list);
		auto *pd = static_cast<PathData *>(last->data);
		fd = pd->node;
		}

	vdtree_busy_pop(vd);

	return fd;
}

/*
 *----------------------------------------------------------------------------
 * access
 *----------------------------------------------------------------------------
 */

static gboolean selection_is_ok = FALSE;

static gboolean vdtree_select_cb(GtkTreeSelection *, GtkTreeModel *, GtkTreePath *, gboolean, gpointer)
{
	return selection_is_ok;
}

gboolean vdtree_set_fd(ViewDir *vd, FileData *dir_fd)
{
	FileData *fd;
	GtkTreeIter iter;

	if (!dir_fd) return FALSE;
	if (vd->dir_fd == dir_fd) return TRUE;

	file_data_unref(vd->dir_fd);
	vd->dir_fd = file_data_ref(dir_fd);;

	fd = vdtree_populate_path(vd, vd->dir_fd, TRUE, FALSE);

	if (!fd) return FALSE;

	if (vd_find_row(vd, fd, &iter))
		{
		GtkTreeModel *store;
		GtkTreePath *tpath;
		GtkTreePath *old_tpath;
		GtkTreeSelection *selection;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));

		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vd->view));

		/* hack, such that selection is only allowed to be changed from here */
		selection_is_ok = TRUE;
		gtk_tree_selection_select_iter(selection, &iter);
		selection_is_ok = FALSE;

		gtk_tree_view_get_cursor(GTK_TREE_VIEW(vd->view), &old_tpath, nullptr);
		tpath = gtk_tree_model_get_path(store, &iter);

		if (!old_tpath || gtk_tree_path_compare(tpath, old_tpath) != 0)
			{
			/* setting the cursor scrolls the view; do not do that unless it is necessary */
			gtk_tree_view_set_cursor(GTK_TREE_VIEW(vd->view), tpath, nullptr, FALSE);

			/* gtk_tree_view_set_cursor scrolls the window itself, but it sometimes
			   does not work (switch from dir_list to dir_tree) */
			tree_view_row_make_visible(GTK_TREE_VIEW(vd->view), &iter, TRUE);
			}
		gtk_tree_path_free(tpath);
		gtk_tree_path_free(old_tpath);
		}

	return TRUE;
}

void vdtree_refresh(ViewDir *vd)
{
	vdtree_populate_path(vd, vd->dir_fd, FALSE, TRUE);
}

/*
 *----------------------------------------------------------------------------
 * callbacks
 *----------------------------------------------------------------------------
 */

gboolean vdtree_press_key_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	GtkTreePath *tpath;
	GtkTreeIter iter;
	FileData *fd = nullptr;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(vd->view), &tpath, nullptr);
	if (tpath)
		{
		GtkTreeModel *store;
		NodeData *nd;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &nd, -1);

		gtk_tree_path_free(tpath);

		fd = (nd) ? nd->fd : nullptr;
		}

	switch (event->keyval)
		{
		case GDK_KEY_Menu:
			vd->click_fd = fd;
			vd_color_set(vd, vd->click_fd, TRUE);

			vd->popup = vd_pop_menu(vd, vd->click_fd);
			gtk_menu_popup_at_pointer(GTK_MENU(vd->popup), nullptr);

			return TRUE;
			break;
		case GDK_KEY_plus:
		case GDK_KEY_Right:
		case GDK_KEY_KP_Add:
			if (fd)
				{
				vdtree_populate_path_by_iter(vd, &iter, FALSE, vd->dir_fd);

				if (islink(fd->path))
					{
					vdtree_icon_set_by_iter(vd, &iter, vd->pf->link);
					}
				else
					{
					vdtree_icon_set_by_iter(vd, &iter, vd->pf->open);
					}
				}
			break;
		default:
			break;
		}

	return FALSE;
}

static gboolean vdtree_clicked_on_expander(GtkTreeView *treeview, GtkTreePath *tpath,
				           GtkTreeViewColumn *column, gint x, gint, gint *left_of_expander)
{
	gint depth;
	gint size;
	gint sep;
	gint exp_width;

	if (column != gtk_tree_view_get_expander_column(treeview)) return FALSE;

	gtk_widget_style_get(GTK_WIDGET(treeview), "expander-size", &size, "horizontal-separator", &sep, NULL);
	depth = gtk_tree_path_get_depth(tpath);

	exp_width = sep + size + sep;

	if (x <= depth * exp_width)
		{
		if (left_of_expander) *left_of_expander = !(x >= (depth - 1) * exp_width);
		return TRUE;
		}

	return FALSE;
}

gboolean vdtree_press_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	GtkTreePath *tpath;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	NodeData *nd = nullptr;
	FileData *fd;

	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), bevent->x, bevent->y,
					  &tpath, &column, nullptr, nullptr))
		{
		GtkTreeModel *store;
		gint left_of_expander;

		store = gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
		gtk_tree_model_get_iter(store, &iter, tpath);
		gtk_tree_model_get(store, &iter, DIR_COLUMN_POINTER, &nd, -1);
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), tpath, nullptr, FALSE);

		if (vdtree_clicked_on_expander(GTK_TREE_VIEW(widget), tpath, column, bevent->x, bevent->y, &left_of_expander))
			{
			vd->click_fd = nullptr;

			/* clicking this region should automatically reveal an expander, if necessary
			 * treeview bug: the expander will not expand until a button_motion_event highlights it.
			 */
			if (bevent->button == GDK_BUTTON_PRIMARY &&
			    !left_of_expander &&
			    !gtk_tree_view_row_expanded(GTK_TREE_VIEW(vd->view), tpath))
				{
				vdtree_populate_path_by_iter(vd, &iter, FALSE, vd->dir_fd);

				fd = (nd) ? nd->fd : nullptr;
				if (fd && islink(fd->path))
					{
					vdtree_icon_set_by_iter(vd, &iter, vd->pf->link);
					}
				else
					{
					vdtree_icon_set_by_iter(vd, &iter, vd->pf->open);
					}
				}

			gtk_tree_path_free(tpath);
			return FALSE;
			}

		gtk_tree_path_free(tpath);
		}

	vd->click_fd = (nd) ? nd->fd : nullptr;
	vd_color_set(vd, vd->click_fd, TRUE);

	if (bevent->button == GDK_BUTTON_SECONDARY)
		{
		vd->popup = vd_pop_menu(vd, vd->click_fd);
		gtk_menu_popup_at_pointer(GTK_MENU(vd->popup), nullptr);
		}

	return (bevent->button != GDK_BUTTON_PRIMARY);
}

static void vdtree_update_row(ViewDir *vd, GtkTreeView *treeview, GtkTreeIter *iter, GtkTreePath *tpath, GdkPixbuf *pixbuf)
{
	vdtree_populate_path_by_iter(vd, iter, FALSE, nullptr);

	GtkTreeModel *store = gtk_tree_view_get_model(treeview);
	gtk_tree_model_get_iter(store, iter, tpath);

	NodeData *nd = nullptr;
	gtk_tree_model_get(store, iter, DIR_COLUMN_POINTER, &nd, -1);

	FileData *fd = nd ? nd->fd : nullptr;
	if (fd && islink(fd->path))
		{
		pixbuf = vd->pf->link;
		}
	vdtree_icon_set_by_iter(vd, iter, pixbuf);
}

static void vdtree_row_expanded(GtkTreeView *treeview, GtkTreeIter *iter, GtkTreePath *tpath, gpointer data)
{
	gtk_tree_view_set_tooltip_column(treeview, DIR_COLUMN_LINK);

	auto *vd = static_cast<ViewDir *>(data);
	vdtree_update_row(vd, treeview, iter, tpath, vd->pf->open);
}

static void vdtree_row_collapsed(GtkTreeView *treeview, GtkTreeIter *iter, GtkTreePath *tpath, gpointer data)
{
	auto *vd = static_cast<ViewDir *>(data);
	vdtree_update_row(vd, treeview, iter, tpath, vd->pf->close);
}

static gint vdtree_sort_cb(GtkTreeModel *store, GtkTreeIter *a, GtkTreeIter *b, gpointer data)
{
	NodeData *nda;
	NodeData *ndb;
	auto vd = static_cast<ViewDir *>(data);

	gtk_tree_model_get(store, a, DIR_COLUMN_POINTER, &nda, -1);
	gtk_tree_model_get(store, b, DIR_COLUMN_POINTER, &ndb, -1);

	if (!nda->fd && !ndb->fd) return 0;
	if (!nda->fd) return 1;
	if (!ndb->fd) return -1;

	if (vd->layout->options.dir_view_list_sort.method == SORT_NUMBER)
		{
		if (vd->layout->options.dir_view_list_sort.case_sensitive == TRUE)
			{
			return strcmp(nda->fd->collate_key_name_natural, ndb->fd->collate_key_name_natural);
			}

		return strcmp(nda->fd->collate_key_name_nocase_natural, ndb->fd->collate_key_name_nocase_natural);
		}

	if (vd->layout->options.dir_view_list_sort.method == SORT_TIME)
		{
		if (nda->fd->date < ndb->fd->date) return -1;
		if (nda->fd->date > ndb->fd->date) return 1;
		return 0;
		}

	if (vd->layout->options.dir_view_list_sort.case_sensitive == TRUE)
		{
		return strcmp(nda->fd->collate_key_name, ndb->fd->collate_key_name);
		}

	return strcmp(nda->fd->collate_key_name_nocase, ndb->fd->collate_key_name_nocase);
}

/*
 *----------------------------------------------------------------------------
 * core
 *----------------------------------------------------------------------------
 */

static void vdtree_setup_root(ViewDir *vd)
{
	const gchar *path = G_DIR_SEPARATOR_S;
	FileData *fd;


	fd = file_data_new_dir(path);
	vdtree_add_by_data(vd, fd, nullptr);

	vdtree_expand_by_data(vd, fd, TRUE);
	vdtree_populate_path(vd, fd, FALSE, FALSE);
}

static gboolean vdtree_destroy_node_cb(GtkTreeModel *store, GtkTreePath *, GtkTreeIter *iter, gpointer)
{
	NodeData *nd;

	gtk_tree_model_get(store, iter, DIR_COLUMN_POINTER, &nd, -1);
	vdtree_node_free(nd);

	return FALSE;
}

void vdtree_destroy_cb(GtkWidget *, gpointer data)
{
	auto vd = static_cast<ViewDir *>(data);
	GtkTreeModel *store;

	vdtree_dnd_drop_expand_cancel(vd);
	vd_dnd_drop_scroll_cancel(vd);
	widget_auto_scroll_stop(vd->view);

	store = gtk_tree_view_get_model(GTK_TREE_VIEW(vd->view));
	gtk_tree_model_foreach(store, vdtree_destroy_node_cb, vd);
}

ViewDir *vdtree_new(ViewDir *vd, FileData *)
{
	GtkTreeStore *store;
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	vd->info = g_new0(ViewDirInfoTree, 1);

	vd->type = DIRVIEW_TREE;

	vd->dnd_drop_leave_func = vdtree_dnd_drop_expand_cancel;
	vd->dnd_drop_update_func = vdtree_dnd_drop_expand;

	store = gtk_tree_store_new(6, G_TYPE_POINTER, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
	vd->view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(vd->view), FALSE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(vd->view), FALSE);
	gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(store), vdtree_sort_cb, vd, nullptr);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store),
					     GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(vd->view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
	gtk_tree_selection_set_select_function(selection, vdtree_select_cb, vd, nullptr);

	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);

	renderer = gtk_cell_renderer_pixbuf_new();
	gtk_tree_view_column_pack_start(column, renderer, FALSE);
	gtk_tree_view_column_add_attribute(column, renderer, "pixbuf", DIR_COLUMN_ICON);
	gtk_tree_view_column_set_cell_data_func(column, renderer, vd_color_cb, vd, nullptr);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer, "text", DIR_COLUMN_NAME);
	gtk_tree_view_column_set_cell_data_func(column, renderer, vd_color_cb, vd, nullptr);

	gtk_tree_view_append_column(GTK_TREE_VIEW(vd->view), column);

	gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(vd->view), DIR_COLUMN_LINK);

	vdtree_setup_root(vd);

	g_signal_connect(G_OBJECT(vd->view), "row-expanded",
	                 G_CALLBACK(vdtree_row_expanded), vd);
	g_signal_connect(G_OBJECT(vd->view), "row-collapsed",
	                 G_CALLBACK(vdtree_row_collapsed), vd);

	return vd;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
