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

#include "misc.h"

#include <sys/stat.h>
#include <unistd.h>

#include <config.h>

#if !HAVE__NL_TIME_FIRST_WEEKDAY
#  include <clocale>
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glib-object.h>
#include <grp.h>
#include <langinfo.h>
#include <pwd.h>

#include "main.h"
#include "options.h"
#include "ui-fileops.h"

namespace
{

constexpr int BUFSIZE = 128;

/**
 * @def STYLE_SHIFT_STANDARD
 * The standard shift percent for alternating list row colors
 */
constexpr gshort STYLE_SHIFT_STANDARD = 10;

constexpr gint CELL_HEIGHT_OVERRIDE = 512;

} // namespace

gdouble get_zoom_increment()
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
	gchar *s1_t;
	gchar *s2_t;
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

	g_autofree gchar *s1_key = g_utf8_collate_key(s1_t, -1);
	g_autofree gchar *s2_key = g_utf8_collate_key(s2_t, -1);

	ret = strcmp(s1_key, s2_key);

	if (!case_sensitive)
		{
		g_free(s1_t);
		g_free(s2_t);
		}

	return ret;
}

gint gq_gtk_tree_iter_utf8_collate(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gint sort_column_id)
{
	g_autofree gchar *str_a = nullptr;
	gtk_tree_model_get(model, a,
	                   sort_column_id, &str_a,
	                   -1);

	g_autofree gchar *str_b = nullptr;
	gtk_tree_model_get(model, b,
	                   sort_column_id, &str_b,
	                   -1);

	if (str_a && str_b) return g_utf8_collate(str_a, str_b);

	if (!str_a && !str_b) return 0;

	return (!str_a) ? -1 : 1;
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
		struct passwd *passwd;

		g_autofree gchar *username = nullptr;
		if (slash)
			username = g_strndup(notilde, slash - notilde);
		else
			username = g_strdup(notilde);

		passwd = getpwnam(username);

		if (!passwd)
			return g_strdup(filename);

		home = passwd->pw_dir;
		}

	if (slash)
		return g_build_filename(home, G_DIR_SEPARATOR_S, slash + 1, NULL);

	return g_build_filename(home, G_DIR_SEPARATOR_S, NULL);
#endif
}

/* Search for latitude/longitude parameters in a string
 */

#define GEOCODE_NAME "geocode-parameters.awk"

static gchar *decode_geo_script(const gchar *path_dir, const gchar *input_text)
{
	g_autofree gchar *path = g_build_filename(path_dir, GEOCODE_NAME, NULL);
	if (!g_file_test(path, G_FILE_TEST_EXISTS))
		{
		return g_strconcat(input_text, NULL);
		}

	g_autofree gchar *cmd = g_strconcat("echo \'", input_text, "\'  | awk -W posix -f ", path, NULL);

	FILE *fp = popen(cmd, "r");
	if (!fp)
		{
		return g_strconcat("Error: opening pipe\n", input_text, NULL);
		}

	gchar buf[BUFSIZE];
	while (fgets(buf, BUFSIZE, fp))
		{
		DEBUG_1("Output: %s", buf);
		}

	if(pclose(fp))
		{
		return g_strconcat("Error: Command not found or exited with error status\n", input_text, NULL);
		}

	return g_strconcat(buf, NULL);
}

gchar *decode_geo_parameters(const gchar *input_text)
{
	gchar *message;

	message = decode_geo_script(gq_bindir, input_text);
	if (strstr(message, "Error"))
		{
		g_free(message);
		g_autofree gchar *dir = g_build_filename(get_rc_dir(), "applications", NULL);
		message = decode_geo_script(dir, input_text);
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
#if HAVE__NL_TIME_FIRST_WEEKDAY
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
		default:
			break;
		}

	return abday;
}

gchar *convert_rating_to_stars(gint rating)
{
	GString *str = g_string_new(nullptr);

	if (rating == -1)
		{
		str = g_string_append_unichar(str, options->star_rating.rejected);
		return g_string_free(str, FALSE);
		}

	if (rating > 0 && rating < 6)
		{
		for (; rating > 0; --rating)
			{
			str = g_string_append_unichar(str, options->star_rating.star);
			}
		return g_string_free(str, FALSE);
		}

	return g_strdup("");
}

gchar *get_file_group(const gchar *path_utf8)
{
	struct passwd *user;
	gchar *ret;

	struct stat st;

	if (!stat_utf8(path_utf8, &st))
		{
		return nullptr;
		}

	user = getpwuid(st.st_uid);
	if (!user)
		{
		ret = g_strdup_printf("%u", st.st_uid);
		}
	else
		{
		ret = g_strdup(user->pw_name);
		}

	return ret;
}

gchar *get_file_owner(const gchar *path_utf8)
{
	struct group *group;
	gchar *ret;

	struct stat st;

	if (!stat_utf8(path_utf8, &st))
		{
		return nullptr;
		}

	group = getgrgid(st.st_gid);
	if (!group)
		{
		ret = g_strdup_printf("%u", st.st_gid);
		}
	else
		{
		ret = g_strdup(group->gr_name);
		}

	return ret;
}

gchar *get_symbolic_link(const gchar *path_utf8)
{
	gchar *ret = g_strdup("");

	g_autofree gchar *sl = path_from_utf8(path_utf8);

	struct stat st;
	if (lstat(sl, &st) == 0 && S_ISLNK(st.st_mode))
		{
		g_autofree auto *buf = static_cast<gchar *>(g_malloc(st.st_size + 1));

		const gint l = readlink(sl, buf, st.st_size);
		if (l == st.st_size)
			{
			buf[l] = '\0';

			std::swap(ret, buf);
			}
		}

	return ret;
}

gint get_cpu_cores()
{
    return sysconf(_SC_NPROCESSORS_ONLN);
}

#if HAVE_GTK4
GdkRGBA convert_gdkcolor_to_gdkrgba(gpointer data)
{
/* @FIXME GTK4 stub */
}
#else
GdkRGBA convert_gdkcolor_to_gdkrgba(gpointer data)
{
	auto *gdk_color = static_cast<GdkColor *>(data);

	return { std::clamp((double)gdk_color->red / 65535.0, 0.0, 1.0),
	         std::clamp((double)gdk_color->green / 65535.0, 0.0, 1.0),
	         std::clamp((double)gdk_color->blue / 65535.0, 0.0, 1.0),
	         1.0 };
}
#endif

/**
 * @brief Shifts a GdkRGBA values lighter or darker \n
 * val is percent from 1 to 100, or -1 for default (usually 10%) \n
 * direction is -1 darker, 0 auto, 1 lighter
 */
void shift_color(GdkRGBA &src, gshort val, gint direction)
{
	if (val == -1)
		{
		val = STYLE_SHIFT_STANDARD;
		}
	else
		{
		val = std::clamp<gshort>(val, 1, 100);
		}

	const gdouble cs = 1.0 / 100 * val;

	/* up or down ? */
	if (direction < 0 || (direction == 0 && (src.red + src.green + src.blue) / 3 > 0.5))
		{
		src.red = std::max(0.0, src.red - cs);
		src.green = std::max(0.0, src.green - cs);
		src.blue = std::max(0.0, src.blue - cs);
		}
	else
		{
		src.red = std::min(1.0, src.red + cs);
		src.green = std::min(1.0, src.green + cs);
		src.blue = std::min(1.0, src.blue + cs);
		}
}

void gq_gtk_entry_set_text(GtkEntry *entry, const gchar *text)
{
	GtkEntryBuffer *buffer;

	buffer = gtk_entry_get_buffer(entry);
	gtk_entry_buffer_set_text(buffer, text, static_cast<gint>(g_utf8_strlen(text, -1)));
}

const gchar *gq_gtk_entry_get_text(GtkEntry *entry)
{
	GtkEntryBuffer *buffer;

	buffer = gtk_entry_get_buffer(entry);
	return gtk_entry_buffer_get_text(buffer);
}

void gq_gtk_grid_attach(GtkGrid *grid, GtkWidget *child, guint left_attach, guint right_attach, guint top_attach, guint bottom_attach, GtkAttachOptions, GtkAttachOptions, guint, guint)
{
	gtk_grid_attach(grid, child, left_attach, top_attach, right_attach - left_attach, bottom_attach - top_attach);
}

void gq_gtk_grid_attach_default(GtkGrid *grid, GtkWidget *child, guint left_attach, guint right_attach, guint top_attach, guint bottom_attach )
{
	gtk_grid_attach(grid, child, left_attach, top_attach, right_attach - left_attach, bottom_attach - top_attach);
}

/**
 * @brief This overrides the low default of a GtkCellRenderer from 100 to CELL_HEIGHT_OVERRIDE, something sane for our purposes
 */
void cell_renderer_height_override(GtkCellRenderer *renderer)
{
	GParamSpec *spec;

	spec = g_object_class_find_property(G_OBJECT_GET_CLASS(G_OBJECT(renderer)), "height");
	if (spec && G_IS_PARAM_SPEC_INT(spec))
		{
		GParamSpecInt *spec_int;

		spec_int = G_PARAM_SPEC_INT(spec);
		spec_int->maximum = std::max(spec_int->maximum, CELL_HEIGHT_OVERRIDE);
		}
}

/**
 * @brief Set cursor for widget's window.
 * @param widget Widget for which cursor is set
 * @param icon Cursor type from GdkCursorType.
 *        Value -1 means using the cursor of its parent window.
 * @todo Use std::optional for icon since C++17 instead of special -1 value
 */
void widget_set_cursor(GtkWidget *widget, gint icon)
{
	GdkWindow *window = gtk_widget_get_window(widget);
	if (!window) return;

	GdkCursor *cursor = nullptr;

	if (icon != -1)
		{
		GdkDisplay *display = gdk_display_get_default();
		cursor = gdk_cursor_new_for_display(display, static_cast<GdkCursorType>(icon));
		}

	gdk_window_set_cursor(window, cursor);

	if (cursor) g_object_unref(cursor);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
