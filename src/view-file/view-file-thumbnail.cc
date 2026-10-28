/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "view-file-thumbnail.h"

#include <algorithm>
#include <iterator>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "color-label.h"
#include "filedata.h"
#include "options.h"
#include "view-file-icon.h"
#include "view-file-list.h"
#include "view-file.h"

struct ViewFileThumbnails
{
	// Rebuilt only when the model changes. Owned references keep queued files alive.
	std::vector<FileDataRef> rows;
	std::unordered_map<FileData *, size_t> positions;
	std::vector<FileData *> queue;
	std::unordered_set<FileData *> visible;
	size_t next = 0;
	bool model_dirty = false;
	guint idle_id = 0;
	gulong map_handler = 0;
	gulong unmap_handler = 0;
	std::vector<GtkAdjustment *> adjustments;
};

namespace
{

// One cache serves all file panes. Entries own their FileData; the index and LRU
// contain exactly the same files. Visible files in any pane and active loads are
// protected, so their memory may temporarily exceed the configured budget.
std::list<FileDataRef> resident;
std::unordered_map<FileData *, std::list<FileDataRef>::iterator> resident_index;
std::vector<ViewFile *> views;

void touch(FileData *fd)
{
	if (!fd->thumb_pixbuf) return;
	const auto found = resident_index.find(fd);
	if (found != resident_index.end())
		{
		resident.splice(resident.end(), resident, found->second);
		return;
		}
	resident.emplace_back(fd);
	resident_index.emplace(fd, std::prev(resident.end()));
}

bool protected_file(FileData *fd)
{
	return std::any_of(views.begin(), views.end(), [fd](ViewFile *vf)
		{
		return vf->thumbs_filedata == fd || vf->thumbnails->visible.count(fd) != 0;
		});
}

void trim()
{
	gsize bytes = vf_thumbnail_resident_bytes();
	const guint64 budget = static_cast<guint64>(options->thumbnails.memory_cache_size) * 1024 * 1024;
	for (auto it = resident.begin(); it != resident.end();)
		{
		auto *fd = static_cast<FileData *>(*it);
		if (fd->thumb_pixbuf && (bytes <= budget || protected_file(fd)))
			{
			++it;
			continue;
			}
		if (fd->thumb_pixbuf)
			{
			bytes -= gdk_pixbuf_get_byte_length(fd->thumb_pixbuf);
			g_clear_object(&fd->thumb_pixbuf);
			// Drop model pixbufs and bound GtkPicture textures as well as FileData's reference.
			for (auto *vf : views)
				{
				if (vf->thumbnails->positions.count(fd)) vf_set_thumb_fd(vf, fd);
				}
			}
		resident_index.erase(fd);
		it = resident.erase(it);
		}
}

void update_range(ViewFile *vf)
{
	auto &state = *vf->thumbnails;
	state.queue.clear();
	state.visible.clear();
	state.next = 0;
	if (!gtk_widget_get_mapped(vf->listview) ||
	    (vf->type == FILEVIEW_LIST && !VFLIST(vf)->thumbs_enabled)) return;
	const auto visible = vf->type == FILEVIEW_ICON ? vficon_thumb_visible(vf) : vflist_thumb_visible(vf);
	if (visible.empty()) return;
	size_t first = state.rows.size();
	size_t last = 0;
	for (auto *fd : visible)
		{
		const auto found = state.positions.find(fd);
		if (found == state.positions.end()) continue;
		first = std::min(first, found->second);
		last = std::max(last, found->second);
		state.visible.insert(fd);
		state.queue.push_back(fd);
		touch(fd);
		}
	if (first == state.rows.size()) return;
	// Prefetch one viewport on either side, nearest files first. Never walk the
	// whole directory looking for uncached thumbnails after finishing this queue.
	const size_t nearby = last - first + 1;
	for (size_t offset = 1; offset <= nearby; ++offset)
		{
		if (last + offset < state.rows.size()) state.queue.push_back(state.rows[last + offset]);
		if (first >= offset) state.queue.push_back(state.rows[first - offset]);
		}
}

gboolean viewport_idle(gpointer data)
{
	auto *vf = static_cast<ViewFile *>(data);
	vf->thumbnails->idle_id = 0;
	color_label_refresh_visible(vf);
	if (vf->thumbnails->model_dirty)
		{
		vf_thumb_update(vf);
		return G_SOURCE_REMOVE;
		}
	update_range(vf);
	trim();
	vf_thumb_continue(vf);
	return G_SOURCE_REMOVE;
}

} // namespace

gsize vf_thumbnail_resident_bytes()
{
	gsize bytes = 0;
	for (const auto &fd : resident)
		{
		if (fd->thumb_pixbuf) bytes += gdk_pixbuf_get_byte_length(fd->thumb_pixbuf);
		}
	return bytes;
}

void vf_thumbnail_schedule(ViewFile *vf)
{
	if (!vf->thumbnails || vf->thumbnails->idle_id) return;
	vf->thumbnails->idle_id = g_idle_add_full(G_PRIORITY_LOW, viewport_idle, vf, nullptr);
}

void vf_thumbnail_init(ViewFile *vf)
{
	vf->thumbnails = new ViewFileThumbnails;
	views.push_back(vf);
	GtkWidget *scrolled = vf->type == FILEVIEW_ICON ? vf->scrolled : VFLIST(vf)->name_scrolled;
	for (auto *adjustment : {gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolled)),
	                         gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolled))})
		{
		vf->thumbnails->adjustments.push_back(GTK_ADJUSTMENT(g_object_ref(adjustment)));
		g_signal_connect(adjustment, "value-changed", G_CALLBACK(+[](GtkAdjustment *, gpointer data)
			{ vf_thumbnail_schedule(static_cast<ViewFile *>(data)); }), vf);
		g_signal_connect(adjustment, "changed", G_CALLBACK(+[](GtkAdjustment *, gpointer data)
			{ vf_thumbnail_schedule(static_cast<ViewFile *>(data)); }), vf);
		}
	vf->thumbnails->map_handler = g_signal_connect(vf->listview, "map", G_CALLBACK(+[](GtkWidget *, gpointer data)
		{ vf_thumbnail_schedule(static_cast<ViewFile *>(data)); }), vf);
	vf->thumbnails->unmap_handler = g_signal_connect(vf->listview, "unmap", G_CALLBACK(+[](GtkWidget *, gpointer data)
		{ vf_thumbnail_schedule(static_cast<ViewFile *>(data)); }), vf);
}

void vf_thumbnail_destroy(ViewFile *vf)
{
	if (!vf->thumbnails) return;
	g_clear_handle_id(&vf->thumbnails->idle_id, g_source_remove);
	for (auto *adjustment : vf->thumbnails->adjustments)
		{
		g_signal_handlers_disconnect_by_data(adjustment, vf);
		g_object_unref(adjustment);
		}
	if (vf->listview)
		{
		g_signal_handler_disconnect(vf->listview, vf->thumbnails->map_handler);
		g_signal_handler_disconnect(vf->listview, vf->thumbnails->unmap_handler);
		}
	views.erase(std::remove(views.begin(), views.end(), vf), views.end());
	delete vf->thumbnails;
	vf->thumbnails = nullptr;
	if (views.empty())
		{
		resident_index.clear();
		resident.clear();
		}
}

void vf_thumbnail_rebuild(ViewFile *vf)
{
	if (!vf->thumbnails) return;
	auto &state = *vf->thumbnails;
	state.model_dirty = false;
	state.queue.clear();
	state.visible.clear();
	state.rows.clear();
	state.positions.clear();
	if (vf->type == FILEVIEW_LIST && !VFLIST(vf)->thumbs_enabled)
		{
		vf_thumbnail_schedule(vf);
		return;
		}
	const auto add = [&state](FileData *fd)
		{
		state.positions.emplace(fd, state.rows.size());
		state.rows.emplace_back(fd);
		};
	if (vf->type == FILEVIEW_LIST)
		{
		for (auto *fd : vflist_thumb_rows(vf)) add(fd);
		}
	else
		{
		for (GList *work = vf->list; work; work = work->next) add(static_cast<FileData *>(work->data));
		}
	update_range(vf);
	vf_thumbnail_schedule(vf);
}

FileData *vf_thumbnail_next(ViewFile *vf)
{
	if (!vf->thumbnails || vf->thumbnails->model_dirty) return nullptr;
	auto &state = *vf->thumbnails;
	while (state.next < state.queue.size())
		{
		auto *fd = state.queue[state.next++];
		if (!fd->thumb_pixbuf) return fd;
		touch(fd);
		}
	trim();
	return nullptr;
}

void vf_thumbnail_model_changed(ViewFile *vf)
{
	if (!vf->thumbnails) return;
	vf->thumbnails->model_dirty = true;
	vf_thumbnail_schedule(vf);
}

gint vf_thumbnail_position(ViewFile *vf, FileData *fd)
{
	if (!vf->thumbnails || vf->thumbnails->model_dirty) return -1;
	const auto found = vf->thumbnails->positions.find(fd);
	return found == vf->thumbnails->positions.end() ? -1 : static_cast<gint>(found->second);
}

void vf_thumbnail_loaded(ViewFile *, FileData *fd)
{
	touch(fd);
	trim();
}

double vf_thumbnail_progress(ViewFile *vf)
{
	if (!vf->thumbnails || vf->thumbnails->queue.empty()) return 0.0;
	return static_cast<double>(vf->thumbnails->next) / vf->thumbnails->queue.size();
}

void vf_thumbnail_trim()
{
	trim();
}
