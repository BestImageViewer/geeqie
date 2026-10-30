/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "color-label.h"

#include <config.h>

#include "filedata.h"
#include "intl.h"
#include "metadata.h"
#include "options.h"
#include "view-file.h"
#include "view-file/view-file-icon.h"
#include "view-file/view-file-list.h"

namespace
{
const gchar *label_color(int index)
{
	static constexpr const gchar *colors[] = {"#888888", "#d43c3c", "#bd9700", "#328448", "#3979c3", "#9956b8", "#888888"};
	return colors[index >= 0 && index <= COLOR_LABEL_OTHER ? index : COLOR_LABEL_OTHER];
}
} // namespace

const gchar *color_label_name(int index)
{
	const gchar *names[] = {"", options->color_labels.red, options->color_labels.yellow,
	                        options->color_labels.green, options->color_labels.blue, options->color_labels.purple};
	return index >= 0 && index <= COLOR_LABEL_COUNT && names[index] ? names[index] : "";
}

const gchar *color_label_title(int index)
{
	const gchar *titles[] = {_("Unlabeled"), _("Red"), _("Yellow"), _("Green"), _("Blue"), _("Purple"), _("Other labels")};
	return index >= 0 && index <= COLOR_LABEL_OTHER ? titles[index] : "";
}

const gchar *color_label_color(int index)
{
	return label_color(index);
}

int color_label_index(const gchar *text)
{
	if (!text || !*text) return 0;
	if (!g_utf8_validate(text, -1, nullptr)) return COLOR_LABEL_OTHER;
	g_autofree gchar *folded = g_utf8_casefold(text, -1);
	for (int index = 1; index <= COLOR_LABEL_COUNT; ++index)
		{
		const gchar *name = color_label_name(index);
		if (!*name) continue;
		g_autofree gchar *candidate = g_utf8_casefold(name, -1);
		if (g_strcmp0(folded, candidate) == 0) return index;
		}
	return COLOR_LABEL_OTHER;
}

const gchar *color_label_read(FileData *fd)
{
	if (!fd) return "";
	// Cache only files actually displayed or explicitly filtered. FileData's
	// version invalidates this on metadata edits, reverts, and external changes.
	if (!fd->color_label || fd->color_label_version != fd->version)
		{
		g_free(fd->color_label);
		fd->color_label = metadata_read_string(fd, COLOR_LABEL_KEY, METADATA_PLAIN);
		if (!fd->color_label) fd->color_label = g_strdup("");
		fd->color_label_version = fd->version;
		}
	return fd->color_label;
}

gboolean color_label_assign(FileData *fd, int index)
{
	if (!fd || index < 0 || index > COLOR_LABEL_COUNT) return FALSE;
#if !HAVE_EXIV2
	return FALSE;
#else
	const gchar *value = color_label_name(index);
	if (index && !*value) return FALSE;
	return metadata_write_string(fd, COLOR_LABEL_KEY, value);
#endif
}

GList *color_label_filter(GList *files, guint mask)
{
	if (!mask) return files;
	for (GList *work = files; work;)
		{
		GList *link = work;
		work = work->next;
		auto *fd = static_cast<FileData *>(link->data);
		if (mask & (1U << color_label_index(color_label_read(fd)))) continue;
		files = g_list_delete_link(files, link);
		file_data_unref(fd);
		}
	return files;
}

void color_label_widget_update(GtkWidget *label, FileData *fd)
{
	gtk_widget_set_visible(label, options->color_labels.enabled);
	if (!options->color_labels.enabled) return;
	const gchar *text = fd && fd->color_label && fd->color_label_version == fd->version ? fd->color_label : "";
	const int index = color_label_index(text);
	g_autofree gchar *markup = index ? g_markup_printf_escaped("<span foreground=\"%s\">●</span>", color_label_color(index)) : g_strdup("");
	gtk_label_set_markup(GTK_LABEL(label), markup);
	gtk_widget_set_tooltip_text(label, index ? text : _("Unlabeled"));
	gtk_accessible_update_property(GTK_ACCESSIBLE(label), GTK_ACCESSIBLE_PROPERTY_LABEL, index ? text : _("Unlabeled"), -1);
}

void color_label_cell_data(GtkTreeViewColumn *view_column, GtkCellRenderer *renderer, GtkTreeModel *model, GtkTreeIter *iter, gpointer column)
{
	g_object_set(renderer, "cell-background-set", FALSE, nullptr);
	if (!options->color_labels.enabled) return;
	auto *view = gtk_tree_view_column_get_tree_view(view_column);
	if (view && gtk_tree_selection_iter_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), iter)) return;
	FileData *fd = nullptr;
	gtk_tree_model_get(model, iter, GPOINTER_TO_INT(column), &fd, -1);
	// GTK also invokes cell-data functions while measuring offscreen rows.
	// Never read metadata here; the viewport idle loads visible labels instead.
	const int index = color_label_index(fd && fd->color_label_version == fd->version ? fd->color_label : nullptr);
	if (index)
		{
		GdkRGBA color;
		gdk_rgba_parse(&color, color_label_color(index));
		color.alpha = 0.18;
		g_object_set(renderer, "cell-background-rgba", &color, nullptr);
		}
}

void color_label_refresh_visible(ViewFile *vf)
{
	if (!options->color_labels.enabled || !gtk_widget_get_mapped(vf->listview)) return;
	const auto visible = vf->type == FILEVIEW_ICON ? vficon_thumb_visible(vf) : vflist_thumb_visible(vf);
	for (auto *fd : visible)
		{
		if (fd->color_label && fd->color_label_version == fd->version) continue;
		color_label_read(fd);
		vf_set_thumb_fd(vf, fd);
		}
}
