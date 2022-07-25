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
    meson_version : '>=0.53.0',
    default_options : ['warning_level=3', 'buildtype=debugoptimized']
)

# To inhibit warnings from the generated files icons_inline.h and ui_icons.h
add_global_arguments('-Wno-overlength-strings', language : 'c')

# Used for colored text in user messages
c_red = '\033[91m'
c_end = '\033[0m'
disabled = c_red + 'DISABLED' + c_end
notfound = c_red + ' NOT FOUND' + c_end

# Project requirements
project_sources = []
gnome = import('gnome')
thread_dep = dependency('threads')
cc = meson.get_compiler('c')
i18n = import('i18n')
configuration_inc = include_directories('.')

user_options_message = 'Optional features:\n'
user_documentation_message = 'Documentation and other features:\n'
user_thumbnailer_message = 'Thumbnailer options:\n'

# External programs
gdk_pixbuf_csource = find_program('gdk-pixbuf-csource', required : true)
glib_genmarshal = find_program('glib-genmarshal', required : true)
gnome_doc_tool = find_program('yelp-build', required : false)
if gnome_doc_tool.found()
    user_documentation_message += 'Help files will be created\n'
else
    user_documentation_message += 'yelp-build ' + notfound + ' - Help files not created\n'
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

podir = join_paths(meson.source_root(), 'po')

# Create the define constants used in the sources. Set via config.h.in
conf_data = configuration_data()
conf_data.set_quoted('VERSION', run_command('./version.sh', check : true).stdout())
conf_data.set('DEBUG', debug)

gtk_dep = dependency('gtk+-3.0', version : '>=3.22', required: true)
glib_dep = dependency('glib-2.0', version : '>=2.52', required: true)

libarchive_dep = []
req_version = '>=3.4.0'
option = get_option('archive')
if not option.disabled()
    libarchive_dep = dependency('libarchive', version : req_version, required : get_option('archive'))
    if libarchive_dep.found()
        conf_data.set('HAVE_ARCHIVE', 1)
        user_options_message += 'archive: archive files e.g. .zip supported\n'
    else
        user_options_message += 'archive: libarchive ' + req_version + notfound + ' - archive files e.g. .zip not supported\n'
    endif
else
    user_options_message += 'archive: ' + disabled + ' - archive files e.g. .zip not supported\n'
endif

lcms_dep = []
req_version = '>=2.0'
option = get_option('cms')
if not option.disabled()
    lcms_dep = dependency('lcms2', version : req_version, required : get_option('cms'))
    if lcms_dep.found()
        conf_data.set('HAVE_LCMS', 1)
        conf_data.set('HAVE_LCMS2', 1)
        user_options_message += 'cms: color management supported\n'
    else
        user_options_message += 'cms: lcms2 '+ req_version + notfound + ' - color management not supported\n'
    endif
else
    user_options_message += 'cms: ' + disabled + ' - color management not supported\n'
endif

ddjvuapi_dep = []
req_version = '>=2.5.27'
option = get_option('djvu')
if not option.disabled()
    ddjvuapi_dep = dependency('ddjvuapi', version : req_version, required : get_option('djvu'))
    if ddjvuapi_dep.found()
        conf_data.set('HAVE_DJVU', 1)
        user_options_message += 'djvu: djvu files supported\n'
    else
        user_options_message += 'djvu: ddjvuapi ' + req_version + notfound + ' - djvu files not supported\n'
    endif
else
    user_options_message += 'djvu: ' + disabled + ' - djvu files not supported\n'
endif

exiv2_dep = []
req_version = '>=0.11'
option = get_option('exiv2')
if not option.disabled()
    exiv2_dep = dependency('exiv2', version : req_version, required : get_option('exiv2'))
    if exiv2_dep.found()
        conf_data.set('HAVE_EXIV2', 1)
        user_options_message += 'exiv2: image metadata processed by exiv2\n'
    else
        user_options_message += 'exiv2: exiv2 ' + req_version + notfound + ' - image data not processed by exiv2\n'
    endif
else
    user_options_message += 'exiv2: ' + disabled + '- image data not processed by exiv2\n'
endif

libffmpegthumbnailer_dep = []
req_version = '>=2.1.0'
option = get_option('videothumbnailer')
if not option.disabled()
    libffmpegthumbnailer_dep = dependency('libffmpegthumbnailer',
        version : req_version,
        required : get_option('videothumbnailer'))

    if libffmpegthumbnailer_dep.found()
        conf_data.set('HAVE_FFMPEGTHUMBNAILER', 1)
        user_options_message += 'videothumbnailer: thumbnails of video files supported\n'

        if cc.has_member('struct video_thumbnailer_struct', 'prefer_embedded_metadata',
            prefix : '#include <libffmpegthumbnailer/videothumbnailerc.h>')

            conf_data.set('HAVE_FFMPEGTHUMBNAILER_METADATA', 1)
            user_thumbnailer_message += 'fmpegthumbnailer_metadata: found\n'
        else
            user_thumbnailer_message += 'ffmpegthumbnailer_metadata: ' + notfound + '\n'
        endif

        if cc.has_member('struct image_data_struct', 'image_data_width',
            prefix : '#include <libffmpegthumbnailer/videothumbnailerc.h>' )

            conf_data.set('HAVE_FFMPEGTHUMBNAILER_RGB', 1)
            user_thumbnailer_message += 'ffmpegthumbnailer_rgb: found\n'
        else
            user_thumbnailer_message += 'ffmpegthumbnailer_rgb: ' + notfound + '\n'
        endif

        if cc.has_function('video_thumbnailer_set_size',
            dependencies : libffmpegthumbnailer_dep)

            conf_data.set('HAVE_FFMPEGTHUMBNAILER_WH', 1)
            user_thumbnailer_message += 'ffmpegthumbnailer_set_size: found\n'
        else
            user_thumbnailer_message += 'ffmpegthumbnailer_set_size: ' + notfound + '\n'
        endif
    else
        user_options_message += 'videothumbnailer: libvideothumbnailer ' + req_version + notfound + ' - no thumbnails of video files\n'
    endif
else
    user_options_message += 'videothumbnailer: ' + disabled + '- no thumbnails of video files\n'
endif

champlain_dep = []
champlain_gtk_dep = []
clutter_dep = []
clutter_gtk_dep = []
req_version_champlain = '>=0.12'
req_version_champlain_gtk = '>=0.12'
req_version_clutter = '>=1.0'
req_version_clutter_gtk = '>=1.0'
option = get_option('gps-map')
if not option.disabled()
    champlain_dep = dependency('champlain-0.12', version : req_version_champlain, required : get_option('gps-map'))
    champlain_gtk_dep = dependency('champlain-gtk-0.12', version : req_version_champlain_gtk, required : get_option('gps-map'))
    if champlain_dep.found() and champlain_gtk_dep.found()
        clutter_dep = dependency('clutter-1.0', version : req_version_clutter, required : get_option('gps-map'))
        clutter_gtk_dep = dependency('clutter-gtk-1.0', version : req_version_clutter_gtk, required : get_option('gps-map'))
        if clutter_dep.found() and clutter_gtk_dep.found()
            conf_data.set('HAVE_CLUTTER', 1)
            conf_data.set('HAVE_LIBCHAMPLAIN', 1)
            conf_data.set('HAVE_LIBCHAMPLAIN_GTK', 1)
            user_options_message += 'gps-map: GPS map displayed\n'
        else
            if not clutter_dep.found()
                user_options_message += 'gps-map: clutter-1.0 ' + req_version_clutter + notfound + ' - GPS map not displayed\n'
            endif
            if not clutter_gtk_dep.found()
                user_options_message += 'gps-map: clutter-gtk-1.0 ' + req_version_clutter_gtk + notfound + ' - GPS map not displayed\n'
            endif
        endif
    else
        if not champlain_dep.found()
            user_options_message += 'gps-map: champlain-0.12 ' + req_version_champlain + notfound + ' - GPS map not displayed\n'
        endif
        if not champlain_gtk_dep.found()
            user_options_message += 'gps-map: champlain-gtk-0.12 ' + req_version_champlain_gtk + notfound + ' - GPS map not displayed\n'
        endif
    endif
else
    user_options_message += 'gps-map: ' + disabled + ' - GPS map not displayed\n'
endif

libheif_dep = []
req_version = '>=1.3.2'
option = get_option('heif')
if not option.disabled()
    libheif_dep = dependency('libheif', version : req_version, required : get_option('heif'))
    if libheif_dep.found()
        conf_data.set('HAVE_HEIF', 1)
        user_options_message += 'heif: heif files supported\n'
    else
        user_options_message += 'heif: libheif ' + req_version + notfound + ' - heif files not supported\n'
    endif
else
    user_options_message += 'heif: ' + disabled + ' - heif files not supported\n'
endif

libopenjp2_dep = []
req_version = '>=2.3.0'
option = get_option('j2k')
if not option.disabled()
    libopenjp2_dep = dependency('libopenjp2', version : req_version, required : get_option('j2k'))
    if libopenjp2_dep.found()
        conf_data.set('HAVE_J2K', 1)
        user_options_message += 'j2k: j2k files supported\n'
    else
        user_options_message += 'j2k: libopenjp2 ' + req_version + notfound + ' - j2k files not supported\n'
    endif
else
    user_options_message += 'j2k: ' + disabled + ' - j2k files not supported\n'
endif

libjpeg_dep = []
option = get_option('jpeg')
if not option.disabled()
libjpeg_dep = dependency('libjpeg', required : get_option('jpeg'))
    if libjpeg_dep.found()
        if cc.has_function('jpeg_destroy_decompress', dependencies : libjpeg_dep)
            conf_data.set('HAVE_JPEG', 1)
            user_options_message += 'jpeg: jpeg files supported\n'
        else
            user_options_message += 'jpeg: jpeg_destroy_decompress ' + notfound + ' - jpeg files not supported\n'
        endif
    else
        user_options_message += 'jpeg: libjpeg: ' + notfound + '\n'
    endif
else
    user_options_message += 'jpeg: ' + disabled + ' - jpeg files not supported\n'
endif

libjxl_dep = []
req_version = '>=0.3.7'
option = get_option('jpegxl')
if not option.disabled()
    libjxl_dep = dependency('libjxl', version : req_version, required : get_option('jpegxl'))
    if libjxl_dep.found()
        conf_data.set('HAVE_JPEGXL', 1)
        user_options_message += 'jpegxl: jpegxl files supported\n'
    else
        user_options_message += 'jpegxl: libjxl ' + req_version + notfound + ' - jpegxl files not supported\n'
    endif
else
    user_options_message += 'jpegxl: ' + disabled + ' - jpegxl files not supported\n'
endif

libraw_dep = []
req_version = '>=0.20'
option = get_option('libraw')
if not option.disabled()
    libraw_dep = dependency('libraw', version : req_version, required : get_option('libraw'))
    if libraw_dep.found()
        conf_data.set('HAVE_RAW', 1)
        user_options_message += 'libraw: .cr3 files supported\n'
    else
        user_options_message += 'libraw: libraw ' + req_version + notfound + ' - .cr3 files not supported\n'
    endif
else
    user_options_message += 'libraw: ' + disabled + ' - .cr3 files not supported\n'
endif

lua_dep = []
req_version = '>=5.1'
option = get_option('lua')
if not option.disabled()
    lua_dep = dependency('lua5.1',  version : req_version, required : get_option('lua'))
    if lua_dep.found()
        conf_data.set('HAVE_LUA', 1)
        user_options_message += 'lua: lua supported\n'
    else
        user_options_message += 'lua: lua5.1 ' + req_version + notfound + ' - lua not supported\n'
    endif
else
    user_options_message += 'lua: ' + disabled + ' - lua not supported\n'
endif

poppler_glib_dep = []
req_version = '>=0.62'
option = get_option('pdf')
if not option.disabled()
    poppler_glib_dep = dependency('poppler-glib', version : req_version, required : get_option('pdf'))
    if poppler_glib_dep.found()
        conf_data.set('HAVE_PDF', 1)
        user_options_message += 'pdf: pdf files supported\n'
    else
        user_options_message += 'pdf: poppler-glib ' + req_version + notfound + ' - pdf files not supported\n'
    endif
else
    user_options_message += 'pdf: ' + disabled + ' - pdf files not supported\n'
endif

gspell_dep = []
req_version = '>=1.6'
option = get_option('spell')
if not option.disabled()
    gspell_dep = dependency('gspell-1', version : req_version, required: get_option('spell'))
    if gspell_dep.found()
        conf_data.set('HAVE_SPELL', 1)
        user_options_message += 'spell: spelling checks enabled\n'
    else
        user_options_message += 'spell: gspell-1 ' + req_version + notfound + ' - spelling checks not enabled\n'
    endif
else
    user_options_message += 'spell: ' + disabled + ' - spelling checks not enabled\n'
endif

tiff_dep = []
option = get_option('tiff')
if not option.disabled()
    tiff_dep = cc.find_library('libtiff', required: get_option('tiff'))
    if tiff_dep.found()
        if cc.has_function('TIFFClientOpen',
            dependencies : tiff_dep)

            conf_data.set('HAVE_TIFF', 1)
            user_options_message += 'tiff: tiff files supported\n'
        else
            user_options_message += 'tiff: TIFFClientOpen ' + notfound + ' - tiff files not supported\n'
        endif
    else
        user_options_message += 'tiff: libtiff ' + notfound + ' - tiff files not supported\n'
    endif
else
    user_options_message += 'tiff: ' + disabled + ' - tiff files not supported\n'
endif

libwebp_dep = []
req_version = '>=0.6.1'
option = get_option('webp')
if not option.disabled()
    libwebp_dep = dependency('libwebp', version : req_version, required : get_option('webp'))
    if libwebp_dep.found()
        conf_data.set('HAVE_WEBP', 1)
        user_options_message += 'webp: webp files supported\n'
    else
        user_options_message += 'webp: libwebp ' + req_version + notfound + ' - webp files not supported\n'
    endif
else
    user_options_message += 'webp: ' + disabled + ' - webp files not supported\n'
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
    user_documentation_message += 'nl_langinfo - first weekday depends on locale\n'
else
    user_documentation_message += 'nl_langinfo ' + notfound + ' - first weekday defaults to Monday\n'
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

# Generate the executable
subdir('src')

# Generate the help files
subdir('doc')

# Install other project files
run_command(find_program('gen_changelog.sh'), meson.source_root(), meson.build_root(), check : true)

pandoc = find_program('pandoc', required : false)
if pandoc.found()
    run_command(find_program('gen_readme.sh'), meson.source_root(), meson.build_root(), check : false)

    install_data('README.md', 'COPYING', 'TODO', 'AUTHORS',
        join_paths(meson.build_root(), 'ChangeLog'),
        join_paths(meson.build_root(), 'README.html'),
        join_paths(meson.build_root(), 'ChangeLog.html'),
        install_dir : helpdir)

    user_documentation_message += 'README.html created\n'
else
    install_data('README.md', 'COPYING', 'TODO', 'AUTHORS',
        join_paths(meson.build_root(), 'ChangeLog'),
        join_paths(meson.build_root(), 'ChangeLog.html'),
        install_dir : helpdir)

    user_documentation_message += 'pandoc ' + notfound + ' - README.html not created\n'
endif

evince = find_program('evince', required : false)
if evince.found()
    user_documentation_message += 'print preview supported \n'
else
    user_documentation_message += 'evince ' + notfound + ' - print preview not available\n'
endif

install_data('geeqie.png', install_dir : icondir)
install_data('geeqie.1', install_dir : mandir1)

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

user_message = ''.join(['Configuration Summary -\n\n', user_options_message, '\n', user_documentation_message, '\n', user_thumbnailer_message])
message(user_message)
