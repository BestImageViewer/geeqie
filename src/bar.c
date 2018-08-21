/*
 * Copyright (C) 2004 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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
#include "bar.h"

#include "filedata.h"
#include "history_list.h"
#include "metadata.h"
#include "misc.h"
#include "ui_fileops.h"
#include "ui_misc.h"
#include "ui_utildlg.h"

#include "ui_menu.h"
#include "bar_comment.h"
#include "bar_keywords.h"
#include "bar_exif.h"
#include "bar_histogram.h"
#include "histogram.h"
#include "rcfile.h"
#include "bar_gps.h"

typedef struct _KnownPanes KnownPanes;
struct _KnownPanes
{
	PaneType type;
	gchar *id;
	gchar *title;
	const gchar *config;
};

static const gchar default_config_histogram[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_histogram id = 'histogram' expanded = 'true' histogram_channel = '4' histogram_mode = '0' />"
"        </bar>"
"    </layout>"
"</gq>";

static const gchar default_config_title[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_comment id = 'title' expanded = 'true' key = 'Xmp.dc.title' height = '40' />"
"        </bar>"
"    </layout>"
"</gq>";

static const gchar default_config_headline[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_comment id = 'headline' expanded = 'true' key = 'Xmp.photoshop.Headline'  height = '40' />"
"        </bar>"
"    </layout>"
"</gq>";

static const gchar default_config_keywords[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_keywords id = 'keywords' expanded = 'true' key = '" KEYWORD_KEY "' />"
"        </bar>"
"    </layout>"
"</gq>";

static const gchar default_config_comment[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_comment id = 'comment' expanded = 'true' key = '" COMMENT_KEY "' height = '150' />"
"        </bar>"
"    </layout>"
"</gq>";
static const gchar default_config_rating[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_comment id = 'rating' expanded = 'true' key = '" RATING_KEY "' height = '10' />"
"        </bar>"
"    </layout>"
"</gq>";

static const gchar default_config_exif[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_exif id = 'exif' expanded = 'true' >"
"                <entry key = 'formatted.Camera' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.DateTime' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.localtime' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.ShutterSpeed' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.Aperture' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.ExposureBias' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.ISOSpeedRating' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.FocalLength' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.FocalLength35mmFilm' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.Flash' if_set = 'true' editable = 'false' />"
"                <entry key = 'Exif.Photo.ExposureProgram' if_set = 'true' editable = 'false' />"
"                <entry key = 'Exif.Photo.MeteringMode' if_set = 'true' editable = 'false' />"
"                <entry key = 'Exif.Photo.LightSource' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.ColorProfile' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.SubjectDistance' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.Resolution' if_set = 'true' editable = 'false' />"
"                <entry key = '" ORIENTATION_KEY "' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.star_rating' if_set = 'true' editable = 'false' />"
"            </pane_exif>"
"        </bar>"
"    </layout>"
"</gq>";

static const gchar default_config_file_info[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_exif id = 'file_info' expanded = 'true' >"
"                <entry key = 'file.mode' if_set = 'false' editable = 'false' />"
"                <entry key = 'file.date' if_set = 'false' editable = 'false' />"
"                <entry key = 'file.size' if_set = 'false' editable = 'false' />"
"                <entry key = 'file.owner' if_set = 'false' editable = 'false' />"
"                <entry key = 'file.group' if_set = 'false' editable = 'false' />"
"                <entry key = 'file.class' if_set = 'false' editable = 'false' />"
"                <entry key = 'file.link' if_set = 'false' editable = 'false' />"
"            </pane_exif>"
"        </bar>"
"    </layout>"
"</gq>";

static const gchar default_config_location[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_exif id = 'location' expanded = 'true' >"
"                <entry key = 'formatted.GPSPosition' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.GPSAltitude' if_set = 'true' editable = 'false' />"
"                <entry key = 'formatted.timezone' if_set = 'true' editable = 'false' />"
"                <entry key = 'Xmp.photoshop.Country' if_set = 'false' editable = 'true' />"
"                <entry key = 'Xmp.iptc.CountryCode' if_set = 'false' editable = 'true' />"
"                <entry key = 'Xmp.photoshop.State' if_set = 'false' editable = 'true' />"
"                <entry key = 'Xmp.photoshop.City' if_set = 'false' editable = 'true' />"
"                <entry key = 'Xmp.iptc.Location' if_set = 'false' editable = 'true' />"
"            </pane_exif>"
"        </bar>"
"    </layout>"
"</gq>";

static const gchar default_config_copyright[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_exif id = 'copyright' expanded = 'true' >"
"                <entry key = 'Xmp.dc.creator' if_set = 'true' editable = 'false' />"
"                <entry key = 'Xmp.dc.contributor' if_set = 'true' editable = 'false' />"
"                <entry key = 'Xmp.dc.rights' if_set = 'false' editable = 'false' />"
"            </pane_exif>"
"        </bar>"
"    </layout>"
"</gq>";

#ifdef HAVE_LIBCHAMPLAIN
#ifdef HAVE_LIBCHAMPLAIN_GTK
static const gchar default_config_gps[] =
"<gq>"
"    <layout id = '_current_'>"
"        <bar>"
"            <pane_gps id = 'gps' expanded = 'true'"
"                      map-id = 'osm::mapnik'"
"                      zoom-level = '8'"
"                      latitude = '50116666'"
"                      longitude = '8683333' />"
"        </bar>"
"    </layout>"
"</gq>";
#endif
#endif

static const KnownPanes known_panes[] = {
/* default sidebar */
	{PANE_HISTOGRAM,	"histogram",	N_("Histogram"),	default_config_histogram},
	{PANE_COMMENT,		"title",	N_("Title"),		default_config_title},
	{PANE_KEYWORDS,		"keywords",	N_("Keywords"),		default_config_keywords},
	{PANE_COMMENT,		"comment",	N_("Comment"),		default_config_comment},
	{PANE_COMMENT,		"rating",	N_("Rating"),		default_config_rating},
	{PANE_COMMENT,		"headline",	N_("Headline"),		default_config_headline},
	{PANE_EXIF,		"exif",		N_("Exif"),		default_config_exif},
/* other pre-configured panes */
	{PANE_EXIF,		"file_info",	N_("File info"),	default_config_file_info},
	{PANE_EXIF,		"location",	N_("Location and GPS"),	default_config_location},
	{PANE_EXIF,		"copyright",	N_("Copyright"),	default_config_copyright},
#ifdef HAVE_LIBCHAMPLAIN
#ifdef HAVE_LIBCHAMPLAIN_GTK
	{PANE_GPS,		"gps",	N_("GPS Map"),	default_config_gps},
#endif
#endif
	{PANE_UNDEF,		NULL,		NULL,			NULL}
};

typedef struct _BarData BarData;
struct _BarData
{
	GtkWidget *widget;
	GtkWidget *vbox;
	FileData *fd;
	GtkWidget *label_file_name;
	GtkWidget *add_button;

	LayoutWindow *lw;
	gint width;
};

static void bar_expander_move(GtkWidget *widget, gpointer data, gboolean up, gboolean single_step)
{
	GtkWidget *expander = data;
	GtkWidget *box;
	gint pos;

	if (!expander) return;
	box = gtk_widget_get_ancestor(expander, GTK_TYPE_BOX);
	if (!box) return;

	gtk_container_child_get(GTK_CONTAINER(box), expander, "position", &pos, NULL);

	if (single_step)
		{
		pos = up ? (pos - 1) : (pos + 1);
		if (pos < 0) pos = 0;
		}
	else
		{
		pos = up ? 0 : -1;
		}

	gtk_box_reorder_child(GTK_BOX(box), expander, pos);
}


static void bar_expander_move_up_cb(GtkWidget *widget, gpointer data)
{
	bar_expander_move(widget, data, TRUE, TRUE);
}

static void bar_expander_move_down_cb(GtkWidget *widget, gpointer data)
{
	bar_expander_move(widget, data, FALSE, TRUE);
}

static void bar_expander_move_top_cb(GtkWidget *widget, gpointer data)
{
	bar_expander_move(widget, data, TRUE, FALSE);
}

static void bar_expander_move_bottom_cb(GtkWidget *widget, gpointer data)
{
	bar_expander_move(widget, data, FALSE, FALSE);
}

static void bar_expander_delete_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *expander = data;
	gtk_widget_destroy(expander);
}

static void bar_expander_add_cb(GtkWidget *widget, gpointer data)
{
	//GtkWidget *bar = data;
	const KnownPanes *pane = known_panes;
	const gchar *id = g_object_get_data(G_OBJECT(widget), "pane_add_id");
	const gchar *config;

	if (!id) return;

	while (pane->id)
		{
		if (strcmp(pane->id, id) == 0) break;
		pane++;
		}
	if (!pane->id) return;

	config = bar_pane_get_default_config(id);
	if (config) load_config_from_buf(config, strlen(config), FALSE);

}


static void bar_menu_popup(GtkWidget *widget)
{
	GtkWidget *menu;
	GtkWidget *bar;
	GtkWidget *expander;
	const KnownPanes *pane = known_panes;
	BarData *bd;

	bd = g_object_get_data(G_OBJECT(widget), "bar_data");
	if (bd)
		{
		expander = NULL;
		bar = widget;
		}
	else
		{
		expander = widget;
		bar = gtk_widget_get_parent(widget);
		while (bar && !g_object_get_data(G_OBJECT(bar), "bar_data"))
			bar = gtk_widget_get_parent(bar);
		if (!bar) return;
		}

	menu = popup_menu_short_lived();

	if (expander)
		{
		menu_item_add_stock(menu, _("Move to _top"), GTK_STOCK_GOTO_TOP, G_CALLBACK(bar_expander_move_top_cb), expander);
		menu_item_add_stock(menu, _("Move _up"), GTK_STOCK_GO_UP, G_CALLBACK(bar_expander_move_up_cb), expander);
		menu_item_add_stock(menu, _("Move _down"), GTK_STOCK_GO_DOWN, G_CALLBACK(bar_expander_move_down_cb), expander);
		menu_item_add_stock(menu, _("Move to _bottom"), GTK_STOCK_GOTO_BOTTOM, G_CALLBACK(bar_expander_move_bottom_cb), expander);
		menu_item_add_divider(menu);
		menu_item_add_stock(menu, _("Remove"), GTK_STOCK_DELETE, G_CALLBACK(bar_expander_delete_cb), expander);
		menu_item_add_divider(menu);
		}

	while (pane->id)
		{
		GtkWidget *item;
		item = menu_item_add_stock(menu, _(pane->title), GTK_STOCK_ADD, G_CALLBACK(bar_expander_add_cb), bar);
		g_object_set_data(G_OBJECT(item), "pane_add_id", pane->id);
		pane++;
		}

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, bar, 0, GDK_CURRENT_TIME);
}

static void bar_menu_add_popup(GtkWidget *widget)
{
	GtkWidget *menu;
	GtkWidget *bar;
	const KnownPanes *pane = known_panes;

	bar = widget;

	menu = popup_menu_short_lived();

	while (pane->id)
		{
		GtkWidget *item;
		item = menu_item_add_stock(menu, _(pane->title), GTK_STOCK_ADD, G_CALLBACK(bar_expander_add_cb), bar);
		g_object_set_data(G_OBJECT(item), "pane_add_id", pane->id);
		pane++;
		}

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, bar, 0, GDK_CURRENT_TIME);
}


static gboolean bar_menu_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	if (bevent->button == MOUSE_BUTTON_RIGHT)
		{
		bar_menu_popup(widget);
		return TRUE;
		}
	return FALSE;
}

static void bar_expander_cb(GObject *object, GParamSpec *param_spec, gpointer data)
{
	GtkExpander *expander;
	GtkWidget *child;

	expander = GTK_EXPANDER(object);
	child = gtk_bin_get_child(GTK_BIN(expander));

	if (gtk_expander_get_expanded(expander))
		{
		gtk_widget_show_all(child);
		}
	else
		{
		gtk_widget_hide(child);
		}
}

static gboolean bar_menu_add_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	bar_menu_add_popup(widget);
	return TRUE;
}


static void bar_pane_set_fd_cb(GtkWidget *expander, gpointer data)
{
	GtkWidget *widget = gtk_bin_get_child(GTK_BIN(expander));
	PaneData *pd = g_object_get_data(G_OBJECT(widget), "pane_data");
	if (!pd) return;
	if (pd->pane_set_fd) pd->pane_set_fd(widget, data);
}

void bar_set_fd(GtkWidget *bar, FileData *fd)
{
	BarData *bd;
	bd = g_object_get_data(G_OBJECT(bar), "bar_data");
	if (!bd) return;

	file_data_unref(bd->fd);
	bd->fd = file_data_ref(fd);

	gtk_container_foreach(GTK_CONTAINER(bd->vbox), bar_pane_set_fd_cb, fd);

	gtk_label_set_text(GTK_LABEL(bd->label_file_name), (bd->fd) ? bd->fd->name : "");

}

static void bar_pane_notify_selection_cb(GtkWidget *expander, gpointer data)
{
	GtkWidget *widget = gtk_bin_get_child(GTK_BIN(expander));
	PaneData *pd = g_object_get_data(G_OBJECT(widget), "pane_data");
	if (!pd) return;
	if (pd->pane_notify_selection) pd->pane_notify_selection(widget, GPOINTER_TO_INT(data));
}

void bar_notify_selection(GtkWidget *bar, gint count)
{
	BarData *bd;
	bd = g_object_get_data(G_OBJECT(bar), "bar_data");
	if (!bd) return;

	gtk_container_foreach(GTK_CONTAINER(bd->vbox), bar_pane_notify_selection_cb, GINT_TO_POINTER(count));
}

gboolean bar_event(GtkWidget *bar, GdkEvent *event)
{
	BarData *bd;
	GList *list, *work;
	gboolean ret = FALSE;

	bd = g_object_get_data(G_OBJECT(bar), "bar_data");
	if (!bd) return FALSE;

	list = gtk_container_get_children(GTK_CONTAINER(bd->vbox));

	work = list;
	while (work)
		{
		GtkWidget *widget = gtk_bin_get_child(GTK_BIN(work->data));
		PaneData *pd = g_object_get_data(G_OBJECT(widget), "pane_data");
		if (!pd) continue;

		if (pd->pane_event && pd->pane_event(widget, event))
			{
			ret = TRUE;
			break;
			}
		work = work->next;
		}
	g_list_free(list);
	return ret;
}

GtkWidget *bar_find_pane_by_id(GtkWidget *bar, PaneType type, const gchar *id)
{
	BarData *bd;
	GList *list, *work;
	GtkWidget *ret = NULL;

	if (!id || !id[0]) return NULL;

	bd = g_object_get_data(G_OBJECT(bar), "bar_data");
	if (!bd) return NULL;

	list = gtk_container_get_children(GTK_CONTAINER(bd->vbox));

	work = list;
	while (work)
		{
		GtkWidget *widget = gtk_bin_get_child(GTK_BIN(work->data));
		PaneData *pd = g_object_get_data(G_OBJECT(widget), "pane_data");
		if (!pd) continue;

		if (type == pd->type && strcmp(id, pd->id) == 0)
			{
			ret = widget;
			break;
			}
		work = work->next;
		}
	g_list_free(list);
	return ret;
}

void bar_clear(GtkWidget *bar)
{
	BarData *bd;
	GList *list, *work;

	bd = g_object_get_data(G_OBJECT(bar), "bar_data");
	if (!bd) return;

	list = gtk_container_get_children(GTK_CONTAINER(bd->vbox));

	work = list;
	while (work)
		{
		GtkWidget *widget = work->data;
		gtk_widget_destroy(widget);
		work = work->next;
		}
	g_list_free(list);
}

void bar_write_config(GtkWidget *bar, GString *outstr, gint indent)
{
	BarData *bd;
	GList *list, *work;

	if (!bar) return;

	bd = g_object_get_data(G_OBJECT(bar), "bar_data");
	if (!bd) return;

	WRITE_NL(); WRITE_STRING("<bar ");
	write_bool_option(outstr, indent, "enabled", gtk_widget_get_visible(bar));
	write_uint_option(outstr, indent, "width", bd->width);
	WRITE_STRING(">");

	indent++;
	WRITE_NL(); WRITE_STRING("<clear/>");

	list = gtk_container_get_children(GTK_CONTAINER(bd->vbox));
	work = list;
	while (work)
		{
		GtkWidget *expander = work->data;
		GtkWidget *widget = gtk_bin_get_child(GTK_BIN(expander));
		PaneData *pd = g_object_get_data(G_OBJECT(widget), "pane_data");
		if (!pd) continue;

		pd->expanded = gtk_expander_get_expanded(GTK_EXPANDER(expander));

		if (pd->pane_write_config)
			pd->pane_write_config(widget, outstr, indent);

		work = work->next;
		}
	g_list_free(list);
	indent--;
	WRITE_NL(); WRITE_STRING("</bar>");
}

void bar_update_expander(GtkWidget *pane)
{
	PaneData *pd = g_object_get_data(G_OBJECT(pane), "pane_data");
	GtkWidget *expander;

	if (!pd) return;

	expander = gtk_widget_get_parent(pane);

	gtk_expander_set_expanded(GTK_EXPANDER(expander), pd->expanded);
}

void bar_add(GtkWidget *bar, GtkWidget *pane)
{
	GtkWidget *expander;
	BarData *bd = g_object_get_data(G_OBJECT(bar), "bar_data");
	PaneData *pd = g_object_get_data(G_OBJECT(pane), "pane_data");

	if (!bd) return;

	pd->lw = bd->lw;
	pd->bar = bar;

	expander = gtk_expander_new(NULL);
	if (pd && pd->title)
		{
		gtk_expander_set_label_widget(GTK_EXPANDER(expander), pd->title);
		gtk_widget_show(pd->title);
		}

	gtk_box_pack_start(GTK_BOX(bd->vbox), expander, FALSE, TRUE, 0);

	g_signal_connect(expander, "button_release_event", G_CALLBACK(bar_menu_cb), bd);
	g_signal_connect(expander, "notify::expanded", G_CALLBACK(bar_expander_cb), pd);

	gtk_container_add(GTK_CONTAINER(expander), pane);

	gtk_expander_set_expanded(GTK_EXPANDER(expander), pd->expanded);

	gtk_widget_show(expander);

	if (bd->fd && pd && pd->pane_set_fd) pd->pane_set_fd(pane, bd->fd);

}

void bar_populate_default(GtkWidget *bar)
{
	const gchar *populate_id[] = {"histogram", "title", "keywords", "comment", "rating", "exif", NULL};
	const gchar **id = populate_id;

	while (*id)
		{
		const gchar *config = bar_pane_get_default_config(*id);
		if (config) load_config_from_buf(config, strlen(config), FALSE);
		id++;
		}
}

static void bar_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data)
{
	BarData *bd = data;

	bd->width = allocation->width;
}

gint bar_get_width(GtkWidget *bar)
{
	BarData *bd;

	bd = g_object_get_data(G_OBJECT(bar), "bar_data");
	if (!bd) return 0;

	return bd->width;
}

void bar_close(GtkWidget *bar)
{
	BarData *bd;

	bd = g_object_get_data(G_OBJECT(bar), "bar_data");
	if (!bd) return;

	gtk_widget_destroy(bd->widget);
}

static void bar_destroy(GtkWidget *widget, gpointer data)
{
	BarData *bd = data;

	file_data_unref(bd->fd);
	g_free(bd);
}

#ifdef HAVE_LIBCHAMPLAIN_GTK
/*
   FIXME: this is an ugly hack that works around this bug:
   https://bugzilla.gnome.org/show_bug.cgi?id=590692
   http://bugzilla.openedhand.com/show_bug.cgi?id=1751
   it should be removed as soon as a better solution exists
*/

static void bar_unrealize_clutter_fix_cb(GtkWidget *widget, gpointer data)
{
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(widget));
	if (child) gtk_widget_unrealize(child);
}
#endif

GtkWidget *bar_new(LayoutWindow *lw)
{
	BarData *bd;
	GtkWidget *box;
	GtkWidget *scrolled;
	GtkWidget *tbar;
	GtkWidget *add_box;

	bd = g_new0(BarData, 1);

	bd->lw = lw;

	bd->widget = gtk_vbox_new(FALSE, PREF_PAD_GAP);
	g_object_set_data(G_OBJECT(bd->widget), "bar_data", bd);
	g_signal_connect(G_OBJECT(bd->widget), "destroy",
			 G_CALLBACK(bar_destroy), bd);

	g_signal_connect(G_OBJECT(bd->widget), "size-allocate",
			 G_CALLBACK(bar_size_allocate), bd);

	g_signal_connect(G_OBJECT(bd->widget), "button_release_event", G_CALLBACK(bar_menu_cb), bd);

	bd->width = SIDEBAR_DEFAULT_WIDTH;
	gtk_widget_set_size_request(bd->widget, bd->width, -1);

	box = gtk_hbox_new(FALSE, 0);

	bd->label_file_name = gtk_label_new("");
	gtk_label_set_ellipsize(GTK_LABEL(bd->label_file_name), PANGO_ELLIPSIZE_END);
	gtk_label_set_selectable(GTK_LABEL(bd->label_file_name), TRUE);
	gtk_misc_set_alignment(GTK_MISC(bd->label_file_name), 0.5, 0.5);
	gtk_box_pack_start(GTK_BOX(box), bd->label_file_name, TRUE, TRUE, 0);
	gtk_widget_show(bd->label_file_name);

	gtk_box_pack_start(GTK_BOX(bd->widget), box, FALSE, FALSE, 0);
	gtk_widget_show(box);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(bd->widget), scrolled, TRUE, TRUE, 0);
	gtk_widget_show(scrolled);


	bd->vbox = gtk_vbox_new(FALSE, 0);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled), bd->vbox);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(gtk_bin_get_child(GTK_BIN(scrolled))), GTK_SHADOW_NONE);

	add_box = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_end(GTK_BOX(bd->widget), add_box, FALSE, FALSE, 0);
	tbar = pref_toolbar_new(add_box, GTK_TOOLBAR_ICONS);
	bd->add_button = pref_toolbar_button(tbar, GTK_STOCK_ADD, NULL, FALSE,
					     _("Add Pane"),
					     G_CALLBACK(bar_menu_add_cb), bd);
	gtk_widget_show(add_box);

#ifdef HAVE_LIBCHAMPLAIN_GTK
	g_signal_connect(G_OBJECT(gtk_bin_get_child(GTK_BIN(scrolled))), "unrealize", G_CALLBACK(bar_unrealize_clutter_fix_cb), NULL);
#endif

	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
	gtk_widget_show(bd->vbox);
	return bd->widget;
}


GtkWidget *bar_update_from_config(GtkWidget *bar, const gchar **attribute_names, const gchar **attribute_values)
{
	gboolean enabled = TRUE;
	gint width = SIDEBAR_DEFAULT_WIDTH;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_BOOL_FULL("enabled", enabled)) continue;
		if (READ_INT_FULL("width", width)) continue;


		log_printf("unknown attribute %s = %s\n", option, value);
		}

#if !GTK_CHECK_VERSION(3,0,0)
/* FIXME: In bar_size_allocate() the width obained is the allocated width. In GTK2 this
 * is the actual width. In GTK3 it is the *minimum* width.
 * This results in the info sidebar being able to increase, but not
 * decrease. There does not seem to be a way in GTK3 to get the actual width of
 * a widget. For GTK3 the only way is to disable it. The width of the sidebar
 * is therefore not preserved across restarts.
 */
	gtk_widget_set_size_request(bar, width, -1);
#endif
	if (enabled)
		{
		gtk_widget_show(bar);
		}
	else
		{
		gtk_widget_hide(bar);
		}
	return bar;
}

GtkWidget *bar_new_from_config(LayoutWindow *lw, const gchar **attribute_names, const gchar **attribute_values)
{
	GtkWidget *bar = bar_new(lw);
	return bar_update_from_config(bar, attribute_names, attribute_values);
}

GtkWidget *bar_pane_expander_title(const gchar *title)
{
	GtkWidget *widget = gtk_label_new(title);

	pref_label_bold(widget, TRUE, FALSE);
	//gtk_label_set_ellipsize(GTK_LABEL(widget), PANGO_ELLIPSIZE_END); //FIXME: do not work

	return widget;
}

gboolean bar_pane_translate_title(PaneType type, const gchar *id, gchar **title)
{
	const KnownPanes *pane = known_panes;

	if (!title) return FALSE;
	while (pane->id)
		{
		if (pane->type == type && strcmp(pane->id, id) == 0) break;
		pane++;
		}
	if (!pane->id) return FALSE;

	if (*title && **title && strcmp(pane->title, *title) != 0) return FALSE;

	g_free(*title);
	*title = g_strdup(_(pane->title));
	return TRUE;
}

const gchar *bar_pane_get_default_config(const gchar *id)
{
	const KnownPanes *pane = known_panes;

	while (pane->id)
		{
		if (strcmp(pane->id, id) == 0) break;
		pane++;
		}
	if (!pane->id) return NULL;
	return pane->config;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
