/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: John Ellis, Vladimir Nadvornik, Laurent Monin
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

#include "history-list.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "options.h"
#include "ui-fileops.h"

namespace
{

struct HistoryChain
{
	const gchar *prev();
	const gchar *next();
	bool push_back(const gchar *path);

private:
	std::vector<gchar *> chain;
	guint index = 0;
	bool is_nav_button = false; /** Used to prevent the nav buttons making entries to the chain **/
};

const gchar *HistoryChain::prev()
{
	is_nav_button = true;

	index = index > 0 ? index - 1 : 0;

	return chain[index];
}

const gchar *HistoryChain::next()
{
	is_nav_button = true;

	guint last = chain.size() - 1;
	index = index < last ? index + 1 : last;

	return chain[index];
}

bool HistoryChain::push_back(const gchar *path)
{
	if (is_nav_button)
		{
		is_nav_button = false;
		return false;
		}

	if (chain.empty())
		{
		chain.push_back(g_strdup(path));
		index = 0;
		}
	else
		{
		if (g_strcmp0(chain.back(), path) != 0)
			{
			chain.push_back(g_strdup(path));
			DEBUG_3("%d %s", chain.size() - 1, path);
			}

		index = chain.size() - 1;
		}

	return true;
}

HistoryChain history_chain{};
HistoryChain image_chain{};

gint dirname_compare(gconstpointer data, gconstpointer user_data)
{
	g_autofree gchar *dirname = g_path_get_dirname(static_cast<const gchar *>(data));
	return g_strcmp0(dirname, static_cast<const gchar *>(user_data));
}

} // namespace

static void update_recent_viewed_folder_image_list(const gchar *path);

/**
 * @file
 *-----------------------------------------------------------------------------
 * Implements a history chain. Used by the Back and Forward toolbar buttons.
 * Selecting any folder appends the path to the end of the chain.
 * Pressing the Back and Forward buttons moves along the chain, but does
 * not make additions to the chain.
 * The chain always increases and is deleted at the end of the session
 *
 *-----------------------------------------------------------------------------
 */

const gchar *history_chain_back()
{
	return history_chain.prev();
}

const gchar *history_chain_forward()
{
	return history_chain.next();
}

/**
 * @brief Appends a path to the history chain
 * @param path Path selected
 *
 * Each time the user selects a new path it is appended to the chain
 * except when it is identical to the current last entry
 * The pointer is always moved to the end of the chain
 */
void history_chain_append_end(const gchar *path)
{
	history_chain.push_back(path);
}

/**
 * @file
 *-----------------------------------------------------------------------------
 * Implements an image history chain. Whenever an image is displayed it is
 * appended to a chain.
 * Pressing the Image Back and Image Forward buttons moves along the chain,
 * but does not make additions to the chain.
 * The chain always increases and is deleted at the end of the session
 *
 *-----------------------------------------------------------------------------
 */

const gchar *image_chain_back()
{
	return image_chain.prev();
}

const gchar *image_chain_forward()
{
	return image_chain.next();
}

/**
 * @brief Appends a path to the image history chain
 * @param path Image path selected
 *
 * Each time the user selects a new image it is appended to the chain
 * except when it is identical to the current last entry
 * The pointer is always moved to the end of the chain
 *
 * Updates the recent viewed image_list
 */
void image_chain_append_end(const gchar *path)
{
	if (!image_chain.push_back(path)) return;

	update_recent_viewed_folder_image_list(path);
}

/*
 *-----------------------------------------------------------------------------
 * history lists
 *-----------------------------------------------------------------------------
 */

struct HistoryData
{
	gchar *key;
	GList *list;
};

static GList *history_list = nullptr;


static gchar *quoted_from_text(const gchar *text)
{
	const gchar *ptr;
	gint c = 0;
	gint l = strlen(text);

	if (l == 0) return nullptr;

	while (c < l && text[c] !='"') c++;
	if (text[c] == '"')
		{
		gint e;
		c++;
		ptr = text + c;
		e = c;
		while (e < l && text[e] !='"') e++;
		if (text[e] == '"')
			{
			if (e - c > 0)
				{
				return g_strndup(ptr, e - c);
				}
			}
		}
	return nullptr;
}

gboolean history_list_load(const gchar *path)
{
	g_autofree gchar *key = nullptr;
	gchar s_buf[1024];

	g_autofree gchar *pathl = path_from_utf8(path);
	g_autoptr(FILE) f = fopen(pathl, "r");
	if (!f) return FALSE;

	/* first line must start with History comment */
	if (!fgets(s_buf, sizeof(s_buf), f) ||
	    strncmp(s_buf, "#History", 8) != 0)
		{
		return FALSE;
		}

	while (fgets(s_buf, sizeof(s_buf), f))
		{
		if (s_buf[0]=='#') continue;
		if (s_buf[0]=='[')
			{
			gint c;
			gchar *ptr;

			ptr = s_buf + 1;
			c = 0;
			while (ptr[c] != ']' && ptr[c] != '\n' && ptr[c] != '\0') c++;

			g_free(key);
			key = g_strndup(ptr, c);
			}
		else
			{
			g_autofree gchar *value = quoted_from_text(s_buf);
			if (value && key)
				{
				history_list_add_to_key(key, value, 0);
				}
			}
		}

	return TRUE;
}

gboolean history_list_save(const gchar *path)
{
	g_autofree gchar *pathl = path_from_utf8(path);

	g_autoptr(GString) gstring = g_string_new("#History lists\n\n");

	for (GList *list = g_list_last(history_list); list; list = list->prev)
		{
		const auto *hd = static_cast<HistoryData *>(list->data);

		g_string_append_printf(gstring, "[%s]\n", hd->key);

		/* save them inverted (oldest to newest)
		 * so that when reading they are added correctly
		 */
		gint list_count = g_list_length(hd->list);
		for (GList *work = g_list_last(hd->list); work; work = work->prev)
			{
			const auto *item = static_cast<gchar *>(work->data);
			if ((strcmp(hd->key, "path_list") != 0 || list_count <= options->open_recent_list_maxsize)
			    &&
			    (strcmp(hd->key, "recent") != 0 || isfile(item))
			    &&
			    (strcmp(hd->key, "image_list") != 0 || list_count <= options->recent_folder_image_list_maxsize))
				{
				g_string_append_printf(gstring, "\"%s\"\n", item);
				}

			list_count--;
			}
		g_string_append(gstring, "\n");
		}

	g_string_append(gstring, "#end\n");

	return secure_save(pathl, gstring->str, -1);
}

static void history_list_free(HistoryData *hd)
{
	if (!hd) return;

	g_free(hd->key);
	g_list_free_full(hd->list, g_free);
	g_free(hd);
}

static HistoryData *history_list_find_by_key(const gchar *key)
{
	if (!key) return nullptr;

	static const auto history_data_compare_key = [](gconstpointer data, gconstpointer user_data)
	{
		auto *hd = static_cast<const HistoryData *>(data);
		return strcmp(hd->key, static_cast<const gchar *>(user_data));
	};

	GList *work = g_list_find_custom(history_list, key, history_data_compare_key);
	return work ? static_cast<HistoryData *>(work->data) : nullptr;
}

const gchar *history_list_find_last_path_by_key(const gchar *key)
{
	HistoryData *hd;

	hd = history_list_find_by_key(key);
	if (!hd || !hd->list) return nullptr;

	return static_cast<const gchar *>(hd->list->data);
}

void history_list_free_key(const gchar *key)
{
	HistoryData *hd;
	hd = history_list_find_by_key(key);
	if (!hd) return;

	history_list = g_list_remove(history_list, hd);
	history_list_free(hd);
}

void history_list_add_to_key(const gchar *key, const gchar *path, gint max)
{
	HistoryData *hd;
	GList *work;

	if (!key || !path) return;

	hd = history_list_find_by_key(key);
	if (!hd)
		{
		hd = g_new(HistoryData, 1);
		hd->key = g_strdup(key);
		hd->list = nullptr;
		history_list = g_list_prepend(history_list, hd);
		}

	/* if already in the list, simply move it to the top */
	work = g_list_find_custom(hd->list, path, reinterpret_cast<GCompareFunc>(strcmp));
	if (work)
		{
		/* if not first, move it */
		if (work != hd->list)
			{
			auto buf = static_cast<gchar *>(work->data);

			hd->list = g_list_remove(hd->list, buf);
			hd->list = g_list_prepend(hd->list, buf);
			}
		return;
		}

	hd->list = g_list_prepend(hd->list, g_strdup(path));

	if (max == -1) max = options->open_recent_list_maxsize;
	if (max > 0)
		{
		work = g_list_nth(hd->list, max);
		if (work)
			{
			GList *last = work->prev;
			if (last)
				{
				last->next = nullptr;
				}

			work->prev = nullptr;
			g_list_free_full(work, g_free);
			}
		}
}

void history_list_item_change(const gchar *key, const gchar *oldpath, const gchar *newpath)
{
	HistoryData *hd;
	GList *work;

	if (!oldpath) return;
	hd = history_list_find_by_key(key);
	if (!hd) return;

	work = hd->list;
	while (work)
		{
		auto buf = static_cast<gchar *>(work->data);

		if (!g_str_has_prefix(buf, ".") || newpath)
			{
			if (strcmp(buf, oldpath) == 0)
				{
				if (newpath)
					{
					work->data = g_strdup(newpath);
					}
				else
					{
					hd->list = g_list_remove(hd->list, buf);
					}
				g_free(buf);
				return;
				}
			}
		else
			{
			hd->list = g_list_remove(hd->list, buf);
			g_free(buf);
			return;
			}
		work = work->next;
		}
}

void history_list_item_move(const gchar *key, const gchar *path, gint direction)
{
	HistoryData *hd;
	GList *work;

	if (!path) return;
	hd = history_list_find_by_key(key);
	if (!hd) return;

	work = g_list_find_custom(hd->list, path, reinterpret_cast<GCompareFunc>(strcmp));
	if (!work) return;

	gint p = g_list_position(hd->list, work) + direction;
	if (p < 0) return;

	auto buf = static_cast<gchar *>(work->data);
	hd->list = g_list_remove(hd->list, buf);
	hd->list = g_list_insert(hd->list, buf, p);
}

void history_list_item_remove(const gchar *key, const gchar *path)
{
	history_list_item_change(key, path, nullptr);
}

/**
 * @brief The returned GList is internal, don't free it
 */
GList *history_list_get_by_key(const gchar *key)
{
	HistoryData *hd;

	hd = history_list_find_by_key(key);
	if (!hd) return nullptr;

	return hd->list;
}

/**
 * @brief Get image last viewed in a folder
 * @param path Must be a folder
 * @returns Last image viewed in folder or NULL
 *
 * Returned string should be freed
 */
gchar *get_recent_viewed_folder_image(gchar *path)
{
	HistoryData *hd;
	GList *work;

	if (options->recent_folder_image_list_maxsize == 0)
		{
		return nullptr;
		}

	hd = history_list_find_by_key("image_list");

	if (!hd)
		{
		hd = g_new(HistoryData, 1);
		hd->key = g_strdup("image_list");
		hd->list = nullptr;
		history_list = g_list_prepend(history_list, hd);
		}

	work = g_list_find_custom(hd->list, path, dirname_compare);
	if (!work || !isfile(static_cast<const gchar *>(work->data)))
		{
		return nullptr;
		}

	return g_strdup(static_cast<const gchar *>(work->data));
}

static void update_recent_viewed_folder_image_list(const gchar *path)
{
	HistoryData *hd;
	GList *work;

	if (options->recent_folder_image_list_maxsize == 0)
		{
		return;
		}

	hd = history_list_find_by_key("image_list");
	if (!hd)
		{
		hd = g_new(HistoryData, 1);
		hd->key = g_strdup("image_list");
		hd->list = nullptr;
		history_list = g_list_prepend(history_list, hd);
		}

	g_autofree gchar *image_dir = g_path_get_dirname(path);
	work = g_list_find_custom(hd->list, image_dir, dirname_compare);
	if (work)
		{
		g_free(work->data);
		work->data = g_strdup(path);
		hd->list = g_list_remove_link(hd->list, work);
		hd->list = g_list_concat(work, hd->list);
		}
	else
		{
		hd->list = g_list_prepend(hd->list, g_strdup(path));
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
