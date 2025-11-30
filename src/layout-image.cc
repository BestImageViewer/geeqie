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

#include "layout-image.h"

#include <array>
#include <cstring>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <pango/pango.h>

#include <config.h>

#include "archives.h"
#include "collect.h"
#include "compat-deprecated.h"
#include "dnd.h"
#include "editors.h"
#include "exif.h"
#include "filedata.h"
#include "fullscreen.h"
#include "history-list.h"
#include "image-overlay.h"
#include "image.h"
#include "img-view.h"
#include "intl.h"
#include "layout-util.h"
#include "layout.h"
#include "main-defines.h"
#include "menu.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "pixbuf-renderer.h"
#include "slideshow.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-utildlg.h"
#include "uri-utils.h"
#include "utilops.h"
#include "view-file.h"

namespace
{

constexpr gint IMAGE_MIN_WIDTH = 100;

} // namespace

static GtkWidget *layout_image_pop_menu(LayoutWindow *lw);
static void layout_image_set_buttons(LayoutWindow *lw);
static gboolean layout_image_animate_new_file(LayoutWindow *lw);
static void layout_image_animate_update_image(LayoutWindow *lw);

/*
 *----------------------------------------------------------------------------
 * full screen
 *----------------------------------------------------------------------------
 */

static void touchpad_zoom_cb(GtkGestureZoom *controller, double, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, gtk_gesture_zoom_get_scale_delta(controller) * image_zoom_get_real(lw->image), TRUE);
}

void layout_image_full_screen_start(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->full_screen) return;

	const auto layout_image_fullscreen_stop_func = [lw](FullScreenData *fs)
	{
		/* restore image window */
		if (lw->image == fs->imd)
			lw->image = fs->normal_imd;

		lw->full_screen = nullptr;
	};
	lw->full_screen = fullscreen_start(lw->window, lw->image,
	                                   layout_image_fullscreen_stop_func);

	/* set to new image window */
	if (lw->full_screen->same_region)
		lw->image = lw->full_screen->imd;

	layout_image_set_buttons(lw);

	layout_keyboard_init(lw, lw->full_screen->window);

	lw->touchpad_zoom = GTK_EVENT_CONTROLLER(gtk_gesture_zoom_new(lw->full_screen->window));
	g_signal_connect(lw->touchpad_zoom, "scale-changed", G_CALLBACK(touchpad_zoom_cb), lw);

	layout_actions_add_window(lw, lw->full_screen->window);

	image_osd_copy_status(lw->full_screen->normal_imd, lw->image);
	layout_image_animate_update_image(lw);

	/** @FIXME This is a hack to fix #1037 Fullscreen loads black
	 * The problem occurs when zoom is set to Original Size.
	 * An extra reload is required to force the image to be displayed.
	 * See also image-view.cc real_view_window_new()
	 * This is probably not the correct solution.
	 **/
	image_reload(lw->image);
}

void layout_image_full_screen_stop(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;
	if (!lw->full_screen) return;

	if (lw->image == lw->full_screen->imd)
		image_osd_copy_status(lw->image, lw->full_screen->normal_imd);

	fullscreen_stop(lw->full_screen);

	g_object_unref(lw->touchpad_zoom);

	layout_image_animate_update_image(lw);
}

void layout_image_full_screen_toggle(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;
	if (lw->full_screen)
		{
		layout_image_full_screen_stop(lw);
		}
	else
		{
		layout_image_full_screen_start(lw);
		}
}

gboolean layout_image_full_screen_active(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return (lw->full_screen != nullptr);
}

/*
 *----------------------------------------------------------------------------
 * slideshow
 *----------------------------------------------------------------------------
 */

static void layout_image_slideshow_next(LayoutWindow *lw)
{
	if (lw->slideshow) lw->slideshow->next();
}

static void layout_image_slideshow_prev(LayoutWindow *lw)
{
	if (lw->slideshow) lw->slideshow->prev();
}

static void layout_image_slideshow_stop_func(LayoutWindow *lw)
{
	lw->slideshow = nullptr;
	layout_status_update_info(lw, nullptr);
}

void layout_image_slideshow_start(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;
	if (lw->slideshow) return;

	const auto slideshow_stop_func = [lw](SlideShow *){ layout_image_slideshow_stop_func(lw); };

	CollectInfo *info;
	CollectionData *cd = image_get_collection(lw->image, &info);

	if (cd && info)
		{
		lw->slideshow = SlideShow::start_from_collection(lw, nullptr, cd, info, slideshow_stop_func);
		}
	else
		{
		lw->slideshow = SlideShow::start(lw, slideshow_stop_func);
		}

	layout_status_update_info(lw, nullptr);
}

/* note that slideshow will take ownership of the list, do not free it */
void layout_image_slideshow_start_from_list(LayoutWindow *lw, GList *list)
{
	if (!layout_valid(&lw)) return;

	if (lw->slideshow || !list)
		{
		file_data_list_free(list);
		return;
		}

	lw->slideshow = SlideShow::start_from_filelist(lw, nullptr, list,
	                                               [lw](SlideShow *){ layout_image_slideshow_stop_func(lw); });

	layout_status_update_info(lw, nullptr);
}

void layout_image_slideshow_stop(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	delete lw->slideshow; /* the stop_func sets lw->slideshow to nullptr for us */
}

void layout_image_slideshow_toggle(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->slideshow)
		{
		layout_image_slideshow_stop(lw);
		}
	else
		{
		layout_image_slideshow_start(lw);
		}
}

gboolean layout_image_slideshow_active(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return (lw->slideshow != nullptr);
}

void layout_image_slideshow_pause_toggle(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if (lw->slideshow) lw->slideshow->pause_toggle();

	layout_status_update_info(lw, nullptr);
}

gboolean layout_image_slideshow_paused(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return lw->slideshow->is_paused();
}

static gboolean layout_image_slideshow_continue_check(LayoutWindow *lw)
{
	if (!lw->slideshow) return FALSE;

	if (!lw->slideshow->should_continue())
		{
		layout_image_slideshow_stop(lw);
		return FALSE;
		}

	return TRUE;
}

/*
 *----------------------------------------------------------------------------
 * Animation
 *----------------------------------------------------------------------------
 */

struct AnimationData
{
	ImageWindow *iw;
	LayoutWindow *lw;
	GdkPixbufAnimation *gpa;
	GdkPixbufAnimationIter *iter;
	GdkPixbuf *gpb;
	FileData *data_adr;
	gint delay;
	gboolean valid;
	GCancellable *cancellable;
	GFile *in_file;
	GFileInputStream *gfstream;
};

static void image_animation_data_free(AnimationData *fd)
{
	if(!fd) return;
	if(fd->iter) g_object_unref(fd->iter);
	if(fd->gpa) g_object_unref(fd->gpa);
	if(fd->cancellable) g_object_unref(fd->cancellable);
	g_free(fd);
}

static gboolean animation_should_continue(AnimationData *fd)
{
	return fd->valid;
}

static gboolean show_next_frame(gpointer data)
{
	auto fd = static_cast<AnimationData*>(data);
	int delay;

	if(animation_should_continue(fd)==FALSE)
		{
		image_animation_data_free(fd);
		return G_SOURCE_REMOVE;
		}

	PixbufRenderer *pr = PIXBUF_RENDERER(fd->iw->pr);

	if (!gq_gdk_pixbuf_animation_iter_advance(fd->iter, nullptr))
		{
		/* This indicates the animation is complete.
		   Return FALSE here to disable looping. */
		}

	fd->gpb = gq_gdk_pixbuf_animation_iter_get_pixbuf(fd->iter);
	image_change_pixbuf(fd->iw,fd->gpb,pr->zoom,FALSE);

	if (fd->iw->func_update)
		fd->iw->func_update(fd->iw, fd->iw->data_update);

	delay = gq_gdk_pixbuf_animation_iter_get_delay_time(fd->iter);
	if (delay!=fd->delay)
		{
		if (delay>0) /* Current frame not static. */
			{
			fd->delay=delay;
			g_timeout_add(delay, show_next_frame, fd);
			}
		else
			{
			image_animation_data_free(fd);
			}
		return G_SOURCE_REMOVE;
		}

	return G_SOURCE_CONTINUE;
}

static gboolean layout_image_animate_check(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	if(!lw->options.animate || lw->image->image_fd == nullptr || lw->image->image_fd->extension == nullptr || (g_ascii_strcasecmp(lw->image->image_fd->extension,".GIF")!=0 && g_ascii_strcasecmp(lw->image->image_fd->extension,".WEBP")!=0))
		{
		if(lw->animation)
			{
			lw->animation->valid = FALSE;
			if (lw->animation->cancellable)
				{
				g_cancellable_cancel(lw->animation->cancellable);
				}
			lw->animation = nullptr;
			}
		return FALSE;
		}

	return TRUE;
}

static void layout_image_animate_update_image(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	if(lw->options.animate && lw->animation)
		{
		if (lw->full_screen && lw->image != lw->full_screen->imd)
			lw->animation->iw = lw->full_screen->imd;
		else
			lw->animation->iw = lw->image;
		}
}


static void animation_async_ready_cb(GObject *, GAsyncResult *res, gpointer data)
{
	auto animation = static_cast<AnimationData *>(data);

	if (!animation) return;

	if (g_cancellable_is_cancelled(animation->cancellable))
		{
		gq_gdk_pixbuf_animation_new_from_stream_finish(res, nullptr);
		g_object_unref(animation->in_file);
		g_object_unref(animation->gfstream);
		image_animation_data_free(animation);
		return;
		}

	g_autoptr(GError) error = nullptr;
	animation->gpa = gq_gdk_pixbuf_animation_new_from_stream_finish(res, &error);
	if (animation->gpa)
		{
		if (!gq_gdk_pixbuf_animation_is_static_image(animation->gpa))
			{
			animation->iter = gq_gdk_pixbuf_animation_get_iter(animation->gpa, nullptr);
			if (animation->iter)
				{
				animation->data_adr = animation->lw->image->image_fd;
				animation->delay = gq_gdk_pixbuf_animation_iter_get_delay_time(animation->iter);
				animation->valid = TRUE;

				layout_image_animate_update_image(animation->lw);

				g_timeout_add(animation->delay, show_next_frame, animation);
				}
			}
		}
	else
		{
		log_printf("Error reading GIF file: %s\n", error->message);
		}

	g_object_unref(animation->in_file);
	g_object_unref(animation->gfstream);
}

static gboolean layout_image_animate_new_file(LayoutWindow *lw)
{
	GFileInputStream *gfstream;
	AnimationData *animation;
	GFile *in_file;

	if(!layout_image_animate_check(lw)) return FALSE;

	if(lw->animation) lw->animation->valid = FALSE;

	if (lw->animation)
		{
		g_cancellable_cancel(lw->animation->cancellable);
		}

	animation = g_new0(AnimationData, 1);
	lw->animation = animation;
	animation->lw = lw;
	animation->cancellable = g_cancellable_new();

	in_file = g_file_new_for_path(lw->image->image_fd->path);
	animation->in_file = in_file;
	g_autoptr(GError) error = nullptr;
	gfstream = g_file_read(in_file, nullptr, &error);
	if (gfstream)
		{
		animation->gfstream = gfstream;
		gq_gdk_pixbuf_animation_new_from_stream_async(G_INPUT_STREAM(gfstream), animation->cancellable, animation_async_ready_cb, animation);
		}
	else
		{
		log_printf("Error reading animation file: %s\nError: %s\n", lw->image->image_fd->path, error->message);
		}

	return TRUE;
}

void layout_image_animate_toggle(LayoutWindow *lw)
{
	GtkAction *action;

	if (!lw) return;

	lw->options.animate = !lw->options.animate;

	action = gq_gtk_action_group_get_action(lw->action_group, "Animate");
	gq_gtk_toggle_action_set_active(GQ_GTK_TOGGLE_ACTION(action), lw->options.animate);

	layout_image_animate_new_file(lw);
}

/*
 *----------------------------------------------------------------------------
 * pop-up menus
 *----------------------------------------------------------------------------
 */

static void li_pop_menu_zoom_in_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_adjust(lw, get_zoom_increment(), FALSE);
}

static void li_pop_menu_zoom_out_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_image_zoom_adjust(lw, -get_zoom_increment(), FALSE);
}

template<int value>
static void li_pop_menu_zoom_set_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_zoom_set(lw, value, FALSE);
}

static void li_pop_menu_edit_cb(GtkWidget *widget, gpointer data)
{
	LayoutWindow *lw;
	auto key = static_cast<const gchar *>(data);

	lw = static_cast<LayoutWindow *>(submenu_item_get_data(widget));

	if (!editor_window_flag_set(key))
		{
		layout_image_full_screen_stop(lw);
		}
	file_util_start_editor_from_file(key, layout_image_get_fd(lw), lw->window);
}

static void li_pop_menu_alter_cb(GtkWidget *widget, gpointer data)
{
	auto *lw = static_cast<LayoutWindow *>(submenu_item_get_data(widget));
	auto type = static_cast<AlterType>GPOINTER_TO_INT(data);

	image_alter_orientation(lw->image, lw->image->image_fd, type);
}

static void li_pop_menu_new_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	view_window_new(layout_image_get_fd(lw));
}

static GtkWidget *li_pop_menu_click_parent(GtkWidget *widget, LayoutWindow *lw)
{
	GtkWidget *menu;
	GtkWidget *parent;

	menu = gtk_widget_get_toplevel(widget);
	if (!menu) return nullptr;

	parent = static_cast<GtkWidget *>(g_object_get_data(G_OBJECT(menu), "click_parent"));

	if (!parent && lw->full_screen)
		{
		parent = lw->full_screen->imd->widget;
		}

	return parent;
}

static void li_pop_menu_copy_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_copy(layout_image_get_fd(lw), nullptr, nullptr,
		       li_pop_menu_click_parent(widget, lw));
}

template<gboolean quoted>
static void li_pop_menu_copy_path_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_copy_path_to_clipboard(layout_image_get_fd(lw), quoted, ClipboardAction::COPY);
}

static void li_pop_menu_cut_path_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_copy_path_to_clipboard(layout_image_get_fd(lw), FALSE, ClipboardAction::CUT);
}

#if HAVE_GTK4
static void li_pop_menu_copy_image_cb(GtkWidget *, gpointer data)
{
/* @FIXME GTK4 stub */
}
#else
static void li_pop_menu_copy_image_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	ImageWindow *imd = lw->image;

	GdkPixbuf *pixbuf;
	pixbuf = image_get_pixbuf(imd);
	if (!pixbuf) return;
	gtk_clipboard_set_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), pixbuf);
}
#endif

static void li_pop_menu_move_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_move(layout_image_get_fd(lw), nullptr, nullptr,
		       li_pop_menu_click_parent(widget, lw));
}

static void li_pop_menu_rename_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_rename(layout_image_get_fd(lw), nullptr,
			 li_pop_menu_click_parent(widget, lw));
}

template<gboolean safe_delete>
static void li_pop_menu_delete_cb(GtkWidget *widget, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	file_util_delete(layout_image_get_fd(lw), nullptr,
	                 li_pop_menu_click_parent(widget, lw), safe_delete);
}

static void li_pop_menu_slide_start_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_slideshow_start(lw);
}

static void li_pop_menu_slide_stop_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_slideshow_stop(lw);
}

static void li_pop_menu_slide_pause_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_slideshow_pause_toggle(lw);
}

static void li_pop_menu_full_screen_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_full_screen_toggle(lw);
}

static void li_pop_menu_animate_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_image_animate_toggle(lw);
}

static void li_pop_menu_hide_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	layout_tools_hide_toggle(lw);
}

static void li_set_layout_path_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd;

	if (!layout_valid(&lw)) return;

	fd = layout_image_get_fd(lw);
	if (fd) layout_set_fd(lw, fd);
}

static void li_open_archive_cb(GtkWidget *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (!layout_valid(&lw)) return;

	g_autofree gchar *dest_dir = open_archive(layout_image_get_fd(lw));
	if (!dest_dir)
		{
		warning_dialog(_("Cannot open archive file"), _("See the Log Window"), GQ_ICON_DIALOG_WARNING, nullptr);
		return;
		}

	LayoutWindow *lw_new = layout_new_from_default();
	layout_set_path(lw_new, dest_dir);
}

static gboolean li_check_if_current_path(LayoutWindow *lw, const gchar *path)
{
	if (!path || !layout_valid(&lw) || !lw->dir_fd) return FALSE;

	g_autofree gchar *dirname = g_path_get_dirname(path);
	return strcmp(lw->dir_fd->path, dirname) == 0;
}

static GList *layout_image_get_fd_list(LayoutWindow *lw)
{
	GList *list = nullptr;
	FileData *fd = layout_image_get_fd(lw);

	if (fd)
		{
		if (lw->vf)
			/* optionally include sidecars if the filelist entry is not expanded */
			list = vf_selection_get_one(lw->vf, fd);
		else
			list = g_list_append(nullptr, file_data_ref(fd));
		}

	return list;
}

/**
 * @brief Add file selection list to a collection
 * @param[in] widget
 * @param[in] data Index to the collection list menu item selected, or -1 for new collection
 *
 *
 */
static void layout_pop_menu_collections_cb(GtkWidget *widget, gpointer data)
{
	auto *lw = static_cast<LayoutWindow *>(submenu_item_get_data(widget));

	g_autoptr(FileDataList) selection_list = g_list_append(nullptr, layout_image_get_fd(lw));
	collection_by_index_add_filelist(GPOINTER_TO_INT(data), selection_list);
}

static void li_pop_menu_selectable_toolbars_toggle_cb(GtkWidget *, gpointer)
{
	current_layout_selectable_toolbars_toggle();
}

static GtkWidget *layout_image_pop_menu(LayoutWindow *lw)
{
	GtkWidget *item;
	GtkWidget *submenu;

	const gchar *path = layout_image_get_path(lw);
	gboolean has_path = path != nullptr;
	gboolean fullscreen = layout_image_full_screen_active(lw);

	GtkWidget *menu = popup_menu_short_lived();

	GtkAccelGroup *accel_group = gtk_accel_group_new();
	gtk_menu_set_accel_group(GTK_MENU(menu), accel_group);

	g_object_set_data(G_OBJECT(menu), "accel_group", accel_group);

	menu_item_add_icon(menu, _("Zoom _in"), GQ_ICON_ZOOM_IN, G_CALLBACK(li_pop_menu_zoom_in_cb), lw);
	menu_item_add_icon(menu, _("Zoom _out"), GQ_ICON_ZOOM_OUT, G_CALLBACK(li_pop_menu_zoom_out_cb), lw);
	menu_item_add_icon(menu, _("Zoom _1:1"), GQ_ICON_ZOOM_100, G_CALLBACK(li_pop_menu_zoom_set_cb<1>), lw);
	menu_item_add_icon(menu, _("Zoom to fit"), GQ_ICON_ZOOM_FIT, G_CALLBACK(li_pop_menu_zoom_set_cb<0>), lw);
	menu_item_add_divider(menu);

	GList *editmenu_fd_list = layout_image_get_fd_list(lw);
	g_signal_connect_swapped(G_OBJECT(menu), "destroy",
	                         G_CALLBACK(file_data_list_free), editmenu_fd_list);
	submenu = submenu_add_edit(menu, has_path, editmenu_fd_list, G_CALLBACK(li_pop_menu_edit_cb), lw);
	menu_item_add_divider(submenu);
	item = submenu_add_alter(menu, G_CALLBACK(li_pop_menu_alter_cb), lw);

	item = menu_item_add_icon(menu, _("View in _new window"), GQ_ICON_NEW, G_CALLBACK(li_pop_menu_new_cb), lw);
	gtk_widget_set_sensitive(item, has_path && !fullscreen);

	item = menu_item_add(menu, _("_Go to directory view"), G_CALLBACK(li_set_layout_path_cb), lw);
	gtk_widget_set_sensitive(item, has_path && !li_check_if_current_path(lw, path));

	item = menu_item_add_icon(menu, _("Open archive"), GQ_ICON_OPEN, G_CALLBACK(li_open_archive_cb), lw);
	gtk_widget_set_sensitive(item, has_path && lw->image->image_fd->format_class == FORMAT_CLASS_ARCHIVE);

	menu_item_add_divider(menu);

	item = menu_item_add_icon(menu, _("_Copy..."), GQ_ICON_COPY, G_CALLBACK(li_pop_menu_copy_cb), lw);
	gtk_widget_set_sensitive(item, has_path);
	item = menu_item_add(menu, _("_Move..."), G_CALLBACK(li_pop_menu_move_cb), lw);
	gtk_widget_set_sensitive(item, has_path);
	item = menu_item_add(menu, _("_Rename..."), G_CALLBACK(li_pop_menu_rename_cb), lw);
	gtk_widget_set_sensitive(item, has_path);
	item = menu_item_add(menu, _("_Copy to clipboard"),
	                     G_CALLBACK(li_pop_menu_copy_path_cb<TRUE>), lw);
	item = menu_item_add(menu, _("_Copy to clipboard (unquoted)"),
	                     G_CALLBACK(li_pop_menu_copy_path_cb<FALSE>), lw);
	item = menu_item_add(menu, _("Copy _image to clipboard"), G_CALLBACK(li_pop_menu_copy_image_cb), lw);
	item = menu_item_add(menu, _("Cut to clipboard"), G_CALLBACK(li_pop_menu_cut_path_cb), lw);
	gtk_widget_set_sensitive(item, has_path);
	menu_item_add_divider(menu);

	item = menu_item_add_icon(menu, options->file_ops.confirm_move_to_trash ?
	                              _("Move to Trash...") : _("Move to Trash"),
	                          GQ_ICON_DELETE,
	                          G_CALLBACK(li_pop_menu_delete_cb<TRUE>), lw);
	gtk_widget_set_sensitive(item, has_path);
	item = menu_item_add_icon(menu, options->file_ops.confirm_delete ?
	                              _("_Delete...") : _("_Delete"),
	                          GQ_ICON_DELETE_SHRED,
	                          G_CALLBACK(li_pop_menu_delete_cb<FALSE>), lw);
	gtk_widget_set_sensitive(item, has_path);
	menu_item_add_divider(menu);

	submenu = submenu_add_collections(menu, TRUE,
	                                  G_CALLBACK(layout_pop_menu_collections_cb), lw);
	menu_item_add_divider(menu);

	if (layout_image_slideshow_active(lw))
		{
		menu_item_add(menu, _("Toggle _slideshow"), G_CALLBACK(li_pop_menu_slide_stop_cb), lw);
		if (layout_image_slideshow_paused(lw))
			{
			item = menu_item_add(menu, _("Continue slides_how"),
					     G_CALLBACK(li_pop_menu_slide_pause_cb), lw);
			}
		else
			{
			item = menu_item_add(menu, _("Pause slides_how"),
					     G_CALLBACK(li_pop_menu_slide_pause_cb), lw);
			}
		}
	else
		{
		menu_item_add(menu, _("Toggle _slideshow"), G_CALLBACK(li_pop_menu_slide_start_cb), lw);
		item = menu_item_add(menu, _("Pause slides_how"), G_CALLBACK(li_pop_menu_slide_pause_cb), lw);
		gtk_widget_set_sensitive(item, FALSE);
		}

	if (!fullscreen)
		{
		menu_item_add_icon(menu, _("_Full screen"), GQ_ICON_FULLSCREEN, G_CALLBACK(li_pop_menu_full_screen_cb), lw);
		}
	else
		{
		menu_item_add_icon(menu, _("Exit _full screen"), GQ_ICON_LEAVE_FULLSCREEN, G_CALLBACK(li_pop_menu_full_screen_cb), lw);
		}

	menu_item_add_check(menu, _("GIF _animation"), lw->options.animate, G_CALLBACK(li_pop_menu_animate_cb), lw);

	menu_item_add_divider(menu);

	item = menu_item_add_check(menu, _("Hide file _list"), lw->options.tools_hidden,
				   G_CALLBACK(li_pop_menu_hide_cb), lw);

	item = menu_item_add_check(menu, _("Hide Selectable Bars"), lw->options.selectable_toolbars_hidden,
	                           G_CALLBACK(li_pop_menu_selectable_toolbars_toggle_cb), nullptr);
	gtk_widget_set_sensitive(item, !fullscreen);

	return menu;
}

void layout_image_menu_popup(LayoutWindow *lw)
{
	GtkWidget *menu;

	menu = layout_image_pop_menu(lw);
	gtk_menu_popup_at_widget(GTK_MENU(menu), lw->image->widget, GDK_GRAVITY_EAST, GDK_GRAVITY_CENTER, nullptr);
}

/*
 *----------------------------------------------------------------------------
 * dnd
 *----------------------------------------------------------------------------
 */

static void layout_image_dnd_receive(GtkWidget *widget, GdkDragContext *,
				     gint, gint,
				     GtkSelectionData *selection_data, guint info,
				     guint, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint i;


	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i] && lw->split_images[i]->pr == widget)
			break;
		}
	if (i < MAX_SPLIT_IMAGES)
		{
		DEBUG_1("dnd image activate %d", i);
		layout_image_activate(lw, i, FALSE);
		}

	if (info == TARGET_TEXT_PLAIN)
		{
		const auto *url = reinterpret_cast<const gchar *>(gtk_selection_data_get_data(selection_data));
		download_web_file(url, FALSE, lw);
		}
	else if (info == TARGET_URI_LIST || info == TARGET_APP_COLLECTION_MEMBER)
		{
		CollectionData *source;
		g_autoptr(FileDataList) list = nullptr;
		GList *info_list;

		if (info == TARGET_URI_LIST)
			{
			list = uri_filelist_from_gtk_selection_data(selection_data);
			source = nullptr;
			info_list = nullptr;
			}
		else
			{
			source = collection_from_dnd_data(reinterpret_cast<const gchar *>(gtk_selection_data_get_data(selection_data)), &list, &info_list);
			}

		if (list)
			{
			auto fd = static_cast<FileData *>(list->data);

			if (isfile(fd->path))
				{
				gint row;
				FileData *dir_fd;

				g_autofree gchar *base = remove_level_from_path(fd->path);
				dir_fd = file_data_new_dir(base);
				if (dir_fd != lw->dir_fd)
					{
					layout_set_fd(lw, dir_fd);
					}
				file_data_unref(dir_fd);

				row = layout_list_get_index(lw, fd);
				if (source && info_list)
					{
					layout_image_set_collection(lw, source, static_cast<CollectInfo *>(info_list->data));
					}
				else if (row == -1)
					{
					layout_image_set_fd(lw, fd);
					}
				else
					{
					layout_image_set_index(lw, row);
					}
				}
			else if (isdir(fd->path))
				{
				layout_set_fd(lw, fd);
				layout_image_set_fd(lw, nullptr);
				}
			}

		g_list_free(info_list);
		}
}

static void layout_image_dnd_get(GtkWidget *widget, GdkDragContext *,
				 GtkSelectionData *selection_data, guint,
				 guint, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	FileData *fd;
	gint i;


	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i] && lw->split_images[i]->pr == widget)
			break;
		}
	if (i < MAX_SPLIT_IMAGES)
		{
		DEBUG_1("dnd get from %d", i);
		fd = image_get_fd(lw->split_images[i]);
		}
	else
		fd = layout_image_get_fd(lw);

	if (fd)
		{
		GList *list;

		list = g_list_append(nullptr, fd);
		uri_selection_data_set_uris_from_filelist(selection_data, list);
		g_list_free(list);
		}
	else
		{
		gtk_selection_data_set(selection_data, gtk_selection_data_get_target(selection_data),
				       8, nullptr, 0);
		}
}

static void layout_image_dnd_end(GtkWidget *, GdkDragContext *context, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	if (gdk_drag_context_get_selected_action(context) == GDK_ACTION_MOVE)
		{
		FileData *fd;
		gint row;

		fd = layout_image_get_fd(lw);
		row = layout_list_get_index(lw, fd);
		if (row < 0) return;

		if (!isfile(fd->path))
			{
			if (static_cast<guint>(row) < layout_list_count(lw) - 1)
				{
				layout_image_next(lw);
				}
			else
				{
				layout_image_prev(lw);
				}
			}
		layout_refresh(lw);
		}
}

static void layout_image_dnd_init(LayoutWindow *lw, gint i)
{
	ImageWindow *imd = lw->split_images[i];

	gtk_drag_source_set(imd->pr, GDK_BUTTON2_MASK,
	                    dnd_file_drag_types.data(), dnd_file_drag_types.size(),
	                    static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	g_signal_connect(G_OBJECT(imd->pr), "drag_data_get",
			 G_CALLBACK(layout_image_dnd_get), lw);
	g_signal_connect(G_OBJECT(imd->pr), "drag_end",
			 G_CALLBACK(layout_image_dnd_end), lw);

	gtk_drag_dest_set(imd->pr,
	                  static_cast<GtkDestDefaults>(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP),
	                  dnd_file_drop_types.data(), dnd_file_drop_types.size(),
	                  static_cast<GdkDragAction>(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK));
	g_signal_connect(G_OBJECT(imd->pr), "drag_data_received",
			 G_CALLBACK(layout_image_dnd_receive), lw);
}


/*
 *----------------------------------------------------------------------------
 * misc
 *----------------------------------------------------------------------------
 */

void layout_image_to_root(LayoutWindow *lw)
{
	image_to_root_window(lw->image, (image_zoom_get(lw->image) == 0));
}

/*
 *----------------------------------------------------------------------------
 * manipulation + accessors
 *----------------------------------------------------------------------------
 */

void layout_image_scroll(LayoutWindow *lw, gint x, gint y, gboolean connect_scroll)
{
	gint i;
	if (!layout_valid(&lw)) return;

	image_scroll(lw->image, x, y);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_scroll(lw->full_screen->imd, x, y);
		}

	if (!connect_scroll) return;

	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i] && lw->split_images[i] != lw->image)
			{
			image_scroll(lw->split_images[i], x, y);
			}
		}

}

void layout_image_zoom_adjust(LayoutWindow *lw, gdouble increment, gboolean connect_zoom)
{
	gint i;
	if (!layout_valid(&lw)) return;

	image_zoom_adjust(lw->image, increment);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_zoom_adjust(lw->full_screen->imd, increment);
		}

	if (!connect_zoom) return;

	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i] && lw->split_images[i] != lw->image)
			image_zoom_adjust(lw->split_images[i], increment); ;
		}
}

void layout_image_zoom_adjust_at_point(LayoutWindow *lw, gdouble increment, gint x, gint y, gboolean connect_zoom)
{
	gint i;
	if (!layout_valid(&lw)) return;

	image_zoom_adjust_at_point(lw->image, increment, x, y);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_zoom_adjust_at_point(lw->full_screen->imd, increment, x, y);
		}
	if (!connect_zoom && !lw->split_mode) return;

	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i] && lw->split_images[i] != lw->image &&
						lw->split_images[i]->mouse_wheel_mode)
			image_zoom_adjust_at_point(lw->split_images[i], increment, x, y);
		}
}

void layout_image_zoom_set(LayoutWindow *lw, gdouble zoom, gboolean connect_zoom)
{
	gint i;
	if (!layout_valid(&lw)) return;

	image_zoom_set(lw->image, zoom);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_zoom_set(lw->full_screen->imd, zoom);
		}

	if (!connect_zoom) return;

	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i] && lw->split_images[i] != lw->image)
			image_zoom_set(lw->split_images[i], zoom);
		}
}

void layout_image_zoom_set_fill_geometry(LayoutWindow *lw, gboolean vertical, gboolean connect_zoom)
{
	gint i;
	if (!layout_valid(&lw)) return;

	image_zoom_set_fill_geometry(lw->image, vertical);

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_zoom_set_fill_geometry(lw->full_screen->imd, vertical);
		}

	if (!connect_zoom) return;

	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i] && lw->split_images[i] != lw->image)
			image_zoom_set_fill_geometry(lw->split_images[i], vertical);
		}
}

void layout_image_alter_orientation(LayoutWindow *lw, AlterType type)
{
	if (!layout_valid(&lw)) return;
	if (!lw || !lw->vf) return;

	vf_selection_foreach(lw->vf, [lw, type](FileData *fd_n) { image_alter_orientation(lw->image, fd_n, type); });
}

static void image_alter_rating(FileData *fd_n, const gchar *rating)
{
	metadata_write_string(fd_n, RATING_KEY, rating);
	read_rating_data(fd_n);
}

void layout_image_rating(LayoutWindow *lw, const gchar *rating)
{
	if (!layout_valid(&lw)) return;
	if (!lw || !lw->vf) return;

	vf_selection_foreach(lw->vf, [rating](FileData *fd_n) { image_alter_rating(fd_n, rating); });
}

void layout_image_reset_orientation(LayoutWindow *lw)
{
	ImageWindow *imd= lw->image;

	if (!layout_valid(&lw)) return;
	if (!imd || !imd->pr || !imd->image_fd) return;

	if (imd->orientation < 1 || imd->orientation > 8) imd->orientation = 1;

	if (options->image.exif_rotate_enable)
		{
		/* ISO/IEC 23008‑12 (HEIF) – Key Sections & Clauses
		 * Annex A – Metadata Specification
		 * Specifies how Exif metadata is carried in HEIF files.
		 * Exif orientation tags are described only as descriptive metadata—decoders are not
		 * required to rotate images based on Exif.
		 * This also applies to jxl files.
		 * Also see commit ac15f03b
		 */
		if (imd->image_fd->supports_exif_orientation())
			{
			imd->orientation = metadata_read_int(imd->image_fd, ORIENTATION_KEY, EXIF_ORIENTATION_TOP_LEFT);
			}
		else
			{
			imd->orientation = EXIF_ORIENTATION_TOP_LEFT;
			}
		}
	else
		{
		imd->orientation = 1;
		}

	if (imd->image_fd->user_orientation != 0)
		{
		 imd->orientation = imd->image_fd->user_orientation;
		}

	pixbuf_renderer_set_orientation(PIXBUF_RENDERER(imd->pr), imd->orientation);
}

void layout_image_set_desaturate(LayoutWindow *lw, gboolean desaturate)
{
	if (!layout_valid(&lw)) return;

	image_set_desaturate(lw->image, desaturate);
}

gboolean layout_image_get_desaturate(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return image_get_desaturate(lw->image);
}

void layout_image_set_overunderexposed(LayoutWindow *lw, gboolean overunderexposed)
{
	if (!layout_valid(&lw)) return;

	image_set_overunderexposed(lw->image, overunderexposed);
}

void layout_image_set_ignore_alpha(LayoutWindow *lw, gboolean ignore_alpha)
{
   if (!layout_valid(&lw)) return;

   lw->options.ignore_alpha = ignore_alpha;
   image_set_ignore_alpha(lw->image, ignore_alpha);
}

/* stereo */
StereoPixbufData layout_image_stereo_pixbuf_get(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return STEREO_PIXBUF_DEFAULT;

	return image_stereo_pixbuf_get(lw->image);
}

void layout_image_stereo_pixbuf_set(LayoutWindow *lw, StereoPixbufData stereo_mode)
{
	if (!layout_valid(&lw)) return;

	image_stereo_pixbuf_set(lw->image, stereo_mode);
}

const gchar *layout_image_get_path(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	return image_get_path(lw->image);
}

FileData *layout_image_get_fd(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return nullptr;

	return image_get_fd(lw->image);
}

CollectionData *layout_image_get_collection(LayoutWindow *lw, CollectInfo **info)
{
	if (!layout_valid(&lw)) return nullptr;

	return image_get_collection(lw->image, info);
}

gint layout_image_get_index(LayoutWindow *lw)
{
	return layout_list_get_index(lw, image_get_fd(lw->image));
}

/*
 *----------------------------------------------------------------------------
 * image changers
 *----------------------------------------------------------------------------
 */

void layout_image_set_fd(LayoutWindow *lw, FileData *fd)
{
	if (!layout_valid(&lw)) return;

	image_change_fd(lw->image, fd, image_zoom_get_default(lw->image));

	if (lw->full_screen && lw->image != lw->full_screen->imd)
		{
		image_change_fd(lw->full_screen->imd, fd, image_zoom_get_default(lw->full_screen->imd));
		}


	layout_list_sync_fd(lw, fd);
	layout_image_slideshow_continue_check(lw);
	layout_bars_new_image(lw);
	layout_image_animate_new_file(lw);

	if (fd)
		{
		image_chain_append_end(fd->path);
		}
}

void layout_image_set_with_ahead(LayoutWindow *lw, FileData *fd, FileData *read_ahead_fd)
{
	if (!layout_valid(&lw)) return;

/** @FIXME This should be handled at the caller: in vflist_select_image
	if (path)
		{
		const gchar *old_path;

		old_path = layout_image_get_path(lw);
		if (old_path && strcmp(path, old_path) == 0) return;
		}
*/
	layout_image_set_fd(lw, fd);
	if (options->image.enable_read_ahead) image_prebuffer_set(lw->image, read_ahead_fd);
}

void layout_image_set_index(LayoutWindow *lw, gint index)
{
	FileData *fd;
	FileData *read_ahead_fd;
	gint old;

	if (!layout_valid(&lw)) return;

	old = layout_list_get_index(lw, layout_image_get_fd(lw));
	fd = layout_list_get_fd(lw, index);

	if (old > index)
		{
		read_ahead_fd = layout_list_get_fd(lw, index - 1);
		}
	else
		{
		read_ahead_fd = layout_list_get_fd(lw, index + 1);
		}

	if (layout_selection_count(lw) > 1)
		{
		GList *x = layout_selection_list_by_index(lw);
		GList *y;
		GList *last;

		for (last = y = x; y; y = y->next)
			last = y;
		for (y = x; y && (GPOINTER_TO_INT(y->data)) != index; y = y->next)
			;

		if (y)
			{
			gint newindex;

			if ((index > old && (index != GPOINTER_TO_INT(last->data) || old != GPOINTER_TO_INT(x->data)))
			    || (old == GPOINTER_TO_INT(last->data) && index == GPOINTER_TO_INT(x->data)))
				{
				if (y->next)
					newindex = GPOINTER_TO_INT(y->next->data);
				else
					newindex = GPOINTER_TO_INT(x->data);
				}
			else
				{
				if (y->prev)
					newindex = GPOINTER_TO_INT(y->prev->data);
				else
					newindex = GPOINTER_TO_INT(last->data);
				}

			read_ahead_fd = layout_list_get_fd(lw, newindex);
			}

		while (x)
			x = g_list_remove(x, x->data);
		}

	layout_image_set_with_ahead(lw, fd, read_ahead_fd);
}

static void layout_image_set_collection_real(LayoutWindow *lw, CollectionData *cd, CollectInfo *info, gboolean forward)
{
	if (!layout_valid(&lw)) return;

	image_change_from_collection(lw->image, cd, info, image_zoom_get_default(lw->image));
	if (options->image.enable_read_ahead)
		{
		CollectInfo *r_info;
		if (forward)
			{
			r_info = collection_next_by_info(cd, info);
			if (!r_info) r_info = collection_prev_by_info(cd, info);
			}
		else
			{
			r_info = collection_prev_by_info(cd, info);
			if (!r_info) r_info = collection_next_by_info(cd, info);
			}
		if (r_info) image_prebuffer_set(lw->image, r_info->fd);
		}

	layout_image_slideshow_continue_check(lw);
	layout_bars_new_image(lw);
}

void layout_image_set_collection(LayoutWindow *lw, CollectionData *cd, CollectInfo *info)
{
	layout_image_set_collection_real(lw, cd, info, TRUE);
	layout_list_sync_fd(lw, layout_image_get_fd(lw));
}

void layout_image_refresh(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return;

	image_reload(lw->image);
}

void layout_image_color_profile_set(LayoutWindow *lw, gint input_type, gboolean use_image)
{
	if (!layout_valid(&lw)) return;

	image_color_profile_set(lw->image, input_type, use_image);
}

gboolean layout_image_color_profile_get(LayoutWindow *lw, gint &input_type, gboolean &use_image)
{
	if (!layout_valid(&lw)) return FALSE;

	return image_color_profile_get(lw->image, input_type, use_image);
}

void layout_image_color_profile_set_use(LayoutWindow *lw, gboolean enable)
{
	if (!layout_valid(&lw)) return;

	image_color_profile_set_use(lw->image, enable);
}

gboolean layout_image_color_profile_get_use(LayoutWindow *lw)
{
	if (!layout_valid(&lw)) return FALSE;

	return image_color_profile_get_use(lw->image);
}

gboolean layout_image_color_profile_get_status(LayoutWindow *lw, gchar **image_profile, gchar **screen_profile)
{
	if (!layout_valid(&lw)) return FALSE;

	return image_color_profile_get_status(lw->image, image_profile, screen_profile);
}

/*
 *----------------------------------------------------------------------------
 * list walkers
 *----------------------------------------------------------------------------
 */

void layout_image_next(LayoutWindow *lw)
{
	gint current;
	CollectionData *cd;
	CollectInfo *info;

	if (!layout_valid(&lw)) return;

	if (layout_image_slideshow_active(lw))
		{
		layout_image_slideshow_next(lw);
		return;
		}

	if (layout_selection_count(lw) > 1)
		{
		GList *x = layout_selection_list_by_index(lw);
		gint old = layout_list_get_index(lw, layout_image_get_fd(lw));
		GList *y;

		for (y = x; y && (GPOINTER_TO_INT(y->data)) != old; y = y->next)
			;
		if (y)
			{
			if (y->next)
				layout_image_set_index(lw, GPOINTER_TO_INT(y->next->data));
			else
				{
				if (options->circular_selection_lists)
					{
					layout_image_set_index(lw, GPOINTER_TO_INT(x->data));
					}
				}
			}
		while (x)
			x = g_list_remove(x, x->data);
		if (y) /* not dereferenced */
			return;
		}

	cd = image_get_collection(lw->image, &info);

	if (cd && info)
		{
		info = collection_next_by_info(cd, info);
		if (info)
			{
			layout_image_set_collection_real(lw, cd, info, TRUE);
			}
		else
			{
			image_osd_icon(lw->image, IMAGE_OSD_LAST, -1);
			}
		return;
		}

	current = layout_image_get_index(lw);

	if (current >= 0)
		{
		if (static_cast<guint>(current) < layout_list_count(lw) - 1)
			{
			layout_image_set_index(lw, current + 1);
			}
		else
			{
			image_osd_icon(lw->image, IMAGE_OSD_LAST, -1);
			}
		}
	else
		{
		layout_image_set_index(lw, 0);
		}
}

void layout_image_prev(LayoutWindow *lw)
{
	gint current;
	CollectionData *cd;
	CollectInfo *info;

	if (!layout_valid(&lw)) return;

	if (layout_image_slideshow_active(lw))
		{
		layout_image_slideshow_prev(lw);
		return;
		}

	if (layout_selection_count(lw) > 1)
		{
		GList *x = layout_selection_list_by_index(lw);
		gint old = layout_list_get_index(lw, layout_image_get_fd(lw));
		GList *y;
		GList *last;

		for (last = y = x; y; y = y->next)
			last = y;
		for (y = x; y && (GPOINTER_TO_INT(y->data)) != old; y = y->next)
			;
		if (y)
			{
			if (y->prev)
				layout_image_set_index(lw, GPOINTER_TO_INT(y->prev->data));
			else
				{
				if (options->circular_selection_lists)
					{
					layout_image_set_index(lw, GPOINTER_TO_INT(last->data));
					}
				}
			}
		while (x)
			x = g_list_remove(x, x->data);
		if (y) /* not dereferenced */
			return;
		}

	cd = image_get_collection(lw->image, &info);

	if (cd && info)
		{
		info = collection_prev_by_info(cd, info);
		if (info)
			{
			layout_image_set_collection_real(lw, cd, info, FALSE);
			}
		else
			{
			image_osd_icon(lw->image, IMAGE_OSD_FIRST, -1);
			}
		return;
		}

	current = layout_image_get_index(lw);

	if (current >= 0)
		{
		if (current > 0)
			{
			layout_image_set_index(lw, current - 1);
			}
		else
			{
			image_osd_icon(lw->image, IMAGE_OSD_FIRST, -1);
			}
		}
	else
		{
		layout_image_set_index(lw, layout_list_count(lw) - 1);
		}
}

void layout_image_first(LayoutWindow *lw)
{
	gint current;
	CollectionData *cd;
	CollectInfo *info;

	if (!layout_valid(&lw)) return;

	cd = image_get_collection(lw->image, &info);

	if (cd && info)
		{
		CollectInfo *first_collection;
		first_collection = collection_get_first(cd);
		if (first_collection != info)
			{
			layout_image_set_collection_real(lw, cd, first_collection, TRUE);
			}
		return;
		}

	current = layout_image_get_index(lw);
	if (current != 0 && layout_list_count(lw) > 0)
		{
		layout_image_set_index(lw, 0);
		}
}

void layout_image_last(LayoutWindow *lw)
{
	CollectionData *cd;
	CollectInfo *info;

	if (!layout_valid(&lw)) return;

	cd = image_get_collection(lw->image, &info);

	if (cd && info)
		{
		CollectInfo *last_collection;
		last_collection = collection_get_last(cd);
		if (last_collection != info)
			{
			layout_image_set_collection_real(lw, cd, last_collection, FALSE);
			}
		return;
		}

	const gint count = layout_list_count(lw);
	if (count > 0 && layout_image_get_index(lw) != count - 1)
		{
		layout_image_set_index(lw, count - 1);
		}
}

/*
 *----------------------------------------------------------------------------
 * mouse callbacks
 *----------------------------------------------------------------------------
 */

static gint image_idx(LayoutWindow *lw, ImageWindow *imd)
{
	gint i;

	for (i = 0; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i] == imd)
			break;
		}
	if (i < MAX_SPLIT_IMAGES)
		{
		return i;
		}
	return -1;
}

static void layout_image_focus_in_cb(ImageWindow *imd, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	gint i = image_idx(lw, imd);

	if (i != -1)
		{
		DEBUG_1("image activate focus_in %d", i);
		layout_image_activate(lw, i, FALSE);
		}
}


static void layout_image_button_cb(ImageWindow *imd, GdkEventButton *event, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;
	LayoutWindow *lw_new;

	switch (event->button)
		{
		case GDK_BUTTON_PRIMARY:
			if (event->type == GDK_2BUTTON_PRESS)
				{
				layout_image_full_screen_toggle(lw);
				}

			else if (options->image_l_click_archive && imd->image_fd && imd->image_fd->format_class == FORMAT_CLASS_ARCHIVE)
				{
				g_autofree gchar *dest_dir = open_archive(imd->image_fd); // @todo Deduplicate
				if (dest_dir)
					{
					lw_new = layout_new_from_default();
					layout_set_path(lw_new, dest_dir);
					}
				else
					{
					warning_dialog(_("Cannot open archive file"), _("See the Log Window"), GQ_ICON_DIALOG_WARNING, nullptr);
					}
				}
			else if (options->image_l_click_video && options->image_l_click_video_editor && imd-> image_fd && imd->image_fd->format_class == FORMAT_CLASS_VIDEO)
				{
				start_editor_from_file(options->image_l_click_video_editor, imd->image_fd);
				}
			else if (options->image_lm_click_nav && lw->split_mode == SPLIT_NONE)
				layout_image_next(lw);
			break;
		case GDK_BUTTON_MIDDLE:
			if (options->image_lm_click_nav && lw->split_mode == SPLIT_NONE)
				layout_image_prev(lw);
			break;
		case GDK_BUTTON_SECONDARY:
			menu = layout_image_pop_menu(lw);
			if (imd == lw->image)
				{
				g_object_set_data(G_OBJECT(menu), "click_parent", imd->widget);
				}
			gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
			break;
		default:
			break;
		}
}

static void layout_image_scroll_cb(ImageWindow *imd, GdkEventScroll *event, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	gint i = image_idx(lw, imd);

	if (i != -1)
		{
		DEBUG_1("image activate scroll %d", i);
		layout_image_activate(lw, i, FALSE);
		}


	if ((event->state & GDK_CONTROL_MASK) ||
				(imd->mouse_wheel_mode && !options->image_lm_click_nav))
		{
		switch (event->direction)
			{
			case GDK_SCROLL_UP:
				layout_image_zoom_adjust_at_point(lw, get_zoom_increment(), event->x, event->y, event->state & GDK_SHIFT_MASK);
				break;
			case GDK_SCROLL_DOWN:
				layout_image_zoom_adjust_at_point(lw, -get_zoom_increment(), event->x, event->y, event->state & GDK_SHIFT_MASK);
				break;
			default:
				break;
			}
		}
	else if (options->mousewheel_scrolls)
		{
		switch (event->direction)
			{
			case GDK_SCROLL_UP:
				image_scroll(imd, 0, -MOUSEWHEEL_SCROLL_SIZE);
				break;
			case GDK_SCROLL_DOWN:
				image_scroll(imd, 0, MOUSEWHEEL_SCROLL_SIZE);
				break;
			case GDK_SCROLL_LEFT:
				image_scroll(imd, -MOUSEWHEEL_SCROLL_SIZE, 0);
				break;
			case GDK_SCROLL_RIGHT:
				image_scroll(imd, MOUSEWHEEL_SCROLL_SIZE, 0);
				break;
			default:
				break;
			}
		}
	else
		{
		switch (event->direction)
			{
			case GDK_SCROLL_UP:
				layout_image_prev(lw);
				break;
			case GDK_SCROLL_DOWN:
				layout_image_next(lw);
				break;
			default:
				break;
			}
		}
}

static void layout_image_drag_cb(ImageWindow *imd, GdkEventMotion *event, gdouble dx, gdouble dy, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	const auto set_scroll_center = [imd, event, dx, dy](ImageWindow *image)
	{
		if (image == imd) return;

		gdouble sx;
		gdouble sy;

		if (event->state & GDK_CONTROL_MASK)
			{
			image_get_scroll_center(imd, &sx, &sy);
			}
		else
			{
			image_get_scroll_center(image, &sx, &sy);
			sx += dx;
			sy += dy;
			}

		image_set_scroll_center(image, sx, sy);
	};

	if (lw->full_screen && lw->full_screen->imd != lw->image)
		{
		set_scroll_center(lw->full_screen->imd);
		}

	if (!(event->state & GDK_SHIFT_MASK)) return;

	for (ImageWindow *split_image : lw->split_images)
		{
		if (split_image)
			{
			set_scroll_center(split_image);
			}
		}
}

static void layout_image_button_inactive_cb(ImageWindow *imd, GdkEventButton *event, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	GtkWidget *menu;
	gint i = image_idx(lw, imd);

	if (i != -1)
		{
		layout_image_activate(lw, i, FALSE);
		}

	switch (event->button)
		{
		case GDK_BUTTON_SECONDARY:
			menu = layout_image_pop_menu(lw);
			if (imd == lw->image)
				{
				g_object_set_data(G_OBJECT(menu), "click_parent", imd->widget);
				}
			gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
			break;
		default:
			break;
		}

}

static void layout_image_drag_inactive_cb(ImageWindow *imd, GdkEventMotion *event, gdouble dx, gdouble dy, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint i = image_idx(lw, imd);

	if (i != -1)
		{
		layout_image_activate(lw, i, FALSE);
		}

	/* continue as with active image */
	layout_image_drag_cb(imd, event, dx, dy, data);
}


static void layout_image_set_buttons(LayoutWindow *lw)
{
	image_set_button_func(lw->image, layout_image_button_cb, lw);
	image_set_scroll_func(lw->image, layout_image_scroll_cb, lw);
}

static void layout_image_set_buttons_inactive(LayoutWindow *lw, gint i)
{
	image_set_button_func(lw->split_images[i], layout_image_button_inactive_cb, lw);
	image_set_scroll_func(lw->split_images[i], layout_image_scroll_cb, lw);
}

/* Returns the length of an integer */
static gint num_length(gint num)
{
	gint len = 0;
	if (num < 0) num = -num;
	while (num)
		{
		num /= 10;
		len++;
		}
	return len;
}

static void layout_status_update_pixel_cb(PixbufRenderer *pr, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	gint width;
	gint height;
	PangoAttrList *attrs;

	if (!data || !layout_valid(&lw) || !lw->image
	    || !lw->options.show_info_pixel || lw->image->unknown) return;

	pixbuf_renderer_get_image_size(pr, &width, &height);
	if (width < 1 || height < 1) return;

	GdkPoint pixel;
	pixbuf_renderer_get_mouse_position(pr, pixel);

	g_autofree gchar *text = nullptr;
	if(pixel.x >= 0 && pixel.y >= 0)
		{
		gint r_mouse;
		gint g_mouse;
		gint b_mouse;
		gint a_mouse;

		pixbuf_renderer_get_pixel_colors(pr, pixel, &r_mouse, &g_mouse, &b_mouse, &a_mouse);

		if (gdk_pixbuf_get_has_alpha(pr->pixbuf))
			{
			text = g_strdup_printf(_("[%*d,%*d]: RGBA(%3d,%3d,%3d,%3d)"),
			                       num_length(width - 1), pixel.x,
			                       num_length(height - 1), pixel.y,
			                       r_mouse, g_mouse, b_mouse, a_mouse);
			}
		else
			{
			text = g_strdup_printf(_("[%*d,%*d]: RGB(%3d,%3d,%3d)"),
			                       num_length(width - 1), pixel.x,
			                       num_length(height - 1), pixel.y,
			                       r_mouse, g_mouse, b_mouse);
			}
		}
	else
		{
		text = g_strdup_printf(_("[%*s,%*s]: RGB(---,---,---)"),
					 num_length(width - 1), " ",
					 num_length(height - 1), " ");
		}
	gtk_label_set_text(GTK_LABEL(lw->info_pixel), text);

	attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_family_new("Monospace"));
	gtk_label_set_attributes(GTK_LABEL(lw->info_pixel), attrs);
	pango_attr_list_unref(attrs);
}


/*
 *----------------------------------------------------------------------------
 * setup
 *----------------------------------------------------------------------------
 */

static void layout_image_update_cb(ImageWindow *, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);
	layout_status_update_image(lw);
}

GtkWidget *layout_image_new(LayoutWindow *lw, gint i)
{
	if (!lw->split_image_sizegroup) lw->split_image_sizegroup = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);

	if (!lw->split_images[i])
		{
		lw->split_images[i] = image_new(TRUE);

		g_object_ref(lw->split_images[i]->widget);

		g_signal_connect(G_OBJECT(lw->split_images[i]->pr), "update-pixel",
				 G_CALLBACK(layout_status_update_pixel_cb), lw);

		image_background_set_color_from_options(lw->split_images[i], FALSE);

		image_auto_refresh_enable(lw->split_images[i], TRUE);

		layout_image_dnd_init(lw, i);
		image_color_profile_set(lw->split_images[i],
					options->color_profile.input_type,
					options->color_profile.use_image);
		image_color_profile_set_use(lw->split_images[i], options->color_profile.enabled);

		gtk_size_group_add_widget(lw->split_image_sizegroup, lw->split_images[i]->widget);
		gtk_widget_set_size_request(lw->split_images[i]->widget, IMAGE_MIN_WIDTH, -1);

		image_set_focus_in_func(lw->split_images[i], layout_image_focus_in_cb, lw);

		lw->split_images_touchpad_zoom[i] = GTK_EVENT_CONTROLLER(gtk_gesture_zoom_new(lw->split_images[i]->pr));
		g_signal_connect(lw->split_images_touchpad_zoom[i], "scale-changed", G_CALLBACK(touchpad_zoom_cb), lw);
		}

	return lw->split_images[i]->widget;
}

static void layout_image_deactivate(LayoutWindow *lw, gint i)
{
	if (!lw->split_images[i]) return;
	image_set_update_func(lw->split_images[i], nullptr, nullptr);
	layout_image_set_buttons_inactive(lw, i);
	image_set_drag_func(lw->split_images[i], layout_image_drag_inactive_cb, lw);

	image_attach_window(lw->split_images[i], nullptr, nullptr, nullptr, FALSE);
	image_select(lw->split_images[i], false);

	/** @FIXME The gtk_gesture_zoom_new() is leaking here
	 * g_object_unref(lw->split_images_touchpad_zoom[i]);
	 */
}

/* force should be set after change of lw->split_mode */
void layout_image_activate(LayoutWindow *lw, gint i, gboolean force)
{
	FileData *fd;

	if (!lw->split_images[i]) return;
	if (!force && lw->active_split_image == i) return;

	/* deactivate currently active */
	if (lw->active_split_image != i)
		layout_image_deactivate(lw, lw->active_split_image);

	lw->image = lw->split_images[i];
	lw->active_split_image = i;

	image_set_update_func(lw->image, layout_image_update_cb, lw);
	layout_image_set_buttons(lw);
	image_set_drag_func(lw->image, layout_image_drag_cb, lw);

	image_attach_window(lw->image, lw->window, nullptr, GQ_APPNAME, FALSE);

	/* do not highlight selected image in SPLIT_NONE */
	/* maybe the image should be selected always and highlight should be controlled by
	   another image option */
	image_select(lw->split_images[i], lw->split_mode != SPLIT_NONE);

	fd = image_get_fd(lw->image);

	if (fd)
		{
		layout_set_fd(lw, fd);
		}
	layout_status_update_image(lw);
}


static void layout_image_setup_split_common(LayoutWindow *lw, gint n)
{
	gboolean frame = (n > 1) || (!lw->options.tools_float && !lw->options.tools_hidden);
	gint i;

	for (i = 0; i < n; i++)
		if (!lw->split_images[i])
			{
			FileData *img_fd = nullptr;
			double zoom = 0.0;

			layout_image_new(lw, i);
			image_set_frame(lw->split_images[i], frame);
			image_set_selectable(lw->split_images[i], (n > 1));

			if (lw->image)
				{
				image_osd_copy_status(lw->image, lw->split_images[i]);
				}

			if (layout_selection_count(lw) > 1)
				{
				GList *work = g_list_last(layout_selection_list(lw));
				gint j = 0;

				while (work && j < i)
					{
					auto fd = static_cast<FileData *>(work->data);
					work = work->prev;

					if (!fd || !*fd->path || fd->parent ||
										fd == lw->split_images[0]->image_fd)
						{
						continue;
						}
					img_fd = fd;

					j++;
					}
				}

			if (!img_fd && lw->image)
				{
				img_fd = image_get_fd(lw->image);
				zoom = image_zoom_get(lw->image);
				}

			if (img_fd)
				{
				gdouble sx;
				gdouble sy;
				image_change_fd(lw->split_images[i], img_fd, zoom);
				image_get_scroll_center(lw->image, &sx, &sy);
				image_set_scroll_center(lw->split_images[i], sx, sy);
				}
			layout_image_deactivate(lw, i);
			}
		else
			{
			image_set_frame(lw->split_images[i], frame);
			image_set_selectable(lw->split_images[i], (n > 1));
			}

	for (i = n; i < MAX_SPLIT_IMAGES; i++)
		{
		if (lw->split_images[i])
			{
			g_object_unref(lw->split_images[i]->widget);
			lw->split_images[i] = nullptr;
			}
		}

	if (!lw->image || lw->active_split_image < 0 || lw->active_split_image >= n)
		{
		layout_image_activate(lw, 0, TRUE);
		}
	else
		{
		/* this will draw the frame around selected image (image_select)
		   on switch from single to split images */
		layout_image_activate(lw, lw->active_split_image, TRUE);
		}
}

static GtkWidget *layout_image_setup_split_none(LayoutWindow *lw)
{
	lw->split_mode = SPLIT_NONE;

	layout_image_setup_split_common(lw, 1);

	return lw->split_images[0]->widget;
}


static GtkWidget *layout_image_setup_split_hv(LayoutWindow *lw, ImageSplitMode mode)
{
	lw->split_mode = mode;

	layout_image_setup_split_common(lw, 2);

	/* horizontal split means vpaned and vice versa */
	GtkWidget *paned = gtk_paned_new((mode == SPLIT_HOR) ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(paned);

	gtk_paned_pack1(GTK_PANED(paned), lw->split_images[0]->widget, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(paned), lw->split_images[1]->widget, TRUE, TRUE);

	gtk_widget_show(lw->split_images[0]->widget);
	gtk_widget_show(lw->split_images[1]->widget);

	return paned;
}

static GtkWidget *layout_image_setup_split_triple(LayoutWindow *lw)
{
	GtkWidget *hpaned1;
	GtkWidget *hpaned2;
	gint i;
	gint pane_pos;

	lw->split_mode = SPLIT_TRIPLE;

	layout_image_setup_split_common(lw, 3);

	hpaned1 = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(hpaned1);
	hpaned2 = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(hpaned2);

	if (lw->bar && gtk_widget_get_visible(lw->bar))
		{
		pane_pos = (gtk_paned_get_position(GTK_PANED(lw->utility_paned))) / 3;
		}
	else
		{
		GtkAllocation allocation;
		gtk_widget_get_allocation(lw->utility_paned, &allocation);

		pane_pos = allocation.width / 3;
		}

	gtk_paned_set_position(GTK_PANED(hpaned1), pane_pos);
	gtk_paned_set_position(GTK_PANED(hpaned2), pane_pos);

	gtk_paned_pack1(GTK_PANED(hpaned1), lw->split_images[0]->widget, TRUE, TRUE);
	gtk_paned_pack1(GTK_PANED(hpaned2), lw->split_images[1]->widget, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(hpaned2), lw->split_images[2]->widget, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(hpaned1), hpaned2, TRUE, TRUE);

	for (i = 0; i < 3; i++)
		{
		gtk_widget_show(lw->split_images[i]->widget);
		}

	gtk_widget_show(hpaned1);
	gtk_widget_show(hpaned2);

	return hpaned1;
}

static GtkWidget *layout_image_setup_split_quad(LayoutWindow *lw)
{
	GtkWidget *hpaned;
	GtkWidget *vpaned1;
	GtkWidget *vpaned2;
	gint i;

	lw->split_mode = SPLIT_QUAD;

	layout_image_setup_split_common(lw, 4);

	hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	DEBUG_NAME(hpaned);
	vpaned1 = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	DEBUG_NAME(vpaned1);
	vpaned2 = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	DEBUG_NAME(vpaned2);

	gtk_paned_pack1(GTK_PANED(vpaned1), lw->split_images[0]->widget, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(vpaned1), lw->split_images[2]->widget, TRUE, TRUE);

	gtk_paned_pack1(GTK_PANED(vpaned2), lw->split_images[1]->widget, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(vpaned2), lw->split_images[3]->widget, TRUE, TRUE);

	gtk_paned_pack1(GTK_PANED(hpaned), vpaned1, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(hpaned), vpaned2, TRUE, TRUE);

	for (i = 0; i < 4; i++)
		gtk_widget_show(lw->split_images[i]->widget);

	gtk_widget_show(vpaned1);
	gtk_widget_show(vpaned2);

	return hpaned;
}

GtkWidget *layout_image_setup_split(LayoutWindow *lw, ImageSplitMode mode)
{
	switch (mode)
		{
		case SPLIT_HOR:
		case SPLIT_VERT:
			lw->split_image_widget = layout_image_setup_split_hv(lw, mode);
			break;
		case SPLIT_TRIPLE:
			lw->split_image_widget = layout_image_setup_split_triple(lw);
			break;
		case SPLIT_QUAD:
			lw->split_image_widget = layout_image_setup_split_quad(lw);
			break;
		case SPLIT_NONE:
		default:
			lw->split_image_widget = layout_image_setup_split_none(lw);
			break;
		}

	return lw->split_image_widget;
}


/*
 *-----------------------------------------------------------------------------
 * maintenance (for rename, move, remove)
 *-----------------------------------------------------------------------------
 */

static void layout_image_maint_renamed(LayoutWindow *lw, FileData *fd)
{
	if (fd == layout_image_get_fd(lw))
		{
		image_set_fd(lw->image, fd);
		}
}

static void layout_image_maint_removed(LayoutWindow *lw, FileData *fd)
{
	if (fd == layout_image_get_fd(lw))
		{
		CollectionData *cd;
		CollectInfo *info;

		cd = image_get_collection(lw->image, &info);
		if (cd && info)
			{
			CollectInfo *next_collection;

			next_collection = collection_next_by_info(cd, info);
			if (!next_collection) next_collection = collection_prev_by_info(cd, info);

			if (next_collection)
				{
				layout_image_set_collection(lw, cd, next_collection);
				return;
				}
			layout_image_set_fd(lw, nullptr);
			}

		/* the image will be set to the next image from the list soon,
		   setting it to NULL here is not necessary*/
		}
}


void layout_image_notify_cb(FileData *fd, NotifyType type, gpointer data)
{
	auto lw = static_cast<LayoutWindow *>(data);

	if (!(type & NOTIFY_CHANGE) || !fd->change) return;

	DEBUG_1("Notify layout_image: %s %04x", fd->path, type);

	switch (fd->change->type)
		{
		case FILEDATA_CHANGE_MOVE:
		case FILEDATA_CHANGE_RENAME:
			layout_image_maint_renamed(lw, fd);
			break;
		case FILEDATA_CHANGE_DELETE:
			layout_image_maint_removed(lw, fd);
			break;
		case FILEDATA_CHANGE_COPY:
		case FILEDATA_CHANGE_UNSPECIFIED:
		case FILEDATA_CHANGE_WRITE_METADATA:
			break;
		}

}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
