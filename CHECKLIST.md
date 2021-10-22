## A checklist to be used after Geeqie has been updated

Before compiling the sources, carry out the following actions when necessary:

* Update `org.geeqie.Geeqie.appdata.xml.in` with the latest released version and date
* If source files have been added or removed from `./src/` directory, resync `./po/POTFILES.in`
```sh
cd ./po
./regen_potfiles.sh | patch -p0
```
* Keep translations in sync with the code
```sh
cd ./po
make update-po
```
* Update the the timezone database
```sh
./scripts/zonedetect/create_timezone_database
```

After compiling the sources, carry out the following actions when necessary:

* Generate a new AppImage (note that this should be run on a **20.04 system**)
```sh
./scripts/generate-appimage.sh <location of local appimages folder>
```
* Upload AppImage to web AppImages location
* Edit `<location of local geeqie.github.io>/AppImage/appimages.txt` to include latest AppImage at the *top* of the list
* Copy Help html files to `<location of local geeqie.github.io>/help`
* Copy `geeqie.desktop` to `<location of local geeqie.github.io>/`
* Copy `org.geeqie.Geeqie.appdata.xml` to `<location of local geeqie.github.io>/`
* Push changes to `geeqie.github.io`
