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

#include "ui-fileops.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glib-object.h>
#include <gtk/gtk.h>

#include <config.h>

#include "compat.h"
#include "filefilter.h"
#include "intl.h"
#include "layout.h"
#include "main-defines.h"
#include "md5-util.h"
#include "ui-utildlg.h"
#include "utilops.h"

/*
 *-----------------------------------------------------------------------------
 * generic file information and manipulation routines (public)
 *-----------------------------------------------------------------------------
 */

namespace
{

using FileDescriptor = int;
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(FileDescriptor, close, -1) // NOLINT(readability-non-const-parameter)

void encoding_dialog(const gchar *path)
{
	static gboolean warned_user = FALSE;
	GenericDialog *gd;
	const gchar *lc;
	const gchar *bf;

	if (warned_user) return;
	warned_user = TRUE;

	lc = getenv("LANG");
	bf = getenv("G_BROKEN_FILENAMES");

	g_autoptr(GString) string = g_string_new(_("One or more filenames are not encoded with the preferred locale character set.\n"));
	g_string_append_printf(string, _("Operations on, and display of these files with %s may not succeed.\n"), PACKAGE);
	g_string_append(string, "\n");
	g_string_append(string, _("If your filenames are not encoded in utf-8, try setting the environment variable G_BROKEN_FILENAMES=1\n"));
	if (bf)
		g_string_append_printf(string, _("It appears G_BROKEN_FILENAMES is set to %s\n"), bf);
	else
		g_string_append(string, _("It appears G_BROKEN_FILENAMES is not set\n"));
	g_string_append(string, "\n");
	g_string_append_printf(string, _("The locale appears to be set to \"%s\"\n(set by the LANG environment variable)\n"), (lc) ? lc : "undefined");
	if (lc && (strstr(lc, "UTF-8") || strstr(lc, "utf-8")))
		{
		g_autofree gchar *name = g_convert(path, -1, "UTF-8", "ISO-8859-1", nullptr, nullptr, nullptr);
		string = g_string_append(string, _("\nPreferred encoding appears to be UTF-8, however the file:\n"));
		g_string_append_printf(string, "\"%s\"\n", (name) ? name : _("[name not displayable]"));

		if (g_utf8_validate(path, -1, nullptr))
			g_string_append_printf(string, _("\"%s\" is encoded in valid UTF-8."), (name) ? name : _("[name not displayable]"));
		else
			g_string_append_printf(string, _("\"%s\" is not encoded in valid UTF-8."), (name) ? name : _("[name not displayable]"));
		g_string_append(string, "\n");
		}

	gd = generic_dialog_new(_("Filename encoding locale mismatch"),
				"locale warning", nullptr, TRUE, nullptr, nullptr);
	generic_dialog_add_button(gd, GQ_ICON_CLOSE, _("Close"), nullptr, TRUE);

	generic_dialog_add_message(gd, GQ_ICON_DIALOG_WARNING,
				   _("Filename encoding locale mismatch"), string->str, TRUE);

	gtk_widget_show(gd->dialog);
}

} // namespace

#if GQ_DEBUG_PATH_UTF8
gchar *path_to_utf8_debug(const gchar *path, const gchar *file, gint line)
#else
gchar *path_to_utf8(const gchar *path)
#endif
{
	gchar *utf8;

	if (!path) return nullptr;

	g_autoptr(GError) error = nullptr;
	utf8 = g_filename_to_utf8(path, -1, nullptr, nullptr, &error);
	if (error)
		{
#if GQ_DEBUG_PATH_UTF8
		log_printf("%s:%d: Unable to convert filename to UTF-8:\n%s\n%s\n", file, line, path, error->message);
#else
		log_printf("Unable to convert filename to UTF-8:\n%s\n%s\n", path, error->message);
#endif
		encoding_dialog(path);
		}

	if (!utf8)
		{
		/* just let it through, but bad things may happen */
		utf8 = g_strdup(path);
		}

	return utf8;
}

#if GQ_DEBUG_PATH_UTF8
gchar *path_from_utf8_debug(const gchar *utf8, const gchar *file, gint line)
#else
gchar *path_from_utf8(const gchar *utf8)
#endif
{
	gchar *path;

	if (!utf8) return nullptr;

	g_autoptr(GError) error = nullptr;
	path = g_filename_from_utf8(utf8, -1, nullptr, nullptr, &error);
	if (error)
		{
#if GQ_DEBUG_PATH_UTF8
		log_printf("%s:%d: Unable to convert filename to locale from UTF-8:\n%s\n%s\n", file, line, utf8, error->message);
#else
		log_printf("Unable to convert filename to locale from UTF-8:\n%s\n%s\n", utf8, error->message);
#endif
		}

	if (!path)
		{
		/* if invalid UTF-8, text probably still in original form, so just copy it */
		path = g_strdup(utf8);
		}

	return path;
}

/* first we try the HOME environment var, if that doesn't work, we try g_get_homedir(). */
const gchar *homedir()
{
	static gchar *home = []()
	{
		gchar *home = path_to_utf8(getenv("HOME"));
		if (home) return home;

		return path_to_utf8(g_get_home_dir());
	}();

	DEBUG_1("Home directory: %s", home);

	return home;
}

const gchar *xdg_data_home_get()
{
	static const gchar *xdg_data_home = path_to_utf8(g_get_user_data_dir());

	return xdg_data_home;
}

const gchar *xdg_config_home_get()
{
	static const gchar *xdg_config_home = path_to_utf8(g_get_user_config_dir());

	return xdg_config_home;
}

const gchar *xdg_cache_home_get()
{
	static const gchar *xdg_cache_home = path_to_utf8(g_get_user_cache_dir());

	return xdg_cache_home;
}

const gchar *get_rc_dir()
{
#if USE_XDG
	static gchar *rc_dir = g_build_filename(xdg_config_home_get(), GQ_APPNAME_LC, NULL);
#else
	static gchar *rc_dir = g_build_filename(homedir(), GQ_RC_DIR, NULL);
#endif

	return rc_dir;
}

const gchar *get_collections_dir()
{
#if USE_XDG
	static gchar *collections_dir = g_build_filename(xdg_data_home_get(), GQ_APPNAME_LC, GQ_COLLECTIONS_DIR, NULL);
#else
	static gchar *collections_dir = g_build_filename(get_rc_dir(), GQ_COLLECTIONS_DIR, NULL);
#endif

	return collections_dir;
}

const gchar *get_trash_dir()
{
#if USE_XDG
	static gchar *trash_dir = g_build_filename(xdg_data_home_get(), GQ_APPNAME_LC, GQ_TRASH_DIR, NULL);
#else
	static gchar *trash_dir = g_build_filename(get_rc_dir(), GQ_TRASH_DIR, NULL);
#endif

	return trash_dir;
}

const gchar *get_window_layouts_dir()
{
#if USE_XDG
	static gchar *window_layouts_dir = g_build_filename(xdg_config_home_get(), GQ_APPNAME_LC, GQ_WINDOW_LAYOUTS_DIR, NULL);
#else
	static gchar *window_layouts_dir = g_build_filename(get_rc_dir(), GQ_WINDOW_LAYOUTS_DIR, NULL);
#endif

	return window_layouts_dir;
}

gboolean stat_utf8(const gchar *s, struct stat *st)
{
	if (!s) return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);

	return stat(sl, st) == 0;
}

gboolean lstat_utf8(const gchar *s, struct stat *st)
{
	if (!s) return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);

	return lstat(sl, st) == 0;
}

gboolean isname(const gchar *s)
{
	struct stat st;

	return stat_utf8(s, &st);
}

gboolean isfile(const gchar *s)
{
	struct stat st;

	return (stat_utf8(s, &st) && S_ISREG(st.st_mode));
}

gboolean isdir(const gchar *s)
{
	struct stat st;

	return (stat_utf8(s, &st) && S_ISDIR(st.st_mode));
}

gboolean islink(const gchar *s)
{
	struct stat st;

	return (lstat_utf8(s, &st) && S_ISLNK(st.st_mode));
}

gint64 filesize(const gchar *s)
{
	struct stat st;

	if (!stat_utf8(s, &st)) return 0;
	return st.st_size;
}

time_t filetime(const gchar *s)
{
	struct stat st;

	if (!stat_utf8(s, &st)) return 0;
	return st.st_mtime;
}

gboolean filetime_set(const gchar *s, time_t tval)
{
	if (tval <= 0) return FALSE;

	struct utimbuf ut;
	ut.actime = ut.modtime = tval;

	g_autofree gchar *sl = path_from_utf8(s);

	return utime(sl, &ut) == 0;
}

gboolean is_readable_file(const gchar *s)
{
	if (!s || !s[0] || !isfile(s)) return FALSE;
	return access_file(s, R_OK);
}

gboolean access_file(const gchar *s, gint mode)
{
	if (!s || !s[0]) return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);

	return access(sl, mode) == 0;
}

gboolean unlink_file(const gchar *s)
{
	if (!s) return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);

	return unlink(sl) == 0;
}

gboolean mkdir_utf8(const gchar *s, gint mode)
{
	if (!s) return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);

	return mkdir(sl, mode) == 0;
}

gboolean rmdir_utf8(const gchar *s)
{
	if (!s) return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);

	return rmdir(sl) == 0;
}

gboolean copy_file_attributes(const gchar *s, const gchar *t, gint perms, gint mtime)
{
	if (!s || !t) return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);

	struct stat st;
	if (stat(sl, &st) != 0) return FALSE;

	g_autofree gchar *tl = path_from_utf8(t);
	gboolean ret = TRUE;

	/* set the dest file attributes to that of source (ignoring errors) */

	if (perms)
		{
		/* Ignores chown errors, while still doing chown
		   (so root still can copy files preserving ownership) */
		[[maybe_unused]] int err = chown(tl, st.st_uid, st.st_gid);

		if (chmod(tl, st.st_mode) < 0)
			{
			struct stat st2;
			if (stat(tl, &st2) != 0 || st2.st_mode != st.st_mode)
				{
				ret = FALSE;
				}
			}
		}

	struct utimbuf tb;
	tb.actime = st.st_atime;
	tb.modtime = st.st_mtime;
	if (mtime && utime(tl, &tb) < 0) ret = FALSE;

	return ret;
}

/* paths are in filesystem encoding */
static gboolean hard_linked(const gchar *a, const gchar *b)
{
	struct stat sta;
	struct stat stb;

	if (stat(a, &sta) !=  0 || stat(b, &stb) != 0) return FALSE;

	return (sta.st_dev == stb.st_dev &&
		sta.st_ino == stb.st_ino);
}

gboolean copy_file(const gchar *s, const gchar *t)
{
	gchar buf[16384];
	size_t b;
	gint fd = -1;

	g_autofree gchar *sl = path_from_utf8(s);
	g_autofree gchar *tl = path_from_utf8(t);

	if (hard_linked(sl, tl))
		{
		return TRUE;
		}

	/* Do not dereference absolute symlinks, but copy them "as is".
	* For a relative symlink, we don't know how to properly change it when
	* copied/moved to another dir to keep pointing it to same target as
	* a relative symlink, so we turn it into absolute symlink using
	* realpath() instead. */
	auto copy_symlink = [](const gchar *sl, const gchar *tl) -> gboolean
		{
		struct stat st;
		if (!lstat_utf8(sl, &st) && S_ISLNK(st.st_mode))
			{
			return FALSE;
			}

		g_autofree auto *link_target = static_cast<gchar *>(g_malloc(st.st_size + 1));
		if (readlink(sl, link_target, st.st_size) < 0)
			{
			return FALSE;  // try a "normal" copy
			}
		link_target[st.st_size] = '\0';

		if (link_target[0] != G_DIR_SEPARATOR) // if it is a relative symlink
			{
			gchar *absolute;

			const gchar *lastslash = strrchr(sl, G_DIR_SEPARATOR);
			gint len = lastslash - sl + 1;

			absolute = static_cast<gchar *>(g_malloc(len + st.st_size + 1));
			strncpy(absolute, sl, len);
			strcpy(absolute + len, link_target);
			g_free(link_target);
			link_target = absolute;

			gchar *realPath = realpath(link_target, nullptr);
			if (!realPath) // could not get absolute path, got some error instead
				{
				return FALSE;  // so try a "normal" copy
				}

			g_free(link_target);
			link_target = realPath; // successfully resolved into an absolute path
			}

		if (stat_utf8(tl, &st)) unlink(tl); // first try to remove directory entry in destination directory if such entry exists

		return symlink(link_target, tl) == 0;
		};

	if (copy_symlink(sl, tl))
		{
		return TRUE;
		}

	// if symlink did not succeed, continue on to try a copy procedure
	g_autoptr(FILE) fi = fopen(sl, "rb");
	if (!fi)
		{
		return FALSE;
		}

	/* First we write to a temporary file, then we rename it on success,
	   and attributes from original file are copied */
	g_autofree gchar *randname = g_strconcat(tl, ".tmp_XXXXXX", NULL);
	if (!randname)
		{
		return FALSE;
		}

	fd = g_mkstemp(randname);
	if (fd == -1)
		{
		return FALSE;
		}

	g_autoptr(FILE) fo = fdopen(fd, "wb");
	if (!fo)
		{
		close(fd);
		return FALSE;
		}

	while ((b = fread(buf, sizeof(gchar), sizeof(buf), fi)) != 0)
		{
		if (fwrite(buf, sizeof(gchar), b, fo) != b)
			{
			unlink(randname);
			return FALSE;
			}
		}

        /* close explicitly the files before rename and copy_file_attributes,
        to avoid buffered data being flushed after copy_file_attributes,
        which would reset mtime to current time (cf issue #1535) */
	fclose(fi); fi = nullptr;
	fclose(fo); fo = nullptr;

	if (rename(randname, tl) < 0)
		{
		unlink(randname);
		return FALSE;
		}

	return copy_file_attributes(s, t, TRUE, TRUE);
}

gboolean move_file(const gchar *s, const gchar *t)
{
	if (!s || !t) return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);
	g_autofree gchar *tl = path_from_utf8(t);
	if (rename(sl, tl) < 0)
		{
		/* this may have failed because moving a file across filesystems
		was attempted, so try copy and delete instead */

		if (!copy_file(s, t)) return FALSE;

		if (unlink(sl) < 0)
			{
			/* err, now we can't delete the source file so return FALSE */
			return FALSE;
			}
		}

	return TRUE;
}

gboolean rename_file(const gchar *s, const gchar *t)
{
	if (!s || !t) return FALSE;

	g_autofree gchar *sl = path_from_utf8(s);
	g_autofree gchar *tl = path_from_utf8(t);

	return rename(sl, tl) == 0;
}

gchar *get_current_dir()
{
	g_autofree gchar *pathl = g_get_current_dir();

	return path_to_utf8(pathl);
}

GList *string_list_copy(const GList *list)
{
	GList *new_list = nullptr;
	auto work = const_cast<GList *>(list);

	while (work)
		{
		gchar *path;

		path = static_cast<gchar *>(work->data);
		work = work->next;

		new_list = g_list_prepend(new_list, g_strdup(path));
		}

	return g_list_reverse(new_list);
}

GString *string_list_join(const GList *list, const gchar *sep)
{
	GString *result = g_string_new(nullptr);

	for (const GList *work = list; work; work = work->next)
		{
		auto *str = static_cast<gchar *>(work->data);
		if (!str) continue;

		if (result->len > 0)
			{
			result = g_string_append(result, sep);
			}

		result = g_string_append(result, str);
		}

	return result;
}

gchar *unique_filename(const gchar *path, const gchar *ext, const gchar *divider, gboolean pad)
{
	gint n = 1;

	if (!ext) ext = "";
	if (!divider) divider = "";

	g_autofree gchar *unique = g_strconcat(path, ext, NULL);
	while (isname(unique))
		{
		g_free(unique);
		if (pad)
			{
			unique = g_strdup_printf("%s%s%03d%s", path, divider, n, ext);
			}
		else
			{
			unique = g_strdup_printf("%s%s%d%s", path, divider, n, ext);
			}
		n++;
		if (n > 999)
			{
			/* well, we tried */
			return nullptr;
			}
		}

	return g_steal_pointer(&unique);
}

const gchar *filename_from_path(const gchar *path)
{
	const gchar *base;

	if (!path) return nullptr;

	base = strrchr(path, G_DIR_SEPARATOR);
	if (base) return base + 1;

	return path;
}

gchar *remove_trailing_slash(const gchar *path)
{
	if (!path) return nullptr;

	size_t l = strlen(path);
	while (l > 1 && path[l - 1] == G_DIR_SEPARATOR) l--;

	return g_strndup(path, l);
}

gchar *remove_level_from_path(const gchar *path)
{
	if (!path) return g_strdup("");

	const gchar *base = strrchr(path, G_DIR_SEPARATOR);
	if (!base) return g_strdup("");

	/* Take account of a file being in the root ( / ) folder - ensure the returned value
	 * is at least one character long */
	return g_strndup(path, (path == base) ? 1 : (strlen(path) - strlen(base)));
}

gboolean file_extension_match(const gchar *path, const gchar *ext)
{
	gint p;
	gint e;

	if (!path) return FALSE;
	if (!ext) return TRUE;

	p = strlen(path);
	e = strlen(ext);

	/** @FIXME utf8 */
	return (p > e && g_ascii_strncasecmp(path + p - e, ext, e) == 0);
}

gchar *remove_extension_from_path(const gchar *path)
{
	const gchar *reg_ext;

	if (!path) return nullptr;

	reg_ext = registered_extension_from_path(path);

	return g_strndup(path, strlen(path) - (reg_ext == nullptr ? 0 : strlen(reg_ext)));
}

/**
 * @brief Warning note: this modifies path string!
 */
void parse_out_relatives(gchar *path)
{
	gint s;
	gint t;

	if (!path) return;

	s = t = 0;

	while (path[s] != '\0')
		{
		if (path[s] == G_DIR_SEPARATOR && path[s+1] == '.')
			{
			/* /. occurrence, let's see more */
			gint p = s + 2;

			if (path[p] == G_DIR_SEPARATOR || path[p] == '\0')
				{
				/* /./ or /., just skip this part */
				s = p;
				continue;
				}

			if (path[p] == '.' && (path[p+1] == G_DIR_SEPARATOR || path[p+1] == '\0'))
				{
				/* /../ or /.., remove previous part, ie. /a/b/../ becomes /a/ */
				s = p + 1;
				if (t > 0) t--;
				while (path[t] != G_DIR_SEPARATOR && t > 0) t--;
				continue;
				}
			}

		if (s != t) path[t] = path[s];
		t++;
		s++;
		}

	if (t == 0 && path[t] == G_DIR_SEPARATOR) t++;
	if (t > 1 && path[t-1] == G_DIR_SEPARATOR) t--;
	path[t] = '\0';
}

gboolean file_in_path(const gchar *name)
{
	if (!name) return FALSE;

	g_autofree gchar *path = g_strdup(getenv("PATH"));
	if (!path) return FALSE;

	g_auto(GStrv) paths = g_strsplit(path, ":", 0);
	g_autofree gchar *namel = path_from_utf8(name);

	for (gint i = 0; paths[i]; i++)
		{
		g_autofree gchar *f = g_build_filename(paths[i], namel, NULL);
		if (isfile(f)) return TRUE;
		}

	return FALSE;
}

gboolean recursive_mkdir_if_not_exists(const gchar *path, mode_t mode)
{
	if (!path) return FALSE;

	if (!isdir(path))
		{
		g_autofree gchar *npath = g_strdup(path);
		gchar *p = npath;

		while (p[0] != '\0')
			{
			p++;
			if (p[0] == G_DIR_SEPARATOR || p[0] == '\0')
				{
				gboolean end = TRUE;

				if (p[0] != '\0')
					{
					p[0] = '\0';
					end = FALSE;
					}

				if (!isdir(npath))
					{
					DEBUG_1("creating sub dir:%s", npath);
					if (!mkdir_utf8(npath, mode))
						{
						log_printf("create dir failed: %s\n", npath);
						return FALSE;
						}
					}

				if (!end) p[0] = G_DIR_SEPARATOR;
				}
			}
		}

	return TRUE;
}

/* does filename utf8 to filesystem encoding first */
gboolean md5_get_digest_from_file_utf8(const gchar *path, guchar digest[16])
{
	g_autofree gchar *pathl = path_from_utf8(path);

	return md5_get_digest_from_file(pathl, digest);
}

/**
 * @brief Generate md5 string from file,
 * on failure returns newly allocated copy of error_text, error_text may be NULL
 */
gchar *md5_text_from_file_utf8(const gchar *path, const gchar *error_text)
{
	g_autofree gchar *pathl = path_from_utf8(path);

	auto md5_text = md5_get_string_from_file(pathl);

	return md5_text ? md5_text : g_strdup(error_text);
}

/* Download web file
 */
struct WebData
{
	GenericDialog *gd;
	GCancellable *cancellable;
	LayoutWindow *lw;

	GtkWidget *progress;
	GFile *tmp_g_file;
	GFile *web_file;
};

static void web_file_async_ready_cb(GObject *source_object, GAsyncResult *res, gpointer data)
{
	auto web = static_cast<WebData *>(data);

	if (!g_cancellable_is_cancelled(web->cancellable))
		{
		generic_dialog_close(web->gd);
		}

	g_autoptr(GError) error = nullptr;
	if (g_file_copy_finish(G_FILE(source_object), res, &error))
		{
		g_autofree gchar *tmp_filename = g_file_get_parse_name(web->tmp_g_file); // @todo Is it required?
		layout_set_path(web->lw, g_file_get_path(web->tmp_g_file));
		}
	else
		{
		file_util_warning_dialog(_("Web file download failed"), error->message, GQ_ICON_DIALOG_ERROR, nullptr);
		}

	g_object_unref(web->tmp_g_file);
	web->tmp_g_file = nullptr;
	g_object_unref(web->cancellable);
	g_object_unref(web->web_file);
}

static void web_file_progress_cb(goffset current_num_bytes, goffset total_num_bytes, gpointer data)
{
	auto web = static_cast<WebData *>(data);

	if (!g_cancellable_is_cancelled(web->cancellable))
		{
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(web->progress), static_cast<gdouble>(current_num_bytes) / total_num_bytes);
		}
}

static void download_web_file_cancel_button_cb(GenericDialog *, gpointer data)
{
	auto web = static_cast<WebData *>(data);

	g_cancellable_cancel(web->cancellable);
}

/**
 * @brief Determine if file is a web URL
 * @param text File name to check
 * @param minimized
 * @param data Layout window or null
 * @returns Full path to the created temporary file or null
 *
 * If the file is a web file, start a background load to a temporary file.
 */
gchar *download_web_file(const gchar *text, gboolean minimized, gpointer data)
{
	g_autofree gchar *scheme = g_uri_parse_scheme(text);
	if (g_strcmp0("http", scheme) != 0 && g_strcmp0("https", scheme) != 0)
		{
		return FALSE;
		}

	FileFormatClass format_class = filter_file_get_class(text);
	if (format_class != FORMAT_CLASS_IMAGE &&
	    format_class != FORMAT_CLASS_RAWIMAGE &&
	    format_class != FORMAT_CLASS_VIDEO &&
	    format_class != FORMAT_CLASS_DOCUMENT)
		{
		return FALSE;
		}

	g_autoptr(GError) error = nullptr;
	g_autofree gchar *tmp_dir = g_dir_make_tmp("geeqie_XXXXXX", &error);
	if (error)
		{
		log_printf("Error: could not create temporary file n%s\n", error->message);
		return nullptr;
		}

	auto *web = g_new0(WebData, 1);
	web->lw = static_cast<LayoutWindow *>(data);

	web->web_file = g_file_new_for_uri(text);

	g_autofree gchar *base = g_file_get_basename(web->web_file);
	web->tmp_g_file = g_file_new_for_path(g_build_filename(tmp_dir, base, NULL));

	web->gd = generic_dialog_new(_("Download web file"), "download_web_file", nullptr, TRUE, download_web_file_cancel_button_cb, web);

	g_autofree gchar *message = g_strconcat(_("Downloading "), base, NULL);
	generic_dialog_add_message(web->gd, GQ_ICON_DIALOG_INFO, message, nullptr, FALSE);

	web->progress = gtk_progress_bar_new();
	gq_gtk_box_pack_start(GTK_BOX(web->gd->vbox), web->progress, FALSE, FALSE, 0);
	gtk_widget_show(web->progress);
	if (minimized)
		{
		gtk_window_iconify(GTK_WINDOW(web->gd->dialog));
		}

	gtk_widget_show(web->gd->dialog);
	web->cancellable = g_cancellable_new();
	g_file_copy_async(web->web_file, web->tmp_g_file, G_FILE_COPY_OVERWRITE, G_PRIORITY_LOW, web->cancellable, web_file_progress_cb, web, web_file_async_ready_cb, web);

	return g_file_get_path(web->tmp_g_file);
}

gboolean rmdir_recursive(GFile *file, GCancellable *cancellable, GError **error)
{
	g_autoptr(GFileEnumerator) enumerator = nullptr;

	enumerator = g_file_enumerate_children(file, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, nullptr);

	while (enumerator != nullptr)
		{
		 GFile *child;

		if (!g_file_enumerator_iterate(enumerator, nullptr, &child, cancellable, error))
			return FALSE;
		if (child == nullptr)
			break;
		if (!rmdir_recursive(child, cancellable, error))
			return FALSE;
		}

	return g_file_delete(file, cancellable, error);
}

guchar *map_file(const gchar *path, gsize &map_len)
{
	g_auto(FileDescriptor) fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd == -1) return nullptr;

	struct stat st;
	if (fstat(fd, &st) == -1) return nullptr;

	auto *map_data = static_cast<guchar *>(mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
	if (map_data == MAP_FAILED) return nullptr;

	map_len = st.st_size;
	return map_data;
}

/**
 * @brief Get list of file extensions supported by gdk_pixbuf_loader
 * @returns List of gchar
 *
 * Returned list must be freed with data by caller
 */
GList *pixbuf_gdk_known_extensions()
{
	GList *extensions_list = nullptr;

	g_autoptr(GSList) formats_list = gdk_pixbuf_get_formats();
	for (GSList *work = formats_list; work; work = work->next)
		{
		auto *fm = static_cast<GdkPixbufFormat *>(work->data);
		g_auto(GStrv) extensions = gdk_pixbuf_format_get_extensions(fm);
		const guint extensions_count = g_strv_length(extensions);

		for (guint i = 0; i < extensions_count; i++)
			{
			extensions_list = g_list_prepend(extensions_list, g_strdup(extensions[i]));
			}
		}

	return g_list_sort(extensions_list, reinterpret_cast<GCompareFunc>(g_strcmp0));
}

/**
 * @brief Save file in a safe manner
 * @param file_name
 * @param contents
 * @param length Length of contents or -1 if null-terminated string
 * @returns
 *
 * The operation is atomic in the sense that it is first written to a
 * temporary file which is then renamed to the final name.
 * After a failure, either the old version of the file or the
 * new version of the file will be available, but not a mixture.
 *
 * If the file already exists, mode and group are preserved.
 */
gboolean secure_save(const gchar *file_name, const gchar *contents, gsize length)
{
	constexpr mode_t mask = S_IXUSR | S_IRWXG | S_IRWXO;
	const mode_t saved_mask = umask(mask);

	guint permissions = 0;
	guint32 gid = 0;
	guint32 uid = 0;
	struct stat st;
	if (stat(file_name, &st) == 0)
		{
		permissions = st.st_mode & 0777;
		gid = st.st_gid;
		uid = st.st_uid;
		}
	else
		{
		permissions = 0600;
		gid = getgid();
		uid = getuid();
		}

	constexpr auto flags = static_cast<GFileSetContentsFlags>(G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE);
	g_autoptr(GError) error = nullptr;
	gboolean success = g_file_set_contents_full(file_name, contents, length, flags, 0600, &error);
	if (!success)
		{
		g_autoptr(GNotification) notification = g_notification_new("Geeqie");
		g_notification_set_priority(notification, G_NOTIFICATION_PRIORITY_URGENT);
		g_notification_set_default_action(notification, "app.null");
		g_notification_set_title(notification, _("File was not saved"));

		if (error)
			{
			g_notification_set_body(notification, error->message);
			log_printf("Error: Failed to save file: %s\n%s", file_name, error->message);
			}
		else
			{
			g_notification_set_body(notification, file_name);
			log_printf("Error: Failed to save file: %s", file_name);
			}

		GApplication *app = g_application_get_default();
		g_application_send_notification(G_APPLICATION(app), "save-file-notification", notification);
		}
	else
		{
		if (chown(file_name, uid, gid) == -1)
			{
			log_printf("secure_save: chown failed");
			}

		if (chmod(file_name, permissions) == -1)
			{
			log_printf("secure_save: chmod failed");
			}
		}

	umask(saved_mask);

	return success;
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
