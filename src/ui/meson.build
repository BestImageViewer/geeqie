# This file is a part of Geeqie project (https://www.geeqie.org/).
# Copyright (C) 2008 - 2023 The Geeqie Team
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#

target_string = '--target=' + meson.current_build_dir() + '/ui.h'
ui_resources_h = custom_target('ui_resources_h',
                input : 'ui.gresource.xml',
                output : 'ui.h',
                command : [glib_compile_resources, '--generate-header', target_string, '--sourcedir=@CURRENT_SOURCE_DIR@', '@INPUT@'])

target_string = '--target=' + meson.current_build_dir() + '/ui.c'
ui_resources_c = custom_target('ui_resources_c',
                input : 'ui.gresource.xml',
                output : 'ui.c',
                command : [glib_compile_resources, '--generate-source', target_string, '--sourcedir=@CURRENT_SOURCE_DIR@', '@INPUT@'])

project_sources += ui_resources_h
project_sources += ui_resources_c

ui_sources += files('search-and-run.ui')
