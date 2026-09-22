# Geeqie

GitHub Code Status:

![Build Status](https://github.com/BestImageViewer/geeqie/actions/workflows/check-build-actions.yml/badge.svg)
![GitHub Downloads](https://img.shields.io/github/downloads/BestImageViewer/geeqie/total)

Package Status:

[![Packaging status](https://repology.org/badge/tiny-repos/geeqie.svg)](https://repology.org/project/geeqie/versions)
[![latest packaged version(s)](https://repology.org/badge/latest-versions/geeqie.svg)](https://repology.org/project/geeqie/versions)

## ![](data/icons/geeqie.svg) Geeqie - an image viewer

Geeqie is a free open software image viewer and organiser program for Linux,
FreeBSD and other Unix-like operating systems. It can be used as a simple
database-free image viewer, but it also has extensive capabilities.

Geeqie is a successor of GQview.

## Geeqie Features

Geeqie is a graphics file viewer. Basic features:

* Single click image viewing / navigation.

* Auto next folder - automatically advance to the next or previous sibling folder when reaching the last or first image in the current folder.

* Zoom functions.

* Thumbnails, with optional caching.

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
    * 3FR ANI ARW AVIF BMP CR2 CR3 CRW CUR DDS DJVU DNG ERF EXR FIT FITS FTS GIF GQV HEIC HEIF ICO JP2 JPE JPEG JPG JPS JXL KDC MEF MOS MPO MRW NEF NPY NRW ORF PBM PDF PEF PGM PNG PNM PPM PSD QIF QTIF RAF RAW RW2 SCR SR2 SRF SVG SVGZ TGA TIF TIFF WEBP XBM XPM.
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

* Speed of operation can be increased by caching thumbnails and similarity data of images. When Geeqie is run as a stand-alone command line program (`GQ_CACHE_MAINTENANCE=y geeqie --cache-maintenance=&lt;path&gt;`) these data will be recursively created from the defined start point. This program can be called from `cron` or `anacron` so that cache updating is automatically done at specified intervals.

* Extensible via plugins.

## Documentation

* [Downloading, installing, and building](INSTALL.md)
* [Testing](TESTING.md)
* [Coding style](CODING.md)
* [Developer notes](DEVELOPER-NOTES.md)
* [Translators](TRANSLATORS.md)
* [Code of conduct](CODE_OF_CONDUCT.md)

## Contact

Please send any questions, problems or suggestions to the
[mailing list](mailto:geeqie@freelists.org) or open an issue on
[Geeqie at GitHub](https://github.com/BestImageViewer/geeqie/issues).

Unless you first subscribe to the mailing list, you will not receive automated
responses. Subscribe to the mailing list
[here](https://www.freelists.org/list/geeqie).

The project website is <https://www.geeqie.org/> and you will find the latest
sources in the [Geeqie repository](http://geeqie.org/cgi-bin/gitweb.cgi?p=geeqie.git).

## Latest release

See the NEWS file in the installation folder, or
[Geeqie News at GitHub](https://github.com/BestImageViewer/geeqie/blob/master/NEWS).

See either the ChangeLog file or
[Geeqie ChangeLog](http://geeqie.org/cgi-bin/gitweb.cgi?p=geeqie.git;a=shortlog).

## Contributing

If you plan on making any major changes to the code that will be offered for
inclusion to the main source, please contact us first so that we can avoid
duplication of effort.

Known bugs are tracked in the
[Geeqie Bug Tracker](https://github.com/BestImageViewer/geeqie/issues).
