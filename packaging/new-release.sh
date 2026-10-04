#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

## @file
## @brief Prepare and verify a source release from reviewed NEWS.
##
## Run from the repository root after updating NEWS and pushing source changes.
## The first NEWS line must be Geeqie major.minor or Geeqie major.minor.patch.
## With no options, the version and stable branch are inferred from NEWS.
## Preparation takes place in a unique temporary directory; the checkout is
## unchanged. Without -r, nothing is pushed or uploaded.
##
## -v <major.minor> Explicit version, checked against NEWS (optional).
## -p <patch> Explicit patch number, checked against NEWS (optional).
## -s <commit> Starting master commit for a major/minor release (optional).
## -r Push prepared stable branch, signed tag, and master commit atomically.
## -h Print Help.

fail()
{
	printf '%s\n' "$*" >&2
	exit 1
}

sign()
{
	if [ -n "$release_signing_key" ]
	then
		gpg --local-user "$release_signing_key" "$@"
	else
		gpg "$@"
	fi
}

version=
start=
patch=
push=false
while getopts "v:s:p:hr" option
do
	case $option in
		h)
			printf '%s\n' \
				'Usage: ./packaging/new-release.sh [-v major.minor] [-p patch] [-s commit] [-r]' \
				'Without version options, use the first line of NEWS.' \
				'Without -r, prepare and verify locally; nothing is published.' \
				'-s selects a master commit for a major/minor release.' \
				'-r pushes the prepared branches and signed tag after verification.'
			exit 0
			;;
		v) version="$OPTARG" ;;
		s) start="$OPTARG" ;;
		p) patch="$OPTARG" ;;
		r) push=true ;;
		*) exit 1 ;;
	esac
done
shift "$((OPTIND - 1))"
[ "$#" -eq 0 ] || fail 'Unexpected positional arguments'

orig_dir=$PWD
[ -f NEWS ] && [ -f data/org.geeqie.Geeqie.metainfo.xml.in ] || fail 'Run from the Geeqie repository root'
news_version=$(sed -n '1s/^Geeqie //p' NEWS)
printf '%s\n' "$news_version" | LC_ALL=C grep -E -q '^[0-9]+\.[0-9]+(\.[0-9]+)?$' || fail 'The first NEWS line must be Geeqie major.minor[.patch]'
news_base=$(printf '%s\n' "$news_version" | cut -d . -f 1,2)
news_patch=$(printf '%s\n' "$news_version" | cut -s -d . -f 3)
[ -z "$version" ] || [ "$version" = "$news_base" ] || fail 'Version option does not match NEWS'
[ -z "$patch" ] || [ "$patch" = "$news_patch" ] || fail 'Patch option does not match NEWS'
version=$news_base
patch=$news_patch
revision=$news_version
[ -z "$start" ] || [ -z "$patch" ] || fail 'Cannot combine a start commit with a patch release'

for tool in git gpg meson ninja python3 help2man doclifter tar xz msgfmt msgmerge xgettext itstool xvfb-run Xvfb
do
	command -v "$tool" > /dev/null || fail "Required tool not found: $tool"
done
git var GIT_COMMITTER_IDENT > /dev/null
release_user_name=$(git config --get user.name || :)
release_user_email=$(git config --get user.email || :)
release_signing_key=$(git config --get user.signingkey || :)
# Check signing availability before doing an expensive build. GPG may prompt
# for a passphrase here and when the actual tag and archive are signed.
printf 'Geeqie release signing check\n' | sign --armor --detach-sign > /dev/null

release_dir=$(mktemp -d "${TMPDIR:-/tmp}/geeqie-release.XXXXXXXXXX")
working_dir="$release_dir/geeqie-$revision"
trap 'printf "Release work directory: %s\n" "$release_dir" >&2' EXIT
printf 'Preparing Geeqie %s in %s\n' "$revision" "$release_dir"
git clone git://git.geeqie.org/geeqie.git "$working_dir"
cd "$working_dir"
if [ -n "$release_user_name" ]
then
	git config user.name "$release_user_name"
fi
if [ -n "$release_user_email" ]
then
	git config user.email "$release_user_email"
fi
if [ -n "$release_signing_key" ]
then
	git config user.signingkey "$release_signing_key"
fi

if git rev-parse --verify --quiet "refs/tags/v$revision" > /dev/null
then
	fail "Tag v$revision already exists"
fi
if [ -n "$patch" ]
then
	git rev-parse --verify --quiet "refs/remotes/origin/stable/$version" > /dev/null || fail "Stable branch $version does not exist"
	git checkout -b "stable/$version" --track "origin/stable/$version"
else
	if git rev-parse --verify --quiet "refs/remotes/origin/stable/$version" > /dev/null
	then
		fail "Stable branch $version already exists; use a patch version in NEWS"
	fi
	if [ -n "$start" ]
	then
		git merge-base --is-ancestor "$start" master || fail 'Start commit is not on master'
	else
		start=$(git rev-parse master)
	fi
	git checkout -b "stable/$version" "$start"
fi
printf 'Release source commit: %s\n' "$(git rev-parse HEAD)"
cp "$orig_dir/NEWS" NEWS
# Retain local metadata edits, but create the release entry automatically.
cp "$orig_dir/data/org.geeqie.Geeqie.metainfo.xml.in" data/
python3 - "$revision" <<'PY'
import datetime
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

path = pathlib.Path('data/org.geeqie.Geeqie.metainfo.xml.in')
text = path.read_text(encoding='utf-8')
root = ET.fromstring(text)
releases = root.find('releases')
if releases is None:
    sys.exit('AppStream metadata has no releases element')
version = sys.argv[1]
if not any(entry.get('version') == version for entry in releases):
    entry = f'\n    <release version="{version}" date="{datetime.date.today().isoformat()}" />'
    text, count = re.subn(r'<releases\s*>', lambda match: match[0] + entry, text, count=1)
    if count != 1:
        sys.exit('Cannot locate AppStream releases element')
    ET.fromstring(text)
    path.write_text(text, encoding='utf-8')
PY

meson setup -Dunit_tests=enabled build
ninja -C build update-translations
for translation in po/*.po
do
	printf 'Checking translation: %s\n' "$translation"
	msgfmt --check --statistics -o /dev/null "$translation"
done
ninja -C build
xvfb-run --auto-servernum meson test -C build --print-errorlogs
./build-aux/generate-man-page.sh
# Include translation updates in both the release branch and master.
git add NEWS data/org.geeqie.Geeqie.metainfo.xml.in data/man/geeqie.1 doc/docbook/CommandLineOptions.xml po/
git diff --cached --check
git commit --allow-empty -m "Preparing for release v$revision"
git tag --sign "v$revision" --message="Release v$revision"
git verify-tag "v$revision"

# Export only tracked release files: no build output, VCS files, or local debris.
archive="$release_dir/geeqie-$revision.tar.xz"
git archive --format=tar --prefix="geeqie-$revision/" "v$revision" > "$release_dir/source.tar"
xz -c "$release_dir/source.tar" > "$archive"
rm "$release_dir/source.tar"
sign --armor --detach-sign --output "$archive.asc" "$archive"
gpg --verify "$archive.asc" "$archive"

# Check that the distributed source builds independently of the Git checkout.
mkdir "$release_dir/archive-test"
tar -xf "$archive" -C "$release_dir/archive-test"
cd "$release_dir/archive-test/geeqie-$revision"
[ "$(./build-aux/version.sh)" = "$revision" ] || fail 'Archive version does not match NEWS'
meson setup -Dunit_tests=enabled build
ninja -C build
xvfb-run --auto-servernum meson test -C build --print-errorlogs
cd "$working_dir"

# Prepare master too, so review and publication use the same release files.
git diff "v$revision^" "v$revision" -- po/ > "$release_dir/translations.patch"
git checkout master
git checkout "v$revision" -- NEWS data/org.geeqie.Geeqie.metainfo.xml.in data/man/geeqie.1 doc/docbook/CommandLineOptions.xml
if [ -s "$release_dir/translations.patch" ]
then
	# Merge the translation changes instead of replacing newer master translations
	# with older stable-branch files during a patch release.
	git apply --3way --index "$release_dir/translations.patch" || fail 'Translation merge needs review in the retained repository; nothing has been pushed'
fi
git diff --cached --check
if ! git diff --cached --quiet
then
	git commit -m "Release v$revision files"
fi

if [ "$push" = true ]
then
	git push --atomic git@geeqie.org:geeqie "stable/$version" "v$revision" master
fi
printf '\nPrepared Geeqie %s\nArchive: %s\nSignature: %s.asc\nRepository: %s\n' "$revision" "$archive" "$archive" "$working_dir"
if [ "$push" = false ]
then
	printf '\nAfter reviewing, publish these exact commits with:\n'
	printf "git -C '%s' push --atomic git@geeqie.org:geeqie 'stable/%s' 'v%s' master\n" "$working_dir" "$version" "$revision"
fi
printf '\nUpload the archive and signature to the GitHub release after publishing the tag.\n'
