#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief Generate an HTML manual preview with highlighted working-tree or commit changes.

"""Preview uncommitted documentation changes against HEAD, or a commit against its first parent."""

import argparse
import html
import io
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import quote
import zipfile

try:
    from lxml import etree
    from lxml import html as lxml_html
    from lxml.html.diff import htmldiff
except ImportError as error:
    raise SystemExit('Documentation previews require the Python lxml module.') from error


HIGHLIGHT_STYLE = '''
ins { background: #c8f7ce !important; color: #143d1b !important;
      text-decoration: underline; }
del { background: #ffd5d5 !important; color: #651919 !important;
      text-decoration: line-through; }
.doc-preview-legend { padding: 1em; border: 1px solid #888; }
'''


def highlight_pages(output, before_directory, records, sources, comparison):
    """Annotate rendered article content, leaving page navigation unchanged."""
    pairs = {path.name: path.name for path in (output / 'changes').glob('*.html')}
    for _, old, new in records:
        before = page_info(sources[0] / old if old else None)
        after = page_info(sources[1] / new if new else None)
        if before and after:
            pairs[after[0] + '.html'] = before[0] + '.html'
    matched = set(pairs.values())
    for path in before_directory.glob('*.html'):
        if path.name not in matched and not (output / 'changes' / path.name).exists():
            pairs[path.name] = path.name
    for after_name, before_name in pairs.items():
        before_path = before_directory / before_name
        after_path = output / 'changes' / after_name
        before = lxml_html.parse(str(before_path)) if before_path.exists() else None
        after = lxml_html.parse(str(after_path)) if after_path.exists() else None

        def article_content(tree):
            if tree is None:
                return ''
            articles = tree.xpath('//article')
            if not articles:
                return ''
            # Yelp puts previous/next links inside the article. Compare only
            # manual content so navigation changes do not produce highlights.
            return ''.join(lxml_html.tostring(child, encoding='unicode')
                           for child in articles[0] if child.tag != 'nav')

        old_content = article_content(before)
        new_content = article_content(after)
        if old_content == new_content:
            continue
        tree = after if after is not None else before
        articles = tree.xpath('//article')
        if not articles:
            continue
        article = articles[0]
        for child in list(article):
            if child.tag != 'nav':
                article.remove(child)
        fragment = lxml_html.fragment_fromstring(
            htmldiff(old_content, new_content), create_parent='div')
        for marker in fragment.xpath('.//ins | .//del'):
            if not marker.text_content() and not len(marker):
                marker.drop_tag()
        article.append(fragment)
        legend = lxml_html.fragment_fromstring(
            '<p class="doc-preview-legend">' + html.escape(comparison) + ': '
            '<ins>inserted text</ins> · <del>deleted text</del></p>')
        article.insert(0, legend)
        style = etree.SubElement(tree.find('head'), 'style')
        style.text = HIGHLIGHT_STYLE
        after_path.write_bytes(lxml_html.tostring(tree, encoding='utf-8',
                                                doctype='<!DOCTYPE html>'))


def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])


def page_info(path):
    if path is None or path.suffix != '.xml' or not path.is_file():
        return None
    text = path.read_text(encoding='utf-8')
    root = re.search(r'<(?:book|chapter|section|appendix)\b[^>]*\bid="([^"]+)"', text)
    if root is None:
        return None
    title = re.search(r'<title\b[^>]*>(.*?)</title>', text, re.DOTALL)
    label = html.unescape(re.sub('<[^>]+>', '', title[1])) if title else path.stem
    return root[1], label


def build(repo, revision, destination, source):
    if revision is None:
        paths = git(repo, 'ls-files', '-z', '--cached', '--others',
                    '--exclude-standard', '--', 'doc').decode().split('\0')
        for name in set(paths) - {''}:
            path = repo / name
            if path.is_file():
                target = source / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
    else:
        archive = git(repo, 'archive', '--format=zip', revision, 'doc')
        with zipfile.ZipFile(io.BytesIO(archive)) as files:
            files.extractall(source)
    docbook = source / 'doc/docbook'
    entries = [path for path in docbook.glob('*.xml')
               if re.search(r'<book\b', path.read_text(encoding='utf-8'))]
    if len(entries) != 1:
        raise RuntimeError(f'{revision or "working tree"}: expected one DocBook manual entry point')
    destination.mkdir()
    subprocess.run(['yelp-build', 'html', str(entries[0]), '-o', str(destination),
                    '-p', str(source / 'doc/graphics')], check=True)
    return source


def page_link(info, output):
    if info is None:
        return None
    filename = info[0] + '.html'
    if not (output / 'changes' / filename).is_file():
        return None
    return 'changes/' + quote(filename, safe='')


def document(title, body):
    return ('<!doctype html><html lang="en"><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width">'
            f'<title>{html.escape(title)}</title><style>'
            'body{font:16px sans-serif;margin:2rem}li{margin:1rem 0}'
            'iframe{width:100%;height:80vh;border:1px solid #888}'
            '</style>' + body + '</html>')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('commit', nargs='?',
                        help='compare this commit with its first parent; if omitted, '
                             'compare uncommitted changes (staged, unstaged, and '
                             'non-ignored untracked files under doc/) with HEAD')
    parser.add_argument('-d', '--dir', type=Path,
                        help='directory for uniquely named previews '
                             '(default: ~/geeqie-doc-previews/)')
    parser.add_argument('-o', '--open', action='store_true',
                        help='open the preview in the default browser using xdg-open')
    parser.add_argument('-b', '--browser', metavar='EXECUTABLE',
                        help='open the preview using this browser executable instead of xdg-open')
    args = parser.parse_args()
    if shutil.which('yelp-build') is None:
        parser.error('yelp-build is required (usually supplied by yelp-tools)')
    repo = Path(git(Path.cwd(), 'rev-parse', '--show-toplevel').decode().strip())
    commit = (git(repo, 'rev-parse', '--verify', '--end-of-options',
                  args.commit + '^{commit}').decode().strip() if args.commit else None)
    parent = git(repo, 'rev-parse', commit + '^1' if commit else 'HEAD').decode().strip()
    changes = git(repo, 'diff', '--name-status', '-z', '--find-renames',
                  parent, *([commit] if commit else []), '--', 'doc').decode().split('\0')
    records = []
    cursor = 0
    while cursor < len(changes) and changes[cursor]:
        status, path = changes[cursor:cursor + 2]
        cursor += 2
        old = None if status == 'A' else path
        new = None if status == 'D' else path
        if status.startswith(('R', 'C')):
            new = changes[cursor]
            cursor += 1
        records.append((status, old, new))
    if commit is None:
        untracked = git(repo, 'ls-files', '-z', '--others', '--exclude-standard',
                        '--', 'doc').decode().split('\0')
        for path in filter(None, untracked):
            deleted = next((i for i, (status, old, _) in enumerate(records)
                            if status == 'D' and old == path), None)
            if deleted is None:
                records.append(('A', None, path))
            else:
                records[deleted] = ('M', path, path)
    previews = (args.dir.expanduser().resolve() if args.dir
                else Path.home() / 'geeqie-doc-previews')
    previews.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix='preview-' + (commit[:12] if commit else 'working-tree') + '-', dir=previews))
    # Reserve the entry filename exclusively as well as its supporting folder.
    preview_file = previews / (output.name + '.html')
    with preview_file.open('x', encoding='utf-8'):
        pass
    with tempfile.TemporaryDirectory(prefix='geeqie-doc-source-') as temporary:
        temporary = Path(temporary)
        before_directory = temporary / 'before-html'
        sources = [
            build(repo, parent, before_directory, temporary / 'before-source'),
            build(repo, commit, output / 'changes', temporary / 'after-source'),
        ]
        comparison = 'Changes from the parent commit' if commit else 'Uncommitted changes from HEAD'
        highlight_pages(output, before_directory, records, sources, comparison)
        items = []
        for number, (status, old, new) in enumerate(records):
            old_info = page_info(sources[0] / old if old else None)
            new_info = page_info(sources[1] / new if new else None)
            label = (new_info or old_info or (None, new or old))[1]
            description = status
            if status.startswith('R'):
                description = 'Renamed'
                before = (sources[0] / old).read_bytes()
                after = (sources[1] / new).read_bytes()
                # Include paths can change as part of a filename-only revision.
                for rename_status, previous, current in records:
                    if rename_status.startswith('R'):
                        before = before.replace(Path(previous).name.encode(),
                                                Path(current).name.encode())
                description += '; content unchanged' if before == after else '; content changed'
            # A removed page is retained in the preview with deletions.
            link = page_link(new_info or old_info, output)
            if link:
                content = f'<iframe title="Changes" src="{link}"></iframe>'
            else:
                content = '<p>No standalone manual page for this file.</p>'
            comparison = f'change-{number}.html'
            title = html.escape(label)
            details = html.escape(f'{old or "(added)"} → {new or "(deleted)"}')
            (output / comparison).write_text(document(label,
                f'<p><a href="../{preview_file.name}">All changes</a></p><h1>{title}</h1>'
                f'<p>{details}</p>{content}'), encoding='utf-8')
            items.append(f'<li><a href="{output.name}/{comparison}">{title}</a> '
                         f'({html.escape(description)})'
                         f'<br><small>{details}</small></li>')
        description = (f'Commit <code>{commit}</code> compared with its first parent '
                       if commit else 'Uncommitted working-tree changes compared with HEAD ')
        body = (f'<h1>Documentation changes</h1><p>{description}<code>{parent}</code>.</p>'
                '<p>Open a page to review its changes. '
                'Inserted text is highlighted in green and '
                'deleted text in red with a strikethrough.</p>')
        body += '<ul>' + ''.join(items) + '</ul>' if items else '<p>No changes under doc/.</p>'
        preview_file.write_text(document('Documentation changes', body), encoding='utf-8')
    preview = str(preview_file)
    print(f'Preview: {preview}', flush=True)
    if args.open or args.browser:
        try:
            subprocess.run([args.browser or 'xdg-open', preview], check=True)
        except (OSError, subprocess.CalledProcessError) as error:
            print(f'Could not open the browser: {error}. '
                  'The preview is still available at the link above.', file=sys.stderr)


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError, zipfile.BadZipFile) as error:
        raise SystemExit(f'Documentation preview failed: {error}') from error
