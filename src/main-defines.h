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

#ifndef _MAIN_DEFINES_H
#define _MAIN_DEFINES_H

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
#define GQ_RESOURCE_PATH_IMAGES "/org/geeqie/ui"
#define GQ_RESOURCE_PATH_UI "/org/geeqie/ui"

#define GQ_SYSTEM_WIDE_DIR    "/etc/" GQ_APPNAME_LC

#define RC_FILE_NAME GQ_APPNAME_LC "rc.xml"
#define DEFAULT_WINDOW_LAYOUT "default_window_layout.xml"

#define GQ_COLLECTION_EXT ".gqv"

#define GQ_DEFAULT_SHELL_PATH "/bin/sh"
#define GQ_DEFAULT_SHELL_OPTIONS "-c"

#define DEFAULT_MINIMAL_WINDOW_SIZE 100

#define DEFAULT_OVERLAY_INFO	"%collection:<i>*</i>\\n%" \
				"(%number%/%total%) [%zoom%] <b>%name%</b>\n" \
				"%res%|%date%|%size%\n" \
				"%formatted.Aperture%|%formatted.ShutterSpeed%|%formatted.ISOSpeedRating:ISO *%|%formatted.FocalLength%|%formatted.ExposureBias:* Ev%\n" \
				"%formatted.Camera:40%|%formatted.Flash%\n"            \
				"%formatted.star_rating%"

#define GQ_LINK_STR "↩"

#define TIMEZONE_DATABASE_WEB "https://cdn.bertold.org/zonedetect/db/db.zip"
#define TIMEZONE_DATABASE_FILE "timezone21.bin"
#define TIMEZONE_DATABASE_VERSION "out_v1"
#define HELP_SEARCH_ENGINE "https://duckduckgo.com/?q=site:geeqie.org/help "

#define STAR_RATING_NOT_READ -12345
#define STAR_RATING_REJECTED 0x274C //Unicode Character 'Cross Mark'
#define STAR_RATING_STAR 0x2738 //Unicode Character 'Heavy Eight Pointed Rectilinear Black Star'

#define CMD_COPY     "geeqie-copy-command.desktop"
#define CMD_MOVE     "geeqie-move-command.desktop"
#define CMD_RENAME   "geeqie-rename-command.desktop"
#define CMD_DELETE   "geeqie-delete-command.desktop"
#define CMD_FOLDER   "geeqie-folder-command.desktop"

#define FILEDATA_MARKS_SIZE 10

#define GQ_ICON_ADD "list-add"
#define GQ_ICON_REMOVE "list-remove"
#define GQ_ICON_UNDO "edit-undo"
#define GQ_ICON_REDO "edit-redo"
#define GQ_ICON_OPEN "document-open"
#define GQ_ICON_OPEN_WITH "open-menu"
#define GQ_ICON_SAVE "document-save"
#define GQ_ICON_SAVE_AS "document-save-as"
#define GQ_ICON_NEW "document-new"
#define GQ_ICON_EDIT "document-edit"
#define GQ_ICON_REVERT "document-revert"
#define GQ_ICON_CLOSE "window-close"
#define GQ_ICON_RUN "system-run"
#define GQ_ICON_STOP "process-stop"
#define GQ_ICON_FULLSCREEN "view-fullscreen"
#define GQ_ICON_LEAVE_FULLSCREEN "view-restore"
#define GQ_ICON_REFRESH "view-refresh"
#define GQ_ICON_ABOUT "help-about"
#define GQ_ICON_QUIT "application-exit"
#define GQ_ICON_DELETE "edit-delete"
#define GQ_ICON_DELETE_SHRED "edit-delete-shred"
#define GQ_ICON_CLEAR "edit-clear"
#define GQ_ICON_COPY "edit-copy"
#define GQ_ICON_FIND "edit-find"
#define GQ_ICON_REPLACE "edit-find-replace"
#define GQ_ICON_PRINT "document-print"
#define GQ_ICON_FILE_FILTER "preview-file"
#define GQ_ICON_USER_TRASH "user-trash"

#define GQ_ICON_GO_TOP "go-top"
#define GQ_ICON_GO_BOTTOM "go-bottom"
#define GQ_ICON_GO_UP "go-up"
#define GQ_ICON_GO_DOWN "go-down"
#define GQ_ICON_GO_FIRST "go-first"
#define GQ_ICON_GO_LAST "go-last"
#define GQ_ICON_GO_PREV "go-previous"
#define GQ_ICON_GO_NEXT "go-next"
#define GQ_ICON_GO_JUMP "go-jump"
#define GQ_ICON_HOME "go-home"

#define GQ_ICON_PREV_PAGE "media-skip-backward"
#define GQ_ICON_NEXT_PAGE "media-skip-forward"
#define GQ_ICON_BACK_PAGE "media-seek-backward"
#define GQ_ICON_FORWARD_PAGE "media-seek-forward"

#define GQ_ICON_PLAY "media-playback-start"
#define GQ_ICON_PAUSE "media-playback-pause"

#define GQ_ICON_ZOOM_IN "zoom-in"
#define GQ_ICON_ZOOM_OUT "zoom-out"
#define GQ_ICON_ZOOM_100 "zoom-original"
#define GQ_ICON_ZOOM_FIT "zoom-fit-best"

// might need replacing
#define GQ_ICON_PREFERENCES "preferences-system"
#define GQ_ICON_HELP "help-contents" // "help-browser"?
#define GQ_ICON_EXPORT "document-export" // use collection icon?

// not available in some themes
#define GQ_ICON_OK "emblem-ok"
#define GQ_ICON_APPLY "emblem-ok" // need something else?
#define GQ_ICON_CANCEL "dialog-cancel" // missing in adwaita and others, seen in breeze
#define GQ_ICON_PAN_DOWN "pan-down-symbolic" // adwaita, breeze, hicolor supports this
#define GQ_ICON_PAN_UP "pan-up-symbolic" // adwaita, breeze, hicolor supports this

#define GQ_ICON_DIALOG_ERROR "dialog-error"
#define GQ_ICON_DIALOG_INFO "dialog-information"
#define GQ_ICON_DIALOG_QUESTION "dialog-question"
#define GQ_ICON_DIALOG_WARNING "dialog-warning"

#define GQ_ICON_UNREADABLE "emblem-unreadable"
#define GQ_ICON_LINK "emblem-symbolic-link"
#define GQ_ICON_READONLY "emblem-readonly"

#define GQ_ICON_FLIP_HORIZONTAL "object-flip-horizontal"
#define GQ_ICON_FLIP_VERTICAL "object-flip-vertical"
#define GQ_ICON_ROTATE_LEFT "object-rotate-left"
#define GQ_ICON_ROTATE_RIGHT "object-rotate-right"

#define GQ_ICON_DIRECTORY "folder"
#define GQ_ICON_MISSING_IMAGE "image-missing"
#define GQ_ICON_STRIKETHROUGH "format-text-strikethrough"
#define GQ_ICON_FILE "text-x-generic"
#define GQ_ICON_GENERIC "text-x-generic"
#define GQ_ICON_SELECT_FONT "font-select"
#define GQ_ICON_SELECT_COLOR "color-select"
#define GQ_ICON_COLOR_MANAGEMENT "preferences-color" // breeze has nicer "color-management" icon, missing in others

// not done... plus many more
// PIXBUF_INLINE_ICON_ZOOMFILLHOR
// PIXBUF_INLINE_ICON_ZOOMFILLVERT

#endif /* _MAIN_DEFINES_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
