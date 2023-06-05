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

#include "main.h"
#include "osd.h"

#include "dnd.h"
#include "exif.h"
#include "glua.h"
#include "metadata.h"
#include "ui-fileops.h"
#include "ui-misc.h"

#include <math.h>

static const gchar *predefined_tags[][2] = {
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
	{NULL, NULL}};

static GtkTargetEntry osd_drag_types[] = {
	{ const_cast<gchar *>("text/plain"), GTK_TARGET_SAME_APP, TARGET_TEXT_PLAIN }
};

typedef struct _TagData TagData;
struct _TagData
{
	gchar *key;
	gchar *title;
};

static void tag_button_cb(GtkWidget *widget, gpointer data)
{
	auto image_overlay_template_view = static_cast<GtkTextView *>(data);
	GtkTextBuffer *buffer;
	TagData *td;

	buffer = gtk_text_view_get_buffer(image_overlay_template_view);
	td = static_cast<TagData *>(g_object_get_data(G_OBJECT(widget), "tag_data"));
	gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(buffer), td->key, -1);

	gtk_widget_grab_focus(GTK_WIDGET(image_overlay_template_view));
}

static void osd_dnd_get_cb(GtkWidget *btn, GdkDragContext *UNUSED(context),
								GtkSelectionData *selection_data, guint UNUSED(info),
								guint UNUSED(time), gpointer data)
{
	TagData *td;
	auto image_overlay_template_view = static_cast<GtkTextView *>(data);

	td = static_cast<TagData *>(g_object_get_data(G_OBJECT(btn), "tag_data"));
	gtk_selection_data_set_text(selection_data, td->key, -1);

	gtk_widget_grab_focus(GTK_WIDGET(image_overlay_template_view));
}

static void osd_btn_destroy_cb(GtkWidget *btn, GdkDragContext *UNUSED(context),
								GtkSelectionData *UNUSED(selection_data), guint UNUSED(info),
								guint UNUSED(time), gpointer UNUSED(data))
{
	TagData *td;

	td = static_cast<TagData *>(g_object_get_data(G_OBJECT(btn), "tag_data"));
	g_free(td->key);
	g_free(td->title);
}

static void set_osd_button(GtkTable *table, const gint rows, const gint cols, const gchar *key, const gchar *title, GtkWidget *template_view)
{
	GtkWidget *new_button;
	TagData *td;

	new_button = gtk_button_new_with_label(title);
	g_signal_connect(G_OBJECT(new_button), "clicked", G_CALLBACK(tag_button_cb), template_view);
	gtk_widget_show(new_button);

	td = g_new0(TagData, 1);
	td->key = g_strdup(key);
	td->title = g_strdup(title);

	g_object_set_data(G_OBJECT(new_button), "tag_data", td);

	gtk_drag_source_set(new_button, GDK_BUTTON1_MASK, osd_drag_types, 1, GDK_ACTION_COPY);
	g_signal_connect(G_OBJECT(new_button), "drag_data_get",
							G_CALLBACK(osd_dnd_get_cb), template_view);
	g_signal_connect(G_OBJECT(new_button), "destroy",
							G_CALLBACK(osd_btn_destroy_cb), new_button);

	gtk_table_attach_defaults(table, new_button, cols, cols+1, rows, rows+1);

}

GtkWidget *osd_new(gint max_cols, GtkWidget *template_view)
{
	GtkWidget *vbox;
	GtkWidget *scrolled;
	gint i = 0;
	gint rows = 0;
	gint max_rows = 0;
	gint cols = 0;
	gdouble entries;
	GtkWidget *viewport;

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

	pref_label_new(vbox, _("To include predefined tags in the template, click a button or drag-and-drop"));

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_box_pack_start(GTK_BOX(vbox), scrolled, FALSE, FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled), PREF_PAD_BORDER);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_show(scrolled);
	gtk_widget_set_size_request(scrolled, -1, 140);

	viewport = gtk_viewport_new(NULL, NULL);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(scrolled), viewport);
	gtk_widget_show(viewport);

	entries = (sizeof(predefined_tags) / sizeof(predefined_tags[0])) - 1;
	max_rows = ceil(entries / max_cols);

	GtkTable *table;
	table = GTK_TABLE(gtk_table_new(max_rows, max_cols, FALSE));
	gtk_container_add(GTK_CONTAINER(viewport), GTK_WIDGET(table));
	gtk_widget_show(GTK_WIDGET(table));

	for (rows = 0; rows < max_rows; rows++)
		{
		cols = 0;

		while (cols < max_cols && predefined_tags[i][0])
			{
			set_osd_button(table, rows, cols, predefined_tags[i][0], predefined_tags[i][1], template_view);
			i = i + 1;
			cols++;
			}
		}
	return vbox;
}
static gchar *keywords_to_string(FileData *fd)
{
	GList *keywords;
	GString *kwstr = NULL;
	gchar *ret = NULL;

	g_assert(fd);

	keywords = metadata_read_list(fd, KEYWORD_KEY, METADATA_PLAIN);

	if (keywords)
		{
		GList *work = keywords;

		while (work)
			{
			auto kw = static_cast<gchar *>(work->data);
			work = work->next;

			if (!kw) continue;
			if (!kwstr)
				kwstr = g_string_new("");
			else
				g_string_append(kwstr, ", ");

			g_string_append(kwstr, kw);
			}
		string_list_free(keywords);
		}

	if (kwstr)
		{
		ret = kwstr->str;
		g_string_free(kwstr, FALSE);
		}

	return ret;
}

gchar *image_osd_mkinfo(const gchar *str, FileData *fd, GHashTable *vars)
{
	gchar delim = '%', imp = '|', sep[] = " - ";
	gchar *start, *end;
	guint pos, prev;
	gboolean want_separator = FALSE;
	gchar *name, *data;
	GString *osd_info;
	gchar *ret;

	if (!str || !*str) return g_strdup("");

	osd_info = g_string_new(str);

	prev = -1;

	while (TRUE)
		{
		guint limit = 0;
		gchar *trunc = NULL;
		gchar *limpos = NULL;
		gchar *extra = NULL;
		gchar *extrapos = NULL;
		gchar *p;

		start = strchr(osd_info->str + (prev + 1), delim);
		if (!start)
			break;
		end = strchr(start+1, delim);
		if (!end)
			break;

		/* Search for optional modifiers
		 * %name:99:extra% -> name = "name", limit=99, extra = "extra"
		 */
		for (p = start + 1; p < end; p++)
			{
			if (p[0] == ':')
				{
				if (g_ascii_isdigit(p[1]) && !limpos)
					{
					limpos = p + 1;
					if (!trunc) trunc = p;
					}
				else
					{
					extrapos = p + 1;
					if (!trunc) trunc = p;
					break;
					}
				}
			}

		if (limpos)
			limit = static_cast<guint>(atoi(limpos));

		if (extrapos)
			extra = g_strndup(extrapos, end - extrapos);

		name = g_strndup(start+1, (trunc ? trunc : end)-start-1);
		pos = start - osd_info->str;
		data = NULL;

		if (strcmp(name, "keywords") == 0)
			{
			data = keywords_to_string(fd);
			}
		else if (strcmp(name, "comment") == 0)
			{
			data = metadata_read_string(fd, COMMENT_KEY, METADATA_PLAIN);
			}
		else if (strcmp(name, "imagecomment") == 0)
			{
			data = exif_get_image_comment(fd);
			}
		else if (strcmp(name, "rating") == 0)
			{
			data = metadata_read_string(fd, RATING_KEY, METADATA_PLAIN);
			}
#ifdef HAVE_LUA
		else if (strncmp(name, "lua/", 4) == 0)
			{
			gchar *tmp;
			tmp = strchr(name+4, '/');
			if (!tmp)
				break;
			*tmp = '\0';
			data = lua_callvalue(fd, name+4, tmp+1);
			}
#endif
		else
			{
			data = g_strdup(static_cast<const gchar *>(g_hash_table_lookup(vars, static_cast<gconstpointer>(name))));
			if (!data)
				data = metadata_read_string(fd, name, METADATA_FORMATTED);
			}

		if (data && *data && limit > 0 && strlen(data) > limit + 3)
			{
			gchar *new_data = g_strdup_printf("%-*.*s...", limit, limit, data);
			g_free(data);
			data = new_data;
			}

		if (data)
			{
			/* Since we use pango markup to display, we need to escape here */
			gchar *escaped = g_markup_escape_text(data, -1);
			g_free(data);
			data = escaped;
			}

		if (extra)
			{
			if (data && *data)
				{
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
				 * "ISO *"	-> prefix data with "ISO " (ie. "ISO 100")
				 * "\**\*"	-> prefix data with a star, and append a star (ie. "*100*")
				 * "\\*"	-> prefix data with an anti slash (ie "\100")
				 * "Collection <b>*</b>\n" -> display data in bold prefixed by "Collection " and a newline is appended
				 */
				/** @FIXME using background / foreground colors lead to weird results.
				 */
				gchar *new_data;
				gchar *left = NULL;
				gchar *right = extra;
				gchar *p;
				guint len = strlen(extra);

				/* Search for left and right parts and unescape characters */
				for (p = extra; *p; p++, len--)
					if (p[0] == '\\')
						{
						if (p[1] == 'n')
							{
							memmove(p+1, p+2, --len);
							p[0] = '\n';
							}
						else if (p[1] != '\0')
							memmove(p, p+1, len--); // includes \0
						}
					else if (p[0] == '*' && !left)
						{
						right = p + 1;
						left = extra;
						}

				if (left) right[-1] = '\0';

				new_data = g_strdup_printf("%s%s%s", left ? left : "", data, right);
				g_free(data);
				data = new_data;
				}
			g_free(extra);
			}

		g_string_erase(osd_info, pos, end-start+1);
		if (data && *data)
			{
			if (want_separator)
				{
				/* insert separator */
				g_string_insert(osd_info, pos, sep);
				pos += strlen(sep);
				want_separator = FALSE;
				}

			g_string_insert(osd_info, pos, data);
			pos += strlen(data);
		}

		if (pos-prev >= 1 && osd_info->str[pos] == imp)
			{
			/* pipe character is replaced by a separator, delete it
			 * and raise a flag if needed */
			g_string_erase(osd_info, pos--, 1);
			want_separator |= (data && *data);
			}

		if (osd_info->str[pos] == '\n') want_separator = FALSE;

		prev = pos - 1;

		g_free(name);
		g_free(data);
		}

	/* search and destroy empty lines */
	end = osd_info->str;
	while ((start = strchr(end, '\n')))
		{
		end = start;
		while (*++(end) == '\n')
			;
		g_string_erase(osd_info, start-osd_info->str, end-start-1);
		}

	g_strchomp(osd_info->str);

	ret = osd_info->str;
	g_string_free(osd_info, FALSE);

	return ret;
}

void osd_template_insert(GHashTable *vars, const gchar *keyword, const gchar *value, OsdTemplateFlags flags)
{
	if (!value)
		{
		g_hash_table_insert(vars, const_cast<gchar *>(keyword), g_strdup(""));
		return;
		}

	if (flags & OSDT_NO_DUP)
		{
		g_hash_table_insert(vars, const_cast<gchar *>(keyword), (gpointer)value);
		return;
		}
	else
		{
		g_hash_table_insert(vars, const_cast<gchar *>(keyword), g_strdup(value));
		}

	if (flags & OSDT_FREE) g_free(const_cast<gchar *>(value));
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
