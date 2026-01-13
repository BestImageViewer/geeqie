/*
 * Copyright (C) 2018 The Geeqie Team
 *
 * Author: Colin Clark
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

/* Routines for creating the Overlay Screen Display text. Also
 * used for the same purposes by the Print routines
 */

#include "osd.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include <gdk/gdk.h>
#include <glib-object.h>

#include <config.h>

#include "compat.h"
#include "dnd.h"
#include "exif.h"
#include "glua.h"
#include "intl.h"
#include "metadata.h"
#include "ui-fileops.h"
#include "ui-misc.h"

namespace {

struct TagData
{
	gchar *key;
	GtkWidget *image_overlay_template_view;
};

constexpr struct OsdTag
{
	const gchar *key;
	const gchar *title;
} predefined_tags[] = {
	{"%name%",							N_("Name")},
	{"%path:60%",						N_("Path")},
	{"%date%",							N_("Date")},
	{"%size%",							N_("Size")},
	{"%zoom%",							N_("Zoom")},
	{"%dimensions%",					N_("Dimensions")},
	{"%collection%",					N_("Collection")},
	{"%number%",						N_("Image index")},
	{"%total%",							N_("Images total")},
	{"%comment%",						N_("Comment")},
	{"%keywords%",						N_("Keywords")},
	{"%file.ctime%",					N_("File ctime")},
	{"%file.mode%",						N_("File mode")},
	{"%file.owner%",					N_("File owner")},
	{"%file.group%",					N_("File group")},
	{"%file.link%",						N_("File link")},
	{"%file.class%",					N_("File class")},
	{"%file.page_no%",					N_("File page no.")},
	{"%formatted.DateTime%",			N_("Image date")},
	{"%formatted.DateTimeDigitized%",	N_("Date digitized")},
	{"%formatted.ShutterSpeed%",		N_("ShutterSpeed")},
	{"%formatted.Aperture%",			N_("Aperture")},
	{"%formatted.ExposureBias%",		N_("Exposure bias")},
	{"%formatted.Resolution%",			N_("Resolution")},
	{"%formatted.Camera%",				N_("Camera")},
	{"%lua.lensID%",					N_("Lens")},
	{"%formatted.ISOSpeedRating%",		N_("ISO")},
	{"%formatted.FocalLength%",			N_("Focal length")},
	{"%formatted.FocalLength35mmFilm%",	N_("Focal len. 35mm")},
	{"%formatted.SubjectDistance%",		N_("Subject distance")},
	{"%formatted.Flash%",				N_("Flash")},
	{"%formatted.ColorProfile%",		N_("Color profile")},
	{"%formatted.GPSPosition%",			N_("Lat, Long")},
	{"%formatted.GPSAltitude%",			N_("Altitude")},
	{"%formatted.localtime%",			N_("Local time")},
	{"%formatted.timezone%",			N_("Timezone")},
	{"%formatted.countryname%",			N_("Country name")},
	{"%formatted.countrycode%",			N_("Country code")},
	{"%rating%",						N_("Rating")},
	{"%formatted.star_rating%",			N_("Star rating")},
	{"%Xmp.dc.creator%",				N_("© Creator")},
	{"%Xmp.dc.contributor%",			N_("© Contributor")},
	{"%Xmp.dc.rights%",					N_("© Rights")},
};

constexpr std::array<GtkTargetEntry, 1> osd_drag_types{{
	{ const_cast<gchar *>("text/plain"), GTK_TARGET_SAME_APP, TARGET_TEXT_PLAIN }
}};

void tag_data_add_key_to_template(TagData *td)
{
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(td->image_overlay_template_view));
	gtk_text_buffer_insert_at_cursor(buffer, td->key, -1);

	gtk_widget_grab_focus(td->image_overlay_template_view);
}

void tag_data_add_key_to_selection(TagData *td, GdkDragContext *, GtkSelectionData *selection_data, guint, guint, gpointer)
{
	gtk_selection_data_set_text(selection_data, td->key, -1);
	gtk_widget_grab_focus(td->image_overlay_template_view);
}

void tag_data_free(TagData *td)
{
	g_free(td->key);
	g_free(td);
}

GtkWidget *osd_tag_button_new(const OsdTag &tag, GtkWidget *template_view)
{
	auto *td = g_new0(TagData, 1);
	td->key = g_strdup(tag.key);
	td->image_overlay_template_view = template_view;

	GtkWidget *tag_button = gtk_button_new_with_label(tag.title);
	g_signal_connect_swapped(G_OBJECT(tag_button), "clicked", G_CALLBACK(tag_data_add_key_to_template), td);
	g_signal_connect_swapped(G_OBJECT(tag_button), "destroy", G_CALLBACK(tag_data_free), td);
	gtk_widget_show(tag_button);

	gtk_drag_source_set(tag_button, GDK_BUTTON1_MASK, osd_drag_types.data(), osd_drag_types.size(), GDK_ACTION_COPY);
	g_signal_connect_swapped(G_OBJECT(tag_button), "drag_data_get", G_CALLBACK(tag_data_add_key_to_selection), td);

	return tag_button;
}

/* Search for optional modifiers
 * %name:99:extra% -> name = "name", limit=99, extra = "extra"
 */
gchar *get_osd_name(const gchar *start, const gchar *end, guint &limit, gchar **extra)
{
	const gchar *trunc = nullptr;
	const gchar *limit_pos = nullptr;

	for (const gchar *p = start + 1; p < end; p++)
		{
		if (p[0] == ':')
			{
			if (g_ascii_isdigit(p[1]) && !limit_pos)
				{
				limit_pos = p + 1;
				limit = static_cast<guint>(atoi(limit_pos));

				if (!trunc) trunc = p;
				}
			else
				{
				const gchar *extra_pos = p + 1;
				*extra = g_strndup(extra_pos, end - extra_pos);

				if (!trunc) trunc = p;
				break;
				}
			}
		}

	return g_strndup(start + 1, (trunc ? trunc : end) - start - 1);
}

gchar *get_osd_data(gchar *name, FileData *fd, const OsdTemplate &vars)
{
	if (strcmp(name, "keywords") == 0)
		{
		g_assert(fd);

		GList *keywords = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);

		GString *kwstr = string_list_join(keywords, ", ");

		g_list_free_full(keywords, g_free);

		return g_string_free(kwstr, FALSE);
		}

	if (strcmp(name, "comment") == 0)
		{
		return metadata_read_string(fd, COMMENT_KEY, METADATA_PLAIN);
		}

	if (strcmp(name, "imagecomment") == 0)
		{
		return exif_get_image_comment(fd);
		}

	if (strcmp(name, "rating") == 0)
		{
		return metadata_read_string(fd, RATING_KEY, METADATA_PLAIN);
		}

#if HAVE_LUA
	constexpr gchar lua_prefix[] = "lua/";
	const size_t lua_prefix_len = strlen(lua_prefix);
	if (strncmp(name, "lua/", lua_prefix_len) == 0)
		{
		gchar *tmp = strchr(name + lua_prefix_len, '/');
		if (!tmp) return nullptr;

		*tmp = '\0';
		return lua_callvalue(fd, name + lua_prefix_len, tmp + 1);
		}
#endif

	try
		{
		return g_strdup(vars.at(name).c_str());
		}
	catch (const std::out_of_range &)
		{
		return metadata_read_string(fd, name, METADATA_FORMATTED);
		}

	return nullptr;
}

/* Display data between left and right parts of extra string
 * the data is expressed by a '*' character. A '*' may be escaped
 * by a \. You should escape all '*' characters, do not rely on the
 * current implementation which only replaces the first unescaped '*'.
 * If no "*" is present, the extra string is just appended to data string.
 * Pango mark up is accepted in left and right parts.
 * Any \n is replaced by a newline
 * Examples:
 * "<i>*</i>\n" -> data is displayed in italics ended with a newline
 * "\n" 	-> ended with newline
 * 'ISO *'	-> prefix data with 'ISO ' (ie. 'ISO 100')
 * "\**\*"	-> prefix data with a star, and append a star (ie. "*100*")
 * "\\*"	-> prefix data with an anti slash (ie "\100")
 * 'Collection <b>*</b>\n' -> display data in bold prefixed by 'Collection ' and a newline is appended
 */
/** @FIXME using background / foreground colors lead to weird results.
 */
gchar *add_osd_extra(const gchar *data, gchar *extra)
{
	gchar *left = nullptr;
	gchar *right = extra;
	guint len = strlen(extra);

	/* Search for left and right parts and unescape characters */
	for (gchar *p = extra; *p; p++, len--)
		if (p[0] == '\\')
			{
			if (p[1] == 'n')
				{
				memmove(p + 1, p + 2, --len);
				p[0] = '\n';
				}
			else if (p[1] != '\0')
				memmove(p, p + 1, len--); // includes \0
			}
		else if (p[0] == '*' && !left)
			{
			right = p + 1;
			left = extra;
			}

	if (left) right[-1] = '\0';

	return g_strdup_printf("%s%s%s", left ? left : "", data, right);
}

} // namespace

GtkWidget *osd_new(gint max_cols, GtkWidget *template_view)
{
	GtkWidget *vbox;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	pref_label_new(vbox, _("To include predefined tags in the template, click a button or drag-and-drop"));

	GtkWidget *scrolled = gq_gtk_scrolled_window_new(nullptr, nullptr);
	gq_gtk_box_pack_start(GTK_BOX(vbox), scrolled, FALSE, FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled), PREF_PAD_BORDER);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_show(scrolled);
	gtk_widget_set_size_request(scrolled, -1, 140);

	GtkWidget *viewport = gtk_viewport_new(nullptr, nullptr);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
	gq_gtk_container_add(scrolled, viewport);
	gtk_widget_show(viewport);

	constexpr gint entries = std::size(predefined_tags);
	const gint max_rows = ceil(static_cast<gdouble>(entries) / max_cols);

	auto *grid = GTK_GRID(gtk_grid_new());
	gq_gtk_container_add(viewport, GTK_WIDGET(grid));
	gtk_widget_show(GTK_WIDGET(grid));

	gint i = 0;
	for (gint rows = 0; rows < max_rows; rows++)
		{
		for (gint cols = 0; cols < max_cols && i < entries; cols++, i++)
			{
			GtkWidget *button = osd_tag_button_new(predefined_tags[i], template_view);
			gtk_grid_attach(grid, button, cols, rows, 1, 1);
			}
		}
	return vbox;
}

gchar *image_osd_mkinfo(const gchar *str, FileData *fd, const OsdTemplate &vars)
{
	if (!str || !*str) return g_strdup("");

	constexpr gchar delim = '%';
	constexpr gchar imp = '|';
	constexpr gchar sep[] = " - ";
	const size_t sep_len = strlen(sep);
	gboolean want_separator = FALSE;

	GString *osd_info = g_string_new(str);

	guint prev = -1;

	// @TODO Use string split functions instead of manual searching for delimiters
	while (TRUE)
		{
		gchar *start = strchr(osd_info->str + (prev + 1), delim);
		if (!start)
			break;

		gchar *end = strchr(start + 1, delim);
		if (!end)
			break;

		guint limit = 0;
		g_autofree gchar *extra = nullptr;
		g_autofree gchar *name = get_osd_name(start, end, limit, &extra);

		guint pos = start - osd_info->str;

		g_autofree gchar *data = get_osd_data(name, fd, vars);

		if (data && *data && limit > 0 && strlen(data) > limit + 3)
			{
			g_autofree gchar *new_data = g_strdup_printf("%-*.*s...", limit, limit, data);
			std::swap(data, new_data);
			}

		if (data)
			{
			/* Since we use pango markup to display, we need to escape here */
			g_autofree gchar *escaped = g_markup_escape_text(data, -1);
			std::swap(data, escaped);
			}

		if (data && *data && extra)
			{
			g_autofree gchar *new_data = add_osd_extra(data, extra);
			std::swap(data, new_data);
			}

		// Replace name with data
		g_string_erase(osd_info, pos, end-start+1);
		if (data && *data)
			{
			if (want_separator)
				{
				/* insert separator */
				g_string_insert(osd_info, pos, sep);
				pos += sep_len;
				want_separator = FALSE;
				}

			g_string_insert(osd_info, pos, data);
			pos += strlen(data);
		}

		if (pos-prev >= 1 && osd_info->str[pos] == imp)
			{
			/* pipe character is replaced by a separator, delete it
			 * and raise a flag if needed */
			g_string_erase(osd_info, pos, 1);
			want_separator |= (data && *data);
			}

		if (osd_info->str[pos] == '\n') want_separator = FALSE;

		prev = pos - 1;
		}

	/* search and destroy empty lines */
	gchar *start;
	gchar *end = osd_info->str;
	while ((start = strchr(end, '\n')))
		{
		end = start;
		while (*++(end) == '\n')
			;
		g_string_erase(osd_info, start-osd_info->str, end-start-1);
		}

	gchar *ret = g_string_free(osd_info, FALSE);

	return g_strchomp(ret);
}

void osd_template_insert(OsdTemplate &vars, const gchar *keyword, const gchar *value)
{
	vars[keyword] = value ? value : "";
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
