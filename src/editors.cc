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

#include "main.h"
#include "editors.h"

#include "filedata.h"
#include "filefilter.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "ui-spinner.h"
#include "utilops.h"

#define EDITOR_WINDOW_WIDTH 500
#define EDITOR_WINDOW_HEIGHT 300



typedef struct _EditorVerboseData EditorVerboseData;
struct _EditorVerboseData {
	GenericDialog *gd;
	GtkWidget *button_close;
	GtkWidget *button_stop;
	GtkWidget *text;
	GtkWidget *progress;
	GtkWidget *spinner;
};

typedef struct _EditorData EditorData;
struct _EditorData {
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


static void editor_verbose_window_progress(EditorData *ed, const gchar *text);
static EditorFlags editor_command_next_start(EditorData *ed);
static EditorFlags editor_command_next_finish(EditorData *ed, gint status);
static EditorFlags editor_command_done(EditorData *ed);

/*
 *-----------------------------------------------------------------------------
 * external editor routines
 *-----------------------------------------------------------------------------
 */

GHashTable *editors = NULL;
GtkListStore *desktop_file_list;
gboolean editors_finished = FALSE;

#ifdef G_KEY_FILE_DESKTOP_GROUP
#define DESKTOP_GROUP G_KEY_FILE_DESKTOP_GROUP
#else
#define DESKTOP_GROUP "Desktop Entry"
#endif

void editor_description_free(EditorDescription *editor)
{
	if (!editor) return;

	g_free(editor->key);
	g_free(editor->name);
	g_free(editor->icon);
	g_free(editor->exec);
	g_free(editor->menu_path);
	g_free(editor->hotkey);
	g_free(editor->comment);
	string_list_free(editor->ext_list);
	g_free(editor->file);
	g_free(editor);
}

static GList *editor_mime_types_to_extensions(gchar **mime_types)
{
	/** @FIXME this should be rewritten to use the shared mime database, as soon as we switch to gio */

	static const gchar *conv_table[][2] = {
		{"image/*",		"*"},
		{"image/bmp",		".bmp"},
		{"image/gif",		".gif"},
		{"image/heic",		".heic"},
		{"image/jpeg",		".jpeg;.jpg;.mpo"},
		{"image/jpg",		".jpg;.jpeg"},
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
		{"image/x-ico",		".ico"},
		{"image/x-kodak-kdc",	".kdc"},
		{"image/x-mrw",		".mrw"},
		{"image/x-minolta-mrw",	".mrw"},
		{"image/x-MS-bmp",	".bmp"},
		{"image/x-nef",		".nef"},
		{"image/x-nikon-nef",	".nef"},
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
		{"image/x-x3f",		".x3f"},
		{"application/x-navi-animation",		".ani"},
		{"application/x-ptoptimizer-script",	".pto"},
		{NULL, NULL}};

	gint i, j;
	GList *list = NULL;

	for (i = 0; mime_types[i]; i++)
		for (j = 0; conv_table[j][0]; j++)
			if (strcmp(mime_types[i], conv_table[j][0]) == 0)
				list = g_list_concat(list, filter_to_list(conv_table[j][1]));

	return list;
}

gboolean editor_read_desktop_file(const gchar *path)
{
	GKeyFile *key_file;
	EditorDescription *editor;
	gchar *extensions;
	gchar *type;
	const gchar *key = filename_from_path(path);
	gchar **categories, **only_show_in, **not_show_in;
	gchar *try_exec;
	GtkTreeIter iter;
	gboolean category_geeqie = FALSE;
	GList *work;
	gboolean disabled;

	if (g_hash_table_lookup(editors, key)) return FALSE; /* the file found earlier wins */

	key_file = g_key_file_new();
	if (!g_key_file_load_from_file(key_file, path, static_cast<GKeyFileFlags>(0), NULL))
		{
		g_key_file_free(key_file);
		return FALSE;
		}

	type = g_key_file_get_string(key_file, DESKTOP_GROUP, "Type", NULL);
	if (!type || strcmp(type, "Application") != 0)
		{
		/* We only consider desktop entries of Application type */
		g_key_file_free(key_file);
		g_free(type);
		return FALSE;
		}
	g_free(type);

	editor = g_new0(EditorDescription, 1);

	editor->key = g_strdup(key);
	editor->file = g_strdup(path);

	g_hash_table_insert(editors, editor->key, editor);

	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "Hidden", NULL)
	    || g_key_file_get_boolean(key_file, DESKTOP_GROUP, "NoDisplay", NULL))
	    	{
	    	editor->hidden = TRUE;
		}

	categories = g_key_file_get_string_list(key_file, DESKTOP_GROUP, "Categories", NULL, NULL);
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
		g_strfreev(categories);
		}
	else
		{
		editor->ignored = TRUE;
		}

	only_show_in = g_key_file_get_string_list(key_file, DESKTOP_GROUP, "OnlyShowIn", NULL, NULL);
	if (only_show_in)
		{
		gboolean found = FALSE;
		gint i;
		for (i = 0; only_show_in[i]; i++)
			if (strcmp(only_show_in[i], "X-Geeqie") == 0)
				{
				found = TRUE;
				break;
				}
		if (!found) editor->ignored = TRUE;
		g_strfreev(only_show_in);
		}

	not_show_in = g_key_file_get_string_list(key_file, DESKTOP_GROUP, "NotShowIn", NULL, NULL);
	if (not_show_in)
		{
		gboolean found = FALSE;
		gint i;
		for (i = 0; not_show_in[i]; i++)
			if (strcmp(not_show_in[i], "X-Geeqie") == 0)
				{
				found = TRUE;
				break;
				}
		if (found) editor->ignored = TRUE;
		g_strfreev(not_show_in);
		}


	try_exec = g_key_file_get_string(key_file, DESKTOP_GROUP, "TryExec", NULL);
	if (try_exec && !editor->hidden && !editor->ignored)
		{
		gchar *try_exec_res = g_find_program_in_path(try_exec);
		if (!try_exec_res) editor->hidden = TRUE;
		g_free(try_exec_res);
		g_free(try_exec);
		}

	if (editor->ignored)
		{
		/* ignored editors will be deleted, no need to parse the rest */
		g_key_file_free(key_file);
		return TRUE;
		}

	editor->name = g_key_file_get_locale_string(key_file, DESKTOP_GROUP, "Name", NULL, NULL);
	editor->icon = g_key_file_get_string(key_file, DESKTOP_GROUP, "Icon", NULL);

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
		editor->icon = NULL;
		}

	editor->exec = g_key_file_get_string(key_file, DESKTOP_GROUP, "Exec", NULL);

	editor->menu_path = g_key_file_get_string(key_file, DESKTOP_GROUP, "X-Geeqie-Menu-Path", NULL);
	if (!editor->menu_path) editor->menu_path = g_strdup("PluginsMenu");

	editor->hotkey = g_key_file_get_string(key_file, DESKTOP_GROUP, "X-Geeqie-Hotkey", NULL);

	editor->comment = g_key_file_get_string(key_file, DESKTOP_GROUP, "Comment", NULL);

	extensions = g_key_file_get_string(key_file, DESKTOP_GROUP, "X-Geeqie-File-Extensions", NULL);
	if (extensions)
		editor->ext_list = filter_to_list(extensions);
	else
		{
		gchar **mime_types = g_key_file_get_string_list(key_file, DESKTOP_GROUP, "MimeType", NULL, NULL);
		if (mime_types)
			{
			editor->ext_list = editor_mime_types_to_extensions(mime_types);
			g_strfreev(mime_types);
			if (!editor->ext_list) editor->hidden = TRUE;
			}
		}

	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "X-Geeqie-Keep-Fullscreen", NULL)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_KEEP_FS);
	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "X-Geeqie-Verbose", NULL)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_VERBOSE);
	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "X-Geeqie-Verbose-Multi", NULL)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_VERBOSE_MULTI);
	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "X-Geeqie-Filter", NULL)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_DEST);
	if (g_key_file_get_boolean(key_file, DESKTOP_GROUP, "Terminal", NULL)) editor->flags = static_cast<EditorFlags>(editor->flags | EDITOR_TERMINAL);

	editor->flags = static_cast<EditorFlags>(editor->flags | editor_command_parse(editor, NULL, FALSE, NULL));

	if ((editor->flags & EDITOR_NO_PARAM) && !category_geeqie) editor->hidden = TRUE;

	g_key_file_free(key_file);

	if (editor->ignored) return TRUE;

	work = options->disabled_plugins;

	disabled = FALSE;
	while (work)
		{
		if (g_strcmp0(path, static_cast<const gchar *>(work->data)) == 0)
			{
			disabled = TRUE;
			break;
			}
		work = work->next;
		}

	editor->disabled = disabled;

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

static gboolean editor_remove_desktop_file_cb(gpointer UNUSED(key), gpointer value, gpointer UNUSED(user_data))
{
	auto editor = static_cast<EditorDescription *>(value);
	return editor->hidden || editor->ignored;
}

void editor_table_finish(void)
{
	g_hash_table_foreach_remove(editors, editor_remove_desktop_file_cb, NULL);
	editors_finished = TRUE;
}

void editor_table_clear(void)
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
	editors = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, reinterpret_cast<GDestroyNotify>(editor_description_free));
	editors_finished = FALSE;
}

static GList *editor_add_desktop_dir(GList *list, const gchar *path)
{
	DIR *dp;
	struct dirent *dir;
	gchar *pathl;

	pathl = path_from_utf8(path);
	dp = opendir(pathl);
	g_free(pathl);
	if (!dp)
		{
		/* dir not found */
		return list;
		}
	while ((dir = readdir(dp)) != NULL)
		{
		gchar *namel = dir->d_name;

		if (g_str_has_suffix(namel, ".desktop"))
			{
			gchar *name = path_to_utf8(namel);
			gchar *dpath = g_build_filename(path, name, NULL);
			list = g_list_prepend(list, dpath);
			g_free(name);
			}
		}
	closedir(dp);
	return list;
}

GList *editor_get_desktop_files(void)
{
	gchar *path;
	gchar *xdg_data_dirs;
	gchar *all_dirs;
	gchar **split_dirs;
	gint i;
	GList *list = NULL;

	xdg_data_dirs = getenv("XDG_DATA_DIRS");
	if (xdg_data_dirs && xdg_data_dirs[0])
		xdg_data_dirs = path_to_utf8(xdg_data_dirs);
	else
		xdg_data_dirs = g_strdup("/usr/share");

	all_dirs = g_strconcat(get_rc_dir(), ":", gq_appdir, ":", xdg_data_home_get(), ":", xdg_data_dirs, NULL);

	g_free(xdg_data_dirs);

	split_dirs = g_strsplit(all_dirs, ":", 0);

	g_free(all_dirs);

	for (i = 0; split_dirs[i]; i++);
	for (--i; i >= 0; i--)
		{
		path = g_build_filename(split_dirs[i], "applications", NULL);
		list = editor_add_desktop_dir(list, path);
		g_free(path);
		}

	g_strfreev(split_dirs);
	return list;
}

static void editor_list_add_cb(gpointer UNUSED(key), gpointer value, gpointer data)
{
	auto listp = static_cast<GList **>(data);
	auto editor = static_cast<EditorDescription *>(value);

	/* do not show the special commands in any list, they are called explicitly */
	if (strcmp(editor->key, CMD_COPY) == 0 ||
	    strcmp(editor->key, CMD_MOVE) == 0 ||
	    strcmp(editor->key, CMD_RENAME) == 0 ||
	    strcmp(editor->key, CMD_DELETE) == 0 ||
	    strcmp(editor->key, CMD_FOLDER) == 0) return;

	if (editor->disabled)
		{
		return;
		}

	*listp = g_list_prepend(*listp, editor);
}

static gint editor_sort(gconstpointer a, gconstpointer b)
{
	auto ea = static_cast<const EditorDescription *>(a);
	auto eb = static_cast<const EditorDescription *>(b);
	gchar *caseless_name_ea;
	gchar *caseless_name_eb;
	gchar *collate_key_ea;
	gchar *collate_key_eb;
	gint ret;

	ret = strcmp(ea->menu_path, eb->menu_path);
	if (ret != 0) return ret;

	caseless_name_ea = g_utf8_casefold(ea->name, -1);
	caseless_name_eb = g_utf8_casefold(eb->name, -1);
	collate_key_ea = g_utf8_collate_key_for_filename(caseless_name_ea, -1);
	collate_key_eb = g_utf8_collate_key_for_filename(caseless_name_eb, -1);
	ret = g_strcmp0(collate_key_ea, collate_key_eb);

	g_free(collate_key_ea);
	g_free(collate_key_eb);
	g_free(caseless_name_ea);
	g_free(caseless_name_eb);

	return ret;
}

GList *editor_list_get(void)
{
	GList *editors_list = NULL;

	if (!editors_finished) return NULL;

	g_hash_table_foreach(editors, editor_list_add_cb, &editors_list);
	editors_list = g_list_sort(editors_list, editor_sort);

	return editors_list;
}

/* ------------------------------ */


static void editor_verbose_data_free(EditorData *ed)
{
	if (!ed->vd) return;
	g_free(ed->vd);
	ed->vd = NULL;
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

static void editor_verbose_window_stop(GenericDialog *UNUSED(gd), gpointer data)
{
	auto ed = static_cast<EditorData *>(data);
	ed->stopping = TRUE;
	ed->count = 0;
	editor_verbose_window_progress(ed, _("stopping..."));
}

static void editor_verbose_window_enable_close(EditorVerboseData *vd)
{
	vd->gd->cancel_cb = editor_verbose_window_close;

	spinner_set_interval(vd->spinner, -1);
	gtk_widget_set_sensitive(vd->button_stop, FALSE);
	gtk_widget_set_sensitive(vd->button_close, TRUE);
}

static EditorVerboseData *editor_verbose_window(EditorData *ed, const gchar *text)
{
	EditorVerboseData *vd;
	GtkWidget *scrolled;
	GtkWidget *hbox;
	gchar *buf;

	vd = g_new0(EditorVerboseData, 1);

	vd->gd = file_util_gen_dlg(_("Edit command results"), "editor_results",
				   NULL, FALSE,
				   NULL, ed);
	buf = g_strdup_printf(_("Output of %s"), text);
	generic_dialog_add_message(vd->gd, NULL, buf, NULL, FALSE);
	g_free(buf);
	//~ vd->button_stop = generic_dialog_add_button(vd->gd, GTK_STOCK_STOP, NULL,
						   //~ editor_verbose_window_stop, FALSE);
	vd->button_stop = generic_dialog_add_button(vd->gd, "process-stop", NULL,
						   editor_verbose_window_stop, FALSE);
	gtk_widget_set_sensitive(vd->button_stop, FALSE);
	vd->button_close = generic_dialog_add_button(vd->gd, GTK_STOCK_CLOSE, NULL,
						    editor_verbose_window_close, TRUE);
	gtk_widget_set_sensitive(vd->button_close, FALSE);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(vd->gd->vbox), scrolled, TRUE, TRUE, 5);
	gtk_widget_show(scrolled);

	vd->text = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(vd->text), FALSE);
	gtk_widget_set_size_request(vd->text, EDITOR_WINDOW_WIDTH, EDITOR_WINDOW_HEIGHT);
	gtk_container_add(GTK_CONTAINER(scrolled), vd->text);
	gtk_widget_show(vd->text);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(vd->gd->vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);

	vd->progress = gtk_progress_bar_new();
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(vd->progress), 0.0);
	gtk_box_pack_start(GTK_BOX(hbox), vd->progress, TRUE, TRUE, 0);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(vd->progress), "");
	gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(vd->progress), TRUE);
	gtk_widget_show(vd->progress);

	vd->spinner = spinner_new(NULL, SPINNER_SPEED);
	gtk_box_pack_start(GTK_BOX(hbox), vd->spinner, FALSE, FALSE, 0);
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
		while (g_io_channel_read_chars(source, buf, sizeof(buf), &count, NULL) == G_IO_STATUS_NORMAL)
			{
			if (!g_utf8_validate(buf, count, NULL))
				{
				gchar *utf8;

				utf8 = g_locale_to_utf8(buf, count, NULL, NULL, NULL);
				if (utf8)
					{
					editor_verbose_window_fill(ed->vd, utf8, -1);
					g_free(utf8);
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
		g_io_channel_shutdown(source, TRUE, NULL);
		return FALSE;
		}

	return TRUE;
}

typedef enum {
	PATH_FILE,
	PATH_FILE_URL,
	PATH_DEST
} PathType;


static gchar *editor_command_path_parse(const FileData *fd, gboolean consider_sidecars, PathType type, const EditorDescription *editor)
{
	GString *string;
	gchar *pathl;
	const gchar *p = NULL;

	DEBUG_2("editor_command_path_parse: %s %d %d %s", fd->path, consider_sidecars, type, editor->key);

	string = g_string_new("");

	if (type == PATH_FILE || type == PATH_FILE_URL)
		{
		GList *work = editor->ext_list;

		if (!work)
			p = fd->path;
		else
			{
			while (work)
				{
				GList *work2;
				auto ext = static_cast<gchar *>(work->data);
				work = work->next;

				if (strcmp(ext, "*") == 0 ||
				    g_ascii_strcasecmp(ext, fd->extension) == 0)
					{
					p = fd->path;
					break;
					}

				work2 = consider_sidecars ? fd->sidecar_files : NULL;
				while (work2)
					{
					auto sfd = static_cast<FileData *>(work2->data);
					work2 = work2->next;

					if (g_ascii_strcasecmp(ext, sfd->extension) == 0)
						{
						p = sfd->path;
						break;
						}
					}
				if (p) break;
				}
			if (!p) return NULL;
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
	string = g_string_append(string, p);

	if (type == PATH_FILE_URL) g_string_prepend(string, "file://");
	pathl = path_from_utf8(string->str);
	g_string_free(string, TRUE);

	if (pathl && !pathl[0]) /* empty string case */
		{
		g_free(pathl);
		pathl = NULL;
		}

	DEBUG_2("editor_command_path_parse: return %s", pathl);
	return pathl;
}

static GString *append_quoted(GString *str, const char *s, gboolean single_quotes, gboolean double_quotes)
{
	const char *p;

	if (!single_quotes)
		{
		if (!double_quotes)
			g_string_append_c(str, '\'');
		else
			g_string_append(str, "\"'");
		}

	for (p = s; *p != '\0'; p++)
		{
		if (*p == '\'')
			g_string_append(str, "'\\''");
		else
			g_string_append_c(str, *p);
		}

	if (!single_quotes)
		{
		if (!double_quotes)
			g_string_append_c(str, '\'');
		else
			g_string_append(str, "'\"");
		}

	return str;
}


EditorFlags editor_command_parse(const EditorDescription *editor, GList *list, gboolean consider_sidecars, gchar **output)
{
	auto  flags = static_cast<EditorFlags>(0);
	const gchar *p;
	GString *result = NULL;
	gboolean escape = FALSE;
	gboolean single_quotes = FALSE;
	gboolean double_quotes = FALSE;

	DEBUG_2("editor_command_parse: %s %d %d", editor->key, consider_sidecars, !!output);

	if (output)
		result = g_string_new("");

	if (editor->exec == NULL || editor->exec[0] == '\0')
		{
		flags = static_cast<EditorFlags>(flags | EDITOR_ERROR_EMPTY);
		goto err;
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
			if (output) result = g_string_append_c(result, *p);
			}
		else if (*p == '\\')
			{
			if (!single_quotes) escape = TRUE;
			if (output) result = g_string_append_c(result, *p);
			}
		else if (*p == '\'')
			{
			if (output) result = g_string_append_c(result, *p);
			if (!single_quotes && !double_quotes)
				single_quotes = TRUE;
			else if (single_quotes)
				single_quotes = FALSE;
			}
		else if (*p == '"')
			{
			if (output) result = g_string_append_c(result, *p);
			if (!single_quotes && !double_quotes)
				double_quotes = TRUE;
			else if (double_quotes)
				double_quotes = FALSE;
			}
		else if (*p == '%' && p[1])
			{
			gchar *pathl = NULL;

			p++;

			switch (*p)
				{
				case 'f': /* single file */
				case 'u': /* single url */
					flags = static_cast<EditorFlags>(flags | EDITOR_FOR_EACH);
					if (flags & EDITOR_SINGLE_COMMAND)
						{
						flags = static_cast<EditorFlags>(flags | EDITOR_ERROR_INCOMPATIBLE);
						goto err;
						}
					if (list)
						{
						/* use the first file from the list */
						if (!list->data)
							{
							flags = static_cast<EditorFlags>(flags | EDITOR_ERROR_NO_FILE);
							goto err;
							}
						pathl = editor_command_path_parse(static_cast<FileData *>(list->data),
										  consider_sidecars,
										  (*p == 'f') ? PATH_FILE : PATH_FILE_URL,
										  editor);
						if (!output)
							{
							/* just testing, check also the rest of the list (like with F and U)
							   any matching file is OK */
							GList *work = list->next;

							while (!pathl && work)
								{
								auto fd = static_cast<FileData *>(work->data);
								pathl = editor_command_path_parse(fd,
												  consider_sidecars,
												  (*p == 'f') ? PATH_FILE : PATH_FILE_URL,
												  editor);
								work = work->next;
								}
							}

						if (!pathl)
							{
							flags = static_cast<EditorFlags>(flags | EDITOR_ERROR_NO_FILE);
							goto err;
							}
						if (output)
							{
							result = append_quoted(result, pathl, single_quotes, double_quotes);
							}
						g_free(pathl);
						}
					break;

				case 'F':
				case 'U':
					flags = static_cast<EditorFlags>(flags | EDITOR_SINGLE_COMMAND);
					if (flags & (EDITOR_FOR_EACH | EDITOR_DEST))
						{
						flags = static_cast<EditorFlags>(flags | EDITOR_ERROR_INCOMPATIBLE);
						goto err;
						}

					if (list)
						{
						/* use whole list */
						GList *work = list;
						gboolean ok = FALSE;

						while (work)
							{
							auto fd = static_cast<FileData *>(work->data);
							pathl = editor_command_path_parse(fd, consider_sidecars, (*p == 'F') ? PATH_FILE : PATH_FILE_URL, editor);
							if (pathl)
								{
								ok = TRUE;

								if (output)
									{
									ok = TRUE;
									if (work != list) g_string_append_c(result, ' ');
									result = append_quoted(result, pathl, single_quotes, double_quotes);
									}
								g_free(pathl);
								}
							work = work->next;
							}
						if (!ok)
							{
							flags = static_cast<EditorFlags>(flags | EDITOR_ERROR_NO_FILE);
							goto err;
							}
						}
					break;
				case 'i':
					if (editor->icon && *editor->icon)
						{
						if (output)
							{
							result = g_string_append(result, "--icon ");
							result = append_quoted(result, editor->icon, single_quotes, double_quotes);
							}
						}
					break;
				case 'c':
					if (output)
						{
						result = append_quoted(result, editor->name, single_quotes, double_quotes);
						}
					break;
				case 'k':
					if (output)
						{
						result = append_quoted(result, editor->file, single_quotes, double_quotes);
						}
					break;
				case '%':
					/* %% = % escaping */
					if (output) result = g_string_append_c(result, *p);
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
					flags = static_cast<EditorFlags>(flags | EDITOR_ERROR_SYNTAX);
					goto err;
				}
			}
		else
			{
			if (output) result = g_string_append_c(result, *p);
			}
		p++;
		}

	if (!(flags & (EDITOR_FOR_EACH | EDITOR_SINGLE_COMMAND))) flags = static_cast<EditorFlags>(flags | EDITOR_NO_PARAM);

	if (output)
		{
		*output = g_string_free(result, FALSE);
		DEBUG_3("Editor cmd: %s", *output);
		}

	return flags;


err:
	if (output)
		{
		g_string_free(result, TRUE);
		*output = NULL;
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
	gchar *command;
	auto fd = static_cast<FileData *>((ed->flags & EDITOR_NO_PARAM) ? NULL : list->data);;
	GPid pid;
	gint standard_output;
	gint standard_error;
	gboolean ok;

	ed->pid = -1;
	ed->flags = editor->flags;
	ed->flags = static_cast<EditorFlags>(ed->flags | editor_command_parse(editor, list, TRUE, &command));

	ok = !EDITOR_ERRORS(ed->flags);

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
		gchar *working_directory;
		gchar *args[4];
		guint n = 0;

		working_directory = fd ? remove_level_from_path(fd->path) : g_strdup(ed->working_directory);
		args[n++] = options->shell.path;
		if (options->shell.options && *options->shell.options)
			args[n++] = options->shell.options;
		args[n++] = command;
		args[n] = NULL;

		if ((ed->flags & EDITOR_DEST) && fd && fd->change && fd->change->dest) /** @FIXME error handling */
			{
			g_setenv("GEEQIE_DESTINATION", fd->change->dest, TRUE);
			}
		else
			{
			g_unsetenv("GEEQIE_DESTINATION");
			}

		ok = g_spawn_async_with_pipes(working_directory, args, NULL,
				      G_SPAWN_DO_NOT_REAP_CHILD, /* GSpawnFlags */
				      NULL, NULL,
				      &pid,
				      NULL,
				      ed->vd ? &standard_output : NULL,
				      ed->vd ? &standard_error : NULL,
				      NULL);

		g_free(working_directory);

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
			gchar *buf;

			buf = g_strdup_printf(_("Failed to run command:\n%s\n"), editor->file);
			editor_verbose_window_fill(ed->vd, buf, strlen(buf));
			g_free(buf);

			}
		else
			{
			GIOChannel *channel_output;
			GIOChannel *channel_error;

			channel_output = g_io_channel_unix_new(standard_output);
			g_io_channel_set_flags(channel_output, G_IO_FLAG_NONBLOCK, NULL);
			g_io_channel_set_encoding(channel_output, NULL, NULL);

			g_io_add_watch_full(channel_output, G_PRIORITY_HIGH, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP),
					    editor_verbose_io_cb, ed, NULL);
			g_io_add_watch_full(channel_output, G_PRIORITY_HIGH, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP),
					    editor_verbose_io_cb, ed, NULL);
			g_io_channel_unref(channel_output);

			channel_error = g_io_channel_unix_new(standard_error);
			g_io_channel_set_flags(channel_error, G_IO_FLAG_NONBLOCK, NULL);
			g_io_channel_set_encoding(channel_error, NULL, NULL);

			g_io_add_watch_full(channel_error, G_PRIORITY_HIGH, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP),
					    editor_verbose_io_cb, ed, NULL);
			g_io_channel_unref(channel_error);
			}
		}

	g_free(command);

	return static_cast<EditorFlags>(EDITOR_ERRORS(ed->flags));
}

static EditorFlags editor_command_next_start(EditorData *ed)
{
	if (ed->vd) editor_verbose_window_fill(ed->vd, "\n", 1);

	if ((ed->list || (ed->flags & EDITOR_NO_PARAM)) && ed->count < ed->total)
		{
		FileData *fd;
		EditorFlags error;

		fd = static_cast<FileData *>((ed->flags & EDITOR_NO_PARAM) ? NULL : ed->list->data);

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
			gtk_widget_set_sensitive(ed->vd->button_stop, (ed->list != NULL) );
			if ((ed->flags & EDITOR_FOR_EACH) && fd)
				{
				editor_verbose_window_fill(ed->vd, fd->path, strlen(fd->path));
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
		GList *fd_element = ed->list;

		ed->list = g_list_remove_link(ed->list, fd_element);
		if (ed->callback)
			{
			cont = ed->callback(ed->list ? ed : NULL, ed->flags, fd_element, ed->data);
			if (ed->stopping && cont == EDITOR_CB_CONTINUE) cont = EDITOR_CB_SKIP;
			}
		filelist_free(fd_element);
		}
	else
		{
		/* handle whole list */
		if (ed->callback)
			cont = ed->callback(NULL, ed->flags, ed->list, ed->data);
		filelist_free(ed->list);
		ed->list = NULL;
		}

	switch (cont)
		{
		case EDITOR_CB_SUSPEND:
			return static_cast<EditorFlags>(EDITOR_ERRORS(ed->flags));
		case EDITOR_CB_SKIP:
			return editor_command_done(ed);
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
		if (ed->callback) ed->callback(NULL, ed->flags, ed->list, ed->data);
		filelist_free(ed->list);
		ed->list = NULL;
		}

	ed->count = 0;

	flags = static_cast<EditorFlags>(EDITOR_ERRORS(ed->flags));

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

	if (EDITOR_ERRORS(flags)) return static_cast<EditorFlags>(EDITOR_ERRORS(flags));

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
	return static_cast<EditorFlags>(EDITOR_ERRORS(flags));
}

gboolean is_valid_editor_command(const gchar *key)
{
	if (!key) return FALSE;
	return g_hash_table_lookup(editors, key) != NULL;
}

EditorFlags start_editor_from_filelist_full(const gchar *key, GList *list, const gchar *working_directory, EditorCallback cb, gpointer data)
{
	EditorFlags error;
	EditorDescription *editor;
	if (!key) return EDITOR_ERROR_EMPTY;

	editor = static_cast<EditorDescription *>(g_hash_table_lookup(editors, key));

	if (!editor) return EDITOR_ERROR_EMPTY;
	if (!list && !(editor->flags & EDITOR_NO_PARAM)) return EDITOR_ERROR_NO_FILE;

	error = editor_command_parse(editor, list, TRUE, NULL);

	if (EDITOR_ERRORS(error)) return error;

	error = static_cast<EditorFlags>(error | editor_command_start(editor, editor->name, list, working_directory, cb, data));

	if (EDITOR_ERRORS(error))
		{
		gchar *text = g_strdup_printf(_("%s\n\"%s\""), editor_get_error_str(error), editor->file);

		file_util_warning_dialog(_("Invalid editor command"), text, GTK_STOCK_DIALOG_ERROR, NULL);
		g_free(text);
		}

	return static_cast<EditorFlags>(EDITOR_ERRORS(error));
}

EditorFlags start_editor_from_filelist(const gchar *key, GList *list)
{
	return start_editor_from_filelist_full(key, list, NULL, NULL, NULL);
}

EditorFlags start_editor_from_file_full(const gchar *key, FileData *fd, EditorCallback cb, gpointer data)
{
	GList *list;
	EditorFlags error;

	if (!fd) return static_cast<EditorFlags>(FALSE);

	list = g_list_append(NULL, fd);
	error = start_editor_from_filelist_full(key, list, NULL, cb, data);
	g_list_free(list);
	return error;
}

EditorFlags start_editor_from_file(const gchar *key, FileData *fd)
{
	return start_editor_from_file_full(key, fd, NULL, NULL);
}

EditorFlags start_editor(const gchar *key, const gchar *working_directory)
{
	return start_editor_from_filelist_full(key, NULL, working_directory, NULL, NULL);
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

//const gchar *editor_get_name(const gchar *key)
//{
	//EditorDescription *editor = g_hash_table_lookup(editors, key);

	//if (!editor) return NULL;

	//return editor->name;
//}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
