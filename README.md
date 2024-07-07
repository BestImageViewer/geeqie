# Geeqie Readme

GitHub Code Status:  
![Build Status](https://github.com/BestImageViewer/geeqie/actions/workflows/check-build-actions.yml/badge.svg)
![GitHub Downloads](https://img.shields.io/github/downloads/BestImageViewer/geeqie/total)

Package Status:  
[![Packaging status](https://repology.org/badge/tiny-repos/geeqie.svg)](https://repology.org/project/geeqie/versions)
[![latest packaged version(s)](https://repology.org/badge/latest-versions/geeqie.svg)](https://repology.org/project/geeqie/versions)

## ![Geeqie icon][image_ref_geeqie_png] Geeqie - an image viewer

This is Geeqie, a successor of GQview.

Geeqie is a free open software image viewer and organiser program for Linux, FreeBSD and other Unix-like operating systems

It can be used as a simple database-free image viewer but it also has extensive capabilities.

Please send any questions, problems or suggestions to the [mailing list](mailto:geeqie@freelists.org) or
open an issue on [Geeqie at GitHub](https://github.com/BestImageViewer/geeqie/issues).

(NB Unless you first subscribe to the mailing list, you will not receive automated responses)

Subscribe to the mailing list [here](https://www.freelists.org/list/geeqie).

The project website is <https://www.geeqie.org/> and you will find the latest sources in the
[Geeqie repository](http://geeqie.org/cgi-bin/gitweb.cgi?p=geeqie.git).

## Contents

* [Features](#features)
* [Downloading](#downloading)
* [AppImages](#appimages)
* [Installation Scripts](#installation-scripts)
* [Compiling and Installing](#compiling-and-installing)
* [Notes and changes for the latest release](#notes-and-changes-for-the-latest-release)
* [Required libraries](#required-libraries)

### Features

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

* Support for stereoscopic images
    * input: side-by-side (JPS) and MPO format
    * output: single image, anaglyph, SBS, mirror, SBS half size (3DTV)

* Viewing raster and vector images, in the following formats:
    * 3FR ANI ARW AVIF BMP CR2 CR3 CRW CUR DDS DJVU DNG ERF GIF HEIC HEIF ICO JP2 JPE JPEG JPG JPS JXL KDC MEF MOS MPO MRW NEF ORF PBM PDF PEF PGM PNG PNM PPM PSD QIF QTIF RAF RAW RW2 SCR SR2 SRF SVG SVGZ TGA TIF TIFF WEBP XBM XPM.
    * Display images in archive files (.ZIP, .RAR etc.).
    * Animated GIF and WEBP files are supported.

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

* Maps from [OpenStreetMap](https://www.openstreetmap.org) may be displayed in a side panel. If an image has GPS coordinates embedded, its position will be displayed on the map - if Image Direction is encoded, that will be displayed also. If an image does not have embedded GPS coordinates, it may be dragged-and-dropped onto the map to encode its position.

* Speed of operation can be increased by caching thumbnails and similarity data of images. When Geeqie is run as a stand-alone command line program (`geeqie --cache-maintenance <path>`) these data will be recursively created from the defined start point. This program can be called from `cron` or `anacron` so that cache updating is automatically done at specified intervals.

* Extensible via plugins

### Downloading

Geeqie is available:  

* as a package for Linux and BSD systems (See the [project web page](https://www.geeqie.org#download)).

* as a [flatpak](https://flathub.org/apps/details/org.geeqie.Geeqie) from the [Flathub site](https://flathub.org/home).

* as an [AppImage](https://github.com/BestImageViewer/geeqie/releases) - x86_64 and arm64 (Generated from the latest sources).

* as a [Homebrew](https://formulae.brew.sh/formula/geeqie) or [MacPorts](https://ports.macports.org/port/geeqie) package for macOS.

* via WSL2 on Windows 11 - see notes below.

### AppImages

The Continuous Build release version has AppImages that are automatically generated each time the source code is updated. There are two versions - the full version is about 120MB and the minimal version is about 10MB and will therefore load much faster.

The minimal version can display jpegs, pngs and some other formats, but does not have the range of the full version.

This script file will download to `$HOME/bin` the latest Continuous Build AppImages for you:

```sh
wget https://raw.githubusercontent.com/BestImageViewer/geeqie/master/scripts/geeqie-download-appimage.sh
chmod +x geeqie-download-appimage.sh
```

The script can download either the full size or the minimal version of the AppIamge, and sets a symbolic link to the executable file. The `--help` option lists all options.

The full version takes a noticeable time to load, and runs slightly slower than a normal packaged release.
The above script has the option `--extract` which extracts the contents of either AppImage to a sub-directory under `$HOME/bin`.
With this option the loading and run time is the same as for a packaged release.

There are also options to install desktop icons, menu items, and to revert to earlier downloaded versions.

AppImages have a "portable mode" which is described [here](https://docs.appimage.org/user-guide/portable-mode.html).

**Note:**

Command line auto-completion does not work with AppImages. If you are using the AppImage `--extract` option, this is a work-around.

Assuming you have extracted the AppImage to `$HOME/bin/Geeqie-latest-x86_64-AppImage/`, create a symbolic link as follows:

```sh
mkdir --parents $HOME/.local/share/bash-completion/completions/
ln --symbolic $HOME/bin/Geeqie-latest-x86_64-AppImage/squashfs-root/usr/local/share/bash-completion/completions/geeqie $HOME/.local/share/bash-completion/completions/geeqie
```

### Installation scripts

Geeqie is stable and you may compile the latest version from sources.

There are two scripts which will download and compile the sources for you.

The first script will install Geeqie to a defined location, and will run under any system. However, it is left to you to make sure dependencies are fulfilled.
To get the script, from the command line type:

```sh
wget https://raw.githubusercontent.com/pixlsus/Scripts/master/build-geeqie
chmod +x build-geeqie
```

The second script will run only on Debian-based system, but will fulfil all dependencies and also give you the opportunity to include additional pixbuf loaders and other useful programs.
To get the script, from the command line type:

```sh
wget https://raw.githubusercontent.com/BestImageViewer/geeqie/master/geeqie-install-debian.sh
chmod +x geeqie-install-debian.sh
```

If you wish to compile the sources yourself you may download the latest version (if you have installed git) from here:

`git clone http://git.geeqie.org/git/geeqie.git`

### Compiling and Installing

```sh
meson setup build
ninja -C build install
```

List compile options:

```sh
meson configure build
```

Apply options e.g.:

```sh
sudo ninja -C build uninstall
meson configure build -Dpdf=enabled -Dwebp=disabled
ninja -C build install
```

Re-display configuration data:

```sh
ninja -C build reconfigure
```

Meaning of options:  
`auto` If the library is not found, continue the installation  
`enabled` If the library is not found, stop the installation  
`disabled` Do not look for the library  

Uninstall:

```sh
sudo ninja -C build uninstall
```

Install new version:

```sh
sudo ninja -C build uninstall
git pull
ninja -C build install
```

#### Note

It is recommended to always use `git clone git://git.geeqie.org/geeqie.git` to download Geeqie. After installing Geeqie you may delete the folder you have cloned Geeqie into.

However if you leave the folder intact, whenever new features or patches are available, execute:

`sudo ninja -C build uninstall; git pull; ninja -C build install`

Only the changed sources are downloaded, which makes this a quick operation.

Your configuration file, history file and desktop files are not affected by this process.

### Notes and changes for the latest release

See the NEWS file in the installation folder, or [Geeqie News at GitHub](https://github.com/BestImageViewer/geeqie/blob/master/NEWS)

And either the ChangeLog file or [Geeqie ChangeLog](http://geeqie.org/cgi-bin/gitweb.cgi?p=geeqie.git;a=shortlog)

### Required libraries

Required libraries for a Debian installation may be listed by:

```sh
wget https://raw.githubusercontent.com/BestImageViewer/geeqie/master/geeqie-install-debian.sh
chmod +x geeqie-install-debian.sh
./geeqie-install-debian.sh --list
```

### Code hackers

If you plan on making any major changes to the code that will be offered for
inclusion to the main source, please contact us first - so that we can avoid
duplication of effort.

### Known bugs

See the Geeqie Bug Tracker at <https://github.com/BestImageViewer/geeqie/issues>

### Windows

Geeqie can be run on Windows 11 (and possibly Windows 10) via Windows Subsystem for Linux (WSL2).

If the Ubuntu distribution is loaded by WSL, Geeqie can be run as an Ubuntu package or as an extracted AppImage. Geeqie can also be compiled from sources.

Note that some icons are not displayed correctly, and Help and Print do not work. However the Help manual is available [on-line](https://www.geeqie.org/help/GuideIndex.html)

[image_ref_geeqie_png]:
data:image/png;base64,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
