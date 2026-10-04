# Checklist for code updates and new releases of Geeqie

Run commands from the repository root unless a step says otherwise. Follow
[CODING.md](CODING.md) for contribution style and [TESTING.md](TESTING.md) for
build and test details.

## Code updates

Before committing changes:

* Update the relevant Help pages when behavior, preferences, or menus change.
* Keep the plugin template `data/plugins/org.geeqie.template.desktop.in` consistent
  with supported desktop-file keys.
* Update `file_types`, `actions`, and `options` in `data/completions/geeqie` when
  supported extensions, actions, or command-line options change.
* Build and run the tests appropriate to the change:

```sh
ninja -C build
meson test -C build --print-errorlogs
git diff --check
```

Unit tests require a build configured with `-Dunit_tests=enabled`. Review failed
and skipped tests rather than relying only on the final exit status.

When command-line options change, regenerate the man page and the Command Line
Options Help section after building. This requires `help2man` and `doclifter`:

```sh
./build-aux/generate-man-page.sh
```

Check that changes to action descriptions or `data/accels.ini` appear correctly
in the dynamically generated keyboard shortcuts window. Review the diff, commit
related source and generated documentation together, and push the changes.

## Prepare a release

* Ask Codex to summarize the changes since the previous release tag into `NEWS`.
  Review the result and edit it by hand as needed. Its first line must be exactly
  `Geeqie n.m` or `Geeqie n.m.p`, matching the intended version.
* Ensure all intended source changes have been committed and pushed to the
  release repository. The script clones `git://git.geeqie.org/geeqie.git`; only
  `NEWS` and the AppStream template are copied from the local checkout.
* Run from the repository root:

```sh
./packaging/new-release.sh
```

The script infers the version and stable branch from `NEWS`. For a major/minor
release it starts from remote master; for a patch release it uses the existing
`stable/n.m` branch. Use `-s COMMIT` to select a master commit for a major/minor
release. The existing `-v n.m` and `-p p` options remain available and must match
`NEWS`. Numeric versions are supported; release-candidate suffixes are not.

The script checks required tools and signing availability, adds a dated AppStream
release entry if needed, updates translations, builds with unit tests enabled,
runs the configured test suite under Xvfb, and regenerates the man page and
Command Line Options Help. It commits these changes, signs and verifies the release tag,
exports and signs the source archive, verifies its signature, then builds and
runs tests again from the extracted archive. A matching release-files commit is
prepared on master.

Work and artifacts are retained in a unique `geeqie-release.XXXXXXXXXX` directory
under `${TMPDIR:-/tmp}`. Repeated runs do not overwrite earlier results. The local
checkout and its build directory are unchanged. No branches, tags, or files are
published without `-r`. Required tools include Git, GPG with a signing key, Meson,
Ninja, Python 3, gettext tools, `itstool`, `help2man`, `doclifter`, tar, xz, Xvfb,
and `xvfb-run`, plus Geeqie's build and test dependencies. Unit-test setup may download test images
and Googletest if they are not already available.

## Review and publish the source release

Review the release notes, generated metadata and documentation, translation
changes, and test results in the retained repository. Check CI and smoke-test
startup, image loading, zooming and panning, collections, keyboard navigation,
and file operations on relevant platforms. Scripted checks do not replace this
review.

After a successful run, the script prints the archive and signature paths and a
`git -C ... push --atomic ...` command. Run that command after review to publish
those exact prepared stable-branch, tag, and master commits to
`git@geeqie.org:geeqie`. It does not rebuild or recreate the reviewed artifacts.
Atomic publication requires server support; a rejected push leaves the local
release available for inspection.

The `-r` option still prepares and verifies a release, then pushes it immediately.
For the review-first workflow, omit `-r` and use the printed command afterward.
Fetch the published branch and tag into the working checkout before continuing.

## Publish the release and packages

* Open [GitHub releases](https://github.com/BestImageViewer/geeqie/releases) and
   draft a release using the existing `vn.m` or `vn.m.p` tag.
* Set the title to `Geeqie n.m` or `Geeqie n.m.p`, copy the relevant `NEWS` section,
   and upload the source archive and detached signature. Review the assets and
   release notes, then publish with the appropriate latest/prerelease settings.
* Wait for all four Continuous Build AppImages (full/minimal, x86_64/aarch64) to
   finish. Check the workflow commit and embedded version against the intended
   release; the mutable Continuous Build assets may contain later changes. From
   a checkout with current tags, run:

```sh
./packaging/new-release-appimages.sh
```

This script downloads the AppImages, renames them, clears Continuous Build update
information, and uploads them with `gh release upload`; no separate manual upload
is needed. It requires authenticated GitHub CLI access. It chooses the tag using
`git tag | tail -1`, so verify that this selects the intended release before
running it. Renaming an AppImage does not change its embedded version; the script
also documents a patch-version limitation. Smoke-test the published files.

* The `Geeqie amd64 Snap build` GitHub Actions workflow builds and tests a Snap
   and stores it as a workflow artifact. It does not publish to the Snap Store.
   Inspect and test that artifact, or build locally with `snapcraft`. Upload the
   actual generated filename, for example:

```sh
snapcraft upload /path/to/geeqie_VERSION_amd64.snap --release=edge
```

The current recipe uses `grade: devel` and appends `.edge` to its version. Publishing
an official stable Snap requires reviewing the recipe's grade and version first;
see [Snapcraft package configuration](https://ubuntu.com/docs/snapcraft/latest/how-to/crafting/configure-package-information/)
and [publishing revisions](https://documentation.ubuntu.com/snapcraft/8.9.0/how-to/publishing/manage-revisions-and-releases/).

## Update the website

Build HTML and PDF Help before publishing it, with `yelp_build` and `help_pdf`
enabled and their dependencies installed. `tools/web-help.sh` expects the website
checkout at `../geeqie.github.io`, replaces its Help files, and copies
`build/doc/html/` and `build/doc/help.pdf`:

```sh
./tools/web-help.sh
```

Copy the generated `build/org.geeqie.Geeqie.desktop` and
`build/org.geeqie.Geeqie.metainfo.xml` to the website checkout when they change.
Review, commit, and push the website changes separately. Verify the published
Help and download links. Update the [Wikipedia entry](https://en.wikipedia.org/wiki/Geeqie)
when appropriate.
