/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "pan-index.h"

#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gio/gio.h>

#include "cache.h"
#include "filefilter.h"
#include "intl.h"
#include "main-defines.h"
#include "options.h"
#include "pan-types.h"
#include "thumb-standard.h"
#include "ui-fileops.h"

namespace
{

struct Entry
{
	std::string path;
	struct stat attributes{};
};

struct Directory
{
	std::string path;
	std::vector<Entry> entries;
};

struct Progress
{
	std::atomic<unsigned int> directories{0};
	std::atomic<unsigned int> entries{0};
};

struct Request
{
	std::string path;
	bool show_hidden;
	bool dot_hidden;
	bool ignore_links;
	std::shared_ptr<Progress> progress;
};

using Identity = std::pair<dev_t, ino_t>;
struct Pending
{
	std::string path;
	std::vector<Identity> ancestors;
};

void scan_task(GTask *task, gpointer, gpointer task_data, GCancellable *cancellable)
{
	const auto &request = *static_cast<Request *>(task_data);
	auto result = std::make_unique<std::vector<Directory>>();
	std::vector<Pending> pending{{request.path, {}}};
	while (!pending.empty() && !g_cancellable_is_cancelled(cancellable))
		{
		auto current = std::move(pending.back());
		pending.pop_back();
		g_autofree gchar *local = path_from_utf8(current.path.c_str());
		struct stat directory_stat{};
		if (!local || stat(local, &directory_stat) != 0) continue;
		const Identity identity{directory_stat.st_dev, directory_stat.st_ino};
		if (std::find(current.ancestors.begin(), current.ancestors.end(), identity) != current.ancestors.end())
			continue;
		current.ancestors.push_back(identity);
		g_autoptr(GDir) stream = g_dir_open(local, 0, nullptr);
		if (!stream) continue;
		Directory directory{current.path, {}};
		while (!g_cancellable_is_cancelled(cancellable))
			{
			const auto *name = g_dir_read_name(stream);
			if (!name) break;
			if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
			++request.progress->entries;
			g_autofree gchar *path = g_build_filename(local, name, nullptr);
			if (!request.show_hidden)
				{
				if (request.dot_hidden && *name == '.') continue;
				if (!request.dot_hidden)
					{
					g_autoptr(GFile) file = g_file_new_for_path(path);
					g_autoptr(GFileInfo) info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
																  G_FILE_QUERY_INFO_NONE, cancellable, nullptr);
					if (info && g_file_info_get_is_hidden(info)) continue;
					}
				}
			struct stat attributes{};
			if (lstat(path, &attributes) != 0) continue;
			const bool link = S_ISLNK(attributes.st_mode);
			if (link && stat(path, &attributes) != 0) continue;
			g_autofree gchar *utf8 = path_to_utf8(path);
			if (!utf8) continue;
			if (S_ISDIR(attributes.st_mode))
				{
				if (strcmp(name, GQ_CACHE_LOCAL_THUMB) == 0 || strcmp(name, GQ_CACHE_LOCAL_METADATA) == 0 ||
					strcmp(name, THUMB_FOLDER_LOCAL) == 0)
					continue;
				if ((!link || !request.ignore_links) && strcmp(name, GQ_RC_DIR) != 0)
					pending.push_back({utf8, current.ancestors});
				}
			directory.entries.push_back({utf8, attributes});
			}
		result->push_back(std::move(directory));
		++request.progress->directories;
		}
	if (g_task_return_error_if_cancelled(task)) return;
	g_task_return_pointer(task, result.release(),
						  [](gpointer value) { delete static_cast<std::vector<Directory> *>(value); });
}

struct Files
{
	GList *files = nullptr;
	GList *dirs = nullptr;
	~Files()
	{
		file_data_list_free(files);
		file_data_list_free(dirs);
	}
};

}  // namespace

struct PanIndex::Data
{
	PanWindow *window = nullptr;
	GCancellable *cancellable = g_cancellable_new();
	std::function<void()> ready;
	std::shared_ptr<Progress> progress = std::make_shared<Progress>();
	std::unique_ptr<std::vector<Directory>> raw;
	std::unordered_map<std::string, std::unique_ptr<Files>> directories;
	std::vector<FileData *> ordered;
	std::string root;
	std::size_t directory = 0;
	std::size_t entry = 0;
	guint progress_id = 0;
	bool scanning = true;
	bool imported = false;
	~Data()
	{
		g_object_unref(cancellable);
	}
};

PanIndex::PanIndex() : data(std::make_unique<Data>()) {}
PanIndex::~PanIndex()
{
	cancel();
}

std::shared_ptr<PanIndex> PanIndex::start(PanWindow *window, std::function<void()> ready)
{
	auto index = std::shared_ptr<PanIndex>(new PanIndex());
	auto &state = *index->data;
	state.window = window;
	state.ready = std::move(ready);
	state.root = window->dir_fd->path;
	auto *request = new Request{state.root, static_cast<bool>(options->file_filter.show_hidden_files),
								static_cast<bool>(options->file_filter.dot_prefix_hidden_files),
								static_cast<bool>(window->ignore_symlinks), state.progress};
	state.progress_id = g_timeout_add(
		100,
		[](gpointer value) -> gboolean
			{
			auto &state = *static_cast<Data *>(value);
			if (!state.window) return G_SOURCE_REMOVE;
			g_autofree gchar *message =
				g_strdup_printf(_("Scanning folders… %u folders, %u entries"), state.progress->directories.load(),
								state.progress->entries.load());
			gtk_label_set_text(GTK_LABEL(state.window->label_message), message);
			return G_SOURCE_CONTINUE;
			},
		index->data.get());
	g_autoptr(GTask) task = g_task_new(
		nullptr, state.cancellable,
		[](GObject *, GAsyncResult *result, gpointer value)
			{
			std::unique_ptr<std::shared_ptr<PanIndex>> owner(static_cast<std::shared_ptr<PanIndex> *>(value));
			auto &state = *(*owner)->data;
			g_autoptr(GError) error = nullptr;
			state.raw.reset(static_cast<std::vector<Directory> *>(g_task_propagate_pointer(G_TASK(result), &error)));
			state.scanning = false;
			if (state.progress_id) g_source_remove(std::exchange(state.progress_id, 0));
			if (!state.window || error) return;
			if (state.ready) state.ready();
			},
		new std::shared_ptr<PanIndex>(index));
	g_task_set_task_data(task, request, [](gpointer value) { delete static_cast<Request *>(value); });
	g_task_run_in_thread(task, scan_task);
	return index;
}

void PanIndex::cancel()
{
	g_cancellable_cancel(data->cancellable);
	data->window = nullptr;
	data->ready = nullptr;
	if (data->progress_id) g_source_remove(std::exchange(data->progress_id, 0));
}

bool PanIndex::scanning() const
{
	return data->scanning;
}

bool PanIndex::step()
{
	if (data->imported) return true;
	if (data->scanning || !data->raw) return false;
	const auto deadline = g_get_monotonic_time() + 4000;
	unsigned int count = 0;
	while (data->directory < data->raw->size())
		{
		auto &directory = (*data->raw)[data->directory];
		auto &files = data->directories[directory.path];
		if (!files) files = std::make_unique<Files>();
		while (data->entry < directory.entries.size())
			{
			auto &entry = directory.entries[data->entry++];
			const bool is_directory = S_ISDIR(entry.attributes.st_mode);
			if (is_directory || filter_name_exists(entry.path.c_str()))
				{
				auto fd = FileData::new_from_stat(entry.path.c_str(), entry.attributes);
				auto &list = is_directory ? files->dirs : files->files;
				list = g_list_prepend(list, fd.release());
				}
			if (++count >= 128 || g_get_monotonic_time() >= deadline) return false;
			}
		files->files = FileData::FileList::group_sidecars(files->files);
		files->files = filelist_sort(files->files, {SORT_NAME, TRUE, TRUE});
		files->dirs = filelist_sort(files->dirs, {SORT_NAME, TRUE, TRUE});
		++data->directory;
		data->entry = 0;
		if (g_get_monotonic_time() >= deadline) return false;
		}
	std::vector<std::string> pending{data->root};
	while (!pending.empty())
		{
		auto path = std::move(pending.back());
		pending.pop_back();
		auto found = data->directories.find(path);
		if (found == data->directories.end()) continue;
		for (auto *work = found->second->files; work; work = work->next)
			data->ordered.push_back(static_cast<FileData *>(work->data));
		for (auto *work = g_list_last(found->second->dirs); work; work = work->prev)
			pending.emplace_back(static_cast<FileData *>(work->data)->path);
		}
	data->raw.reset();
	data->imported = true;
	return true;
}

GList *PanIndex::files(SortType method) const
{
	GList *list = nullptr;
	for (auto *fd : data->ordered) list = g_list_prepend(list, file_data_ref(fd));
	list = g_list_reverse(list);
	if (method != SORT_NONE && method != SORT_NAME) list = filelist_sort(list, {method, TRUE, TRUE});
	return list;
}

bool PanIndex::directory(const gchar *path, GList **files, GList **dirs) const
{
	*files = nullptr;
	*dirs = nullptr;
	auto found = data->directories.find(path);
	if (found == data->directories.end()) return false;
	*files = filelist_copy(found->second->files);
	*dirs = filelist_copy(found->second->dirs);
	return true;
}
