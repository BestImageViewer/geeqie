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

#include "editors.h"

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <glib-object.h>

#include "compat.h"
#include "filedata.h"
#include "filefilter.h"
#include "intl.h"
#include "main-defines.h"
#include "main.h"
#include "options.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"
#include "utilops.h"

namespace
{

struct EditorVerboseData {
	GenericDialog *gd;
	GtkWidget *button_close;
	GtkWidget *button_stop;
	GtkWidget *text;
	GtkWidget *progress;
	GtkWidget *spinner;
};

struct EditorData {
	EditorFlags flags;
	GPid pid;
	GList *list;
	gint count;
	gint total;
	gboolean stopping;
	EditorVerboseData *vd;
	EditorCallback callback;
	gpointer data;
	const EditorDescription *editor;
	gchar *working_directory; /* fallback if no files are given (editor_no_param) */
};

constexpr gint EDITOR_WINDOW_WIDTH = 500;
constexpr gint EDITOR_WINDOW_HEIGHT = 300;

GHashTable *editors = nullptr;

} // namespace

static void editor_verbose_window_progress(EditorData *ed, const gchar *text);
static EditorFlags editor_command_next_start(EditorData *ed);
static EditorFlags editor_command_next_finish(EditorData *ed, gint status);
static EditorFlags editor_command_done(EditorData *ed);

/*
 *-----------------------------------------------------------------------------
 * external editor routines
 *-----------------------------------------------------------------------------
 */

GtkListStore *desktop_file_list;
static gboolean editors_finished = FALSE;

#ifdef G_KEY_FILE_DESKTOP_GROUP
#define DESKTOP_GROUP G_KEY_FILE_DESKTOP_GROUP
#else
#define DESKTOP_GROUP "Desktop Entry"
#endif

static void editor_description_free(EditorDescription *editor)
{
	if (!editor) return;

	g_free(editor->key);
	g_free(editor->name);
	g_free(editor->icon);
	g_free(editor->exec);
	g_free(editor->menu_path);
	g_free(editor->hotkey);
	g_free(editor->comment);
	g_list_free_full(editor->ext_list, g_free);
	g_free(editor->file);
	g_free(editor);
}

static GList *editor_mime_types_to_extensions(gchar **mime_types)
{
	/** @FIXME this should be rewritten to use the shared mime database, as soon as we switch to gio */

	static constexpr struct
	{
		const gchar *mime_type;
		const gchar *extensions;
	} conv_table[] = {
		{"image/*",		"*"},
		{"image/bmp",		".bmp"},
		{"image/gif",		".gif"},
		{"image/heic",		".heic"},
		{"image/jpeg",		".jpeg;.jpg;.mpo"},
		{"image/jpg",		".jpg;.jpeg"},
		{"image/jxl",		".jxl"},
		{"image/webp",		".webp"},
		{"image/pcx",		".pcx"},
		{"image/png",		".png"},
		{"image/svg",		".svg"},
		{"image/svg+xml",	".svg"},
		{"image/svg+xml-compressed", 	".svg"},
		{"image/tiff",		".tiff;.tif;.mef"},
		{"image/vnd-ms.dds",	".dds"},
		{"image/x-adobe-dng",	".dng"},
		{"image/x-bmp",		".bmp"},
		{"image/x-canon-crw",	".crw"},
		{"image/x-canon-cr2",	".cr2"},
		{"image/x-canon-cr3",	".cr3"},
		{"image/x-cr2",		".cr2"},
		{"image/x-dcraw",	"%raw;.mos"},
		{"image/x-epson-erf",	"%erf"},
		{"image/x-exr",		".exr"},
		{"image/x-ico",		".ico"},
		{"image/x-kodak-kdc",	".kdc"},
		{"image/x-mrw",		".mrw"},
		{"image/x-minolta-mrw",	".mrw"},
		{"image/x-MS-bmp",	".bmp"},
		{"image/x-nef",		".nef"},
		{"image/x-nikon-nef",	".nef"},
		{"image/x-nikon-nrw",	".nrw"},
		{"image/x-panasonic-raw",	".raw"},
		{"image/x-panasonic-rw2",	".rw2"},
		{"image/x-pentax-pef",	".pef"},
		{"image/x-orf",		".orf"},
		{"image/x-olympus-orf",	".orf"},
		{"image/x-pcx",		".pcx"},
		{"image/xpm",		".xpm"},
		{"image/x-png",		".png"},
		{"image/x-portable-anymap",	".pam"},
		{"image/x-portable-bitmap",	".pbm"},
		{"image/x-portable-graymap",	".pgm"},
		{"image/x-portable-pixmap",	".ppm"},
		{"image/x-psd",		".psd"},
		{"image/x-raf",		".raf"},
		{"image/x-fuji-raf",	".raf"},
		{"image/x-sgi",		".sgi"},
		{"image/x-sony-arw",	".arw"},
		{"image/x-sony-sr2",	".sr2"},
		{"image/x-sony-srf",	".srf"},
		{"image/x-tga",		".tga"},
		{"image/x-xbitmap",	".xbm"},
		{"image/x-xcf",		".xcf"},
		{"image/x-xpixmap",	".xpm"},
		{"application/x-navi-animation",		".ani"},
		{"application/x-ptoptimizer-script",	".pto"},
	};

	gint i;
	GList *list = nullptr;

	for (i = 0; mime_types[i]; i++)
		for (const auto& c : conv_table)
			if (strcmp(mime_types[i], c.mime_type) == 0)
				list = g_list_concat(list, filter_to_list(c.extensions));

	return list;
}

gboolean editor_read_desktop_file(const gchar *path)
{
	GKeyFile *key_file;
	EditorDescription *editor;
	gchar *extensions;
	const gchar *key = filename_from_path(path);
	GtkTreeIter iter;
	gboolean category_geeqie = FALSE;

	if (g_hash_table_lookup(editors, key)) return FALSE; /* the file found earlier wins */

	key_file = g_key_file_new();
	if (!g_key_file_load_from_file(key_file, path, static_cast<GKeyFileFlags>(0), nullptr))
		{
		g_key_file_free(key_file);
		return FALSE;
		}

	g_autofree gchar *type = g_key_file_get_string(key_file, DESKTOP_GROUP, "Type", nullptr);
	if (!type || strcmp(type, "Application") != 0)
		{
		/* We only consider desktop entries of Application type */
		g_key_file_free(key_file);
		return FALSE;
		}

	editor = g_new0(EditorDescription, 1);

	editor->key = g_strdup(key);
	editor->file = g_strdup(path);

	g_hash_table_insert(editors, editor->key, editor);

	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "Hidden", nullptr)
	    || g_key_file_get_boolean(key_file, DESKTOP_GROUP, "NoDisplay", nullptr))
		{
		editor->hidden = TRUE;
		}

	g_auto(GStrv) categories = g_key_file_get_string_list(key_file, DESKTOP_GROUP, "Categories", nullptr, nullptr);
	if (categories)
		{
		gboolean found = FALSE;
		gint i;
		for (i = 0; categories[i]; i++)
			{
			/* IMHO "Graphics" is exactly the category that we are interested in, so this does not have to be configurable */
			if (strcmp(categories[i], "Graphics") == 0)
				{
				found = TRUE;
				}
			if (strcmp(categories[i], "X-Geeqie") == 0)
				{
				found = TRUE;
				category_geeqie = TRUE;
				break;
				}
			}
		if (!found) editor->ignored = TRUE;
		}
	else
		{
		editor->ignored = TRUE;
		}

	g_auto(GStrv) only_show_in = g_key_file_get_string_list(key_file, DESKTOP_GROUP, "OnlyShowIn", nullptr, nullptr);
	if (only_show_in && !g_strv_contains(only_show_in, "X-Geeqie"))
		{
		editor->ignored = TRUE;
		}

	g_auto(GStrv) not_show_in = g_key_file_get_string_list(key_file, DESKTOP_GROUP, "NotShowIn", nullptr, nullptr);
	if (not_show_in && g_strv_contains(not_show_in, "X-Geeqie"))
		{
		editor->ignored = TRUE;
		}


	g_autofree gchar *try_exec = g_key_file_get_string(key_file, DESKTOP_GROUP, "TryExec", nullptr);
	if (try_exec && !editor->hidden && !editor->ignored)
		{
		g_autofree gchar *try_exec_res = g_find_program_in_path(try_exec);
		if (!try_exec_res) editor->hidden = TRUE;
		}

	if (editor->ignored)
		{
		/* ignored editors will be deleted, no need to parse the rest */
		g_key_file_free(key_file);
		return TRUE;
		}

	editor->name = g_key_file_get_locale_string(key_file, DESKTOP_GROUP, "Name", nullptr, nullptr);
	editor->icon = g_key_file_get_string(key_file, DESKTOP_GROUP, "Icon", nullptr);

	/* Icon key can be either a full path (absolute with file name extension) or an icon name (without extension) */
	if (editor->icon && !g_path_is_absolute(editor->icon))
		{
		gchar *ext = strrchr(editor->icon, '.');

		if (ext && strlen(ext) == 4 &&
		    (!strcmp(ext, ".png") || !strcmp(ext, ".xpm") || !strcmp(ext, ".svg")))
			{
			log_printf(_("Desktop file '%s' should not include extension in Icon key: '%s'\n"),
				   editor->file, editor->icon);

			// drop extension
			*ext = '\0';
			}
		}
	if (editor->icon && !register_theme_icon_as_stock(editor->key, editor->icon))
		{
		g_free(editor->icon);
		editor->icon = nullptr;
		}

	editor->exec = g_key_file_get_string(key_file, DESKTOP_GROUP, "Exec", nullptr);

	editor->menu_path = g_key_file_get_string(key_file, DESKTOP_GROUP, "X-Geeqie-Menu-Path", nullptr);
	if (!editor->menu_path) editor->menu_path = g_strdup("PluginsMenu");

	editor->hotkey = g_key_file_get_string(key_file, DESKTOP_GROUP, "X-Geeqie-Hotkey", nullptr);

	editor->comment = g_key_file_get_string(key_file, DESKTOP_GROUP, "Comment", nullptr);

	extensions = g_key_file_get_string(key_file, DESKTOP_GROUP, "X-Geeqie-File-Extensions", nullptr);
	if (extensions)
		editor->ext_list = filter_to_list(extensions);
	else
		{
		g_auto(GStrv) mime_types = g_key_file_get_string_list(key_file, DESKTOP_GROUP, "MimeType", nullptr, nullptr);
		if (mime_types)
			{
			editor->ext_list = editor_mime_types_to_extensions(mime_types);
			if (!editor->ext_list) editor->hidden = TRUE;
			}
		}

	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "X-Geeqie-Keep-Fullscreen", nullptr)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_KEEP_FS);
	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "X-Geeqie-Verbose", nullptr)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_VERBOSE);
	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "X-Geeqie-Verbose-Multi", nullptr)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_VERBOSE_MULTI);
	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "X-Geeqie-Filter", nullptr)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_DEST);
	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "Terminal", nullptr)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_TERMINAL);

	editor->flags = static_cast<EditorFlags>(editor->flags | editor_command_parse(editor, nullptr, FALSE, nullptr));

	if ((editor->flags & EDITOR_NO_PARAM) && !category_geeqie) editor->hidden = TRUE;

	g_key_file_free(key_file);

	editor->disabled = !path || std::any_of(options->disabled_plugins.cbegin(), options->disabled_plugins.cend(),
	                                        [path](const std::string &plugin){ return plugin == path; });

	gtk_list_store_append(desktop_file_list, &iter);
	gtk_list_store_set(desktop_file_list, &iter,
			   DESKTOP_FILE_COLUMN_KEY, key,
			   DESKTOP_FILE_COLUMN_DISABLED, editor->disabled,
			   DESKTOP_FILE_COLUMN_NAME, editor->name,
			   DESKTOP_FILE_COLUMN_HIDDEN, editor->hidden ? _("yes") : _("no"),
			   DESKTOP_FILE_COLUMN_WRITABLE, access_file(path, W_OK),
			   DESKTOP_FILE_COLUMN_PATH, path, -1);

	return TRUE;
}

static gboolean editor_remove_desktop_file_cb(gpointer, gpointer value, gpointer)
{
	auto editor = static_cast<EditorDescription *>(value);
	return editor->hidden || editor->ignored;
}

void editor_table_finish()
{
	g_hash_table_foreach_remove(editors, editor_remove_desktop_file_cb, nullptr);
	editors_finished = TRUE;
}

void editor_table_clear()
{
	if (desktop_file_list)
		{
		gtk_list_store_clear(desktop_file_list);
		}
	else
		{
		desktop_file_list = gtk_list_store_new(DESKTOP_FILE_COLUMN_COUNT, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
		}
	if (editors)
		{
		g_hash_table_destroy(editors);
		}
	editors = g_hash_table_new_full(g_str_hash, g_str_equal, nullptr, reinterpret_cast<GDestroyNotify>(editor_description_free));
	editors_finished = FALSE;
}

static GList *editor_add_desktop_dir(GList *list, const gchar *path)
{
	DIR *dp;
	struct dirent *dir;

	g_autofree gchar *pathl = path_from_utf8(path);
	dp = opendir(pathl);
	if (!dp)
		{
		/* dir not found */
		return list;
		}
	while ((dir = readdir(dp)) != nullptr)
		{
		gchar *namel = dir->d_name;

		if (g_str_has_suffix(namel, ".desktop"))
			{
			g_autofree gchar *name = path_to_utf8(namel);
			gchar *dpath = g_build_filename(path, name, NULL);
			list = g_list_prepend(list, dpath);
			}
		}
	closedir(dp);
	return list;
}

GList *editor_get_desktop_files()
{
	GList *list = nullptr;

	const gchar *xdg_data_dirs_env = getenv("XDG_DATA_DIRS");
	g_autofree gchar *xdg_data_dirs = (xdg_data_dirs_env && *xdg_data_dirs_env) ? path_to_utf8(xdg_data_dirs_env) : g_strdup("/usr/share");

	g_autofree gchar *all_dirs = g_strjoin(":", get_rc_dir(), gq_appdir, xdg_data_home_get(), xdg_data_dirs, NULL);

	g_auto(GStrv) split_dirs = g_strsplit(all_dirs, ":", 0);

	for (gint i = g_strv_length(split_dirs) - 1; i >= 0; i--)
		{
		g_autofree gchar *path = g_build_filename(split_dirs[i], "applications", NULL);
		list = editor_add_desktop_dir(list, path);
		}

	return list;
}

static void editor_list_add_cb(gpointer, gpointer value, gpointer data)
{
	auto editor = static_cast<EditorDescription *>(value);

	if (editor->disabled) return;

	/* do not show the special commands in any list, they are called explicitly */
	if (strcmp(editor->key, CMD_COPY) == 0 ||
	    strcmp(editor->key, CMD_MOVE) == 0 ||
	    strcmp(editor->key, CMD_RENAME) == 0 ||
	    strcmp(editor->key, CMD_DELETE) == 0 ||
	    strcmp(editor->key, CMD_FOLDER) == 0) return;

	auto *list = static_cast<EditorsList *>(data);
	list->push_back(editor);
}

EditorsList editor_list_get()
{
	if (!editors_finished) return {};

	EditorsList editors_list;
	g_hash_table_foreach(editors, editor_list_add_cb, &editors_list);

	static const auto editor_sort = [](const EditorDescription *a, const EditorDescription *b)
	{
		gint ret = strcmp(a->menu_path, b->menu_path);
		if (ret != 0) return ret < 0;

		g_autofree gchar *caseless_name_a = g_utf8_casefold(a->name, -1);
		g_autofree gchar *caseless_name_b = g_utf8_casefold(b->name, -1);
		g_autofree gchar *collate_key_a = g_utf8_collate_key_for_filename(caseless_name_a, -1);
		g_autofree gchar *collate_key_b = g_utf8_collate_key_for_filename(caseless_name_b, -1);

		return g_strcmp0(collate_key_a, collate_key_b) < 0;
	};
	std::sort(editors_list.begin(), editors_list.end(), editor_sort);

	return editors_list;
}

/* ------------------------------ */


static void editor_verbose_data_free(EditorData *ed)
{
	if (!ed->vd) return;
	g_free(ed->vd);
	ed->vd = nullptr;
}

static void editor_data_free(EditorData *ed)
{
	editor_verbose_data_free(ed);
	g_free(ed->working_directory);
	g_free(ed);
}

static void editor_verbose_window_close(GenericDialog *gd, gpointer data)
{
	auto ed = static_cast<EditorData *>(data);

	generic_dialog_close(gd);
	editor_verbose_data_free(ed);
	if (ed->pid == -1) editor_data_free(ed); /* the process has already terminated */
}

static void editor_verbose_window_stop(GenericDialog *, gpointer data)
{
	auto ed = static_cast<EditorData *>(data);
	ed->stopping = TRUE;
	ed->count = 0;
	editor_verbose_window_progress(ed, _("stopping..."));
}

static void editor_verbose_window_enable_close(EditorVerboseData *vd)
{
	vd->gd->cancel_cb = editor_verbose_window_close;

	gtk_spinner_stop(GTK_SPINNER(vd->spinner));
	gtk_widget_set_sensitive(vd->button_stop, FALSE);
	gtk_widget_set_sensitive(vd->button_close, TRUE);
}

static EditorVerboseData *editor_verbose_window(EditorData *ed, const gchar *text)
{
	EditorVerboseData *vd;
	GtkWidget *scrolled;
	GtkWidget *hbox;

	vd = g_new0(EditorVerboseData, 1);

	vd->gd = file_util_gen_dlg(_("Edit command results"), "editor_results",
				   nullptr, FALSE,
				   nullptr, ed);
	g_autofree gchar *buf = g_strdup_printf(_("Output of %s"), text);
	generic_dialog_add_message(vd->gd, nullptr, buf, nullptr, FALSE);
	vd->button_stop = generic_dialog_add_button(vd->gd, GQ_ICON_STOP, nullptr,
						   editor_verbose_window_stop, FALSE);
	gtk_widget_set_sensitive(vd->button_stop, FALSE);

	vd->button_close = generic_dialog_add_button(vd->gd, GQ_ICON_CLOSE, _("Close"),
						    editor_verbose_window_close, TRUE);
	gtk_widget_set_sensitive(vd->button_close, FALSE);

	scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gq_gtk_box_pack_start(GTK_BOX(vd->gd->vbox), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	vd->text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(vd->text), FALSE);
	gtk_widget_set_size_request(vd->text, EDITOR_WINDOW_WIDTH, EDITOR_WINDOW_HEIGHT);
	gq_gtk_container_add(scrolled, vd->text);
	gtk_widget_show(vd->text);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gq_gtk_box_pack_start(GTK_BOX(vd->gd->vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	vd->progress = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(vd->progress), 0.0);
	gq_gtk_box_pack_start(GTK_BOX(hbox), vd->progress, TRUE, TRUE, 0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(vd->progress), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(vd->progress), TRUE);
	gtk_widget_show(vd->progress);

	vd->spinner = gtk_spinner_new();
	gtk_spinner_start(GTK_SPINNER(vd->spinner));
	gq_gtk_box_pack_start(GTK_BOX(hbox), vd->spinner, FALSE, FALSE, 0);
	gtk_widget_show(vd->spinner);

	gtk_widget_show(vd->gd->dialog);

	ed->vd = vd;
	return vd;
}

static void editor_verbose_window_fill(EditorVerboseData *vd, const gchar *text, gint len)
{
	GtkTextBuffer *buffer;
	GtkTextIter iter;

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vd->text));
	gtk_text_buffer_get_iter_at_offset(buffer, &iter, -1);
	gtk_text_buffer_insert(buffer, &iter, text, len);
}

static void editor_verbose_window_progress(EditorData *ed, const gchar *text)
{
	if (!ed->vd) return;

	if (ed->total)
		{
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ed->vd->progress), static_cast<gdouble>(ed->count) / ed->total);
		}

	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ed->vd->progress), (text) ? text : "");
}

static gboolean editor_verbose_io_cb(GIOChannel *source, GIOCondition condition, gpointer data)
{
	auto ed = static_cast<EditorData *>(data);
	gchar buf[512];
	gsize count;

	if (condition & G_IO_IN)
		{
		while (g_io_channel_read_chars(source, buf, sizeof(buf), &count, nullptr) == G_IO_STATUS_NORMAL)
			{
			if (!g_utf8_validate(buf, count, nullptr))
				{
				g_autofree gchar *utf8 = g_locale_to_utf8(buf, count, nullptr, nullptr, nullptr);
				if (utf8)
					{
					editor_verbose_window_fill(ed->vd, utf8, -1);
					}
				else
					{
					editor_verbose_window_fill(ed->vd, "Error converting text to valid utf8\n", -1);
					}
				}
			else
				{
				editor_verbose_window_fill(ed->vd, buf, count);
				}
			}
		}

	if (condition & (G_IO_ERR | G_IO_HUP))
		{
		g_io_channel_shutdown(source, TRUE, nullptr);
		return FALSE;
		}

	return TRUE;
}

enum PathType {
	PATH_FILE,
	PATH_FILE_URL,
	PATH_DEST
};


static gchar *editor_command_path_parse(const FileData *fd, gboolean consider_sidecars, PathType type, const EditorDescription *editor)
{
	gchar *pathl;
	const gchar *p = nullptr;

	DEBUG_2("editor_command_path_parse: %s %d %d %s", fd->path, consider_sidecars, type, editor->key);

	if (type == PATH_FILE || type == PATH_FILE_URL)
		{
		GList *work = editor->ext_list;

		if (!work)
			p = fd->path;
		else
			{
			const auto file_data_compare_ext = [](gconstpointer data, gconstpointer user_data)
			{
				return g_ascii_strcasecmp(static_cast<const FileData *>(data)->extension, static_cast<const gchar *>(user_data));
			};

			while (work)
				{
				auto ext = static_cast<gchar *>(work->data);
				work = work->next;

				if (strcmp(ext, "*") == 0 ||
				    g_ascii_strcasecmp(ext, fd->extension) == 0)
					{
					p = fd->path;
					break;
					}

				if (consider_sidecars)
					{
					GList *work2 = g_list_find_custom(fd->sidecar_files, ext, file_data_compare_ext);
					if (work2)
						{
						auto sfd = static_cast<FileData *>(work2->data);
						p = sfd->path;
						}
					}

				if (p) break;
				}

			if (!p) return nullptr;
			}
		}
	else if (type == PATH_DEST)
		{
		if (fd->change && fd->change->dest)
			p = fd->change->dest;
		else
			p = "";
		}

	g_assert(p);

	g_autoptr(GString) string = g_string_new(p);
	if (type == PATH_FILE_URL) g_string_prepend(string, "file://");

	pathl = path_from_utf8(string->str);
	if (pathl && !pathl[0]) /* empty string case */
		{
		g_free(pathl);
		pathl = nullptr;
		}

	DEBUG_2("editor_command_path_parse: return %s", pathl);
	return pathl;
}

struct CommandBuilder
{
	~CommandBuilder()
	{
		if (!str) return;

		g_string_free(str, TRUE);
	}

	void init()
	{
		if (str) return;

		str = g_string_new("");
	}

	void append(const gchar *val)
	{
		if (!str) return;

		str = g_string_append(str, val);
	}

	void append_c(gchar c)
	{
		if (!str) return;

		str = g_string_append_c(str, c);
	}

	void append_quoted(const char *s, gboolean single_quotes, gboolean double_quotes)
	{
		if (!str) return;

		if (!single_quotes)
			{
			if (!double_quotes)
				str = g_string_append_c(str, '\'');
			else
				str = g_string_append(str, "\"'");
			}

		for (const char *p = s; *p != '\0'; p++)
			{
			if (*p == '\'')
				str = g_string_append(str, "'\\''");
			else
				str = g_string_append_c(str, *p);
			}

		if (!single_quotes)
			{
			if (!double_quotes)
				str = g_string_append_c(str, '\'');
			else
				str = g_string_append(str, "'\"");
			}
	}

	gchar *get_command()
	{
		if (!str) return nullptr;

		auto command = g_string_free(str, FALSE);
		str = nullptr;
		return command;
	}

private:
	GString *str{nullptr};
};


EditorFlags editor_command_parse(const EditorDescription *editor, GList *list, gboolean consider_sidecars, gchar **output)
{
	auto flags = static_cast<EditorFlags>(0);
	const gchar *p;
	CommandBuilder result;
	gboolean escape = FALSE;
	gboolean single_quotes = FALSE;
	gboolean double_quotes = FALSE;

	DEBUG_2("editor_command_parse: %s %d %d", editor->key, consider_sidecars, !!output);

	if (output)
		{
		*output = nullptr;
		result.init();
		}

	if (editor->exec == nullptr || editor->exec[0] == '\0')
		{
		return static_cast<EditorFlags>(flags | EDITOR_ERROR_EMPTY);
		}

	p = editor->exec;
	/* skip leading whitespaces if any */
	while (g_ascii_isspace(*p)) p++;

	/* command */

	while (*p)
		{
		if (escape)
			{
			escape = FALSE;
			result.append_c(*p);
			}
		else if (*p == '\\')
			{
			if (!single_quotes) escape = TRUE;
			result.append_c(*p);
			}
		else if (*p == '\'')
			{
			result.append_c(*p);
			if (!single_quotes && !double_quotes)
				single_quotes = TRUE;
			else if (single_quotes)
				single_quotes = FALSE;
			}
		else if (*p == '"')
			{
			result.append_c(*p);
			if (!single_quotes && !double_quotes)
				double_quotes = TRUE;
			else if (double_quotes)
				double_quotes = FALSE;
			}
		else if (*p == '%' && p[1])
			{
			p++;

			switch (*p)
				{
				case 'f': /* single file */
				case 'u': /* single url */
					flags = static_cast<EditorFlags>(flags | EDITOR_FOR_EACH);
					if (flags & EDITOR_SINGLE_COMMAND)
						{
						return static_cast<EditorFlags>(flags | EDITOR_ERROR_INCOMPATIBLE);
						}
					if (list)
						{
						/* use the first file from the list */
						if (!list->data)
							{
							return static_cast<EditorFlags>(flags | EDITOR_ERROR_NO_FILE);
							}

						PathType path_type = (*p == 'f') ? PATH_FILE : PATH_FILE_URL;
						g_autofree gchar *pathl = editor_command_path_parse(static_cast<FileData *>(list->data),
						                                                    consider_sidecars,
						                                                    path_type,
						                                                    editor);
						if (!output)
							{
							/* just testing, check also the rest of the list (like with F and U)
							   any matching file is OK */
							GList *work = list->next;

							while (!pathl && work)
								{
								pathl = editor_command_path_parse(static_cast<FileData *>(work->data),
								                                  consider_sidecars,
								                                  path_type,
								                                  editor);
								work = work->next;
								}
							}

						if (!pathl)
							{
							return static_cast<EditorFlags>(flags | EDITOR_ERROR_NO_FILE);
							}
						result.append_quoted(pathl, single_quotes, double_quotes);
						}
					break;

				case 'F':
				case 'U':
					flags = static_cast<EditorFlags>(flags | EDITOR_SINGLE_COMMAND);
					if (flags & (EDITOR_FOR_EACH | EDITOR_DEST))
						{
						return static_cast<EditorFlags>(flags | EDITOR_ERROR_INCOMPATIBLE);
						}

					if (list)
						{
						/* use whole list */
						GList *work = list;
						gboolean ok = FALSE;
						PathType path_type = (*p == 'F') ? PATH_FILE : PATH_FILE_URL;

						while (work)
							{
							g_autofree gchar *pathl = editor_command_path_parse(static_cast<FileData *>(work->data),
							                                                    consider_sidecars,
							                                                    path_type,
							                                                    editor);
							if (pathl)
								{
								ok = TRUE;

								if (work != list)
									{
									result.append_c(' ');
									}
								result.append_quoted(pathl, single_quotes, double_quotes);
								}
							work = work->next;
							}
						if (!ok)
							{
							return static_cast<EditorFlags>(flags | EDITOR_ERROR_NO_FILE);
							}
						}
					break;
				case 'i':
					if (editor->icon && *editor->icon)
						{
						result.append("--icon ");
						result.append_quoted(editor->icon, single_quotes, double_quotes);
						}
					break;
				case 'c':
					result.append_quoted(editor->name, single_quotes, double_quotes);
					break;
				case 'k':
					result.append_quoted(editor->file, single_quotes, double_quotes);
					break;
				case '%':
					/* %% = % escaping */
					result.append_c(*p);
					break;
				case 'd':
				case 'D':
				case 'n':
				case 'N':
				case 'v':
				case 'm':
					/* deprecated according to spec, ignore */
					break;
				default:
					return static_cast<EditorFlags>(flags | EDITOR_ERROR_SYNTAX);
				}
			}
		else
			{
			result.append_c(*p);
			}
		p++;
		}

	if (!(flags & (EDITOR_FOR_EACH | EDITOR_SINGLE_COMMAND))) flags = static_cast<EditorFlags>(flags | EDITOR_NO_PARAM);

	if (output)
		{
		*output = result.get_command();
		DEBUG_3("Editor cmd: %s", *output);
		}

	return flags;
}


static void editor_child_exit_cb(GPid pid, gint status, gpointer data)
{
	auto ed = static_cast<EditorData *>(data);
	g_spawn_close_pid(pid);
	ed->pid = -1;

	editor_command_next_finish(ed, status);
}


static EditorFlags editor_command_one(const EditorDescription *editor, GList *list, EditorData *ed)
{
	g_autofree gchar *command = nullptr;
	auto fd = static_cast<FileData *>((ed->flags & EDITOR_NO_PARAM) ? nullptr : list->data);;
	GPid pid;
	gint standard_output;
	gint standard_error;
	gboolean ok;

	ed->pid = -1;
	ed->flags = editor->flags;
	ed->flags = static_cast<EditorFlags>(ed->flags | editor_command_parse(editor, list, TRUE, &command));

	ok = !editor_errors(ed->flags);

	if (ok)
		{
		ok = (options->shell.path && *options->shell.path);
		if (!ok) log_printf("ERROR: empty shell command\n");

		if (ok)
			{
			ok = (access(options->shell.path, X_OK) == 0);
			if (!ok) log_printf("ERROR: cannot execute shell command '%s'\n", options->shell.path);
			}

		if (!ok) ed->flags = static_cast<EditorFlags>(ed->flags | EDITOR_ERROR_CANT_EXEC);
		}

	if (ok)
		{
		gchar *args[4];
		guint n = 0;

		g_autofree gchar *working_directory = fd ? remove_level_from_path(fd->path) : g_strdup(ed->working_directory);
		args[n++] = options->shell.path;
		if (options->shell.options && *options->shell.options)
			args[n++] = options->shell.options;
		args[n++] = command;
		args[n] = nullptr;

		if ((ed->flags & EDITOR_DEST) && fd && fd->change && fd->change->dest) /** @FIXME error handling */
			{
			g_setenv("GEEQIE_DESTINATION", fd->change->dest, TRUE);
			}
		else
			{
			g_unsetenv("GEEQIE_DESTINATION");
			}

		ok = g_spawn_async_with_pipes(working_directory, args, nullptr,
				      G_SPAWN_DO_NOT_REAP_CHILD, /* GSpawnFlags */
				      nullptr, nullptr,
				      &pid,
				      nullptr,
				      ed->vd ? &standard_output : nullptr,
				      ed->vd ? &standard_error : nullptr,
				      nullptr);

		if (!ok) ed->flags = static_cast<EditorFlags>(ed->flags | EDITOR_ERROR_CANT_EXEC);
		}

	if (ok)
		{
		g_child_watch_add(pid, editor_child_exit_cb, ed);
		ed->pid = pid;
		}

	if (ed->vd)
		{
		if (!ok)
			{
			g_autofree gchar *buf = g_strdup_printf(_("Failed to run command:\n%s\n"), editor->file);

			editor_verbose_window_fill(ed->vd, buf, -1);
			}
		else
			{
			GIOChannel *channel_output;
			GIOChannel *channel_error;

			channel_output = g_io_channel_unix_new(standard_output);
			g_io_channel_set_flags(channel_output, G_IO_FLAG_NONBLOCK, nullptr);
			g_io_channel_set_encoding(channel_output, nullptr, nullptr);

			g_io_add_watch_full(channel_output, G_PRIORITY_HIGH, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP),
					    editor_verbose_io_cb, ed, nullptr);
			g_io_add_watch_full(channel_output, G_PRIORITY_HIGH, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP),
					    editor_verbose_io_cb, ed, nullptr);
			g_io_channel_unref(channel_output);

			channel_error = g_io_channel_unix_new(standard_error);
			g_io_channel_set_flags(channel_error, G_IO_FLAG_NONBLOCK, nullptr);
			g_io_channel_set_encoding(channel_error, nullptr, nullptr);

			g_io_add_watch_full(channel_error, G_PRIORITY_HIGH, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP),
					    editor_verbose_io_cb, ed, nullptr);
			g_io_channel_unref(channel_error);
			}
		}

	return static_cast<EditorFlags>(editor_errors(ed->flags));
}

static EditorFlags editor_command_next_start(EditorData *ed)
{
	if (ed->vd) editor_verbose_window_fill(ed->vd, "\n", 1);

	if ((ed->list || (ed->flags & EDITOR_NO_PARAM)) && ed->count < ed->total)
		{
		FileData *fd;
		EditorFlags error;

		fd = static_cast<FileData *>((ed->flags & EDITOR_NO_PARAM) ? nullptr : ed->list->data);

		if (ed->vd)
			{
			if ((ed->flags & EDITOR_FOR_EACH) && fd)
				editor_verbose_window_progress(ed, fd->path);
			else
				editor_verbose_window_progress(ed, _("running..."));
			}
		ed->count++;

		error = editor_command_one(ed->editor, ed->list, ed);
		if (!error && ed->vd)
			{
			gtk_widget_set_sensitive(ed->vd->button_stop, (ed->list != nullptr) );
			if ((ed->flags & EDITOR_FOR_EACH) && fd)
				{
				editor_verbose_window_fill(ed->vd, fd->path, -1);
				editor_verbose_window_fill(ed->vd, "\n", 1);
				}
			}

		if (!error)
			return static_cast<EditorFlags>(0);

		/* command was not started, call the finish immediately */
		return editor_command_next_finish(ed, 0);
		}

	/* everything is done */
	return editor_command_done(ed);
}

static EditorFlags editor_command_next_finish(EditorData *ed, gint status)
{
	gint cont = ed->stopping ? EDITOR_CB_SKIP : EDITOR_CB_CONTINUE;

	if (status)
		ed->flags = static_cast<EditorFlags>(ed->flags | EDITOR_ERROR_STATUS);

	if (ed->flags & EDITOR_FOR_EACH)
		{
		/* handle the first element from the list */
		g_autoptr(FileDataList) fd_element = ed->list;

		ed->list = g_list_remove_link(ed->list, fd_element);
		if (ed->callback)
			{
			cont = ed->callback(ed->list ? ed : nullptr, ed->flags, fd_element, ed->data);
			if (ed->stopping && cont == EDITOR_CB_CONTINUE) cont = EDITOR_CB_SKIP;
			}
		}
	else
		{
		/* handle whole list */
		if (ed->callback)
			cont = ed->callback(nullptr, ed->flags, ed->list, ed->data);
		file_data_list_free(ed->list);
		ed->list = nullptr;
		}

	switch (cont)
		{
		case EDITOR_CB_SUSPEND:
		return static_cast<EditorFlags>(editor_errors(ed->flags));
		case EDITOR_CB_SKIP:
			return editor_command_done(ed);
		default:
			break;
		}

	return editor_command_next_start(ed);
}

static EditorFlags editor_command_done(EditorData *ed)
{
	EditorFlags flags;

	if (ed->vd)
		{
		if (ed->count == ed->total)
			{
			editor_verbose_window_progress(ed, _("done"));
			}
		else
			{
			editor_verbose_window_progress(ed, _("stopped by user"));
			}
		editor_verbose_window_enable_close(ed->vd);
		}

	/* free the not-handled items */
	if (ed->list)
		{
		ed->flags = static_cast<EditorFlags>(ed->flags | EDITOR_ERROR_SKIPPED);
		if (ed->callback) ed->callback(nullptr, ed->flags, ed->list, ed->data);
		file_data_list_free(ed->list);
		ed->list = nullptr;
		}

	ed->count = 0;

	flags = static_cast<EditorFlags>(editor_errors(ed->flags));

	if (!ed->vd) editor_data_free(ed);

	return flags;
}

void editor_resume(gpointer ed)
{
 	editor_command_next_start(reinterpret_cast<EditorData *>(ed));
}

void editor_skip(gpointer ed)
{
	editor_command_done(static_cast<EditorData *>(ed));
}

static EditorFlags editor_command_start(const EditorDescription *editor, const gchar *text, GList *list, const gchar *working_directory, EditorCallback cb, gpointer data)
{
	EditorData *ed;
	EditorFlags flags = editor->flags;

	if (editor_errors(flags)) return static_cast<EditorFlags>(editor_errors(flags));

	ed = g_new0(EditorData, 1);
	ed->list = filelist_copy(list);
	ed->flags = flags;
	ed->editor = editor;
	ed->total = (flags & (EDITOR_SINGLE_COMMAND | EDITOR_NO_PARAM)) ? 1 : g_list_length(list);
	ed->callback = cb;
	ed->data = data;
	ed->working_directory = g_strdup(working_directory);

	if ((flags & EDITOR_VERBOSE_MULTI) && list && list->next)
		flags = static_cast<EditorFlags>(flags | EDITOR_VERBOSE);

	if (flags & EDITOR_VERBOSE)
		editor_verbose_window(ed, text);

	editor_command_next_start(ed);
	/* errors from editor_command_next_start will be handled via callback */
	return static_cast<EditorFlags>(editor_errors(flags));
}

EditorDescription *get_editor_by_command(const gchar *key)
{
	if (!key) return nullptr;
	return static_cast<EditorDescription *>(g_hash_table_lookup(editors, key));
}

bool is_valid_editor_command(const gchar *key)
{
	return get_editor_by_command(key) != nullptr;
}

EditorFlags start_editor_from_filelist_full(const gchar *key, GList *list, const gchar *working_directory, EditorCallback cb, gpointer data)
{
	EditorFlags error;
	EditorDescription *editor;
	if (!key) return EDITOR_ERROR_EMPTY;

	editor = static_cast<EditorDescription *>(g_hash_table_lookup(editors, key));

	if (!editor) return EDITOR_ERROR_EMPTY;
	if (!list && !(editor->flags & EDITOR_NO_PARAM)) return EDITOR_ERROR_NO_FILE;

	error = editor_command_parse(editor, list, TRUE, nullptr);

	if (editor_errors(error)) return error;

	error = static_cast<EditorFlags>(error | editor_command_start(editor, editor->name, list, working_directory, cb, data));

	if (editor_errors(error))
		{
		g_autofree gchar *text = g_strdup_printf(_("%s\n\"%s\""), editor_get_error_str(error), editor->file);

		file_util_warning_dialog(_("Invalid editor command"), text, GQ_ICON_DIALOG_ERROR, nullptr);
		}

	return static_cast<EditorFlags>(editor_errors(error));
}

EditorFlags start_editor_from_filelist(const gchar *key, GList *list)
{
	return start_editor_from_filelist_full(key, list, nullptr, nullptr, nullptr);
}

EditorFlags start_editor_from_file_full(const gchar *key, FileData *fd, EditorCallback cb, gpointer data)
{
	GList *list;
	EditorFlags error;

	if (!fd) return static_cast<EditorFlags>(FALSE);

	list = g_list_append(nullptr, fd);
	error = start_editor_from_filelist_full(key, list, nullptr, cb, data);
	g_list_free(list);
	return error;
}

EditorFlags start_editor_from_file(const gchar *key, FileData *fd)
{
	return start_editor_from_file_full(key, fd, nullptr, nullptr);
}

EditorFlags start_editor(const gchar *key, const gchar *working_directory)
{
	return start_editor_from_filelist_full(key, nullptr, working_directory, nullptr, nullptr);
}

gboolean editor_window_flag_set(const gchar *key)
{
	EditorDescription *editor;
	if (!key) return TRUE;

	editor = static_cast<EditorDescription *>(g_hash_table_lookup(editors, key));
	if (!editor) return TRUE;

	return !!(editor->flags & EDITOR_KEEP_FS);
}

gboolean editor_is_filter(const gchar *key)
{
	EditorDescription *editor;
	if (!key) return TRUE;

	editor = static_cast<EditorDescription *>(g_hash_table_lookup(editors, key));
	if (!editor) return TRUE;

	return !!(editor->flags & EDITOR_DEST);
}

gboolean editor_no_param(const gchar *key)
{
	EditorDescription *editor;
	if (!key) return FALSE;

	editor = static_cast<EditorDescription *>(g_hash_table_lookup(editors, key));
	if (!editor) return FALSE;

	return !!(editor->flags & EDITOR_NO_PARAM);
}

gboolean editor_blocks_file(const gchar *key)
{
	EditorDescription *editor;
	if (!key) return FALSE;

	editor = static_cast<EditorDescription *>(g_hash_table_lookup(editors, key));
	if (!editor) return FALSE;

	/* Decide if the image file should be blocked during editor execution
	   Editors like gimp can be used long time after the original file was
	   saved, for editing unrelated files.
	   %f vs. %F seems to be a good heuristic to detect this kind of editors.
	*/

	return !(editor->flags & EDITOR_SINGLE_COMMAND);
}

const gchar *editor_get_error_str(EditorFlags flags)
{
	if (flags & EDITOR_ERROR_EMPTY) return _("Editor template is empty.");
	if (flags & EDITOR_ERROR_SYNTAX) return _("Editor template has incorrect syntax.");
	if (flags & EDITOR_ERROR_INCOMPATIBLE) return _("Editor template uses incompatible macros.");
	if (flags & EDITOR_ERROR_NO_FILE) return _("Can't find matching file type.");
	if (flags & EDITOR_ERROR_CANT_EXEC) return _("Can't execute external editor.");
	if (flags & EDITOR_ERROR_STATUS) return _("External editor returned error status.");
	if (flags & EDITOR_ERROR_SKIPPED) return _("File was skipped.");
	return _("Unknown error.");
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
