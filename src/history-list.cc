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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
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

std::unordered_map<std::string, HistoryList> history_list_map;

bool dirname_compare(const std::string &item, const gchar *path)
{
	g_autofree gchar *dirname = g_path_get_dirname(item.c_str());
	return g_strcmp0(dirname, path) == 0;
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

static gchar *quoted_from_text(const gchar *text)
{
	if (text[0] == '\0') return nullptr;

	constexpr gint max_tokens = 3;
	g_auto(GStrv) text_split = g_strsplit(text, "\"", max_tokens);
	if (g_strv_length(text_split) < max_tokens) return nullptr; // text doesn't have quoted substring

	const gchar *quoted = text_split[1];
	if (quoted[0] == '\0') return nullptr; // skip empty quoted substring

	return g_strdup(quoted);
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

	for (const auto &[key, items] : history_list_map)
		{
		g_string_append_printf(gstring, "[%s]\n", key.c_str());

		const bool is_recent = (key == "recent");

		/* save them inverted (oldest to newest)
		 * so that when reading they are added correctly
		 */
		auto last = items.crend();
		if (key == "path_list")
			{
			if (static_cast<size_t>(options->open_recent_list_maxsize) < items.size())
				{
				last = std::next(items.crbegin(), options->open_recent_list_maxsize);
				}
			}
		else if (key == "image_list")
			{
			if (static_cast<size_t>(options->recent_folder_image_list_maxsize) < items.size())
				{
				last = std::next(items.crbegin(), options->recent_folder_image_list_maxsize);
				}
			}

		for (auto work = items.crbegin(); work != last; ++work)
			{
			if (is_recent && !isfile(work->c_str())) continue;

			g_string_append_printf(gstring, "\"%s\"\n", work->c_str());
			}
		g_string_append(gstring, "\n");
		}

	g_string_append(gstring, "#end\n");

	return secure_save(pathl, gstring->str, -1);
}

static HistoryList &history_list_get_by_key(const gchar *key)
{
	if (history_list_map.find(key) == history_list_map.end())
		{
		history_list_map[key] = {};
		}

	return history_list_map[key];
}

const gchar *history_list_find_last_path_by_key(const gchar *key)
{
	HistoryList *items = history_list_find_by_key(key);
	if (!items) return nullptr;

	return items->front().c_str();
}

void history_list_free_key(const gchar *key)
{
	if (key) history_list_map.erase(key);
}

void history_list_add_to_key(const gchar *key, const gchar *path, gint max)
{
	if (!key || !path) return;

	HistoryList &items = history_list_get_by_key(key);

	/* if already in the list, simply move it to the top */
	const auto work = std::find(items.cbegin(), items.cend(), path);
	if (work != items.cend())
		{
		/* if not first, move it */
		if (work != items.cbegin())
			{
			items.splice(items.cbegin(), items, work);
			}

		return;
		}

	items.emplace_front(path);

	if (max == -1) max = options->open_recent_list_maxsize;
	if (max > 0 && static_cast<size_t>(max) < items.size())
		{
		auto last = std::next(items.begin(), max);

		items.erase(last, items.end());
		}
}

/**
 * @brief Replaces or removes (if newpath is nullptr)
 * oldpath in history list for key.
 * If found item should be removed, also check for "dot" entries.
 * See commit 9ccfac429a9bd1a745efd8bc94b6081a7dd6ee23 for rationale.
 */
void history_list_item_change(const gchar *key, const gchar *oldpath, const gchar *newpath)
{
	if (!oldpath) return;

	HistoryList *items = history_list_find_by_key(key);
	if (!items) return;

	const auto find_path = [oldpath, newpath](const std::string &buf)
	{
		return (!newpath && g_str_has_prefix(buf.c_str(), "."))
		    || buf == oldpath;
	};

	auto work = std::find_if(items->begin(), items->end(), find_path);
	if (work == items->end()) return;

	if (newpath)
		{
		*work = newpath;
		}
	else
		{
		items->erase(work);
		}
}

void history_list_item_move(const gchar *key, const gchar *path, gint direction)
{
	if (!path || direction == 0) return;

	HistoryList *items = history_list_find_by_key(key);
	if (!items) return;

	const auto work = std::find(items->cbegin(), items->cend(), path);
	if (work == items->cend()) return;

	if ((direction < 0 && std::distance(items->cbegin(), work) < abs(direction)) ||
	    (direction > 0 && std::distance(work, items->cend()) < direction))
		{
		return;
		}

	const auto pos = std::next(work, direction);

	items->splice(pos, *items, work);
}

void history_list_item_remove(const gchar *key, const gchar *path)
{
	history_list_item_change(key, path, nullptr);
}

/**
 * @brief Returns nullptr if found history list is empty
 */
HistoryList *history_list_find_by_key(const gchar *key)
{
	if (!key) return nullptr;

	auto work = history_list_map.find(key);
	if (work == history_list_map.end() || work->second.empty()) return nullptr;

	return &work->second;
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
	if (options->recent_folder_image_list_maxsize == 0)
		{
		return nullptr;
		}

	HistoryList &items = history_list_get_by_key("image_list");

	auto work = std::find_if(items.cbegin(), items.cend(),
	                         [path](const std::string &item){ return dirname_compare(item, path); });
	if (work == items.cend() || !isfile(work->c_str()))
		{
		return nullptr;
		}

	return g_strdup(work->c_str());
}

static void update_recent_viewed_folder_image_list(const gchar *path)
{
	if (options->recent_folder_image_list_maxsize == 0)
		{
		return;
		}

	HistoryList &items = history_list_get_by_key("image_list");

	g_autofree gchar *image_dir = g_path_get_dirname(path);
	auto work = std::find_if(items.begin(), items.end(),
	                         [image_dir](const std::string &item){ return dirname_compare(item, image_dir); });
	if (work != items.end())
		{
		*work = path;
		items.splice(items.begin(), items, work);
		}
	else
		{
		items.emplace_front(path);
		}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
