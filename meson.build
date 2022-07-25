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

# Meson default directories used in this project:
# prefix - /usr/local
# bindir - bin
# datadir - share

# Meson core options:
# buildtype
# debug

# Project expanded default directories:
# prefix        /usr/local/
# bindir        /usr/local/bin                          geeqie executable
# gq_bindir     /usr/local/lib/geeqie               *   plugins scripts
# datadir       /usr/local/share/
#               /usr/local/share/applications           geeqie.desktop
# [gq_]appdir   /usr/local/share/geeqie/            *   template.desktop
# desktopdir    /usr/local/share/geeqie/applications    plugin desktop files
# appdatadir    /usr/local/share/metainfo               org.geeqie.Geeqie.appdata.xml
# icondir       /usr/local/share/pixmaps                geeqie.png icon
# [gq_]helpdir  /usr/local/share/doc/geeqie         *   readme files etc.
# [gq_]htmldir  /usr/local/share/doc/geeqie/html    *   help files
# gq_localedir  /usr/locale/share/locale
# mandir1       /usr/local/share/man/man1               man page
# podir         project_root/po

# * See meson_options.txt file

project(
    'geeqie',
    'c',
    'cpp',
    version : run_command('./version.sh', check : true).stdout().strip(),
    license : ['GPL-2.0-or-later'],
    meson_version : '>= 0.60.0',
    default_options : ['warning_level=3', 'buildtype=debugoptimized']
)

# To inhibit warnings from the generated files icons_inline.h and ui_icons.h
add_global_arguments('-Wno-overlength-strings', language : 'c')

# Project requirements
project_sources = []
gnome = import('gnome')
thread_dep = dependency('threads')
cc = meson.get_compiler('c')
i18n = import('i18n')
configuration_inc = include_directories('.')

# External programs
gdk_pixbuf_csource = find_program('gdk-pixbuf-csource', required : true)
glib_genmarshal = find_program('glib-genmarshal', required : true)
gnome_doc_tool = find_program('yelp-build', required : false)
if not gnome_doc_tool.found()
    message('yelp-build not found. Help files not created.')
endif

debug = get_option('debug')

# Note that main.c sets prefix to the directory above where the executable is run from.
# This is to allow AppImages to be used

# These gq_* variables are paths relative to /prefix/,
# and are also used in defines in the source as GQ_*
if get_option('gq_appdir') == ''
    gq_appdir = join_paths(get_option('datadir'), 'geeqie')
else
    gq_appdir = get_option('gq_appdir')
endif

# This is not the same as Meson bindir
if get_option('gq_bindir') == ''
    gq_bindir = 'lib/geeqie'
else
    gq_bindir = get_option('gq_bindir')
endif

if get_option('gq_helpdir') == ''
    gq_helpdir = join_paths(get_option('datadir'), 'doc/geeqie')
else
    gq_helpdir = get_option('gq_helpdir')
endif

if get_option('gq_htmldir') == ''
    gq_htmldir = join_paths(get_option('datadir'), 'doc/geeqie/html')
else
    gq_htmldir = get_option('gq_htmldir')
endif

if get_option('gq_localedir') == ''
    gq_localedir = join_paths(get_option('datadir'), 'locale')
else
    gq_localedir = get_option('gq_localedir')
endif


# Set up the absolute directory paths used
prefix = get_option('prefix')
datadir = join_paths(prefix, get_option('datadir'))

# Installation paths are absolute
appdir = join_paths(prefix, gq_appdir)
appdatadir = join_paths(datadir, 'metainfo')
desktopdir = join_paths(datadir, meson.project_name(), 'applications')
helpdir = join_paths(prefix, gq_helpdir)
htmldir = join_paths(prefix, gq_htmldir)
icondir = join_paths(datadir, 'pixmaps')
mandir1 = join_paths(datadir, 'man', 'man1')

podir = join_paths(meson.project_source_root(), 'po')

# Create the define constants used in the sources. Set via config.h.in
conf_data = configuration_data()
conf_data.set_quoted('VERSION', run_command('./version.sh', check : true).stdout())
conf_data.set('DEBUG', debug)

gtk_dep = dependency('gtk+-3.0', version : '>=3.22', required: true)
glib_dep = dependency('glib-2.0', version: '>=2.52', required: true)

libarchive_dep = []
libarchive_dep = dependency('libarchive', version: '>=3.4.0', required : get_option('archive'))
if libarchive_dep.found()
    conf_data.set('HAVE_ARCHIVE', 1)
endif

lcms_dep = []
lcms_dep = dependency('lcms2', version: '>=2.0', required : get_option('cms'))
if lcms_dep.found()
    conf_data.set('HAVE_LCMS', 1)
    conf_data.set('HAVE_LCMS2', 1)
endif

ddjvuapi_dep = []
ddjvuapi_dep = dependency('ddjvuapi', version: '>=3.5.27', required : get_option('djvu'))
if ddjvuapi_dep.found()
    conf_data.set('HAVE_DJVU', 1)
endif

exiv2_dep = []
exiv2_dep = dependency('exiv2', version: '>=0.11', required : get_option('exiv2'))
if exiv2_dep.found()
    conf_data.set('HAVE_EXIV2', 1)
endif

libffmpegthumbnailer_dep = []
libffmpegthumbnailer_dep = dependency('libffmpegthumbnailer',
    version: '>=2.1.0',
    required : get_option('ffmpegthumbnailer'))

if libffmpegthumbnailer_dep.found()
    conf_data.set('HAVE_FFMPEGTHUMBNAILER', 1)

    if cc.has_member('struct video_thumbnailer_struct', 'prefer_embedded_metadata',
        prefix : '#include <libffmpegthumbnailer/videothumbnailerc.h>')

        conf_data.set('HAVE_FFMPEGTHUMBNAILER_METADATA', 1)
    endif

    if cc.has_member('struct image_data_struct', 'image_data_width',
        prefix : '#include <libffmpegthumbnailer/videothumbnailerc.h>' )

        conf_data.set('HAVE_FFMPEGTHUMBNAILER_RGB', 1)
    endif

    if cc.has_function('video_thumbnailer_set_size',
        dependencies : libffmpegthumbnailer_dep)

        conf_data.set('HAVE_FFMPEGTHUMBNAILER_WH', 1)
    endif 
endif

champlain_dep = []
champlain_gtk_dep = []
champlain_dep = dependency('champlain-0.12', version: '>=0.12', required : get_option('gps-map'))
champlain_gtk_dep = dependency('champlain-gtk-0.12', version: '>=0.12', required : get_option('gps-map'))
clutter_dep = []
clutter_gtk_dep = []
if champlain_dep.found() and champlain_gtk_dep.found()
    clutter_dep = dependency('clutter-1.0', required : true)
    clutter_gtk_dep = dependency('clutter-gtk-1.0', required : true)
    if clutter_dep.found() and clutter_gtk_dep.found()
        conf_data.set('HAVE_CLUTTER', 1)
        conf_data.set('HAVE_LIBCHAMPLAIN', 1)
        conf_data.set('HAVE_LIBCHAMPLAIN_GTK', 1)
    endif
endif

libheif_dep = []
libheif_dep = dependency('libheif', version: '>=1.3.2', required : get_option('heif'))
if libheif_dep.found()
    conf_data.set('HAVE_HEIF', 1)
endif

libopenjp2_dep = []
libopenjp2_dep = dependency('libopenjp2', version: '>=2.3.0', required : get_option('j2k'))
if libopenjp2_dep.found()
    conf_data.set('HAVE_J2K', 1)
endif

libjpeg_dep = []
libjpeg_dep = dependency('libjpeg', required : get_option('jpeg'))
if libjpeg_dep.found()
    if cc.has_function('jpeg_destroy_decompress',
        dependencies : libjpeg_dep)

        conf_data.set('HAVE_JPEG', 1)
    endif
endif

libjxl_dep = []
libjxl_dep = dependency('libjxl', version: '>=0.3.7', required : get_option('jpegxl'))
if libjxl_dep.found()
    conf_data.set('HAVE_JPEGXL', 1)
endif

libraw_dep = []
libraw_dep = dependency('libraw', version: '>=0.20', required : get_option('libraw'))
if libraw_dep.found()
    conf_data.set('HAVE_RAW', 1)
endif

lua_dep = []
lua_dep = dependency('lua5.1', required : get_option('lua'))
if lua_dep.found()
    conf_data.set('HAVE_LUA', 1)
endif

poppler_glib_dep = []
poppler_glib_dep = dependency('poppler-glib', version: '>=0.62', required : get_option('pdf'))
if poppler_glib_dep.found()
    conf_data.set('HAVE_PDF', 1)
endif

gspell_dep = []
gspell_dep = dependency('gspell-1', required: get_option('spell'))
if gspell_dep.found()
    conf_data.set('HAVE_SPELL', 1)
endif

tiff_dep = []
tiff_dep = cc.find_library('tiff', required: get_option('tiff'))
if tiff_dep.found()
    if cc.has_function('TIFFClientOpen',
        dependencies : tiff_dep)

        conf_data.set('HAVE_TIFF', 1)
    endif
endif

libwebp_dep = []
    libwebp_dep = dependency('libwebp', version: '>=0.6.1', required : get_option('webp'))
    if libwebp_dep.found()
        conf_data.set('HAVE_WEBP', 1)
    endif

# Check for nl_langinfo and _NL_TIME_FIRST_WEEKDAY
code = '''#include <langinfo.h>
#include<stdio.h>
int main (int argc, char ** argv) {
    char *c;
    c =  nl_langinfo(_NL_TIME_FIRST_WEEKDAY);
    return 0;
}'''
if cc.links(code, name : 'nl_langinfo and _NL_TIME_FIRST_WEEKDAY')
    conf_data.set('HAVE__NL_TIME_FIRST_WEEKDAY', 1)
endif

conf_data.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf_data.set_quoted('GQ_APPDIR', gq_appdir)
conf_data.set_quoted('GQ_BINDIR', gq_bindir)
conf_data.set_quoted('GQ_HELPDIR', gq_helpdir)
conf_data.set_quoted('GQ_HTMLDIR', gq_htmldir)
conf_data.set_quoted('GQ_LOCALEDIR', gq_localedir)

conf_data.set_quoted('PACKAGE', meson.project_name())
conf_data.set_quoted('PACKAGE_NAME', meson.project_name())
conf_data.set_quoted('PACKAGE_STRING', meson.project_version())
conf_data.set_quoted('PACKAGE_TARNAME', meson.project_name())
conf_data.set_quoted('PACKAGE_VERSION', meson.project_version())
conf_data.set_quoted('VERSION', meson.project_version())

configure_file(input : 'config.h.in',
               output : 'config.h',
               encoding : 'UTF-8',
               configuration : conf_data)

# Process subdirs before the sources
subdir('po')
subdir('plugins')

# Generate the executable and the help files
subdir('src')
subdir('doc')

# Install other project files
run_command(find_program('gen_changelog.sh'), meson.project_source_root(), meson.project_build_root(), check : true)

run_command(find_program('gen_readme.sh'), meson.project_source_root(), meson.project_build_root(), check : true)

pandoc = find_program('pandoc')
if pandoc.found()
    install_data('README.md', 'COPYING', join_paths(meson.project_build_root(), 'ChangeLog'), 'TODO', 'AUTHORS', join_paths(meson.project_build_root(), 'README.html'), join_paths(meson.project_build_root(), 'ChangeLog.html'), install_dir : helpdir, install_tag : 'help')
else
    install_data('README.md', 'COPYING', join_paths(meson.project_build_root(), 'ChangeLog'), 'TODO', 'README.lirc', 'AUTHORS', install_dir : helpdir, install_tag : 'help')
endif

install_data('geeqie.png', install_dir : icondir, install_tag : 'icons')
install_data('geeqie.1', install_dir : mandir1, install_tag : 'man')

i18n.merge_file(
    input : 'geeqie.desktop.in',
    output : 'geeqie.desktop',
    type : 'desktop',
    po_dir : podir,
    install : true,
    install_dir : join_paths(datadir, 'applications'))

i18n.merge_file(
    input : 'org.geeqie.Geeqie.appdata.xml.in',
    output : 'org.geeqie.Geeqie.appdata.xml',
    type : 'xml',
    po_dir : podir,
    install : true,
    install_dir : appdatadir)

configure_file(input: 'geeqie.spec.in', output: 'geeqie.spec', configuration: conf_data)
