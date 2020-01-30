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
#include "ui_fileops.h"

#include <langinfo.h>
#include <locale.h>

gdouble get_zoom_increment(void)
{
	return ((options->image.zoom_increment != 0) ? (gdouble)options->image.zoom_increment / 100.0 : 1.0);
}

gchar *utf8_validate_or_convert(const gchar *text)
{
	gint len;

	if (!text) return NULL;

	len = strlen(text);
	if (!g_utf8_validate(text, len, NULL))
		return g_convert(text, len, "UTF-8", "ISO-8859-1", NULL, NULL, NULL);

	return g_strdup(text);
}

gint utf8_compare(const gchar *s1, const gchar *s2, gboolean case_sensitive)
{
	gchar *s1_key, *s2_key;
	gchar *s1_t, *s2_t;
	gint ret;

	g_assert(g_utf8_validate(s1, -1, NULL));
	g_assert(g_utf8_validate(s2, -1, NULL));

	if (!case_sensitive)
		{
		s1_t = g_utf8_casefold(s1, -1);
		s2_t = g_utf8_casefold(s2, -1);
		}
	else
		{
		s1_t = (gchar *) s1;
		s2_t = (gchar *) s2;
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

		if ((fp = popen(cmd, "r")) == NULL)
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

	message = decode_geo_script(GQ_BIN_DIR, input_text);
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
int runcmd(gchar *cmd)
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
	gchar *abday = NULL;

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
	GString *str = g_string_new(NULL);

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

		buf = g_malloc(st.st_size + 1);
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
	FILE *cpuinfo = fopen("/proc/cpuinfo", "rb");
	char *arg = 0;
	size_t size = 0;
	int cores = 1;
	gchar *siblings_line;
	gchar *siblings_str;

	while(getline(&arg, &size, cpuinfo) != -1)
		{
		siblings_line = g_strrstr(arg, "siblings");
		if (siblings_line)
			{
			siblings_str = g_strrstr(siblings_line, ":");
			if (siblings_str)
				{
				cores = g_ascii_strtoll(siblings_str + 1, NULL, 0);
				}
			}
		}
	free(arg);
	fclose(cpuinfo);

	return cores;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
