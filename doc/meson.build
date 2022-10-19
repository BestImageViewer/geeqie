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
#

subdir('html')

guideindex_xml = files('docbook/GuideIndex.xml')
destdir = join_paths(meson.current_build_dir(), 'html/')
iconsdir = join_paths(meson.current_source_dir(), 'icons/')
guideindex_ln = join_paths(destdir, 'index.html')

# Not-found notification already handled by ../meson.build
if gnome_doc_tool.found()
    run_command(gnome_doc_tool, 'html', '-o', destdir, '-p', iconsdir, guideindex_xml, check : false)
    run_command(find_program('ln'), '-s', '-f', 'GuideIndex.html', guideindex_ln, check : false)
endif

install_subdir(destdir, install_dir : helpdir, exclude_directories : 'lua-api/latex')

doxygen = find_program('doxygen', required : false)
if doxygen.found() and running_from_git
    srcdir = join_paths(meson.source_root())
    destdir = join_paths(meson.build_root(), 'doc', 'html', 'lua-api')

    run_command(find_program('create-doxygen-lua-api.sh'), srcdir, destdir, check : false)

    summary({'lua-api' : ['lua-api help file created:', true]}, section : 'Documentation', bool_yn : true)
else
    summary({'lua-api' : ['doxygen not found - lua-api help file created:', false]}, section : 'Documentation', bool_yn : true)
endif
