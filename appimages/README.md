# How to create AppImages for Geeqie

## Download the required tools:

Download the `linuxdeploy` tool. At the time of writing, this is:
```sh
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
```

Move this file to `$HOME/bin` or somewhere else in your `$PATH` and make executable.

## Generate the executable

```sh
cd <your working area>
```

Download Geeqie sources:
```sh
git clone git://www.geeqie.org/geeqie.git
cd geeqie
```

If a run has already been made, remove any existing targets:
```sh
rm -r  <target dir>/AppDir
sudo rm -rf doc/html
```

Create a fresh target directory:
```sh
mkdir <target dir>/AppDir
```

Generate the Geeqie executable:
```sh
sudo make maintainer-clean
./autogen.sh --prefix="/usr/"
make -j
make install DESTDIR=<full path to target dir>/AppDir
```

## Generate the AppImage

```sh
cd <target dir>
linuxdeploy-x86_64.AppImage \
    --appdir ./AppDir --output appimage \
    --desktop-file ./AppDir/usr/share/applications/geeqie.desktop \
    --icon-file ./AppDir/usr/share/pixmaps/geeqie.png \
    --executable ./AppDir/usr/bin/geeqie`
```

## Rename AppImage
If required, rename the AppImage executable - e.g.:
```sh
mv ./Geeqie-v1.6-x86_64.AppImage $(./Geeqie-v1.6-x86_64.AppImage -v | sed 's/git//' | sed 's/-.* /-/' | sed 's/ /-v/' | sed 's/-GTK3//').AppImage
```

The script `./scripts/generate-appimage.sh` automates this process.
