# SPDX-License-Identifier: GPL-2.0-or-later

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
#               /usr/local/share/applications           org.geeqie.Geeqie.desktop
# [gq_]appdir   /usr/local/share/geeqie/            *   org,geeqie.template.desktop
# desktopdir    /usr/local/share/geeqie/applications    plugin desktop files
# appdatadir    /usr/local/share/metainfo               org.geeqie.Geeqie.appdata.xml
# icondir       /usr/local/share/icons/hicolor/scalable/apps  geeqie.svg icon
# [gq_]helpdir  /usr/local/share/doc/geeqie         *   readme files etc.
# [gq_]htmldir  /usr/local/share/doc/geeqie/html    *   help files
# gq_localedir  /usr/locale/share/locale
# mandir1       /usr/local/share/man/man1               man page
# completionsdir /usr/local/share/bash-completion/completions bash command line completions
# podir         project_root/po

# * See meson_options.txt file

project(
    'geeqie',
    'c',
    'cpp',
    version : run_command('./build-aux/version.sh', check : true).stdout().strip(),
    license : ['GPL-2.0-or-later'],
    meson_version : '>=1.3.2',
    default_options : ['cpp_std=c++17', 'warning_level=3', 'buildtype=release', 'cpp_link_args=-rdynamic']
)

# To inhibit warnings from the generated file icons.h
add_global_arguments('-Wno-overlength-strings', language : 'c')

# To enable debug statements in all files
add_global_arguments('-include', 'debug.h', language : 'cpp')

# Project requirements
# code_sources is all human-written geeqie C/C++ code
code_sources = []
# project_sources will include code_sources, as well as auto-generated code,
# third-party code, and some other code-like dependencies.
project_sources = []
gnome = import('gnome')
thread_dep = dependency('threads')
cc = meson.get_compiler('cpp')
i18n = import('i18n')
fs = import('fs')
configuration_inc = include_directories('.')

option = get_option('git')
if not option.disabled()
    running_from_git = find_program('git', required: false).found() and fs.is_dir('.git')
else
    running_from_git = false
    summary({'git' : ['disabled - ChangeLog, ChangeLog.html, lua-api help file created:', false]}, section : 'Documentation', bool_yn : true)
endif

debug = get_option('debug')

# Note that main.cc sets prefix to the directory above where the executable is run from.
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
icondir = join_paths(datadir, 'icons/hicolor/scalable/apps/')
mandir1 = join_paths(datadir, 'man', 'man1')
completionsdir = join_paths(datadir, 'bash-completion', 'completions')

podir = join_paths(meson.project_source_root(), 'po')
buildauxdir = join_paths(meson.project_source_root(), 'build-aux')
packagingdir = join_paths(meson.project_source_root(), 'packaging')
testsdir = join_paths(meson.project_source_root(), 'teste')

summary({'gq_appdir': gq_appdir,
        'gq_bindir': gq_helpdir,
        'gq_helpdir': gq_helpdir,
        'gq_htmldir': gq_htmldir,
        'gq_localedir': gq_localedir,
        }, section: 'Directories')

# Create the define constants used in the sources. Set via config.h.in
conf_data = configuration_data()
conf_data.set('DEBUG', debug)


# Detect if mntent.h is available
conf_data.set('HAVE_MNTENT_H', 0)
if cc.has_header('mntent.h')
    conf_data.set('HAVE_MNTENT_H', 1)
endif

conf_data.set('HAVE_GTK4', 0)
option = get_option('gtk4')
if option.enabled()
    gtk_dep = dependency('gtk4', required: true)
    conf_data.set('HAVE_GTK4', 1)
else
    gtk_dep = dependency('gtk+-3.0', version : '>=3.24', required: true)
endif
glib_dep = dependency('glib-2.0', version : '>=2.66', required: true)
pango_dep = dependency('pango', version : '>=1.46', required: true)

# Required only for seg. fault stacktrace and backtrace debugging
conf_data.set('HAVE_EXECINFO_H', 0)
option = get_option('execinfo')
libexecinfo_dep = []
if not option.disabled()
    result = cc.check_header('execinfo.h')
    if result
        # Include unconditionally dependency for NetBSD.
        libexecinfo_dep = cc.find_library('execinfo', required : false)
        conf_data.set('HAVE_EXECINFO_H', 1)
        summary({'execinfo' : ['stacktrace supported:', true]}, section : 'Debugging', bool_yn : true)
    else
        summary({'execinfo' : ['stacktrace supported:', false]}, section : 'Debugging', bool_yn : true)
    endif
else
    summary({'execinfo' : ['stacktrace supported:', false]}, section : 'Debugging', bool_yn : true)
endif

# To override the default of 'release', run 'meson setup --buildtype=debug build'
# That is necessary for stacktrace output
buildtype = get_option('buildtype')
summary({'buildtype' : buildtype}, section : 'Debugging')

conf_data.set('ENABLE_UNIT_TESTS', 0)
option = get_option('unit_tests')
if not option.disabled()
    conf_data.set('ENABLE_UNIT_TESTS', 1)
    # Summary is handled below, where the test() itself is defined.
endif

conf_data.set('HAVE_ARCHIVE', 0)
libarchive_dep = []
req_version = '>=3.4.0'
option = get_option('archive')
if not option.disabled()
    libarchive_dep = dependency('libarchive', version : req_version, required : get_option('archive'))
    if libarchive_dep.found()
        conf_data.set('HAVE_ARCHIVE', 1)
        summary({'archive' : ['archive files e.g. .zip supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'archive' : ['libarchive ' + req_version + ' not found - archive files e.g. .zip supported::', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'archive' : ['disabled - archive files e.g. .zip supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_LCMS', 0)
lcms_dep = []
req_version = '>=2.0'
option = get_option('cms')
if not option.disabled()
    lcms_dep = dependency('lcms2', version : req_version, required : get_option('cms'))
    if lcms_dep.found()
        conf_data.set('HAVE_LCMS', 1)
        summary({'cms' : ['color management supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'cms' : ['lcms2' + req_version + ' not found - color management supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'cms' : ['disabled - color management supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_DJVU', 0)
ddjvuapi_dep = []
req_version = '>=2.5.27'
option = get_option('djvu')
if not option.disabled()
    ddjvuapi_dep = dependency('ddjvuapi', version : req_version, required : get_option('djvu'))
    if ddjvuapi_dep.found()
        conf_data.set('HAVE_DJVU', 1)
        summary({'djvu' : ['djvu files supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'djvu' : ['ddjvuapi ' + req_version + ' not found - djvu files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'djvu' : ['disabled - djvu files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

# Required only for seg. fault stacktrace and backtrace debugging
conf_data.set('HAVE_EXECINFO_H', 0)
option = get_option('execinfo')
if not option.disabled()
    result = cc.check_header('execinfo.h')
    if result
        conf_data.set('HAVE_EXECINFO_H', 1)
        summary({'execinfo' : ['stacktrace supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'execinfo' : ['stacktrace supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'execinfo' : ['stacktrace supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_EXIV2', 0)
exiv2_dep = []
req_version = '>=0.18'
option = get_option('exiv2')
if not option.disabled()
    exiv2_dep = dependency('exiv2', version : req_version, required : get_option('exiv2'))
    if exiv2_dep.found()
        conf_data.set('HAVE_EXIV2', 1)
        summary({'exiv2' : ['image metadata processed by exiv2:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'exiv2' : ['exiv2 ' + req_version + ' not found - image data not processed by exiv2:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'exiv2' : ['disabled - image data processed by exiv2:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_EXR', 0)
exr_dep = []
req_version_exr = '>=3.0.0'
imath_dep = []
req_version_imath = '>=3.0.0'
option = get_option('exr')
if not option.disabled()
    exr_dep = dependency('OpenEXR', version : req_version_exr, required : get_option('exr'))
    if exr_dep.found()
        imath_dep = dependency('Imath', version : req_version_imath, required : get_option('exr'))
        if imath_dep.found()
            conf_data.set('HAVE_EXR', 1)
            summary({'exr' : ['exr files supported:', true]}, section : 'Configuration', bool_yn : true)
        else
            summary({'exr' : ['imath ' + req_version_imath + ' not found - exr files supported:', false]}, section : 'Configuration', bool_yn : true)
        endif
    else
        summary({'exr' : ['exr ' + req_version_exr + ' not found - exr files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'exr' : ['disabled - exr files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_FITS', 0)
fits_dep = []
option = get_option('fits')
if not option.disabled()
    fits_dep = dependency('cfitsio', required: get_option('fits'))
    if fits_dep.found()
		conf_data.set('HAVE_FITS', 1)
		summary({'fits' : ['fits files supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'fits' : ['libcfitsio not found - fits files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'fits' : ['disabled - fits files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

champlain_dep = []
champlain_gtk_dep = []
clutter_dep = []
clutter_gtk_dep = []
shumate_dep = []

if conf_data.get('HAVE_GTK4', 0) == 1
    conf_data.set('HAVE_LIBSHUMATE', 0)
    req_version_shumate = '>=1.5.0'
    option = get_option('gps-map')
    if not option.disabled()
        shumate_dep = dependency('shumate-1.0', version : req_version_shumate, required : get_option('gps-map'))
        if shumate_dep.found()
            conf_data.set('HAVE_LIBSHUMATE', 1)
            summary({'gps-map' : ['GPS map displayed', true]}, section : 'Configuration', bool_yn : true)
        else
            summary({'gps-map' : ['shumate-1.0 ' + req_version_shumate + ' not found - GPS map displayed:', false]}, section : 'Configuration', bool_yn : true)
        endif
    else
        summary({'gps-map' : ['disabled - GPS map displayed:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    conf_data.set('HAVE_CLUTTER', 0)
    conf_data.set('HAVE_LIBCHAMPLAIN', 0)
    conf_data.set('HAVE_LIBCHAMPLAIN_GTK', 0)
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
                summary({'gps-map' : ['GPS map displayed', true]}, section : 'Configuration', bool_yn : true)
            else
                if not clutter_dep.found()
                    summary({'gps-map-clutter' : ['clutter-1.0 ' + req_version_clutter + ' not found - GPS map displayed:', false]}, section : 'Configuration', bool_yn : true)
                endif
                if not clutter_gtk_dep.found()
                    summary({'gps-map-clutter-gtk' : ['clutter-gtk-1.0 ' + req_version_clutter_gtk + ' not found - GPS map displayed:', false]}, section : 'Configuration', bool_yn : true)
                endif
            endif
        else
            if not champlain_dep.found()
                summary({'gps-map-champlain' : ['champlain-0.12 ' + req_version_champlain + ' not found - GPS map displayed:', false]}, section : 'Configuration', bool_yn : true)
            endif
            if not champlain_gtk_dep.found()
                summary({'gps-map-champlain-gtk' : ['champlain-gtk-0.12 ' + req_version_champlain_gtk + ' not found - GPS map displayed:', false]}, section : 'Configuration', bool_yn : true)
            endif
        endif
    else
        summary({'gps-map' : ['disabled - GPS map displayed:', false]}, section : 'Configuration', bool_yn : true)
    endif
endif

conf_data.set('HAVE_HEIF', 0)
libheif_dep = []
req_version = '>=1.3.2'
option = get_option('heif')
if not option.disabled()
    libheif_dep = dependency('libheif', version : req_version, required : get_option('heif'))
    if libheif_dep.found()
        conf_data.set('HAVE_HEIF', 1)
        summary({'heif' : ['heif files supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'heif' : ['libheif ' + req_version + ' not found - heif files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'heif' : ['disabled - heif files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_J2K', 0)
libopenjp2_dep = []
req_version = '>=2.3.0'
option = get_option('j2k')
if not option.disabled()
    libopenjp2_dep = dependency('libopenjp2', version : req_version, required : get_option('j2k'))
    if libopenjp2_dep.found()
        conf_data.set('HAVE_J2K', 1)
        summary({'j2k' : ['j2k files supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'j2k' : ['libopenjp2 ' + req_version + ' not found - j2k files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'j2k' : ['disabled - j2k files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_JPEG', 0)
libjpeg_dep = []
option = get_option('jpeg')
if not option.disabled()
    libjpeg_dep = dependency('libjpeg', required : get_option('jpeg'))
    if libjpeg_dep.found()
        if cc.has_function('jpeg_destroy_decompress', dependencies : libjpeg_dep)
            conf_data.set('HAVE_JPEG', 1)
            summary({'jpeg' : ['jpeg files supported:', true]}, section : 'Configuration', bool_yn : true)
        else
            summary({'jpeg' : ['jpeg_destroy_decompress not found - jpeg files supported:', false]}, section : 'Configuration', bool_yn : true)
        endif
    else
        summary({'jpeg' : ['libjpeg: not found', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'jpeg' : ['disabled - jpeg files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_JPEGXL', 0)
libjxl_dep = []
req_version = '>=0.3.7'
option = get_option('jpegxl')
if not option.disabled()
    libjxl_dep = dependency('libjxl', version : req_version, required : get_option('jpegxl'))
    if libjxl_dep.found()
        conf_data.set('HAVE_JPEGXL', 1)
        summary({'jpegxl' : ['jpegxl files supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'jpegxl' : ['libjxl ' + req_version + ' not found - jpegxl files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'jpegxl' : ['disabled - jpegxl files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_RAW', 0)
libraw_dep = []
req_version = '>=0.20'
option = get_option('libraw')
if not option.disabled()
    libraw_dep = dependency('libraw', version : req_version, required : get_option('libraw'))
    if libraw_dep.found()
        conf_data.set('HAVE_RAW', 1)
        summary({'libraw' : ['.cr3 files supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'libraw' : ['libraw ' + req_version + ' not found - .cr3 files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'libraw' : ['disabled - .cr3 files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_LUA', 0)
lua_dep = []
req_version = '>=5.3'
option = get_option('lua')
if not option.disabled()
    lua_dep = dependency('lua', 'lua5.3', 'lua-5.3', 'lua53', version: req_version, required: get_option('lua'))
    if lua_dep.found()
        conf_data.set('HAVE_LUA', 1)
        summary({'lua' : ['lua supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'lua' : ['lua ' + req_version + ' not found - lua supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'lua' : ['disabled - lua supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_NPY', 0)
option = get_option('npy')
if not option.disabled()
    conf_data.set('HAVE_NPY', 1)
    summary({'npy' : ['npy supported:', true]}, section : 'Configuration', bool_yn : true)
else
    summary({'npy' : ['npy supported:', false]}, section : 'Configuration', bool_yn : true)
endif

# Install standard documents
option = get_option('pandoc')
if not option.disabled()
    pandoc = find_program('pandoc', required : false)
    if pandoc.found()
        readme_html = custom_target(
            'README.html',
            input: 'README.md',
            output: 'README.html',
            command: [pandoc, '@INPUT@', '-o', '@OUTPUT@'],
            install: true,
            install_dir: helpdir)

        summary({'README' : ['README.html created:', true]}, section : 'Documentation', bool_yn : true)
    else
        summary({'README' : ['pandoc not found - README.html created:', false]}, section : 'Documentation', bool_yn : true)
    endif
else
    summary({'pandoc' : ['disabled - README.html created:', false]}, section : 'Documentation', bool_yn : true)
endif
install_data('README.md', 'COPYING', install_dir : helpdir)

conf_data.set('HAVE_PDF', 0)
poppler_glib_dep = []
req_version = '>=0.62'
option = get_option('pdf')
if not option.disabled()
    poppler_glib_dep = dependency('poppler-glib', version : req_version, required : get_option('pdf'))
    if poppler_glib_dep.found()
        conf_data.set('HAVE_PDF', 1)
        summary({'pdf'  : ['pdf files supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'pdf' : ['poppler-glib ' + req_version + ' not found - pdf files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'pdf' : ['disabled - pdf files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_SPELL', 0)
gspell_dep = []
req_version = '>=1.6'
option = get_option('spell')
if not option.disabled()
    gspell_dep = dependency('gspell-1', version : req_version, required: get_option('spell'))
    if gspell_dep.found()
        conf_data.set('HAVE_SPELL', 1)
        summary({'spell' : ['spelling checks enabled', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'spell' : ['gspell-1 ' + req_version + ' not found - spelling checks enabled', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'spell' : ['disabled - spelling checks enabled', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_TIFF', 0)
tiff_dep = []
option = get_option('tiff')
if not option.disabled()
    tiff_dep = dependency('libtiff-4', required: get_option('tiff'))
    if tiff_dep.found()
        if cc.has_function('TIFFClientOpen', dependencies : tiff_dep)
            conf_data.set('HAVE_TIFF', 1)
            summary({'tiff' : ['tiff files supported:', true]}, section : 'Configuration', bool_yn : true)
        else
            summary({'tiff' : ['TIFFClientOpen not found - tiff files supported:', false]}, section : 'Configuration', bool_yn : true)
        endif
    else
        summary({'tiff' : ['libtiff not found - tiff files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'tiff' : ['disabled - tiff files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_FFMPEGTHUMBNAILER', 0)
conf_data.set('HAVE_FFMPEGTHUMBNAILER_METADATA', 0)
conf_data.set('HAVE_FFMPEGTHUMBNAILER_RGB', 0)
conf_data.set('HAVE_FFMPEGTHUMBNAILER_WH', 0)
libffmpegthumbnailer_dep = []
req_version = '>=2.1.0'
option = get_option('videothumbnailer')
if not option.disabled()
    libffmpegthumbnailer_dep = dependency('libffmpegthumbnailer',
        version : req_version,
        required : get_option('videothumbnailer'))

    if libffmpegthumbnailer_dep.found()
        conf_data.set('HAVE_FFMPEGTHUMBNAILER', 1)
        summary({'videothumbnailer' : ['thumbnails of video files supported:', true]}, section : 'Configuration', bool_yn : true)

        result = cc.has_member('struct video_thumbnailer_struct', 'prefer_embedded_metadata', prefix : '#include <libffmpegthumbnailer/videothumbnailerc.h>')
        if result
            conf_data.set('HAVE_FFMPEGTHUMBNAILER_METADATA', 1)
        endif
        summary({'fmpegthumbnailer_metadata' : ['fmpegthumbnailer_metadata found:', result]}, section : 'Thumbnailer', bool_yn : true)

        result = cc.has_member('struct image_data_struct', 'image_data_width', prefix : '#include <libffmpegthumbnailer/videothumbnailerc.h>' )
        if result
            conf_data.set('HAVE_FFMPEGTHUMBNAILER_RGB', 1)
        endif
        summary({'fmpegthumbnailer_rgb' : ['fmpegthumbnailer_rgb found:', result]}, section : 'Thumbnailer', bool_yn : true)

        result = cc.has_function('video_thumbnailer_set_size', dependencies : libffmpegthumbnailer_dep)
        if result
            conf_data.set('HAVE_FFMPEGTHUMBNAILER_WH', 1)
        endif
        summary({'fmpegthumbnailer_set_size' : ['fmpegthumbnailer_set_size found:', result]}, section : 'Thumbnailer', bool_yn : true)
    else
        summary({'videothumbnailer' : ['libvideothumbnailer ' + req_version + ' not found - thumbnails of video files supported', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'videothumbnailer' : ['disabled -thumbnails of video files supported', false]}, section : 'Configuration', bool_yn : true)
endif

conf_data.set('HAVE_WEBP', 0)
libwebp_dep = []
req_version = '>=0.6.1'
option = get_option('webp')
if not option.disabled()
    libwebp_dep = dependency('libwebp', version : req_version, required : get_option('webp'))
    if libwebp_dep.found()
        conf_data.set('HAVE_WEBP', 1)
        summary({'webp' : ['webp files supported:', true]}, section : 'Configuration', bool_yn : true)
    else
        summary({'webp' : ['libwebp ' + req_version + ' not found - webp files supported:', false]}, section : 'Configuration', bool_yn : true)
    endif
else
    summary({'webp' : ['disabled - webp files supported:', false]}, section : 'Configuration', bool_yn : true)
endif

# Check for nl_langinfo and _NL_TIME_FIRST_WEEKDAY
conf_data.set('HAVE__NL_TIME_FIRST_WEEKDAY', 0)
code = '''#include <langinfo.h>
#include<stdio.h>
int main (int argc, char ** argv) {
    char *c;
    c =  nl_langinfo(_NL_TIME_FIRST_WEEKDAY);
    return 0;
}'''
if cc.links(code, name : 'nl_langinfo and _NL_TIME_FIRST_WEEKDAY')
    conf_data.set('HAVE__NL_TIME_FIRST_WEEKDAY', 1)
    summary({'nl_langinfo' : ['first weekday depends on locale:', true]}, section : 'Documentation', bool_yn : true)
else
    summary({'nl_langinfo' : ['nl_langinfo not found - first weekday depends on locale:', false, 'first weekday defaults to Monday']}, section : 'Documentation', bool_yn : true)
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

conditional_unit_test_deps = []
if conf_data.get('ENABLE_UNIT_TESTS', 0) == 1
    system_gtest_dep = dependency('gtest', main : false, required : false)
    system_gmock_dep = dependency('gmock', required : false)
    if system_gtest_dep.found() and system_gmock_dep.found()
        conditional_unit_test_deps += system_gtest_dep
        conditional_unit_test_deps += system_gmock_dep
    else
        # Use the subproject gtest as a fallback.
        gtest_subproj = subproject('gtest')
        conditional_unit_test_deps += gtest_subproj.get_variable('gtest_dep')
        conditional_unit_test_deps += gtest_subproj.get_variable('gmock_dep')
    endif
endif

# Generate css, icons and ui files
subdir('data')

if conf_data.get('ENABLE_UNIT_TESTS', 0) == 1
    subdir('tests')
endif

# Generate the executable
subdir('src')

subdir('build-aux')

# Generate the help files
subdir('doc')

geeqie_icon = join_paths(meson.project_source_root(), 'data', 'icons', 'geeqie.svg')
install_data(geeqie_icon, install_dir : icondir)

i18n.merge_file(
    input : 'data/org.geeqie.Geeqie.desktop.in',
    output : 'org.geeqie.Geeqie.desktop',
    type : 'desktop',
    po_dir : podir,
    install : true,
    install_dir : join_paths(datadir, 'applications'))

i18n.merge_file(
    input : 'data/org.geeqie.Geeqie.metainfo.xml.in',
    output : 'org.geeqie.Geeqie.metainfo.xml',
    type : 'xml',
    po_dir : podir,
    install : true,
    install_dir : appdatadir)

geeqie_spec_in = join_paths(packagingdir, 'rpm', 'geeqie.spec.in')
configure_file(input: geeqie_spec_in, output: 'geeqie.spec', configuration: conf_data)

isolate_test_sh = find_program('isolate-test.sh', dirs : buildauxdir, required : true)

# Basic test of the executable
xvfb = find_program('xvfb-run', required : false)
if xvfb.found()
    test_cmd = [xvfb.full_path(), '--auto-servernum', geeqie_exe.full_path(), '--version']
    test('Basic test', isolate_test_sh, args: test_cmd, timeout: 100, suite: 'functional')
    summary({'xvfb' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)
else
    summary({'xvfb' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
endif

# The tests are run on GitHub with all options disabled, and then
# Image tests use option unit_tests as a flag so that normal users do not
# download the test image database.

# Image checks
option = get_option('unit_tests')
if option.enabled()
    if xvfb.found()
        get_test_images_sh = find_program('get-test-images.sh', dirs : buildauxdir, required : true)
        image_test_py = find_program('image-test.py', dirs : buildauxdir, required : true)

        images_dir = join_paths(meson.current_build_dir(), 'test-images.p')

        message('Downloading test images')
        sources_list = run_command(get_test_images_sh, images_dir,  'https://github.com/BestImageViewer/geeqie-test-images.git', check: true)

        sources = sources_list.stdout().strip().split('\n')

        foreach image : sources
            path_array = image.split('/')
            image_name = path_array[path_array.length() - 1]

            should_fail = image_name.startswith('fail')
            test_cmd = [image_test_py.full_path(), geeqie_exe.full_path(), image]
            test('Image_ ' + image_name, isolate_test_sh, args: test_cmd, should_fail : should_fail, timeout: 100, suite: ['functional', 'image'])

        endforeach
        summary({'Image tests' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)
    else
        summary({'Image tests' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
    endif
else
    summary({'Image tests' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
endif

# Code correctness checks
if running_from_git
    clang_tidy_exe = find_program('clang-tidy', required : false)
    if clang_tidy_exe.found()
        git_exe = find_program('git', required : true)

        foreach source_file : code_sources
            if fs.name(source_file).endswith('.cc')
                source_file_name = fs.name(source_file)
                config_file = join_paths(meson.project_source_root(), '.clang-tidy')

                test('Code Correctness_ ' + source_file_name, clang_tidy_exe, args : ['-p', './build', '-quiet', '--config-file', config_file,  source_file], timeout : 100, suite : 'analysis')
            endif
        endforeach

        summary({'Code Correctness' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)
    else
        summary({'Code Correctness' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
    endif
else
    summary({'Code Correctness' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
endif

# Single value enum checks
enum_check_sh = find_program('enum-check.sh', dirs : buildauxdir, required : true)
if enum_check_sh.found()
    foreach source_file : code_sources
        source_file_name = fs.name(source_file)
        test('Single Value enum_ ' + source_file_name, enum_check_sh, args : [source_file], timeout : 100, suite : 'analysis')
    endforeach

    summary({'Single Value enum' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)
else
    summary({'Single Value enum' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
endif

# Debug statement checks
debug_check_sh = find_program('debug-check.sh', dirs : buildauxdir, required : true)
if debug_check_sh.found()
    foreach source_file : code_sources
        source_file_name = fs.name(source_file)
        if (source_file_name != 'debug.h')
            test('Debug Statements_ ' + source_file_name, debug_check_sh, args : [source_file], timeout : 100, suite : 'analysis')
        endif
    endforeach

    summary({'Debug Statements' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)
else
    summary({'Debug Statements' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
endif

# Temporary comments checks
tmp_comments_check_sh = find_program('temporary-comments-check.sh', dirs : buildauxdir, required : true)
if tmp_comments_check_sh.found()
    foreach source_file : code_sources
        source_file_name = fs.name(source_file)
        if (source_file_name != 'debug.h')
            test('Temporary Comments_ ' + source_file_name, tmp_comments_check_sh, args : [source_file], timeout : 100, suite : 'analysis')
        endif
    endforeach

    summary({'Temporary Comments' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)
else
    summary({'Temporary Comments' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
endif

# GTK4 migration regression checks
gtk4_migration_check_sh = find_program('gtk4-migration-regression-check.sh', dirs : buildauxdir, required : true)
if gtk4_migration_check_sh.found()
    compat_cc = join_paths(meson.project_source_root(), 'src', 'compat.cc')
    compat_h = join_paths(meson.project_source_root(), 'src', 'compat.h')
    foreach source_file : code_sources
        source_file_name = fs.name(source_file)
        if (source_file_name != 'debug.h')
            test('GTK4 migration_ ' + source_file_name, gtk4_migration_check_sh, args : [source_file, compat_cc, compat_h], timeout : 100, suite : 'analysis')
        endif
    endforeach

    summary({'GTK4 migration' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)
else
    summary({'GTK4 migration' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
endif

# Untranslated text checks
untranslated_text_sh = find_program('untranslated-text.sh', dirs : buildauxdir, required : true)
if untranslated_text_sh.found()
     foreach source_file : code_sources
        if fs.name(source_file).endswith('.cc')
            source_file_name = fs.name(source_file)
            test('Untranslated Text_ ' + source_file_name, untranslated_text_sh, args : [source_file], timeout : 200, suite : 'analysis')
        endif
    endforeach

    summary({'Untranslated Text' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)
else
    summary({'Untranslated Text' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
endif

# Lua test
option = get_option('lua')
if not option.disabled()
    if lua_dep.found()
        if xvfb.found()
            lua_test_sh = find_program('lua-test.sh', dirs : buildauxdir, required : true)
            test('Lua test', isolate_test_sh, args: [lua_test_sh.full_path(), geeqie_exe.full_path()], timeout: 100, suite : 'analysis')

            summary({'lua' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)
        else
            summary({'lua' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
        endif
    else
        summary({'lua' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
    endif
else
    summary({'lua' : ['Test runs:', false]}, section : 'Testing', bool_yn : true)
endif

# Ancillary files test
test_ancillary_files_sh = find_program('test-ancillary-files.sh', dirs : buildauxdir, required : true)
test('Ancillary files', test_ancillary_files_sh, args: [meson.current_source_dir()], timeout: 100, suite : 'analysis')

summary({'Ancillary files' : ['Test runs:', true]}, section : 'Testing', bool_yn : true)

# Unit tests
if conf_data.get('ENABLE_UNIT_TESTS', 0) == 1
    test('Unit tests', isolate_test_sh, args: [geeqie_exe.full_path(), '--run-unit-tests'], suite : 'unit')
    summary({'unit_tests' : ['Tests run:', true]}, section : 'Testing', bool_yn : true)
else
    summary({'unit_tests' : ['Tests run:', false]}, section : 'Testing', bool_yn : true)
endif
