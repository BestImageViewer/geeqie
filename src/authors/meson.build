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

authors_sh = join_paths(scriptsdir, 'authors.sh')

authors_resources = custom_target('authors_resources',
                output : 'authors',
                command : [authors_sh, meson.current_build_dir(), running_from_git.to_string()])

project_sources += gnome.compile_resources('authors', 'authors.gresource.xml', dependencies : authors_resources)
