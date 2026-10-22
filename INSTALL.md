# Downloading, Installing, and Building Geeqie

## Downloading

Geeqie is available:

* as a package for Linux and BSD systems (See the [project web page](https://www.geeqie.org#download)).

* as a [flatpak](https://flathub.org/apps/details/org.geeqie.Geeqie) from the [Flathub site](https://flathub.org/home).

* as an [AppImage](https://github.com/BestImageViewer/geeqie/releases) - x86_64 and arm64 (Generated from the latest sources).

* as a [Homebrew](https://formulae.brew.sh/formula/geeqie) or [MacPorts](https://ports.macports.org/port/geeqie) package for macOS.

* as a [Snap](https://snapcraft.io/geeqie) (x86_64 only) from the  [Snapcraft site](https://snapcraft.io) ([Edge channel](https://snapcraft.io/docs/channels)).

* via WSL2 on Windows 11 - see notes below.

## AppImages

The Continuous Build release version has AppImages that are automatically generated each time the source code is updated. There are two versions - the full version is about 120MB and the minimal version is about 10MB and will therefore load much faster.

The minimal version can display jpegs, pngs and some other formats, but does not have the range of the full version.

Store the AppImage in a directory you own, such as `$HOME/Applications`. Make it executable, then run it directly:

```sh
mkdir -p "$HOME/Applications"
mv "$HOME/Downloads/Geeqie-latest-x86_64.AppImage" "$HOME/Applications/"
chmod +x "$HOME/Applications/Geeqie-latest-x86_64.AppImage"
"$HOME/Applications/Geeqie-latest-x86_64.AppImage"
```

Adjust the filename for the minimal or ARM version.

Continuous Build AppImages contain update information. You can update them using [AppImageUpdate](https://github.com/AppImageCommunity/AppImageUpdate), or its command-line tool, `appimageupdatetool`. These are separate tools; Geeqie does not update itself automatically.

Close Geeqie before updating. With `appimageupdatetool` installed, run:

```sh
appimageupdatetool "$HOME/Applications/Geeqie-latest-x86_64.AppImage"
```

The updater uses the information embedded in the AppImage to find the latest Continuous Build of the same variant and architecture. It downloads only the changed portions where possible. The directory containing the AppImage must be writable.

Alternatively, download a new AppImage and replace the old file. Numbered release AppImages do not contain Continuous Build update information; to update those, download the desired release manually.

This script file will download to `$HOME/bin` the latest Continuous Build AppImages for you:

```sh
wget https://raw.githubusercontent.com/BestImageViewer/geeqie/master/tools/geeqie-download-appimage.sh
chmod +x geeqie-download-appimage.sh
```

The script can download either the full size or the minimal version of the AppImage, set a symbolic link to the executable file, keep local backups, revert to an earlier downloaded AppImage, install desktop icons and menu items, and optionally extract the AppImage. The `--help` option lists all options.

The full version takes a noticeable time to load, and runs slightly slower than a normal packaged release.
The above script has the option `--extract` which extracts the contents of either AppImage to a sub-directory under `$HOME/bin`.
With this option the loading and run time is the same as for a packaged release.

AppImageUpdate does not update an already extracted `squashfs-root` directory. If you use `--extract`, continue to use `geeqie-download-appimage.sh --extract`; when the script downloads a newer AppImage, it re-extracts it and updates the symbolic link.

AppImages have a "portable mode" which is described [here](https://docs.appimage.org/user-guide/portable-mode.html).

**Note:**

Command line auto-completion does not work with AppImages. If you are using the AppImage `--extract` option, this is a work-around.

Assuming you have extracted the AppImage to `$HOME/bin/Geeqie-latest-x86_64-AppImage/`, create a symbolic link as follows:

```sh
mkdir --parents $HOME/.local/share/bash-completion/completions/
ln --symbolic $HOME/bin/Geeqie-latest-x86_64-AppImage/squashfs-root/usr/local/share/bash-completion/completions/geeqie $HOME/.local/share/bash-completion/completions/geeqie
```

**Note:**

Calling an extracted AppImage (`./squashfs-root/AppRun`) via a symbolic link does not work. The script `geeqie-download-appimage.sh --extract` will fix the problem for you. Otherwise create an intermediate script e.g.

```sh
geeqie-symbolic.sh
#!/bin/sh
cd ./squashfs-root
./AppRun
```

**Note:**

Geeqie AppImage requires `glibc >=2.42`. Ubuntu 24.04 is not compatible.

## Snaps

Snaps run in a sandboxed environment which prohibits access to, amongst other areas, hidden folders in the `$HOME` folder.

If you have already run Geeqie with some other installation type and now want to migrate to a Snap, some significant data will not be available. That data is in:

```sh
$HOME/.config/geeqie (Configuration files)
$HOME/.local/share/geeqie (Collections and local Metadata)
```

You may copy this data to the sandboxed area used by Geeqie by running these commands:

```sh
cp -avr "$HOME/.config/geeqie/." "$HOME/snap/geeqie/common/.config/geeqie/"
cp -avr "$HOME/.local/share/geeqie/." "$HOME/snap/geeqie/common/.local/share/geeqie/"
```

Thumbnails and similarity data will also not be available. You must regenerate them or copy them manually from whichever folder they are stored in to a folder under:

```sh
$HOME/snap/geeqie/common/.cache
```

## Project branches

The master branch contains the latest commits and is continually updated.

To find the latest release branch, use:

```sh
git tag --sort=-v:refname | head -n1
```

After cloning Geeqie, switch to the latest release branch by, e.g.:

```sh
git checkout v2.9
```

## System requirements for compiling

* **System:** Linux or a compatible Unix-like environment
* **Compiler:** C compiler and C++ compiler supporting C++17
* **Build tools:** Meson ≥1.3.2, Ninja, pkg-config/pkgconf
* **GTK:** GTK4 ≥4.18, including development headers
* **GLib:** ≥2.66, including GIO Unix support and development headers
* **Pango:** ≥1.46, including development headers
* **Supporting tools:** Python 3 for Meson, gettext, standard shell utilities

These are the versions declared in meson.build. GTK and other dependencies may impose higher minimum versions on their own dependencies.

The master branch uses GTK4. GTK2 and GTK3 are not supported by this branch.
The optional map pane uses libshumate; libchamplain, Clutter and clutter-gtk are
not required.

### Optional libraries

The following are upstream library names, not distribution package names.
Install their development headers and pkg-config files to enable the features
you need. A dash means Geeqie's Meson configuration specifies no minimum
version; the library's own requirements still apply.

| Feature | Upstream library | Minimum version | Meson option |
| --- | --- | --- | --- |
| Archive browsing | libarchive | 3.4.0 | `archive` |
| Color management | Little CMS 2 | 2.0 | `cms` |
| DjVu images | DjVuLibre (`ddjvuapi`) | 2.5.27 | `djvu` |
| EXIF/IPTC/XMP metadata editing | Exiv2 | 0.18 | `exiv2` |
| OpenEXR images | OpenEXR and Imath | 3.0.0 each | `exr` |
| FITS images | CFITSIO | — | `fits` |
| GPS map pane | libshumate (`shumate-1.0`) | 1.5.0 | `gps_map` |
| HEIF/HEIC/AVIF images | libheif | 1.3.2 | `heif` |
| JPEG 2000 images | OpenJPEG (`libopenjp2`) | 2.3.0 | `j2k` |
| Dedicated JPEG decoding | libjpeg-compatible library, such as libjpeg-turbo | — | `jpeg` |
| JPEG XL images | libjxl | 0.3.7 | `jpegxl` |
| Embedded RAW previews | LibRaw | 0.20 | `libraw` |
| Lua scripting | Lua | 5.3 | `lua` |
| PDF images | Poppler GLib | 0.62 | `pdf` |
| Spelling checks | libspelling (`libspelling-1`) | — | `spell` |
| Dedicated TIFF decoding | libtiff | — | `tiff` |
| Video GPS metadata | Exiv2, FFmpeg libavformat and libavutil | Exiv2 0.18; no FFmpeg minimum specified | `videometadata` |
| Video thumbnails | FFmpegthumbnailer | 2.1.0 | `videothumbnailer` |
| WebP images and animation | libwebp and libwebpdemux | 0.6.1 each | `webp` |

Additional image formats can be supplied by GdkPixbuf loaders on your system.
The `npy` option enables NumPy image support without an additional library.

Optional documentation tools include Yelp Tools (`yelp_build`) for HTML help,
Pandoc (`pandoc`) for the HTML README, and Doxygen (`doxygen`) for Lua API help
when building from Git. PDF help (`help_pdf`) uses Apache FOP, libxslt's
`xsltproc`, DocBook XSL stylesheets, Poppler's `pdftotext`, `pdfoutline`, and awk.
These tools are not needed to compile the viewer. Unit tests (`unit_tests`)
use GoogleTest and GoogleMock, with a Meson subproject fallback; see [TESTING.md](TESTING.md).

### Distribution examples

Package names vary between distributions. The Debian-based installation script
can list the packages it uses:

```sh
wget https://raw.githubusercontent.com/BestImageViewer/geeqie/master/geeqie-install-debian.sh
chmod +x geeqie-install-debian.sh
./geeqie-install-debian.sh --list
```

Use the upstream library table above when installing dependencies on other
systems. The script's package list is a distribution-specific example.

## Installation scripts

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

## Compiling and Installing

Configure and compile as your normal user:

```sh
meson setup build
ninja -C build
```

Run directly from the build directory:

```sh
./build/src/geeqie
```

### Build options

List the available options and their current values:

```sh
meson configure build
```

Optional library features default to `auto`:

* `auto`: enable the feature if its dependencies are found; otherwise configure without it.
* `enabled`: require the dependencies; configuration fails if a required library is missing or too old.
* `disabled`: omit the feature and skip its dependency checks.

For example, require PDF support and omit WebP support:

```sh
meson configure build -Dpdf=enabled -Dwebp=disabled
ninja -C build
```

Check Meson's configuration summary for the features actually enabled. Some
documentation and debugging options use additional availability checks rather
than treating `enabled` as a strict requirement. Boolean options take `true`
or `false`, rather than these feature values.

Re-run configuration and display the summary:

```sh
meson setup --reconfigure build
```

### Install for your user

Use a separate build directory configured with a user-owned prefix. This
requires no administrator privileges:

```sh
meson setup build-user --prefix="$HOME/.local"
ninja -C build-user
meson install -C build-user --no-rebuild
```

The executable is installed as `$HOME/.local/bin/geeqie`. Add `$HOME/.local/bin`
to your `PATH` if it is not already present.

### Install system-wide

The default prefix is `/usr/local`. Installing there usually requires
administrator privileges. After compiling as your normal user, install with
your system's privilege mechanism, for example:

```sh
sudo meson install -C build --no-rebuild
```

The `--no-rebuild` option keeps compilation separate from the privileged
installation step. A writable custom prefix does not require `sudo`.

### Update or uninstall

Keep the source checkout and build directory if you want to update or uninstall
this installation. To update a Git checkout, download changes and rebuild as
your normal user:

```sh
git pull
ninja -C build
```

Then repeat the appropriate installation command above. For a user installation,
use `build-user` instead of `build` when rebuilding.

To uninstall, use the same build directory and privileges used for installation:

```sh
ninja -C build-user uninstall
```

Or, for the default system-wide installation:

```sh
sudo ninja -C build uninstall
```

Uninstalling leaves your personal configuration, history, and collections intact.

## Windows

Geeqie can be run on Windows 11 (and possibly Windows 10) via Windows Subsystem for Linux (WSL2).

If the Ubuntu distribution is loaded by WSL, Geeqie can be run as an Ubuntu package or as an extracted AppImage. Geeqie can also be compiled from sources.

Note that some icons are not displayed correctly, and Help and Print do not work. However the Help manual is available [on-line](https://www.geeqie.org/help/GuideIndex.html).
