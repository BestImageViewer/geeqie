      ###################################################################
      ##                          Geeqie x.x                           ##
      ##                                                               ##
      ##              Copyright (C) 2008 - 2018 The Geeqie Team        ##
      ##              Copyright (C) 1999 - 2006 John Ellis.            ##
      ##                                                               ##
      ##                      Use at your own risk!                    ##
      ##                                                               ##
      ##  This software released under the GNU General Public License. ##
      ##       Please read the COPYING file for more information.      ##
      ###################################################################

This is Geeqie, a successor of GQview.

[![Build Status](https://api.travis-ci.org/BestImageViewer/geeqie.svg?branch=master)](https://travis-ci.org/BestImageViewer/geeqie)

Geeqie has been forked from GQview project, because it was not possible to
contact the GQview author and only maintainer.

The Geeqie project will continue the development forward and maintain the existing code.

Geeqie is currently considered stable.

Please send any questions, problems or suggestions to the [mailing list](mailto:geeqie@freelists.org) or
open an issue on [Geeqie at GitHub](https://github.com/BestImageViewer/geeqie/issues).

Subscribe to the mailing list [here](https://www.freelists.org/list/geeqie).

The project website is <http://www.geeqie.org/> and you will find the latest sources in the
[Geeqie repository](http://geeqie.org/cgi-bin/gitweb.cgi?p=geeqie.git).

# README contents:

* Description / Features
* Downloading
* Installation
* Notes and changes for this release
* Requirements


## Description / Features

Geeqie is a graphics file viewer. Basic features:


* Single click image viewing / navigation.

* Zoom functions.

* Thumbnails, with optional caching and .xvpics support.

* Multiple file selection for move, copy, delete, rename, drag and drop.

* Thumbnail preview of the destination for move, copy and rename functions.

* On-the-fly renaming for move and copy functions, with formatted and auto-rename features.

* File grouping (an image having jpeg, RAW and xmp files will appear as a single entity).

* Selectable exif auto-rotation of images.


* Single click file copy or move to pre-defined folders - with undo feature.
* Drag and drop.

* Collections.

* support for stereoscopic images
    * input: side-by-side (JPS) and MPO format
    * output: single image, anaglyph, SBS, mirror, SBS half size (3DTV)

*   Viewing raster and vector images, in the following formats:
3FR, ANI, APM, ARW, BMP, CR2, CRW, CUR, DNG, ERF, GIF, HEIC, ICNS, ICO, JPE/JPEG/JPG, JPS, KDC, MEF, MPO, MOS, MRW, NEF, ORF, PEF, PTX, PBM/PGM/PNM/PPM, PNG, QIF/QTIF (QuickTime Image Format), RAF, RAW, RW2, SR2, SRF, SVG/SVGZ, TGA/TARGA, TIF/TIFF, WMF, XBM, XPM, HEIF (primary image only), WEBP, DjVu. Animated GIFs are supported.

* Preview and thumbnails of video clips can be displayed. Clips can be run via a defined external program.

* Images can be displayed singly in normal or fullscreen mode; static or slideshow mode; in sets of two or four per page for comparison; or as thumbnails of various sizes. Synchronised zoom when multi images are displayed.

* Pan(orama) view displays image thumbnails in calendar, grid, folder and other layouts.
* All available metadata and Exif/IPTC/XMP data can be displayed, as well as colour histograms and assigned tags, keywords and comments.

* Selectable image overlay display box - can contain any text or meta-data.

* Panels can be docked or floating.

* Tags, both predefined and custom, can be assigned to images, and stored either as image metadata (where the file format allows), sidecar files, or in directory metadata files. Keywords and comments can also be assigned.

* Basic editing in the form of lossless 90/180-degree rotation and flipping is supported; external programs such as GIMP, Inkscape, and custom scripts using ImageMagick can be linked to allow further processing.

* Advanced searching is available using criteria such as filename, file size, age, image dimensions, similarity to a specified image, or by keywords or comments. If images have GPS coordinates embedded, you may also search for images within a radius of a geographical point.

* Geeqie supports applying the colour profile embedded in an image along with the system monitor profile (or a user-specified monitor profile).

* Geeqie sessions can be remotely controlled from external software, so it can be used as an image-viewer component of a bigger application.

* Geeqie includes a 'find duplicates' tool which can compare images using a variety of criteria (filename, file size, visual similarity, dimensions, image content), either within a single folder or between two folders. Finding duplicates ignoring the rotation of images is also supported.
* Images may be given a rating value (also known as a "star rating").

* Maps from [OpenStreetMap](http://www.openstreetmap.org) may be displayed in a side panel. If an image has GPS coordinates embedded, its position will be displayed on the map - if Image Direction is encoded, that will be displayed also. If an image does not have embedded GPS coordinates, it may be dragged-and-dropped onto the map to encode its position.

## Downloading

Geeqie is available as a package with some distributions, however Geeqie is stable and you may compile the latest version from sources.

There are two scripts which will download and compile the sources for you.

The first script will install Geeqie to a defined location, and will run under any system. However, it is left to you to make sure dependencies are fulfilled.
To get the script, from the command line type:<br/><br/>
`wget https://raw.githubusercontent.com/pixlsus/Scripts/master/build-geeqie`

The second script will run only on Debian-based system, but will fulfil all dependencies and also give you the opportunity to include additional pixbuf loaders and other useful programs.
To get the script, from the command line type:<br/><br/>
`wget https://raw.githubusercontent.com/BestImageViewer/geeqie/master/web/geeqie-install-debian.sh`


If you wish to compile the sources yourself you may download the latest version (if you have installed git) from here:

Either: `git clone git://www.geeqie.org/geeqie.git`

Or: `git clone http://www.geeqie.org/git/geeqie.git`

## Manual Installation

List compile options: `./autogen.sh --help`

Common options:
`./autogen.sh --disable-gtk3`,

Compilation: `./autogen.sh [options]; make -j<no. of cpu cores> `

Install: `[sudo] make install`

Removal: `[sudo] make uninstall`

#### Note:
The zip and gzip files at geeqie.org and GitHub contain only the sources - they cannot, by themselves, be used to install Geeqie.

It is recommended to always use `git clone  git://www.geeqie.org/geeqie.git` to download Geeqie. After installing Geeqie you may delete the folder you have cloned Geeqie into.

However if you leave the folder intact, whenever new features or patches are available, execute:

`git pull; sudo make uninstall; sudo make maintainer-clean; ./autogen.sh; make -j<no. of cpu cores>; sudo make install`

Only the changed sources are downloaded, which makes this a quick operation.

Your configuration file, history file and desktop files are not affected by this process.



## Notes and changes for the latest release

See the NEWS file in the installation folder, or [Geeqie News at GitHub](https://github.com/BestImageViewer/geeqie/blob/master/NEWS)

And either the ChangeLog file or [Geeqie ChangeLog](http://geeqie.org/cgi-bin/gitweb.cgi?p=geeqie.git;a=shortlog)


## Requirements

### Required libraries:
    GTK+ 3.00
        www.gtk.org
        enabled by default
        disable with configure option: --disable-gtk3
    or
    GTK+ 2.20
        disabled by default when GTK+3 libraries are found.
        enable with configure option: --disable-gtk3
        optional items map display and GPU acceleration are not available
        with GTK2

        Note: GTK+3 is still somehow experimental. It is needed for some
        features but we have several complains about the GTK+3 usability.

        So if you need a stable version, you are advised to compile it
        with GTK+2. If you want to play with the cool new features, use
        GTK+3.

### Optional libraries:
    lcms2 2.0
    or
    lcms 1.14
        www.littlecms.com
        for color management support
        enabled by default
        disable with configure option: --disable-lcms

    exiv2 0.11
        www.exiv2.org
        for enhanced exif support
        enabled by default
        disable with configure option: --disable-exiv2

    lirc
        www.lirc.org
        for remote control support
        enabled by default
        disable with configure option: --disable-lirc

    libchamplain-gtk 0.12
    libchamplain 0.12
    libclutter 1.0
        wiki.gnome.org/Projects/libchamplain
        for map display
        enabled by default
        disable with configure option: --disable-map

    libclutter 1.0
        www.clutter-project.org
        for GPU acceleration (a check-box on Preferences/Image must also be ticked)
        enabled by default
        disable with configure option: --disable-gpu-accel
        explicitly disabling will also disable the map feature

    lua 5.1
        www.lua.org
        support for lua scripting
        enabled by default
        disable with configure option: --disable-lua

    librsvg2-common
        for displaying .svg images

    libwmf0.2-7-gtk
        for displaying .wmf images

    (see also "Additional pixbuf loaders" in the References section of the Help file)

    awk
        when running Geeqie, to use the geo-decode function

    markdown
        when compiling Geeqie, to create this file in html format

    libffmpegthumbnailer 2.1.0
        https://github.com/dirkvdb/ffmpegthumbnailer
        for thumbnailing camera video clips
        disable with configure option: --disable-ffmpegthumbnailer

    libpoppler-glib-dev 0.62
        for displaying pdf files
        disable with configure option: --disable-pdf

    ZoneDetect
        For displaying timezone and UTC/DST time offset
        Download file:
        https://github.com/BertoldVdb/ZoneDetect/raw/master/database/timezone21.bin
        and copy to the location of the Geeqie ancilliary files, which is usually:
        /usr/local/lib/geeqie/

    libimage-exiftool-perl
        For the jpeg extraction plugin

    liblcms2-utils
        For the command-line tool jpgicc, used by the jpeg extraction plugin

     ImageMagick
     exiftran
     gphoto2
     ufraw
     exiv2
        Additional command-line tools for various operations

    libheif
        For displaying HEIF images

    libwebp
        For displaying webp images

    libdjvulibre
        For displaying DjVu images

### Code hackers:

If you plan on making any major changes to the code that will be offered for
inclusion to the main source, please contact us first - so that we can avoid
duplication of effort.
                                                         The Geeqie Team

### Known bugs:

See the Geeqie Bug Tracker at <https://github.com/BestImageViewer/geeqie/issues>
