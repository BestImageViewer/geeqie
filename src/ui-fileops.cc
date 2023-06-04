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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>

#include "main.h"
#include "ui-fileops.h"

#include "md5-util.h"
#include "filefilter.h"
#include "layout.h"
#include "utilops.h"
#include "secure-save.h"

/*
 *-----------------------------------------------------------------------------
 * generic file information and manipulation routines (public)
 *-----------------------------------------------------------------------------
 */



void print_term(gboolean err, const gchar *text_utf8)
{
	gchar *text_l;

	text_l = g_locale_from_utf8(text_utf8, -1, NULL, NULL, NULL);
	if (err)
		{
		fputs((text_l) ? text_l : text_utf8, stderr);
		}
	else
		{
		fputs((text_l) ? text_l : text_utf8, stdout);
		}
	if(command_line && command_line->ssi)
		secure_fputs(command_line->ssi, (text_l) ? text_l : text_utf8);
	g_free(text_l);
}

static void encoding_dialog(const gchar *path)
{
	static gboolean warned_user = FALSE;
	GenericDialog *gd;
	GString *string;
	const gchar *lc;
	const gchar *bf;

	if (warned_user) return;
	warned_user = TRUE;

	lc = getenv("LANG");
	bf = getenv("G_BROKEN_FILENAMES");

	string = g_string_new("");
	g_string_append(string, _("One or more filenames are not encoded with the preferred locale character set.\n"));
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
		gchar *name;
		name = g_convert(path, -1, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);
		string = g_string_append(string, _("\nPreferred encoding appears to be UTF-8, however the file:\n"));
		g_string_append_printf(string, "\"%s\"\n", (name) ? name : _("[name not displayable]"));

		if (g_utf8_validate(path, -1, NULL))
			g_string_append_printf(string, _("\"%s\" is encoded in valid UTF-8."), (name) ? name : _("[name not displayable]"));
		else
			g_string_append_printf(string, _("\"%s\" is not encoded in valid UTF-8."), (name) ? name : _("[name not displayable]"));
		g_string_append(string, "\n");
		g_free(name);
		}

	gd = generic_dialog_new(_("Filename encoding locale mismatch"),
				"locale warning", NULL, TRUE, NULL, NULL);
	generic_dialog_add_button(gd, GTK_STOCK_CLOSE, NULL, NULL, TRUE);

	generic_dialog_add_message(gd, GTK_STOCK_DIALOG_WARNING,
				   _("Filename encoding locale mismatch"), string->str, TRUE);

	gtk_widget_show(gd->dialog);

	g_string_free(string, TRUE);
}

#if GQ_DEBUG_PATH_UTF8
gchar *path_to_utf8_debug(const gchar *path, const gchar *file, gint line)
#else
gchar *path_to_utf8(const gchar *path)
#endif
{
	gchar *utf8;
	GError *error = NULL;

	if (!path) return NULL;

	utf8 = g_filename_to_utf8(path, -1, NULL, NULL, &error);
	if (error)
		{
#if GQ_DEBUG_PATH_UTF8
		log_printf("%s:%d: Unable to convert filename to UTF-8:\n%s\n%s\n", file, line, path, error->message);
#else
		log_printf("Unable to convert filename to UTF-8:\n%s\n%s\n", path, error->message);
#endif
		g_error_free(error);
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
	GError *error = NULL;

	if (!utf8) return NULL;

	path = g_filename_from_utf8(utf8, -1, NULL, NULL, &error);
	if (error)
		{
#if GQ_DEBUG_PATH_UTF8
		log_printf("%s:%d: Unable to convert filename to locale from UTF-8:\n%s\n%s\n", file, line, utf8, error->message);
#else
		log_printf("Unable to convert filename to locale from UTF-8:\n%s\n%s\n", utf8, error->message);
#endif
		g_error_free(error);
		}
	if (!path)
		{
		/* if invalid UTF-8, text probably still in original form, so just copy it */
		path = g_strdup(utf8);
		}

	return path;
}

/* first we try the HOME environment var, if that doesn't work, we try g_get_homedir(). */
const gchar *homedir(void)
{
	static gchar *home = NULL;

	if (!home)
		home = path_to_utf8(getenv("HOME"));

	if (!home)
		home = path_to_utf8(g_get_home_dir());

	DEBUG_1("Home directory: %s", home);

	return home;
}

static gchar *xdg_dir_get(const gchar *key, const gchar *fallback)
{
	gchar *dir = getenv(key);

	if (!dir || dir[0] == '\0')
		{
    		return g_build_filename(homedir(), fallback, NULL);
    		}

	DEBUG_1("Got xdg %s: %s", key, dir);

	return path_to_utf8(dir);
}

const gchar *xdg_data_home_get(void)
{
	static const gchar *xdg_data_home = NULL;

	if (xdg_data_home) return xdg_data_home;

	xdg_data_home = xdg_dir_get("XDG_DATA_HOME", ".local/share");

	return xdg_data_home;
}

const gchar *xdg_config_home_get(void)
{
	static const gchar *xdg_config_home = NULL;

	if (xdg_config_home) return xdg_config_home;

	xdg_config_home = xdg_dir_get("XDG_CONFIG_HOME", ".config");

	return xdg_config_home;
}

const gchar *xdg_cache_home_get(void)
{
	static const gchar *xdg_cache_home = NULL;

	if (xdg_cache_home) return xdg_cache_home;

	xdg_cache_home = xdg_dir_get("XDG_CACHE_HOME", ".cache");

	return xdg_cache_home;
}

const gchar *get_rc_dir(void)
{
	static gchar *rc_dir = NULL;

	if (rc_dir) return rc_dir;

	if (USE_XDG)
		{
		rc_dir = g_build_filename(xdg_config_home_get(), GQ_APPNAME_LC, NULL);
		}
	else
		{
		rc_dir = g_build_filename(homedir(), GQ_RC_DIR, NULL);
		}

	return rc_dir;
}

const gchar *get_collections_dir(void)
{
	static gchar *collections_dir = NULL;

	if (collections_dir) return collections_dir;

	if (USE_XDG)
		{
		collections_dir = g_build_filename(xdg_data_home_get(), GQ_APPNAME_LC, GQ_COLLECTIONS_DIR, NULL);
		}
	else
		{
		collections_dir = g_build_filename(get_rc_dir(), GQ_COLLECTIONS_DIR, NULL);
		}

	return collections_dir;
}

const gchar *get_trash_dir(void)
{
	static gchar *trash_dir = NULL;

	if (trash_dir) return trash_dir;

	if (USE_XDG)
		{
		trash_dir = g_build_filename(xdg_data_home_get(), GQ_APPNAME_LC, GQ_TRASH_DIR, NULL);
		}
	else
		{
		trash_dir = g_build_filename(get_rc_dir(), GQ_TRASH_DIR, NULL);
	}

	return trash_dir;
}

const gchar *get_window_layouts_dir(void)
{
	static gchar *window_layouts_dir = NULL;

	if (window_layouts_dir) return window_layouts_dir;

	if (USE_XDG)
		{
		window_layouts_dir = g_build_filename(xdg_config_home_get(), GQ_APPNAME_LC, GQ_WINDOW_LAYOUTS_DIR, NULL);
		}
	else
		{
		window_layouts_dir = g_build_filename(get_rc_dir(), GQ_WINDOW_LAYOUTS_DIR, NULL);
		}

	return window_layouts_dir;
}

gboolean stat_utf8(const gchar *s, struct stat *st)
{
	gchar *sl;
	gboolean ret;

	if (!s) return FALSE;
	sl = path_from_utf8(s);
	ret = (stat(sl, st) == 0);
	g_free(sl);

	return ret;
}

gboolean lstat_utf8(const gchar *s, struct stat *st)
{
	gchar *sl;
	gboolean ret;

	if (!s) return FALSE;
	sl = path_from_utf8(s);
	ret = (lstat(sl, st) == 0);
	g_free(sl);

	return ret;
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
	gboolean ret = FALSE;

	if (tval > 0)
		{
		struct utimbuf ut;
		gchar *sl;

		ut.actime = ut.modtime = tval;

		sl = path_from_utf8(s);
		ret = (utime(sl, &ut) == 0);
		g_free(sl);
		}

	return ret;
}

gboolean is_readable_file(const gchar *s)
{
	if (!s || !s[0] || !isfile(s)) return FALSE;
	return access_file(s, R_OK);
}

gboolean access_file(const gchar *s, gint mode)
{
	gchar *sl;
	gint ret;

	if (!s || !s[0]) return FALSE;

	sl = path_from_utf8(s);
	ret = (access(sl, mode) == 0);
	g_free(sl);

	return ret;
}

gboolean unlink_file(const gchar *s)
{
	gchar *sl;
	gboolean ret;

	if (!s) return FALSE;

	sl = path_from_utf8(s);
	ret = (unlink(sl) == 0);
	g_free(sl);

	return ret;
}

//gboolean symlink_utf8(const gchar *source, const gchar *target)
//{
	//gchar *sl;
	//gchar *tl;
	//gboolean ret;

	//if (!source || !target) return FALSE;

	//sl = path_from_utf8(source);
	//tl = path_from_utf8(target);

	//ret = (symlink(sl, tl) == 0);

	//g_free(sl);
	//g_free(tl);

	//return ret;
//}

gboolean mkdir_utf8(const gchar *s, gint mode)
{
	gchar *sl;
	gboolean ret;

	if (!s) return FALSE;

	sl = path_from_utf8(s);
	ret = (mkdir(sl, mode) == 0);
	g_free(sl);
	return ret;
}

gboolean rmdir_utf8(const gchar *s)
{
	gchar *sl;
	gboolean ret;

	if (!s) return FALSE;

	sl = path_from_utf8(s);
	ret = (rmdir(sl) == 0);
	g_free(sl);

	return ret;
}

gboolean copy_file_attributes(const gchar *s, const gchar *t, gint perms, gint mtime)
{
	struct stat st;
	gchar *sl, *tl;
	gboolean ret = FALSE;

	if (!s || !t) return FALSE;

	sl = path_from_utf8(s);
	tl = path_from_utf8(t);

	if (stat(sl, &st) == 0)
		{
		struct utimbuf tb;

		ret = TRUE;

		/* set the dest file attributes to that of source (ignoring errors) */

		if (perms)
			{
			ret = chown(tl, st.st_uid, st.st_gid);
			/* Ignores chown errors, while still doing chown
			   (so root still can copy files preserving ownership) */
			ret = TRUE;
			if (chmod(tl, st.st_mode) < 0) {
                            struct stat st2;
                            if (stat(tl, &st2) != 0 || st2.st_mode != st.st_mode) {
                                ret = FALSE;
                            }
                        }
			}

		tb.actime = st.st_atime;
		tb.modtime = st.st_mtime;
		if (mtime && utime(tl, &tb) < 0) ret = FALSE;
		}

	g_free(sl);
	g_free(tl);

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
	FILE *fi = NULL;
	FILE *fo = NULL;
	gchar *sl = NULL;
	gchar *tl = NULL;
	gchar *randname = NULL;
	gchar buf[16384];
	size_t b;
	gint ret = FALSE;
	gint fd = -1;

	sl = path_from_utf8(s);
	tl = path_from_utf8(t);

	if (hard_linked(sl, tl))
		{
		ret = TRUE;
		goto end;
		}

	/* Do not dereference absolute symlinks, but copy them "as is".
	* For a relative symlink, we don't know how to properly change it when
	* copied/moved to another dir to keep pointing it to same target as
	* a relative symlink, so we turn it into absolute symlink using
	* realpath() instead. */
	struct stat st;
	if (lstat_utf8(sl, &st) && S_ISLNK(st.st_mode))
		{
		gchar *link_target;
		ssize_t i;

		link_target = static_cast<gchar *>(g_malloc(st.st_size + 1));
		i = readlink(sl, link_target, st.st_size);
		if (i<0)
			{
			g_free(link_target);
			goto orig_copy;  // try a "normal" copy
			}
		link_target[st.st_size] = '\0';

		if (link_target[0] != G_DIR_SEPARATOR) // if it is a relative symlink
			{
			gchar *absolute;

			gchar *lastslash = strrchr(sl, G_DIR_SEPARATOR);
			gint len = lastslash - sl + 1;

			absolute = static_cast<gchar *>(g_malloc(len + st.st_size + 1));
			strncpy(absolute, sl, len);
			strcpy(absolute + len, link_target);
			g_free(link_target);
			link_target = absolute;

			gchar *realPath;
			realPath = realpath(link_target, NULL);

			if (realPath != NULL) // successfully resolved into an absolute path
				{
				g_free(link_target);
				link_target = g_strdup(realPath);
				g_free(realPath);
				}
			else                 // could not get absolute path, got some error instead
				{
				g_free(link_target);
				goto orig_copy;  // so try a "normal" copy
				}
			}

		if (stat_utf8(tl, &st)) unlink(tl); // first try to remove directory entry in destination directory if such entry exists

		gint success = (symlink(link_target, tl) == 0);
		g_free(link_target);

		if (success)
			{
			ret = TRUE;
			goto end;
			}
		} // if symlink did not succeed, continue on to try a copy procedure
	orig_copy:

	fi = fopen(sl, "rb");
	if (!fi) goto end;

	/* First we write to a temporary file, then we rename it on success,
	   and attributes from original file are copied */
	randname = g_strconcat(tl, ".tmp_XXXXXX", NULL);
	if (!randname) goto end;

	fd = g_mkstemp(randname);
	if (fd == -1) goto end;

	fo = fdopen(fd, "wb");
	if (!fo) {
		close(fd);
		goto end;
	}

	while ((b = fread(buf, sizeof(gchar), sizeof(buf), fi)) && b != 0)
		{
		if (fwrite(buf, sizeof(gchar), b, fo) != b)
			{
			unlink(randname);
			goto end;
			}
		}

	fclose(fi); fi = NULL;
	fclose(fo); fo = NULL;

	if (rename(randname, tl) < 0) {
		unlink(randname);
		goto end;
	}

	ret = copy_file_attributes(s, t, TRUE, TRUE);

end:
	if (fi) fclose(fi);
	if (fo) fclose(fo);
	if (sl) g_free(sl);
	if (tl) g_free(tl);
	if (randname) g_free(randname);
	return ret;
}

gboolean move_file(const gchar *s, const gchar *t)
{
	gchar *sl, *tl;
	gboolean ret = TRUE;

	if (!s || !t) return FALSE;

	sl = path_from_utf8(s);
	tl = path_from_utf8(t);
	if (rename(sl, tl) < 0)
		{
		/* this may have failed because moving a file across filesystems
		was attempted, so try copy and delete instead */
		if (copy_file(s, t))
			{
			if (unlink(sl) < 0)
				{
				/* err, now we can't delete the source file so return FALSE */
				ret = FALSE;
				}
			}
		else
			{
			ret = FALSE;
			}
		}
	g_free(sl);
	g_free(tl);

	return ret;
}

gboolean rename_file(const gchar *s, const gchar *t)
{
	gchar *sl, *tl;
	gboolean ret;

	if (!s || !t) return FALSE;

	sl = path_from_utf8(s);
	tl = path_from_utf8(t);
	ret = (rename(sl, tl) == 0);
	g_free(sl);
	g_free(tl);

	return ret;
}

gchar *get_current_dir(void)
{
	gchar *pathl;
	gchar *path8;

	pathl = g_get_current_dir();
	path8 = path_to_utf8(pathl);
	g_free(pathl);

	return path8;
}

void list_free_wrapper(void *data, void *UNUSED(userdata))
{
	g_free(data);
}

void string_list_free(GList *list)
{
	g_list_foreach(list, (GFunc)list_free_wrapper, NULL);
	g_list_free(list);
}

GList *string_list_copy(const GList *list)
{
	GList *new_list = NULL;
	auto work = (GList *) list;

	while (work)
		{
		gchar *path;

		path = static_cast<gchar *>(work->data);
		work = work->next;

		new_list = g_list_prepend(new_list, g_strdup(path));
		}

	return g_list_reverse(new_list);
}

gchar *unique_filename(const gchar *path, const gchar *ext, const gchar *divider, gboolean pad)
{
	gchar *unique;
	gint n = 1;

	if (!ext) ext = "";
	if (!divider) divider = "";

	unique = g_strconcat(path, ext, NULL);
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
			g_free(unique);
			return NULL;
			}
		}

	return unique;
}

//gchar *unique_filename_simple(const gchar *path)
//{
	//gchar *unique;
	//const gchar *name;
	//const gchar *ext;

	//if (!path) return NULL;

	//name = filename_from_path(path);
	//if (!name) return NULL;

	//ext = registered_extension_from_path(name);

	//if (!ext)
		//{
		//unique = unique_filename(path, NULL, "_", TRUE);
		//}
	//else
		//{
		//gchar *base;

		//base = remove_extension_from_path(path);
		//unique = unique_filename(base, ext, "_", TRUE);
		//g_free(base);
		//}

	//return unique;
//}

const gchar *filename_from_path(const gchar *path)
{
	const gchar *base;

	if (!path) return NULL;

	base = strrchr(path, G_DIR_SEPARATOR);
	if (base) return base + 1;

	return path;
}

gchar *remove_level_from_path(const gchar *path)
{
	const gchar *base;

	if (!path) return g_strdup("");

	base = strrchr(path, G_DIR_SEPARATOR);
	/* Take account of a file being in the root ( / ) folder - ensure the returned value
	 * is at least one character long */
	if (base) return g_strndup(path, (strlen(path)-strlen(base)) == 0 ? 1 : (strlen(path)-strlen(base)));

	return g_strdup("");
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

	if (!path) return NULL;

	reg_ext = registered_extension_from_path(path);

	return g_strndup(path, strlen(path) - (reg_ext == NULL ? 0 : strlen(reg_ext)));
}

void parse_out_relatives(gchar *path)
{
	gint s, t;

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
			else if (path[p] == '.' && (path[p+1] == G_DIR_SEPARATOR || path[p+1] == '\0'))
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
	gchar *path;
	gchar *namel;
	gint p, l;
	gboolean ret = FALSE;

	if (!name) return FALSE;
	path = g_strdup(getenv("PATH"));
	if (!path) return FALSE;
	namel = path_from_utf8(name);

	p = 0;
	l = strlen(path);
	while (p < l && !ret)
		{
		gchar *f;
		gint e = p;
		while (path[e] != ':' && path[e] != '\0') e++;
		path[e] = '\0';
		e++;
		f = g_build_filename(path + p, namel, NULL);
		if (isfile(f)) ret = TRUE;
		g_free(f);
		p = e;
		}
	g_free(namel);
	g_free(path);

	return ret;
}

gboolean recursive_mkdir_if_not_exists(const gchar *path, mode_t mode)
{
	if (!path) return FALSE;

	if (!isdir(path))
		{
		gchar *npath = g_strdup(path);
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
						g_free(npath);
						return FALSE;
						}
					}

				if (!end) p[0] = G_DIR_SEPARATOR;
				}
			}
		g_free(npath);
		}

	return TRUE;
}

/* does filename utf8 to filesystem encoding first */
gboolean md5_get_digest_from_file_utf8(const gchar *path, guchar digest[16])
{
	gboolean success;
	gchar *pathl;

	pathl = path_from_utf8(path);
	success = md5_get_digest_from_file(pathl, digest);
	g_free(pathl);

	return success;
}


gchar *md5_text_from_file_utf8(const gchar *path, const gchar *error_text)
{
	guchar digest[16];

	if (!md5_get_digest_from_file_utf8(path, digest)) return g_strdup(error_text);

	return md5_digest_to_text(digest);
}

/* Download web file
 */
typedef struct _WebData WebData;
struct _WebData
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
	GError *error = NULL;
	auto web = static_cast<WebData *>(data);
	gchar *tmp_filename;

	if (!g_cancellable_is_cancelled(web->cancellable))
		{
		generic_dialog_close(web->gd);
		}

	if (g_file_copy_finish(G_FILE(source_object), res, &error))
		{
		tmp_filename = g_file_get_parse_name(web->tmp_g_file);
		g_free(tmp_filename);
		layout_set_path(web->lw, g_file_get_path(web->tmp_g_file));
		}
	else
		{
		file_util_warning_dialog(_("Web file download failed"), error->message, GTK_STOCK_DIALOG_ERROR, NULL);
		}

	g_object_unref(web->tmp_g_file);
	web->tmp_g_file = NULL;
	g_object_unref(web->cancellable);
	g_object_unref(web->web_file);
}

static void web_file_progress_cb(goffset current_num_bytes, goffset total_num_bytes, gpointer data)
{
	auto web = static_cast<WebData *>(data);

	if (!g_cancellable_is_cancelled(web->cancellable))
		{
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(web->progress), (gdouble)current_num_bytes / total_num_bytes);
		}
}

static void download_web_file_cancel_button_cb(GenericDialog *UNUSED(gd), gpointer data)
{
	auto web = static_cast<WebData *>(data);

	g_cancellable_cancel(web->cancellable);
}

gboolean download_web_file(const gchar *text, gboolean minimized, gpointer data)
{
	gchar *scheme;
	auto lw = static_cast<LayoutWindow *>(data);
	gchar *tmp_dir;
	GError *error = NULL;
	WebData *web;
	gchar *base;
	gboolean ret = FALSE;
	gchar *message;
	FileFormatClass format_class;

	scheme = g_uri_parse_scheme(text);
	if (g_strcmp0("http", scheme) == 0 || g_strcmp0("https", scheme) == 0)
		{
		format_class = filter_file_get_class(text);

		if (format_class == FORMAT_CLASS_IMAGE || format_class == FORMAT_CLASS_RAWIMAGE || format_class == FORMAT_CLASS_VIDEO || format_class == FORMAT_CLASS_DOCUMENT)
			{
			tmp_dir = g_dir_make_tmp("geeqie_XXXXXX", &error);
			if (error)
				{
				log_printf("Error: could not create temporary file n%s\n", error->message);
				g_error_free(error);
				error = NULL;
				ret = TRUE;
				}
			else
				{
				web = g_new0(WebData, 1);
				web->lw = lw;

				web->web_file = g_file_new_for_uri(text);

				base = g_strdup(g_file_get_basename(web->web_file));
				web->tmp_g_file = g_file_new_for_path(g_build_filename(tmp_dir, base, NULL));

				web->gd = generic_dialog_new(_("Download web file"), "download_web_file", NULL, TRUE, download_web_file_cancel_button_cb, web);

				message = g_strconcat(_("Downloading "), base, NULL);
				generic_dialog_add_message(web->gd, GTK_STOCK_DIALOG_INFO, message, NULL, FALSE);

				web->progress = gtk_progress_bar_new();
				gtk_box_pack_start(GTK_BOX(web->gd->vbox), web->progress, FALSE, FALSE, 0);
				gtk_widget_show(web->progress);
				if (minimized)
					{
					gtk_window_iconify(GTK_WINDOW(web->gd->dialog));
					}

				gtk_widget_show(web->gd->dialog);
				web->cancellable = g_cancellable_new();
				g_file_copy_async(web->web_file, web->tmp_g_file, G_FILE_COPY_OVERWRITE, G_PRIORITY_LOW, web->cancellable, web_file_progress_cb, web, web_file_async_ready_cb, web);

				g_free(base);
				g_free(message);
				ret = TRUE;
				}
			}
		}
	else
		{
		ret = FALSE;
		}

	g_free(scheme);
	return ret;

}

gboolean rmdir_recursive(GFile *file, GCancellable *cancellable, GError **error)
{
	g_autoptr(GFileEnumerator) enumerator = NULL;

	enumerator = g_file_enumerate_children(file, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancellable, NULL);

	while (enumerator != NULL)
		{
		 GFile *child;

		if (!g_file_enumerator_iterate(enumerator, NULL, &child, cancellable, error))
			return FALSE;
		if (child == NULL)
			break;
		if (!rmdir_recursive(child, cancellable, error))
			return FALSE;
		}

	return g_file_delete(file, cancellable, error);
}

/**
 * @brief Retrieves the internal scale factor that maps from window coordinates to the actual device pixels
 * @param  -
 * @returns scale factor
 * 
 * 
 */
gint scale_factor()
{
	LayoutWindow *lw = NULL;

	layout_valid(&lw);
	return gtk_widget_get_scale_factor(lw->window);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
