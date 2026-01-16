# SPDX-License-Identifier: GPL-2.0-or-later

main_sources = files('advanced-exif.cc',
'advanced-exif.h',
'archives.cc',
'archives.h',
'bar.cc',
'bar.h',
'bar-comment.cc',
'bar-comment.h',
'bar-exif.cc',
'bar-exif.h',
'bar-histogram.cc',
'bar-histogram.h',
'bar-keywords.cc',
'bar-keywords.h',
'bar-rating.cc',
'bar-rating.h',
'bar-sort.cc',
'bar-sort.h',
'cache.cc',
'cache.h',
'cache-loader.cc',
'cache-loader.h',
'cache-maint.cc',
'cache-maint.h',
'cellrenderericon.cc',
'cellrenderericon.h',
'collect.cc',
'collect.h',
'collect-dlg.cc',
'collect-dlg.h',
'collect-io.cc',
'collect-io.h',
'collect-table.cc',
'collect-table.h',
'color-man.cc',
'color-man.h',
'color-man-heif.cc',
'color-man-heif.h',
'command-line-handling.cc',
'command-line-handling.h',
'compat.cc',
'compat.h',
'compat-deprecated.h',
'debug.cc',
'debug.h',
'desktop-file.cc',
'desktop-file.h',
'dnd.cc',
'dnd.h',
'dupe.cc',
'dupe.h',
'editors.cc',
'editors.h',
'exif-common.cc',
'exif.h',
'filecache.cc',
'filecache.h',
'filedata.cc',
'filedata.h',
'filefilter.cc',
'filefilter.h',
'fullscreen.cc',
'fullscreen.h',
'geometry.cc',
'geometry.h',
'histogram.cc',
'histogram.h',
'history-list.cc',
'history-list.h',
'image.cc',
'image.h',
'image-load.cc',
'image-load.h',
'image-load-collection.cc',
'image-load-collection.h',
'image-load-dds.cc',
'image-load-dds.h',
'image-load-external.cc',
'image-load-external.h',
'image-load-gdk.cc',
'image-load-gdk.h',
'image-load-libraw.cc',
'image-load-libraw.h',
'image-load-psd.cc',
'image-load-psd.h',
'image-load-svgz.cc',
'image-load-svgz.h',
'image-load-zxscr.cc',
'image-load-zxscr.h',
'image-overlay.cc',
'image-overlay.h',
'img-view.cc',
'img-view.h',
'intl.h',
'jpeg-parser.cc',
'jpeg-parser.h',
'layout.cc',
'layout.h',
'layout-config.cc',
'layout-config.h',
'layout-image.cc',
'layout-image.h',
'layout-util.cc',
'layout-util.h',
'logwindow.cc',
'logwindow.h',
'main.cc',
'main.h',
'main-defines.h',
'md5-util.cc',
'md5-util.h',
'menu.cc',
'menu.h',
'metadata.cc',
'metadata.h',
'misc.cc',
'misc.h',
'options.cc',
'options.h',
'osd.cc',
'osd.h',
'pan-view.h',
'pixbuf-renderer.cc',
'pixbuf-renderer.h',
'pixbuf-util.cc',
'pixbuf-util.h',
'preferences.cc',
'preferences.h',
'print.cc',
'print.h',
'rcfile.cc',
'rcfile.h',
'renderer-tiles.cc',
'renderer-tiles.h',
'search-and-run.cc',
'search-and-run.h',
'search.cc',
'search.h',
'shortcuts.cc',
'shortcuts.h',
'similar.cc',
'similar.h',
'slideshow.cc',
'slideshow.h',
'sort-type.cc',
'sort-type.h',
'thumb.cc',
'thumb.h',
'thumb-standard.cc',
'thumb-standard.h',
'toolbar.cc',
'toolbar.h',
'trash.cc',
'trash.h',
'ui-bookmark.cc',
'ui-bookmark.h',
'ui-file-chooser.cc',
'ui-file-chooser.h',
'ui-fileops.cc',
'ui-fileops.h',
'ui-help.cc',
'ui-help.h',
'ui-menu.cc',
'ui-menu.h',
'ui-misc.cc',
'ui-misc.h',
'ui-tabcomp.cc',
'ui-tabcomp.h',
'ui-tree-edit.cc',
'ui-tree-edit.h',
'ui-utildlg.cc',
'ui-utildlg.h',
'uri-utils.cc',
'uri-utils.h',
'utilops.cc',
'utilops.h',
'view-dir.cc',
'view-dir.h',
'view-dir-list.cc',
'view-dir-list.h',
'view-dir-tree.cc',
'view-dir-tree.h',
'view-file.h',
'window.cc',
'window.h')

if conf_data.get('HAVE_DJVU', 0) == 1
    main_sources += files(
        'image-load-djvu.cc',
        'image-load-djvu.h',
    )
endif

if conf_data.get('HAVE_EXIV2', 0) == 1
    main_sources += files(
        'exiv2.cc',
    )
else
    main_sources += files(
        'exif.cc',
        'exif-int.h',
        'format-canon.cc',
        'format-canon.h',
        'format-fuji.cc',
        'format-fuji.h',
        'format-nikon.cc',
        'format-nikon.h',
        'format-olympus.cc',
        'format-olympus.h',
        'format-raw.cc',
        'format-raw.h',
    )
endif

if conf_data.get('HAVE_FFMPEGTHUMBNAILER', 0) == 1
    main_sources += files(
        'image-load-ffmpegthumbnailer.cc',
        'image-load-ffmpegthumbnailer.h',
    )
endif

if conf_data.get('HAVE_EXR', 0) == 1
    main_sources += files(
        'image-load-exr.cc',
        'image-load-exr.h',
    )
endif

if conf_data.get('HAVE_FITS', 0) == 1
    main_sources += files(
        'image-load-fits.cc',
        'image-load-fits.h',
    )
endif

if conf_data.get('HAVE_HEIF', 0) == 1
    main_sources += files(
        'image-load-heif.cc',
        'image-load-heif.h',
    )
endif

if conf_data.get('HAVE_J2K', 0) == 1
    main_sources += files(
        'image-load-j2k.cc',
        'image-load-j2k.h',
    )
endif

if conf_data.get('HAVE_JPEG', 0) == 1
    main_sources += files(
        'image-load-jpeg.cc',
        'image-load-jpeg.h',
    )

    if conf_data.get('HAVE_RAW', 0) == 0
        main_sources += files(
            'image-load-cr3.cc',
            'image-load-cr3.h',
        )
    endif
endif

if conf_data.get('HAVE_JPEGXL', 0) == 1
    main_sources += files(
        'image-load-jpegxl.cc',
        'image-load-jpegxl.h',
    )
endif

if (conf_data.get('HAVE_LIBCHAMPLAIN', 0) == 1 and conf_data.get('HAVE_LIBCHAMPLAIN_GTK', 0) == 1) or conf_data.get('HAVE_LIBSHUMATE', 0) == 1
    main_sources += files(
        'bar-gps.cc',
        'bar-gps.h',
    )
endif

if conf_data.get('HAVE_LUA', 0) == 1
    main_sources += files(
        'glua.h',
        'lua.cc',
    )
endif

if conf_data.get('HAVE_NPY', 0) == 1
    main_sources += files(
        'image-load-npy.cc',
        'image-load-npy.h',
    )
endif

if conf_data.get('HAVE_PDF', 0) == 1
    main_sources += files(
        'image-load-pdf.cc',
        'image-load-pdf.h',
    )
endif

if conf_data.get('HAVE_TIFF', 0) == 1
    main_sources += files(
        'image-load-tiff.cc',
        'image-load-tiff.h',
    )
endif

if conf_data.get('HAVE_WEBP', 0) == 1
    main_sources += files(
        'image-load-webp.cc',
        'image-load-webp.h',
    )
endif

code_sources += main_sources

subdir('filedata')
subdir('pan-view')
subdir('third-party')
subdir('view-file')

## @FIXME code_sources is modified in ./tests/meson.build
# We assume that code_sources is only set above.
project_sources += code_sources

geeqie_exe = executable('geeqie', project_sources,
dependencies : [
gtk_dep,
champlain_dep,
champlain_gtk_dep,
clutter_dep,
clutter_gtk_dep,
ddjvuapi_dep,
exiv2_dep,
exr_dep,
fits_dep,
glib_dep,
gspell_dep,
imath_dep,
lcms_dep,
libarchive_dep,
libexecinfo_dep,
libffmpegthumbnailer_dep,
libheif_dep,
libjpeg_dep,
libjxl_dep,
libopenjp2_dep,
libraw_dep,
libwebp_dep,
lua_dep,
pango_dep,
poppler_glib_dep,
shumate_dep,
thread_dep,
tiff_dep
] + conditional_unit_test_deps,
include_directories : [configuration_inc], install : true)
