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

#ifndef MAIN_H
#define MAIN_H

#include <config.h>

#include "intl.h"

#ifdef HAVE_DEVELOPER
#include "backward.h"
#endif

/*
 *-------------------------------------
 * Standard library includes
 *-------------------------------------
 */

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>

/*
 *-------------------------------------
 * includes for glib / gtk / gdk-pixbuf
 *-------------------------------------
 */

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gdk-pixbuf-loader.h>

#include "compat.h"

/*
 *----------------------------------------------------------------------------
 * defines
 *----------------------------------------------------------------------------
 */

#define USE_XDG 1

#define GQ_APPNAME "Geeqie"
#define GQ_APPNAME_LC "geeqie"
#define GQ_WEBSITE "https://www.geeqie.org/"
#define GQ_EMAIL_ADDRESS "geeqie@freelists.org"

#define GQ_RC_DIR		"." GQ_APPNAME_LC
#define GQ_COLLECTIONS_DIR	"collections"
#define GQ_TRASH_DIR		"trash"
#define GQ_WINDOW_LAYOUTS_DIR	"layouts"
#define GQ_ARCHIVE_DIR	"geeqie-archive"
#define GQ_RESOURCE_PATH_ICONS "/org/geeqie/icons"
#define GQ_RESOURCE_PATH_CREDITS "/org/geeqie/credits"

#define GQ_SYSTEM_WIDE_DIR    "/etc/" GQ_APPNAME_LC

#define RC_FILE_NAME GQ_APPNAME_LC "rc.xml"
#define DEFAULT_WINDOW_LAYOUT "default_window_layout.xml"

#define GQ_COLLECTION_EXT ".gqv"

#define SCROLL_RESET_TOPLEFT 0
#define SCROLL_RESET_CENTER 1
#define SCROLL_RESET_NOCHANGE 2

#define MOUSEWHEEL_SCROLL_SIZE 20


#define GQ_DEFAULT_SHELL_PATH "/bin/sh"
#define GQ_DEFAULT_SHELL_OPTIONS "-c"

#define COLOR_PROFILE_INPUTS 4

#define DEFAULT_THUMB_WIDTH	96
#define DEFAULT_THUMB_HEIGHT	72

#define DEFAULT_MINIMAL_WINDOW_SIZE 100

#define IMAGE_MIN_WIDTH 100
#define SIDEBAR_DEFAULT_WIDTH 250


#define DEFAULT_OVERLAY_INFO	"%collection:<i>*</i>\\n%" \
				"(%number%/%total%) [%zoom%] <b>%name%</b>\n" \
				"%res%|%date%|%size%\n" \
				"%formatted.Aperture%|%formatted.ShutterSpeed%|%formatted.ISOSpeedRating:ISO *%|%formatted.FocalLength%|%formatted.ExposureBias:* Ev%\n" \
				"%formatted.Camera:40%|%formatted.Flash%\n"            \
				"%formatted.star_rating%"

#define GQ_LINK_STR "↗"
#include "typedefs.h"
#include "debug.h"
#include "options.h"

#define APPIMAGE_VERSION_FILE "https://raw.githubusercontent.com/geeqie/geeqie.github.io/master/AppImage/appimages.txt"
#define TIMEZONE_DATABASE_WEB "https://cdn.bertold.org/zonedetect/db/db.zip"
#define TIMEZONE_DATABASE_FILE "timezone21.bin"
#define TIMEZONE_DATABASE_VERSION "out_v1"
#define HELP_SEARCH_ENGINE "https://duckduckgo.com/?q=site:geeqie.org/help "

#define STAR_RATING_NOT_READ -12345
#define STAR_RATING_REJECTED 0x274C //Unicode Character 'Cross Mark'
#define STAR_RATING_STAR 0x2738 //Unicode Character 'Heavy Eight Pointed Rectilinear Black Star'

/* From http://stackoverflow.com/questions/3417837/ */
#ifdef UNUSED
#elif defined(__GNUC__)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED(x) /*@unused@*/ x
#else
# define UNUSED(x) x
#endif

/*
 *----------------------------------------------------------------------------
 * main.cc
 *----------------------------------------------------------------------------
 */

/*
 * This also doubles as the main.cc header.
 */

extern gboolean thumb_format_changed;

extern gchar *gq_prefix;
extern gchar *gq_localedir;
extern gchar *gq_helpdir;
extern gchar *gq_htmldir;
extern gchar *gq_appdir;
extern gchar *gq_bindir;
extern gchar *gq_executable_path;
extern gchar *desktop_file_template;
extern gchar *instance_identifier;

void keyboard_scroll_calc(gint *x, gint *y, GdkEventKey *event);
gint key_press_cb(GtkWidget *widget, GdkEventKey *event, gpointer data);

void exit_program();

#define CASE_SORT(a, b) ( (options->file_sort.case_sensitive) ? strcmp((a), (b)) : strcasecmp((a), (b)) )


#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
