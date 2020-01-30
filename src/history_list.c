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

#include "main.h"
#include "history_list.h"

#include "secure_save.h"
#include "ui_fileops.h"


/*
 *-----------------------------------------------------------------------------
 * Implements a history chain. Used by the Back and Forward toolbar buttons.
 * Selecting any folder appends the path to the end of the chain.
 * Pressing the Back and Forward buttons moves along the chain, but does
 * not make additions to the chain.
 * The chain always increases and is deleted at the end of the session
 *-----------------------------------------------------------------------------
 */

static GList *history_chain = NULL;
static guint chain_index = G_MAXUINT;
static gboolean nav_button = FALSE; /** Used to prevent the nav buttons making entries to the chain **/

const gchar *history_chain_back()
{
	nav_button = TRUE;

	chain_index = chain_index > 0 ? chain_index - 1 : 0;

	return g_list_nth_data(history_chain, chain_index);
}

const gchar *history_chain_forward()
{
	nav_button= TRUE;
	guint last = g_list_length(history_chain) - 1;

	chain_index = chain_index < last ? chain_index + 1 : last;

	return g_list_nth_data(history_chain, chain_index);
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
	GList *work;

	if (!nav_button)
		{
		if(chain_index == G_MAXUINT)
			{
			history_chain = g_list_append (history_chain, g_strdup(path));
			chain_index = 0;
			}
		else
			{
			work = g_list_last(history_chain);
			if (g_strcmp0(work->data , path) != 0)
				{
				history_chain = g_list_append (history_chain, g_strdup(path));
				chain_index = g_list_length(history_chain) - 1;
				DEBUG_3("%d %s", chain_index, path);
				}
			else
				{
				chain_index = g_list_length(history_chain) - 1;
				}
			}
		}
	else
		{
		nav_button = FALSE;
		}
}

/*
 *-----------------------------------------------------------------------------
 * Implements an image history chain. Whenever an image is displayed it is
 * appended to a chain.
 * Pressing the Image Back and Image Forward buttons moves along the chain,
 * but does not make additions to the chain.
 * The chain always increases and is deleted at the end of the session
 *-----------------------------------------------------------------------------
 */
static GList *image_chain = NULL;
static guint image_chain_index = G_MAXUINT;
static gboolean image_nav_button = FALSE; /** Used to prevent the nav buttons making entries to the chain **/
const gchar *image_chain_back()
{
	image_nav_button = TRUE;

	image_chain_index = image_chain_index > 0 ? image_chain_index - 1 : 0;

	return g_list_nth_data(image_chain, image_chain_index);
}

const gchar *image_chain_forward()
{
	image_nav_button= TRUE;
	guint last = g_list_length(image_chain) - 1;

	image_chain_index = image_chain_index < last ? image_chain_index + 1 : last;

	return g_list_nth_data(image_chain, image_chain_index);
}

/**
 * @brief Appends a path to the image history chain
 * @param path Image path selected
 * 
 * Each time the user selects a new image it is appended to the chain
 * except when it is identical to the current last entry
 * The pointer is always moved to the end of the chain
 */
void image_chain_append_end(const gchar *path)
{
	GList *work;

	if (!image_nav_button)
		{
		if(image_chain_index == G_MAXUINT)
			{
			image_chain = g_list_append(image_chain, g_strdup(path));
			image_chain_index = 0;
			}
		else
			{
			work = g_list_last(image_chain);
			if (g_strcmp0(work->data , path) != 0)
				{
				image_chain = g_list_append(image_chain, g_strdup(path));
				image_chain_index = g_list_length(image_chain) - 1;
				DEBUG_3("%d %s", image_chain_index, path);
				}
			else
				{
				image_chain_index = g_list_length(image_chain) - 1;
				}
			}
		}
	else
		{
		image_nav_button = FALSE;
		}
}

/*
 *-----------------------------------------------------------------------------
 * history lists
 *-----------------------------------------------------------------------------
 */

#define HISTORY_DEFAULT_KEY_COUNT 16


typedef struct _HistoryData HistoryData;
struct _HistoryData
{
	gchar *key;
	GList *list;
};

static GList *history_list = NULL;


static gchar *quoted_from_text(const gchar *text)
{
	const gchar *ptr;
	gint c = 0;
	gint l = strlen(text);

	if (l == 0) return NULL;

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
	return NULL;
}

gboolean history_list_load(const gchar *path)
{
	FILE *f;
	gchar *key = NULL;
	gchar s_buf[1024];
	gchar *pathl;

	pathl = path_from_utf8(path);
	f = fopen(pathl, "r");
	g_free(pathl);
	if (!f) return FALSE;

	/* first line must start with History comment */
	if (!fgets(s_buf, sizeof(s_buf), f) ||
	    strncmp(s_buf, "#History", 8) != 0)
		{
		fclose(f);
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
			gchar *value;

			value = quoted_from_text(s_buf);
			if (value && key)
				{
				history_list_add_to_key(key, value, 0);
				}
			g_free(value);
			}
		}

	fclose(f);

	g_free(key);

	return TRUE;
}

gboolean history_list_save(const gchar *path)
{
	SecureSaveInfo *ssi;
	GList *list;
	gchar *pathl;
	gint list_count;

	pathl = path_from_utf8(path);
	ssi = secure_open(pathl);
	g_free(pathl);
	if (!ssi)
		{
		log_printf(_("Unable to write history lists to: %s\n"), path);
		return FALSE;
		}

	secure_fprintf(ssi, "#History lists\n\n");

	list = g_list_last(history_list);
	while (list && secsave_errno == SS_ERR_NONE)
		{
		HistoryData *hd;
		GList *work;

		hd = list->data;
		list = list->prev;

		secure_fprintf(ssi, "[%s]\n", hd->key);

		/* save them inverted (oldest to newest)
		 * so that when reading they are added correctly
		 */
		work = g_list_last(hd->list);
		list_count = g_list_position(hd->list, g_list_last(hd->list)) + 1;
		while (work && secsave_errno == SS_ERR_NONE)
			{
			if ((!(strcmp(hd->key, "path_list") == 0 && list_count > options->open_recent_list_maxsize)) && (!(strcmp(hd->key, "recent") == 0 && (!isfile(work->data)))))
				{
				secure_fprintf(ssi, "\"%s\"\n", (gchar *)work->data);
				}
			work = work->prev;
			list_count--;
			}
		secure_fputc(ssi, '\n');
		}

	secure_fprintf(ssi, "#end\n");

	return (secure_close(ssi) == 0);
}

static void history_list_free(HistoryData *hd)
{
	GList *work;

	if (!hd) return;

	work = hd->list;
	while (work)
		{
		g_free(work->data);
		work = work->next;
		}

	g_free(hd->key);
	g_free(hd);
}

static HistoryData *history_list_find_by_key(const gchar *key)
{
	GList *work = history_list;

	if (!key) return NULL;

	while (work)
		{
		HistoryData *hd = work->data;
		if (strcmp(hd->key, key) == 0) return hd;
		work = work->next;
		}
	return NULL;
}

const gchar *history_list_find_last_path_by_key(const gchar *key)
{
	HistoryData *hd;

	hd = history_list_find_by_key(key);
	if (!hd || !hd->list) return NULL;

	return hd->list->data;
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
		hd->list = NULL;
		history_list = g_list_prepend(history_list, hd);
		}

	/* if already in the list, simply move it to the top */
	work = hd->list;
	while (work)
		{
		gchar *buf = work->data;

		if (strcmp(buf, path) == 0)
			{
			/* if not first, move it */
			if (work != hd->list)
				{
				hd->list = g_list_remove(hd->list, buf);
				hd->list = g_list_prepend(hd->list, buf);
				}
			return;
			}
		work = work->next;
		}

	hd->list = g_list_prepend(hd->list, g_strdup(path));

	if (max == -1) max = HISTORY_DEFAULT_KEY_COUNT;
	if (max > 0)
		{
		gint len = 0;
		GList *work = hd->list;
		GList *last = NULL;

		while (work)
			{
			len++;
			last = work;
			work = work->next;
			}

		work = last;
		while (work && len > max)
			{
			GList *node = work;
			work = work->prev;

			g_free(node->data);
			hd->list = g_list_delete_link(hd->list, node);
			len--;
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
		gchar *buf = work->data;
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
		work = work->next;
		}
}

void history_list_item_move(const gchar *key, const gchar *path, gint direction)
{
	HistoryData *hd;
	GList *work;
	gint p = 0;

	if (!path) return;
	hd = history_list_find_by_key(key);
	if (!hd) return;

	work = hd->list;
	while (work)
		{
		gchar *buf = work->data;
		if (strcmp(buf, path) == 0)
			{
			p += direction;
			if (p < 0) return;
			hd->list = g_list_remove(hd->list, buf);
			hd->list = g_list_insert(hd->list, buf, p);
			return;
			}
		work = work->next;
		p++;
		}
}

void history_list_item_remove(const gchar *key, const gchar *path)
{
	history_list_item_change(key, path, NULL);
}

GList *history_list_get_by_key(const gchar *key)
{
	HistoryData *hd;

	hd = history_list_find_by_key(key);
	if (!hd) return NULL;

	return hd->list;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
