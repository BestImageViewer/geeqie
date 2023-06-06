/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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
#include "misc.h"

#include "ui-fileops.h"

#include <langinfo.h>

gdouble get_zoom_increment(void)
{
	return ((options->image.zoom_increment != 0) ? static_cast<gdouble>(options->image.zoom_increment) / 100.0 : 1.0);
}

gchar *utf8_validate_or_convert(const gchar *text)
{
	gint len;

	if (!text) return nullptr;

	len = strlen(text);
	if (!g_utf8_validate(text, len, nullptr))
		return g_convert(text, len, "UTF-8", "ISO-8859-1", nullptr, nullptr, nullptr);

	return g_strdup(text);
}

gint utf8_compare(const gchar *s1, const gchar *s2, gboolean case_sensitive)
{
	gchar *s1_key, *s2_key;
	gchar *s1_t, *s2_t;
	gint ret;

	g_assert(g_utf8_validate(s1, -1, nullptr));
	g_assert(g_utf8_validate(s2, -1, nullptr));

	if (!case_sensitive)
		{
		s1_t = g_utf8_casefold(s1, -1);
		s2_t = g_utf8_casefold(s2, -1);
		}
	else
		{
		s1_t = const_cast<gchar *>(s1);
		s2_t = const_cast<gchar *>(s2);
		}

	s1_key = g_utf8_collate_key(s1_t, -1);
	s2_key = g_utf8_collate_key(s2_t, -1);

	ret = strcmp(s1_key, s2_key);

	g_free(s1_key);
	g_free(s2_key);

	if (!case_sensitive)
		{
		g_free(s1_t);
		g_free(s2_t);
		}

	return ret;
}

/* Borrowed from gtkfilesystemunix.c */
gchar *expand_tilde(const gchar *filename)
{
#ifndef G_OS_UNIX
	return g_strdup(filename);
#else
	const gchar *notilde;
	const gchar *slash;
	const gchar *home;

	if (filename[0] != '~')
		return g_strdup(filename);

	notilde = filename + 1;
	slash = strchr(notilde, G_DIR_SEPARATOR);
	if (slash == notilde || !*notilde)
		{
		home = g_get_home_dir();
		if (!home)
			return g_strdup(filename);
		}
	else
		{
		gchar *username;
		struct passwd *passwd;

		if (slash)
			username = g_strndup(notilde, slash - notilde);
		else
			username = g_strdup(notilde);

		passwd = getpwnam(username);
		g_free(username);

		if (!passwd)
			return g_strdup(filename);

		home = passwd->pw_dir;
		}

	if (slash)
		return g_build_filename(home, G_DIR_SEPARATOR_S, slash + 1, NULL);
	else
		return g_build_filename(home, G_DIR_SEPARATOR_S, NULL);
#endif
}

/* Search for latitude/longitude parameters in a string
 */

#define GEOCODE_NAME "geocode-parameters.awk"
#define BUFSIZE 128

gchar *decode_geo_script(const gchar *path_dir, const gchar *input_text)
{
	gchar *message;
	gchar *path = g_build_filename(path_dir, GEOCODE_NAME, NULL);
	gchar *cmd = g_strconcat("echo \'", input_text, "\'  | awk -f ", path, NULL);

	if (g_file_test(path, G_FILE_TEST_EXISTS))
		{
		gchar buf[BUFSIZE];
		FILE *fp;

		if ((fp = popen(cmd, "r")) == nullptr)
			{
			message = g_strconcat("Error: opening pipe\n", input_text, NULL);
			}
		else
			{
			while (fgets(buf, BUFSIZE, fp))
				{
				DEBUG_1("Output: %s", buf);
				}

			message = g_strconcat(buf, NULL);

			if(pclose(fp))
				{
				message = g_strconcat("Error: Command not found or exited with error status\n", input_text, NULL);
				}
			}
		}
	else
		{
		message = g_strconcat(input_text, NULL);
		}

	g_free(path);
	g_free(cmd);
	return message;
}

gchar *decode_geo_parameters(const gchar *input_text)
{
	gchar *message;
	gchar *dir;

	message = decode_geo_script(gq_bindir, input_text);
	if (strstr(message, "Error"))
		{
		g_free(message);
		dir = g_build_filename(get_rc_dir(), "applications", NULL);
		message = decode_geo_script(dir, input_text);
		g_free(dir);
		}

	return message;
}

/* Run a command like system() but may output debug messages. */
int runcmd(const gchar *cmd)
{
#if 1
	return system(cmd);
	return 0;
#else
	/* For debugging purposes */
	int retval = -1;
	FILE *in;

	DEBUG_1("Running command: %s", cmd);

	in = popen(cmd, "r");
	if (in)
		{
		int status;
		const gchar *msg;
		gchar buf[2048];

		while (fgets(buf, sizeof(buf), in) != NULL )
			{
			DEBUG_1("Output: %s", buf);
			}

		status = pclose(in);

		if (WIFEXITED(status))
			{
			msg = "Command terminated with exit code";
			retval = WEXITSTATUS(status);
			}
		else if (WIFSIGNALED(status))
			{
			msg = "Command was killed by signal";
			retval = WTERMSIG(status);
			}
		else
			{
			msg = "pclose() returned";
			retval = status;
			}

		DEBUG_1("%s : %d\n", msg, retval);
	}

	return retval;
#endif
}

/**
 * @brief Returns integer representing first_day_of_week
 * @returns Integer in range 1 to 7
 * 
 * Uses current locale to get first day of week.
 * If _NL_TIME_FIRST_WEEKDAY is not available, ISO 8601
 * states first day of week is Monday.
 * USA, Mexico and Canada (and others) use Sunday as first day of week.
 * 
 * Sunday == 1
 */
gint date_get_first_day_of_week()
{
#ifdef HAVE__NL_TIME_FIRST_WEEKDAY
	return nl_langinfo(_NL_TIME_FIRST_WEEKDAY)[0];
#else
	gchar *dot;
	gchar *current_locale;

	current_locale = setlocale(LC_ALL, NULL);
	dot = strstr(current_locale, ".");
	if ((strncmp(dot - 2, "US", 2) == 0) || (strncmp(dot - 2, "MX", 2) == 0) || (strncmp(dot - 2, "CA", 2) == 0))
		{
		return 1;
		}
	else
		{
		return 2;
		}
#endif
}

/**
 * @brief Get an abbreviated day name from locale
 * @param day Integer in range 1 to 7, representing day of week
 * @returns String containing abbreviated day name
 * 
 *  Uses current locale to get day name
 * 
 * Sunday == 1
 * Result must be freed
 */
gchar *date_get_abbreviated_day_name(gint day)
{
	gchar *abday = nullptr;

	switch (day)
		{
		case 1:
		abday = g_strdup(nl_langinfo(ABDAY_1));
		break;
		case 2:
		abday = g_strdup(nl_langinfo(ABDAY_2));
		break;
		case 3:
		abday = g_strdup(nl_langinfo(ABDAY_3));
		break;
		case 4:
		abday = g_strdup(nl_langinfo(ABDAY_4));
		break;
		case 5:
		abday = g_strdup(nl_langinfo(ABDAY_5));
		break;
		case 6:
		abday = g_strdup(nl_langinfo(ABDAY_6));
		break;
		case 7:
		abday = g_strdup(nl_langinfo(ABDAY_7));
		break;
		}

	return abday;
}

gchar *convert_rating_to_stars(gint rating)
{
	gchar *ret;
	GString *str = g_string_new(nullptr);

	if (rating == -1)
		{
		str = g_string_append_unichar(str, options->star_rating.rejected);
		ret = g_strdup(str->str);
		g_string_free(str, TRUE);
		}
	else if (rating > 0 && rating < 6)
		{
		while (rating > 0)
			{
			str = g_string_append_unichar(str, options->star_rating.star);
			rating = rating - 1;
			}
		ret = g_strdup(str->str);
		g_string_free(str, TRUE);
		}
	else
		{
		ret = g_strdup("");
		}

	return ret;
}

gchar *get_symbolic_link(const gchar *path_utf8)
{
	gchar *sl;
	struct stat st;
	gchar *ret = g_strdup("");

	sl = path_from_utf8(path_utf8);

	if (lstat(sl, &st) == 0 && S_ISLNK(st.st_mode))
		{
		gchar *buf;
		gint l;

		buf = static_cast<gchar *>(g_malloc(st.st_size + 1));
		l = readlink(sl, buf, st.st_size);

		if (l == st.st_size)
			{
			buf[l] = '\0';

			ret = buf;
			}
		else
			{
			g_free(buf);
			}
		}

	g_free(sl);

	return ret;
}

gint get_cpu_cores(void)
{
    return sysconf(_SC_NPROCESSORS_ONLN);
}

void tree_path_free_wrapper(void *data, void *UNUSED(useradata))
{
	gtk_tree_path_free(static_cast<GtkTreePath *>(data));
}

/* Copied from the libarchive .repo. examples */

#ifndef HAVE_ARCHIVE
gchar *open_archive(FileData *UNUSED(fd))
{
	log_printf("%s", _("Warning: libarchive not installed"));
	return NULL;
}

#else

#include <archive.h>
#include <archive_entry.h>

static void errmsg(const char *);
static gboolean extract(const char *filename, int do_extract, int flags);
static int copy_data(struct archive *, struct archive *);
static void msg(const char *);
static int verbose = 0;

gchar *open_archive(FileData *fd)
{
	int flags;
	gchar *current_dir;
	gchar *destination_dir;
	gboolean success;
	gint error;

	destination_dir = g_build_filename(g_get_tmp_dir(), GQ_ARCHIVE_DIR, instance_identifier, fd->path, NULL);

	if (!recursive_mkdir_if_not_exists(destination_dir, 0755))
		{
		log_printf("%s%s%s", _("Open Archive - Cannot create directory: "), destination_dir, "\n");
		g_free(destination_dir);
		return nullptr;
		}

	current_dir = g_get_current_dir();
	error = chdir(destination_dir);
	if (error)
		{
		log_printf("%s%s%s%s%s", _("Open Archive - Cannot change directory to: "), destination_dir, _("\n  Error code: "), strerror(errno), "\n");
		g_free(destination_dir);
		g_free(current_dir);
		return nullptr;
		}

	flags = ARCHIVE_EXTRACT_TIME;
	success = extract(fd->path, 1, flags);

	error = chdir(current_dir);
	if (error)
		{
		log_printf("%s%s%s%s%s", _("Open Archive - Cannot change directory to: "), current_dir, _("\n  Error code: "), strerror(errno), "\n");
		g_free(destination_dir);
		g_free(current_dir);
		return nullptr;
		}
	g_free(current_dir);

	if (!success)
		{
		g_free(destination_dir);
		destination_dir = nullptr;
		}

	return destination_dir;
}

static gboolean extract(const char *filename, int do_extract, int flags)
{
	struct archive *a;
	struct archive *ext;
	struct archive_entry *entry;
	int r;

	a = archive_read_new();
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	if (filename != nullptr && strcmp(filename, "-") == 0)
		{
		filename = nullptr;
		}
	if ((r = archive_read_open_filename(a, filename, 10240)))
		{
		errmsg(archive_error_string(a));
		errmsg("\n");
		return(FALSE);
		}
	for (;;)
		{
		int needcr = 0;

		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_EOF)
			{
			break;
			}
		if (r != ARCHIVE_OK)
			{
			errmsg(archive_error_string(a));
			errmsg("\n");
			return(FALSE);
			}
		if (verbose && do_extract)
			{
			msg("x ");
			}
		if (verbose || !do_extract)
			{
			msg(archive_entry_pathname(entry));
			msg(" ");
			needcr = 1;
			}
		if (do_extract)
			{
			r = archive_write_header(ext, entry);
			if (r != ARCHIVE_OK)
				{
				errmsg(archive_error_string(a));
				needcr = 1;
				}
			else
				{
				r = copy_data(a, ext);
				if (r != ARCHIVE_OK)
					{
					needcr = 1;
					}
				}
			}
		if (needcr)
			{
			msg("\n");
			}
		}
	archive_read_close(a);
	archive_read_free(a);

	archive_write_close(ext);
	archive_write_free(ext);
	return(TRUE);
}

static int copy_data(struct archive *ar, struct archive *aw)
{
	int r;
	const void *buff;
	size_t size;
	int64_t offset;

	for (;;)
		{
		r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF)
			return (ARCHIVE_OK);
		if (r != ARCHIVE_OK)
			{
			errmsg(archive_error_string(ar));
			return (r);
			}
		r = archive_write_data_block(aw, buff, size, offset);
		if (r != ARCHIVE_OK)
			{
			errmsg(archive_error_string(ar));
			return (r);
			}
		}
}

static void msg(const char *m)
{
	log_printf("Open Archive - libarchive error: %s \n", m);
}

static void errmsg(const char *m)
{
	if (m == nullptr)
		{
		m = "Error: No error description provided.\n";
		}
	log_printf("Open Archive - libarchive error: %s \n", m);
}
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
