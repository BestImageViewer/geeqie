# This file is a part of Geeqie project (https://www.geeqie.org/).
# Copyright (C) 2008 - 2022 The Geeqie Team
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

icons_inline_pairs = [
'folder_closed', files('folder_closed.png'),
'folder_locked', files('folder_locked.png'),
'folder_open', files('folder_open.png'),
'folder_up', files('folder_up.png'),
'gqview_icon', files('gqview_icon.png'),
'geeqie_logo', files('geeqie_logo.png'),
'icon_float', files('icon_float.png'),
'icon_thumb', files('icon_thumb.png'),
'icon_scroller', files('scroller.png'),
'icon_broken', files('sheet_broken.png'),
'icon_archive', files('archive_file.png'),
'icon_metadata', files('sheet_metadata.png'),
'icon_unknown', files('sheet_unknown.png'),
'icon_video', files('sheet_video.png'),
'icon_collection', files('collection.png'),
'icon_book', files('icon_book.png'),
'icon_config', files('icon_config.png'),
'icon_tools', files('icon_tools.png'),
'icon_view', files('icon_view.png'),
'icon_panorama', files('icon_panorama.png'),
'icon_maintenance', files('icon_maintenance.png'),
'icon_zoomfillhor', files('icon_zoomfillhor.png'),
'icon_zoomfillvert', files('icon_zoomfillvert.png'),
'icon_hidetools', files('icon_hidetools.png'),
'icon_exif', files('icon_exif.png'),
'icon_marks', files('icon_marks.png'),
'icon_info', files('icon_info.png'),
'icon_sort', files('icon_sort.png'),
'icon_pdf', files('icon_pdf.png'),
'icon_draw_rectangle', files('icon_draw_rectangle.png'),
'icon_move', files('icon_move.png'),
'icon_rename', files('icon_rename.png'),
'icon_select_all', files('icon_select_all.png'),
'icon_select_none', files('icon_select_none.png'),
'icon_select_invert', files('icon_select_invert.png'),
'icon_select_rectangle', files('icon_select_rectangle.png'),
'icon_file_filter', files('icon_file_filter.png'),
'icon_rotate_clockwise', files('icon_rotate_clockwise.png'),
'icon_rotate_counter_clockwise', files('icon_rotate_counter_clockwise.png'),
'icon_rotate_180', files('icon_rotate_180.png'),
'icon_mirror', files('icon_mirror.png'),
'icon_flip', files('icon_flip.png'),
'icon_original', files('icon_original.png'),
'icon_trash', files('icon_trash.png'),
'icon_heic', files('icon_heic.png'),
'icon_grayscale', files('icon_grayscale.png'),
'icon_exposure', files('icon_exposure.png'),
'icon_split_pane_sync', files('icon_split_pane_sync.png')
]

icons_inline_h = custom_target('icons_inline', output : 'icons_inline.h',
        command : [gdk_pixbuf_csource, '--raw', '--extern', '--build-list', icons_inline_pairs],
        capture : true)

project_sources += icons_inline_h

subdir('svg')
