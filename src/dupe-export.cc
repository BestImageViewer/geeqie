/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "dupe.h"

#include <cinttypes>
#include <cmath>
#include <cstring>

#include <gio/gio.h>

#include "filedata.h"
#include "intl.h"
#include "ui-fileops.h"

namespace
{

void append_json_string(GString *output, const gchar *value)
{
	g_autofree gchar *valid = g_utf8_make_valid(value ? value : "", -1);
	g_string_append_c(output, '"');
	for (const auto *p = reinterpret_cast<const guchar *>(valid); *p; ++p)
		{
		if (*p == '"' || *p == '\\')
			{
			g_string_append_c(output, '\\');
			g_string_append_c(output, *p);
			}
		else if (*p < 0x20) g_string_append_printf(output, "\\u%04x", *p);
		else g_string_append_c(output, *p);
		}
	g_string_append_c(output, '"');
}

void append_member(GString *output, const DupeItem *item, const GList *selection, const gdouble *rank)
{
	g_autofree gchar *path = path_from_utf8(item->fd->path);
	g_autoptr(GFile) file = g_file_new_for_path(path);
	g_autofree gchar *uri = g_file_get_uri(file);
	g_string_append(output, "{\"id\":");
	append_json_string(output, uri);
	g_string_append(output, ",\"path\":");
	append_json_string(output, item->fd->path);
	g_string_append_printf(output, ",\"set\":%d,\"selected\":%s,\"size\":%" PRId64 ",\"mtime\":%" PRId64,
	                       item->second ? 2 : 1,
	                       g_list_find(const_cast<GList *>(selection), item->fd) ? "true" : "false",
	                       item->fd->size, static_cast<int64_t>(item->fd->date));
	if (item->dimensions.empty()) g_string_append(output, ",\"width\":null,\"height\":null");
	else g_string_append_printf(output, ",\"width\":%d,\"height\":%d", item->dimensions.width, item->dimensions.height);
	g_string_append(output, ",\"similarity_to_representative\":");
	if (rank && std::isfinite(*rank))
		{
		gchar number[G_ASCII_DTOSTR_BUF_SIZE];
		g_string_append(output, g_ascii_dtostr(number, sizeof(number), *rank));
		}
	else g_string_append(output, "null");
	g_string_append_c(output, '}');
}

} // namespace

static void dupe_script_wait_cb(GObject *source, GAsyncResult *result, gpointer data)
{
	g_autoptr(GTask) task = G_TASK(data);
	g_autoptr(GError) error = nullptr;
	const gboolean success = g_subprocess_wait_check_finish(G_SUBPROCESS(source), result, &error);
	g_file_delete(G_FILE(g_task_get_task_data(task)), nullptr, nullptr);
	if (success) g_task_return_boolean(task, TRUE);
	else g_task_return_error(task, g_steal_pointer(&error));
}

void dupe_run_results_script(GFile *script, const gchar *json, GAsyncReadyCallback callback, gpointer data)
{
	g_autoptr(GTask) task = g_task_new(script, nullptr, callback, data);
	g_autoptr(GError) error = nullptr;
	g_autoptr(GFileIOStream) stream = nullptr;
	g_autoptr(GFile) snapshot = g_file_new_tmp("geeqie-duplicates-XXXXXX", &stream, &error);
	if (snapshot)
		{
		g_task_set_task_data(task, g_object_ref(snapshot), [](gpointer file)
			{
			g_file_delete(G_FILE(file), nullptr, nullptr);
			g_object_unref(file);
			});
		GOutputStream *output = g_io_stream_get_output_stream(G_IO_STREAM(stream));
		if (g_output_stream_write_all(output, json, strlen(json), nullptr, nullptr, &error) &&
		    g_io_stream_close(G_IO_STREAM(stream), nullptr, &error))
			{
			g_autofree gchar *program = g_file_get_path(script);
			g_autofree gchar *path = g_file_get_path(snapshot);
			if (!program)
				g_set_error_literal(&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, _("Script must be a local executable"));
			else
				{
				g_autoptr(GSubprocess) process = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, program, path, nullptr);
				if (process)
					{
					g_subprocess_wait_check_async(process, nullptr, dupe_script_wait_cb, g_steal_pointer(&task));
					return;
					}
				}
			}
		g_file_delete(snapshot, nullptr, nullptr);
		}
	g_task_return_error(task, g_steal_pointer(&error));
}

gboolean dupe_run_results_script_finish(GAsyncResult *result, GError **error)
{
	return g_task_propagate_boolean(G_TASK(result), error);
}

gboolean dupe_export_ready(const DupeWindow *dw)
{
	return dw && !dw->idle_id && !dw->img_loader && !dw->add_files_queue_id && !dw->add_files_queue;
}

GString *dupe_export_json(const DupeWindow *dw, const GList *selection)
{
	GString *output = g_string_new("{\"schema\":\"org.geeqie.duplicates\",\"version\":1,\"status\":");
	const gboolean incomplete = dw && !dupe_export_ready(dw);
	append_json_string(output, !dw ? "no_window" : incomplete ? "incomplete" : "ready");
	if (!dw || incomplete)
		{
		g_string_append(output, ",\"groups\":[]}\n");
		return output;
		}
	const gboolean similarity = dw->match_mask & DUPE_MATCH_SIM;
	g_string_append(output, ",\"criteria\":[");
	struct Criterion { DupeMatchType mask; const gchar *name; };
	constexpr Criterion criteria[] = {
		{DUPE_MATCH_NAME, "name"}, {DUPE_MATCH_SIZE, "size"}, {DUPE_MATCH_DATE, "date"},
		{DUPE_MATCH_DIM, "dimensions"}, {DUPE_MATCH_SUM, "checksum"}, {DUPE_MATCH_PATH, "path"},
		{DUPE_MATCH_SIM, "similarity"}, {DUPE_MATCH_NAME_CI, "name_case_insensitive"},
		{DUPE_MATCH_NAME_CONTENT, "name_different_content"},
		{DUPE_MATCH_NAME_CI_CONTENT, "name_case_insensitive_different_content"}, {DUPE_MATCH_ALL, "all"}
	};
	gboolean first = TRUE;
	for (const auto &criterion : criteria)
		if (dw->match_mask & criterion.mask)
			{
			if (!first) g_string_append_c(output, ',');
			append_json_string(output, criterion.name);
			first = FALSE;
			}
	g_string_append_c(output, ']');
	g_string_append_printf(output, ",\"match_mask\":%u,\"two_sets\":%s,\"algorithm\":",
	                       static_cast<unsigned int>(dw->match_mask), dw->second_set ? "true" : "false");
	if (similarity) append_json_string(output, dw->comparison_perceptual ? "phash" : "standard");
	else g_string_append(output, "null");
	g_string_append_printf(output, ",\"ignore_orientation\":%s,\"threshold\":",
	                       dw->comparison_rotation_invariant ? "true" : "false");
	if (similarity) g_string_append_printf(output, "%d", dw->comparison_threshold);
	else g_string_append(output, "null");
	g_string_append(output, ",\"score_semantics\":\"member_to_representative_percent\",\"groups\":[");
	guint group_id = 0;
	for (const GList *group = dw->dupes; group; group = group->next)
		{
		auto *parent = static_cast<DupeItem *>(group->data);
		if (group_id) g_string_append_c(output, ',');
		g_string_append_printf(output, "{\"id\":%u,\"representative_id\":", ++group_id);
		g_autofree gchar *path = path_from_utf8(parent->fd->path);
		g_autoptr(GFile) file = g_file_new_for_path(path);
		g_autofree gchar *uri = g_file_get_uri(file);
		append_json_string(output, uri);
		g_string_append(output, ",\"members\":[");
		append_member(output, parent, selection, nullptr);
		for (const GList *member = parent->group; member; member = member->next)
			{
			auto *match = static_cast<DupeMatch *>(member->data);
			g_string_append_c(output, ',');
			append_member(output, match->di, selection, similarity ? &match->rank : nullptr);
			}
		g_string_append(output, "]}");
		}
	g_string_append(output, "]}\n");
	return output;
}
