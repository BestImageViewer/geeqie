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
#include "bar-exif.h"

#include "exif.h"
#include "metadata.h"
#include "filedata.h"
#include "ui-misc.h"
#include "ui-menu.h"
#include "bar.h"
#include "rcfile.h"
#include "dnd.h"
#include "ui-utildlg.h"
#include "layout.h"

#define MIN_HEIGHT 25
/*
 *-------------------------------------------------------------------
 * EXIF widget
 *-------------------------------------------------------------------
 */

struct PaneExifData;

struct ExifEntry
{
	GtkWidget *ebox;
	GtkWidget *box;
	GtkWidget *title_label;
	GtkWidget *value_widget;

	gchar *key;
	gchar *title;
	gboolean if_set;
	gboolean auto_title;
	gboolean editable;

	PaneExifData *ped;
};


struct PaneExifData
{
	PaneData pane;
	GtkWidget *vbox;
	GtkWidget *widget;
	GtkSizeGroup *size_group;

	gint min_height;

	gboolean all_hidden;
	gboolean show_all;

	FileData *fd;
};

struct ConfDialogData
{
	GtkWidget *widget; /* pane or entry, devidet by presenceof "pane_data" or "entry_data" */

	/* dialog parts */
	GenericDialog *gd;
	GtkWidget *key_entry;
	GtkWidget *title_entry;
	gboolean if_set;
	gboolean editable;
};

static void bar_pane_exif_entry_dnd_init(GtkWidget *entry);
static void bar_pane_exif_entry_update_title(ExifEntry *ee);
static void bar_pane_exif_update(PaneExifData *ped);
static gboolean bar_pane_exif_menu_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data);
static void bar_pane_exif_notify_cb(FileData *fd, NotifyType type, gpointer data);
static gboolean bar_pane_exif_copy_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data);

static void bar_pane_exif_entry_changed(GtkEntry *, gpointer data)
{
	auto ee = static_cast<ExifEntry *>(data);
	gchar *text;
	if (!ee->ped->fd) return;

	text = text_widget_text_pull(ee->value_widget);
	metadata_write_string(ee->ped->fd, ee->key, text);
	g_free(text);
}

static void bar_pane_exif_entry_destroy(GtkWidget *, gpointer data)
{
	auto ee = static_cast<ExifEntry *>(data);

	g_free(ee->key);
	g_free(ee->title);
	g_free(ee);
}

static void bar_pane_exif_setup_entry_box(PaneExifData *ped, ExifEntry *ee)
{
	gboolean horizontal = !ee->editable;
	gboolean editable = ee->editable;

	if (ee->box) gtk_widget_destroy(ee->box);

	ee->box = horizontal ? gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0) : gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(ee->ebox), ee->box);
	gtk_widget_show(ee->box);

	ee->title_label = gtk_label_new(nullptr);
	gtk_label_set_xalign(GTK_LABEL(ee->title_label), horizontal ? 1.0 : 0.0);
	gtk_label_set_yalign(GTK_LABEL(ee->title_label), 0.5);
	gtk_size_group_add_widget(ped->size_group, ee->title_label);
	gtk_box_pack_start(GTK_BOX(ee->box), ee->title_label, FALSE, TRUE, 0);
	gtk_widget_show(ee->title_label);

	if (editable)
		{
		ee->value_widget = gtk_entry_new();
		g_signal_connect(G_OBJECT(ee->value_widget), "changed",
			 G_CALLBACK(bar_pane_exif_entry_changed), ee);

		}
	else
		{
		ee->value_widget = gtk_label_new(nullptr);
		gtk_label_set_ellipsize(GTK_LABEL(ee->value_widget), PANGO_ELLIPSIZE_END);
		gtk_label_set_xalign(GTK_LABEL(ee->value_widget), 0.0);
		gtk_label_set_yalign(GTK_LABEL(ee->value_widget), 0.5);
		}

	gtk_box_pack_start(GTK_BOX(ee->box), ee->value_widget, TRUE, TRUE, 1);
	gtk_widget_show(ee->value_widget);
}

static GtkWidget *bar_pane_exif_add_entry(PaneExifData *ped, const gchar *key, const gchar *title, gboolean if_set, gboolean editable)
{
	auto ee = g_new0(ExifEntry, 1);

	ee->key = g_strdup(key);
	if (title && title[0])
		{
		ee->title = g_strdup(title);
		}
	else
		{
		ee->title = exif_get_description_by_key(key);
		ee->auto_title = TRUE;
		}

	ee->if_set = if_set;
	ee->editable = editable;

	ee->ped = ped;

	ee->ebox = gtk_event_box_new();
	g_object_set_data(G_OBJECT(ee->ebox), "entry_data", ee);
	g_signal_connect_after(G_OBJECT(ee->ebox), "destroy",
			       G_CALLBACK(bar_pane_exif_entry_destroy), ee);

	gtk_box_pack_start(GTK_BOX(ped->vbox), ee->ebox, FALSE, FALSE, 0);

	bar_pane_exif_entry_dnd_init(ee->ebox);
	g_signal_connect(ee->ebox, "button_release_event", G_CALLBACK(bar_pane_exif_menu_cb), ped);
	g_signal_connect(ee->ebox, "button_press_event", G_CALLBACK(bar_pane_exif_copy_cb), ped);

	bar_pane_exif_setup_entry_box(ped, ee);

	bar_pane_exif_entry_update_title(ee);
	bar_pane_exif_update(ped);

	return ee->ebox;
}

static void bar_pane_exif_reparent_entry(GtkWidget *entry, GtkWidget *pane)
{
	auto ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	PaneExifData *old_ped;
	auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(entry), "entry_data"));

	if (!ped || !ee) return;

	old_ped = ee->ped;

	g_object_ref(entry);

	gtk_size_group_remove_widget(old_ped->size_group, ee->title_label);
	gtk_container_remove(GTK_CONTAINER(old_ped->vbox), entry);

	ee->ped = ped;
	gtk_size_group_add_widget(ped->size_group, ee->title_label);
	gtk_box_pack_start(GTK_BOX(ped->vbox), entry, FALSE, FALSE, 0);
}

static void bar_pane_exif_entry_update_title(ExifEntry *ee)
{
	gchar *markup;

	markup = g_markup_printf_escaped("<span size='small'>%s:</span>", (ee->title) ? ee->title : _("<empty label, fixme>"));
	gtk_label_set_markup(GTK_LABEL(ee->title_label), markup);
	g_free(markup);
}

static void bar_pane_exif_update_entry(PaneExifData *ped, GtkWidget *entry, gboolean update_title)
{
	gchar *text;
	auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(entry), "entry_data"));
	gshort rating;

	if (!ee) return;
	if (g_strcmp0(ee->key, "Xmp.xmp.Rating") == 0)
		{
		rating = metadata_read_int(ee->ped->fd, ee->key, 0);
		text = g_strdup_printf("%d", rating);
		}
	else
		{
		text = metadata_read_string(ped->fd, ee->key, ee->editable ? METADATA_PLAIN : METADATA_FORMATTED);
		}

	if (!ped->show_all && ee->if_set && !ee->editable && (!text || !*text))
		{
		gtk_label_set_text(GTK_LABEL(ee->value_widget), nullptr);
		gtk_widget_hide(entry);
		}
	else
		{
		if (ee->editable)
			{
			g_signal_handlers_block_by_func(ee->value_widget, (gpointer *)bar_pane_exif_entry_changed, ee);
			gtk_entry_set_text(GTK_ENTRY(ee->value_widget), text ? text : "");
			g_signal_handlers_unblock_by_func(ee->value_widget, (gpointer)bar_pane_exif_entry_changed, ee);
			gtk_widget_set_tooltip_text(ee->box, nullptr);
			}
		else
			{
			gtk_label_set_text(GTK_LABEL(ee->value_widget), text);
			gtk_widget_set_tooltip_text(ee->box, text);
			}
		gtk_widget_show(entry);
		ped->all_hidden = FALSE;
		}

	g_free(text);

	if (update_title) bar_pane_exif_entry_update_title(ee);
}

static void bar_pane_exif_update(PaneExifData *ped)
{
	GList *list, *work;

	ped->all_hidden = TRUE;

	list = gtk_container_get_children(GTK_CONTAINER(ped->vbox));
	work = list;
	while (work)
		{
		auto entry = static_cast<GtkWidget *>(work->data);
		work = work->next;

		bar_pane_exif_update_entry(ped, entry, FALSE);
		}
	g_list_free(list);

	gtk_widget_set_sensitive(ped->pane.title, !ped->all_hidden);
}

void bar_pane_exif_set_fd(GtkWidget *widget, FileData *fd)
{
	PaneExifData *ped;

	ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(widget), "pane_data"));
	if (!ped) return;

	file_data_unref(ped->fd);
	ped->fd = file_data_ref(fd);

	bar_pane_exif_update(ped);
}

gint bar_pane_exif_event(GtkWidget *bar, GdkEvent *event)
{
	PaneExifData *ped;
	gboolean ret = FALSE;
	GList *list, *work;

	ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(bar), "pane_data"));
	if (!ped) return FALSE;

	list = gtk_container_get_children(GTK_CONTAINER(ped->vbox));
	work = list;
	while (!ret && work)
		{
		auto entry = static_cast<GtkWidget *>(work->data);
		auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(entry), "entry_data"));
		work = work->next;

		if (ee->editable && gtk_widget_has_focus(ee->value_widget)) ret = gtk_widget_event(ee->value_widget, event);
		}
	g_list_free(list);
	return ret;
}

static void bar_pane_exif_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto ped = static_cast<PaneExifData *>(data);
	if ((type & (NOTIFY_REREAD | NOTIFY_CHANGE | NOTIFY_METADATA)) && fd == ped->fd)
		{
		DEBUG_1("Notify pane_exif: %s %04x", fd->path, type);
		bar_pane_exif_update(ped);
		}
}


/*
 *-------------------------------------------------------------------
 * dnd
 *-------------------------------------------------------------------
 */

static GtkTargetEntry bar_pane_exif_drag_types[] = {
	{ const_cast<gchar *>(TARGET_APP_EXIF_ENTRY_STRING), GTK_TARGET_SAME_APP, TARGET_APP_EXIF_ENTRY },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
};
static gint n_exif_entry_drag_types = 2;

static GtkTargetEntry bar_pane_exif_drop_types[] = {
	{ const_cast<gchar *>(TARGET_APP_EXIF_ENTRY_STRING), GTK_TARGET_SAME_APP, TARGET_APP_EXIF_ENTRY },
	{ const_cast<gchar *>("text/plain"), 0, TARGET_TEXT_PLAIN }
};
static gint n_exif_entry_drop_types = 2;


static void bar_pane_exif_entry_dnd_get(GtkWidget *entry, GdkDragContext *,
				     GtkSelectionData *selection_data, guint info,
				     guint, gpointer)
{
	auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(entry), "entry_data"));

	switch (info)
		{
		case TARGET_APP_EXIF_ENTRY:
			gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data),
					       8, reinterpret_cast<const guchar *>(&entry), sizeof(entry));
			break;

		case TARGET_TEXT_PLAIN:
		default:
			gtk_selection_data_set_text(selection_data, ee->key, -1);
			break;
		}

}

static void bar_pane_exif_dnd_receive(GtkWidget *pane, GdkDragContext *,
					  gint x, gint y,
					  GtkSelectionData *selection_data, guint info,
					  guint, gpointer)
{
	PaneExifData *ped;
	GList *work, *list;
	gint pos;
	GtkWidget *new_entry = nullptr;

	ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!ped) return;

	switch (info)
		{
		case TARGET_APP_EXIF_ENTRY:
			new_entry = GTK_WIDGET(*(gpointer *)gtk_selection_data_get_data(selection_data));

			if (gtk_widget_get_parent(new_entry) && gtk_widget_get_parent(new_entry) != ped->vbox) bar_pane_exif_reparent_entry(new_entry, pane);

			break;
		default:
			/** @FIXME this needs a check for valid exif keys */
			new_entry = bar_pane_exif_add_entry(ped, reinterpret_cast<const gchar *>(gtk_selection_data_get_data(selection_data)), nullptr, TRUE, FALSE);
			break;
		}

	list = gtk_container_get_children(GTK_CONTAINER(ped->vbox));
	work = list;
	pos = 0;
	while (work)
		{
		gint nx, ny;
		auto entry = static_cast<GtkWidget *>(work->data);
		GtkAllocation allocation;
		work = work->next;

		if (entry == new_entry) continue;

		gtk_widget_get_allocation(entry, &allocation);

		if (gtk_widget_is_drawable(entry) &&
		    gtk_widget_translate_coordinates(pane, entry, x, y, &nx, &ny) &&
		    ny < allocation.height / 2) break;
		pos++;
		}
	g_list_free(list);

	gtk_box_reorder_child(GTK_BOX(ped->vbox), new_entry, pos);
}

static void bar_pane_exif_entry_dnd_begin(GtkWidget *entry, GdkDragContext *context, gpointer)
{
	auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(entry), "entry_data"));

	if (!ee) return;
	dnd_set_drag_label(entry, context, ee->key);
}

static void bar_pane_exif_entry_dnd_end(GtkWidget *, GdkDragContext *, gpointer)
{
}

static void bar_pane_exif_entry_dnd_init(GtkWidget *entry)
{
	auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(entry), "entry_data"));

	gtk_drag_source_set(entry, static_cast<GdkModifierType>(GDK_BUTTON1_MASK | GDK_BUTTON2_MASK),
			    bar_pane_exif_drag_types, n_exif_entry_drag_types,
			    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	g_signal_connect(G_OBJECT(entry), "drag_data_get",
			 G_CALLBACK(bar_pane_exif_entry_dnd_get), ee);

	g_signal_connect(G_OBJECT(entry), "drag_begin",
			 G_CALLBACK(bar_pane_exif_entry_dnd_begin), ee);
	g_signal_connect(G_OBJECT(entry), "drag_end",
			 G_CALLBACK(bar_pane_exif_entry_dnd_end), ee);
}

static void bar_pane_exif_dnd_init(GtkWidget *pane)
{
	gtk_drag_dest_set(pane,
			  static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT | GTK_DEST_DEFAULT_DROP),
			  bar_pane_exif_drop_types, n_exif_entry_drop_types,
			  static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE));
	g_signal_connect(G_OBJECT(pane), "drag_data_received",
			 G_CALLBACK(bar_pane_exif_dnd_receive), NULL);
}

static void bar_pane_exif_edit_close_cb(GtkWidget *, gpointer data)
{
	auto gd = static_cast<GenericDialog *>(data);
	generic_dialog_close(gd);
}

static void bar_pane_exif_edit_destroy_cb(GtkWidget *, gpointer data)
{
	auto cdd = static_cast<ConfDialogData *>(data);
	g_signal_handlers_disconnect_by_func(cdd->widget, (gpointer)(bar_pane_exif_edit_close_cb), cdd->gd);
	g_free(cdd);
}

static void bar_pane_exif_edit_cancel_cb(GenericDialog *, gpointer)
{
}

static void bar_pane_exif_edit_ok_cb(GenericDialog *, gpointer data)
{
	auto cdd = static_cast<ConfDialogData *>(data);

	/* either one or the other */
	auto ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(cdd->widget), "pane_data"));
	auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(cdd->widget), "entry_data"));

	if (ped)
		{
		bar_pane_exif_add_entry(ped,
					gtk_entry_get_text(GTK_ENTRY(cdd->key_entry)),
					gtk_entry_get_text(GTK_ENTRY(cdd->title_entry)),
					cdd->if_set, cdd->editable);
		}

	if (ee)
		{
		const gchar *title;
		GtkWidget *pane = gtk_widget_get_parent(cdd->widget);

		while (pane)
			{
			ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
			if (ped) break;
			pane = gtk_widget_get_parent(pane);
			}

		if (!pane) return;

		g_free(ee->key);
		ee->key = g_strdup(gtk_entry_get_text(GTK_ENTRY(cdd->key_entry)));
		title = gtk_entry_get_text(GTK_ENTRY(cdd->title_entry));
		if (!title || strlen(title) == 0)
			{
			g_free(ee->title);
			ee->title = exif_get_description_by_key(ee->key);
			ee->auto_title = TRUE;
			}
		else if (!ee->title || strcmp(ee->title, title) != 0)
			{
			g_free(ee->title);
			ee->title = g_strdup(title);
			ee->auto_title = FALSE;
			}

		ee->if_set = cdd->if_set;
		ee->editable = cdd->editable;

		bar_pane_exif_setup_entry_box(ped, ee);

		bar_pane_exif_entry_update_title(ee);
		bar_pane_exif_update(ped);
		}
}

static void bar_pane_exif_conf_dialog(GtkWidget *widget)
{
	ConfDialogData *cdd;
	GenericDialog *gd;
	GtkWidget *table;

	/* the widget can be either ExifEntry (for editing) or Pane (for new entry)
	   we can decide it by the attached data */
	auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(widget), "entry_data"));

	cdd = g_new0(ConfDialogData, 1);

	cdd->widget = widget;


	cdd->if_set = ee ? ee->if_set : TRUE;
	cdd->editable = ee ? ee->editable : FALSE;

	cdd->gd = gd = generic_dialog_new(ee ? _("Configure entry") : _("Add entry"), "exif_entry_edit",
				widget, TRUE,
				bar_pane_exif_edit_cancel_cb, cdd);
	g_signal_connect(G_OBJECT(gd->dialog), "destroy",
			 G_CALLBACK(bar_pane_exif_edit_destroy_cb), cdd);

	/* in case the entry is deleted during editing */
	g_signal_connect(G_OBJECT(widget), "destroy",
			 G_CALLBACK(bar_pane_exif_edit_close_cb), gd);

	generic_dialog_add_message(gd, nullptr, ee ? _("Configure entry") : _("Add entry"), nullptr, FALSE);

	generic_dialog_add_button(gd, GQ_ICON_OK, "OK",
				  bar_pane_exif_edit_ok_cb, TRUE);

	table = pref_table_new(gd->vbox, 3, 2, FALSE, TRUE);
	pref_table_label(table, 0, 0, _("Key:"), GTK_ALIGN_END);

	cdd->key_entry = gtk_entry_new();
	gtk_widget_set_size_request(cdd->key_entry, 300, -1);
	if (ee) gtk_entry_set_text(GTK_ENTRY(cdd->key_entry), ee->key);
	gtk_table_attach_defaults(GTK_TABLE(table), cdd->key_entry, 1, 2, 0, 1);
	generic_dialog_attach_default(gd, cdd->key_entry);
	gtk_widget_show(cdd->key_entry);

	pref_table_label(table, 0, 1, _("Title:"), GTK_ALIGN_END);

	cdd->title_entry = gtk_entry_new();
	gtk_widget_set_size_request(cdd->title_entry, 300, -1);
	if (ee) gtk_entry_set_text(GTK_ENTRY(cdd->title_entry), ee->title);
	gtk_table_attach_defaults(GTK_TABLE(table), cdd->title_entry, 1, 2, 1, 2);
	generic_dialog_attach_default(gd, cdd->title_entry);
	gtk_widget_show(cdd->title_entry);

	pref_checkbox_new_int(gd->vbox, _("Show only if set"), cdd->if_set, &cdd->if_set);
	pref_checkbox_new_int(gd->vbox, _("Editable (supported only for XMP)"), cdd->editable, &cdd->editable);

	gtk_widget_show(gd->dialog);
}

static void bar_pane_exif_conf_dialog_cb(GtkWidget *, gpointer data)
{
	auto widget = static_cast<GtkWidget *>(data);
	bar_pane_exif_conf_dialog(widget);
}

static void bar_pane_exif_delete_entry_cb(GtkWidget *, gpointer data)
{
	auto entry = static_cast<GtkWidget *>(data);
	gtk_widget_destroy(entry);
}

static void bar_pane_exif_copy_entry_cb(GtkWidget *, gpointer data)
{
	auto widget = static_cast<GtkWidget *>(data);
	GtkClipboard *clipboard;
	const gchar *value;
	ExifEntry *ee;

	ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(widget), "entry_data"));
	value = gtk_label_get_text(GTK_LABEL(ee->value_widget));
	clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text(clipboard, value, -1);
}

static void bar_pane_exif_toggle_show_all_cb(GtkWidget *, gpointer data)
{
	auto ped = static_cast<PaneExifData *>(data);
	ped->show_all = !ped->show_all;
	bar_pane_exif_update(ped);
}

static void bar_pane_exif_menu_popup(GtkWidget *widget, PaneExifData *ped)
{
	GtkWidget *menu;
	/* the widget can be either ExifEntry (for editing) or Pane (for new entry)
	   we can decide it by the attached data */
	auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(widget), "entry_data"));

	menu = popup_menu_short_lived();

	if (ee)
		{
		/* for the entry */
		gchar *conf = g_strdup_printf(_("Configure \"%s\""), ee->title);
		gchar *del = g_strdup_printf(_("Remove \"%s\""), ee->title);
		gchar *copy = g_strdup_printf(_("Copy \"%s\""), ee->title);

		menu_item_add_icon(menu, conf, GQ_ICON_EDIT, G_CALLBACK(bar_pane_exif_conf_dialog_cb), widget);
		menu_item_add_icon(menu, del, GQ_ICON_DELETE, G_CALLBACK(bar_pane_exif_delete_entry_cb), widget);
		menu_item_add_icon(menu, copy, GQ_ICON_COPY, G_CALLBACK(bar_pane_exif_copy_entry_cb), widget);
		menu_item_add_divider(menu);

		g_free(conf);
		g_free(del);
		}

	/* for the pane */
	menu_item_add_icon(menu, _("Add entry"), GQ_ICON_ADD, G_CALLBACK(bar_pane_exif_conf_dialog_cb), ped->widget);
	menu_item_add_check(menu, _("Show hidden entries"), ped->show_all, G_CALLBACK(bar_pane_exif_toggle_show_all_cb), ped);

	gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
}

static gboolean bar_pane_exif_menu_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer data)
{
	auto ped = static_cast<PaneExifData *>(data);
	if (bevent->button == MOUSE_BUTTON_RIGHT)
		{
		bar_pane_exif_menu_popup(widget, ped);
		return TRUE;
		}
	return FALSE;
}

static gboolean bar_pane_exif_copy_cb(GtkWidget *widget, GdkEventButton *bevent, gpointer)
{
	const gchar *value;
	GtkClipboard *clipboard;
	ExifEntry *ee;

	if (bevent->button == MOUSE_BUTTON_LEFT)
		{
		ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(widget), "entry_data"));
		value = gtk_label_get_text(GTK_LABEL(ee->value_widget));
		clipboard = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
		gtk_clipboard_set_text(clipboard, value, -1);

		return TRUE;
		}

	return FALSE;
}



static void bar_pane_exif_entry_write_config(GtkWidget *entry, GString *outstr, gint indent)
{
	auto ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(entry), "entry_data"));
	if (!ee) return;

	WRITE_NL(); WRITE_STRING("<entry ");
	WRITE_CHAR(*ee, key);
	if (!ee->auto_title) WRITE_CHAR(*ee, title);
	WRITE_BOOL(*ee, if_set);
	WRITE_BOOL(*ee, editable);
	WRITE_STRING("/>");
}

static void bar_pane_exif_write_config(GtkWidget *pane, GString *outstr, gint indent)
{
	PaneExifData *ped;
	GList *work, *list;

	ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!ped) return;

	WRITE_NL(); WRITE_STRING("<pane_exif ");
	write_char_option(outstr, indent, "id", ped->pane.id);
	write_char_option(outstr, indent, "title", gtk_label_get_text(GTK_LABEL(ped->pane.title)));
	WRITE_BOOL(ped->pane, expanded);
	WRITE_BOOL(*ped, show_all);
	WRITE_STRING(">");
	indent++;

	list = gtk_container_get_children(GTK_CONTAINER(ped->vbox));
	work = list;
	while (work)
		{
		auto entry = static_cast<GtkWidget *>(work->data);
		work = work->next;

		bar_pane_exif_entry_write_config(entry, outstr, indent);
		}
	g_list_free(list);
	indent--;
	WRITE_NL(); WRITE_STRING("</pane_exif>");
}

GList * bar_pane_exif_list()
{
	PaneExifData *ped;
	GList *list;
	GList *work_windows;
	GList *exif_list = nullptr;
	LayoutWindow *lw;
	GtkWidget *bar;
	GtkWidget *pane;
	GtkWidget *entry;
	ExifEntry *ee;

	work_windows = layout_window_list;
	lw = static_cast<LayoutWindow *>(work_windows->data);
	bar = lw->bar;
	pane = bar_find_pane_by_id(bar, PANE_EXIF, "exif");
	if (pane)
		{
		ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));

		list = gtk_container_get_children(GTK_CONTAINER(ped->vbox));
		GList *work = list;
		while (work)
			{
			entry = static_cast<GtkWidget *>(work->data);
			work = work->next;
			ee = static_cast<ExifEntry *>(g_object_get_data(G_OBJECT(entry), "entry_data"));
			exif_list = g_list_append(exif_list, g_strdup(ee->title));
			exif_list = g_list_append(exif_list, g_strdup(ee->key));
			}

		g_list_free(list);
		}
	return exif_list;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
void bar_pane_exif_close_unused(GtkWidget *widget)
{
	PaneExifData *ped;

	ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(widget), "pane_data"));
	if (!ped) return;

	gtk_widget_destroy(ped->vbox);
}
#pragma GCC diagnostic pop

static void bar_pane_exif_destroy(GtkWidget *, gpointer data)
{
	auto ped = static_cast<PaneExifData *>(data);

	file_data_unregister_notify_func(bar_pane_exif_notify_cb, ped);
	g_object_unref(ped->size_group);
	file_data_unref(ped->fd);
	g_free(ped->pane.id);
	g_free(ped);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
static void bar_pane_exif_size_request_unused(GtkWidget *, GtkRequisition *requisition, gpointer data)
{
	PaneExifData *ped = static_cast<PaneExifData *>(data);
	if (requisition->height < ped->min_height)
		{
		requisition->height = ped->min_height;
		}
}
#pragma GCC diagnostic pop

static void bar_pane_exif_size_allocate(GtkWidget *, GtkAllocation *alloc, gpointer data)
{
	auto ped = static_cast<PaneExifData *>(data);
	ped->min_height = alloc->height;
	gtk_widget_set_size_request(ped->widget, -1, ped->min_height);
}

static GtkWidget *bar_pane_exif_new(const gchar *id, const gchar *title, gboolean expanded, gboolean show_all)
{
	PaneExifData *ped;

	ped = g_new0(PaneExifData, 1);

	ped->pane.pane_set_fd = bar_pane_exif_set_fd;
	ped->pane.pane_write_config = bar_pane_exif_write_config;
	ped->pane.pane_event = bar_pane_exif_event;
	ped->pane.title = bar_pane_expander_title(title);
	ped->pane.id = g_strdup(id);
	ped->pane.expanded = expanded;
	ped->pane.type = PANE_EXIF;
	ped->show_all = show_all;

	ped->size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
	ped->widget = gtk_event_box_new();
	ped->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_container_add(GTK_CONTAINER(ped->widget), ped->vbox);
	gtk_widget_show(ped->vbox);

	ped->min_height = MIN_HEIGHT;
	g_object_set_data(G_OBJECT(ped->widget), "pane_data", ped);
	g_signal_connect_after(G_OBJECT(ped->widget), "destroy",
			       G_CALLBACK(bar_pane_exif_destroy), ped);
	gtk_widget_set_size_request(ped->widget, -1, ped->min_height);
	g_signal_connect(G_OBJECT(ped->widget), "size-allocate",
			 G_CALLBACK(bar_pane_exif_size_allocate), ped);

	bar_pane_exif_dnd_init(ped->widget);
	g_signal_connect(ped->widget, "button_release_event", G_CALLBACK(bar_pane_exif_menu_cb), ped);

	file_data_register_notify_func(bar_pane_exif_notify_cb, ped, NOTIFY_PRIORITY_LOW);

	gtk_widget_show(ped->widget);

	return ped->widget;
}

GtkWidget *bar_pane_exif_new_from_config(const gchar **attribute_names, const gchar **attribute_values)
{
	gchar *title = nullptr;
	gchar *id = g_strdup("exif");
	gboolean expanded = TRUE;
	gboolean show_all = FALSE;
	GtkWidget *ret;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("id", id)) continue;
		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_BOOL_FULL("expanded", expanded)) continue;
		if (READ_BOOL_FULL("show_all", show_all)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	bar_pane_translate_title(PANE_EXIF, id, &title);
	ret = bar_pane_exif_new(id, title, expanded, show_all);
	g_free(title);
	g_free(id);
	return ret;
}

void bar_pane_exif_update_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values)
{
	PaneExifData *ped;
	gchar *title = nullptr;

	ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!ped) return;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_BOOL_FULL("expanded", ped->pane.expanded)) continue;
		if (READ_BOOL_FULL("show_all", ped->show_all)) continue;
		if (READ_CHAR_FULL("id", ped->pane.id)) continue;


		log_printf("unknown attribute %s = %s\n", option, value);
		}

	if (title)
		{
		bar_pane_translate_title(PANE_EXIF, ped->pane.id, &title);
		gtk_label_set_text(GTK_LABEL(ped->pane.title), title);
		g_free(title);
		}

	bar_update_expander(pane);
	bar_pane_exif_update(ped);
}


void bar_pane_exif_entry_add_from_config(GtkWidget *pane, const gchar **attribute_names, const gchar **attribute_values)
{
	PaneExifData *ped;
	gchar *key = nullptr;
	gchar *title = nullptr;
	gboolean if_set = TRUE;
	gboolean editable = FALSE;

	ped = static_cast<PaneExifData *>(g_object_get_data(G_OBJECT(pane), "pane_data"));
	if (!ped) return;

	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR_FULL("key", key)) continue;
		if (READ_CHAR_FULL("title", title)) continue;
		if (READ_BOOL_FULL("if_set", if_set)) continue;
		if (READ_BOOL_FULL("editable", editable)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}

	if (key && key[0]) bar_pane_exif_add_entry(ped, key, title, if_set, editable);
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
