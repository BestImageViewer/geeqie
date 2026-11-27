# Coding and Documentation Style

For strings that are not user-facing, use an inline comment such as
`// NOTRANSLATE: D-Bus protocol method.` rather than adding a string to the
exclusion list in `build-aux/untranslated-text.sh`. The marker suppresses the
whole line, so keep user-facing strings on separate lines and explain why the
annotated text must not be translated. Use `/* NOTRANSLATE: reason */` before
the trailing backslash on a continued macro line. Ordinary comments do not
suppress the check, and exclusions must not be added for entire files.

[Error Logging](#error-logging)  
[SPDX license](#spdx-license)  
[Git change log](#git-change-log)  
[Source Code Style](#source-code-style)  
[Shell Script Style](#shell-script-style)  
[External Software Tools](#external-software-tools)  
[Geeqie Software Tools](#geeqie-software-tools)  
[Documentation](#documentation)  
[Documentation - C code](#c-code)  
[Documentation - Script files](#script-files)  
[Documentation - Markdown](#markdown)  
[Doxygen](#doxygen)  

---

## Error Logging

### Categorized debug logging

Use `DEBUG_LOG(DebugCategory::THUMBNAILS, 2, "Cache hit: %s", path)` for new
subsystem diagnostics. Select categories at runtime with
`--debug-domains=image,render,metadata` or another comma-separated list.

| Category | Scope |
|----------|-------|
| `general` | Startup and general lifecycle |
| `thumbnails` | Thumbnail loading and queues |
| `cache` | Memory and disk caches |
| `image` | Image loading, decoding, animation, and preloading |
| `render` | Drawing, scaling, overlays, and color management |
| `metadata` | EXIF, keywords, and metadata writes |
| `files` | File operations and monitoring |
| `ui` | Navigation, views, selection, and drag and drop |
| `duplicates` | Duplicate search and comparison |
| `config` | Configuration loading and parsing |
| `collections` | Collection loading, updates, and management |
| `external` | External editors, command execution, and output |

The default is `all`; `none` disables ordinary debug messages. The filter is
independent of `--debug=N`, applies to the running session, and is available
only in debug builds. Invalid domain lists leave the previous filter unchanged.

Use these verbosity levels for new or migrated messages:

| Level | Purpose |
|-------|---------|
| 0 | Ordinary debug logging disabled |
| 1 | Operation summaries and failures |
| 2 | Decisions, cache hits/misses, and fallbacks |
| 3 | Detailed per-file or per-item activity |
| 4 | Rendering, frame, and internal-state tracing |

Use `DEBUG_LOG()` for ordinary diagnostics. It retains compiler printf-format
checking and does not evaluate format arguments when its category or level is
disabled, including in release builds. Keep debug arguments free of required side
effects. Numbered compatibility macros remain temporarily in the general category until
their callers are migrated. Their historical format-warning suppression remains
only during that transition.

Categorized debug messages use the same record format:

```text
[+0.123456] [cache] [debug:2] [pid:1234] [thread:3] message
```

Time is monotonic elapsed time since debug timing was initialized. Process IDs
distinguish instances sharing a log. Thread numbers
are stable session-local identifiers assigned on first use, not operating system
thread IDs. Multiline messages repeat the same context on every line. Source
locations are off by default at every verbosity; enable them separately with
`--debug-source=on`. Use `--debug-source=off`
to turn them off again. The existing timer preference and the log window's
**Delta** toggle control an additional per-thread interval since the previous
debug record. The elapsed timestamp is always present.

Record timing uses a shared monotonic origin and independent per-thread deltas.
Startup initializes the origin directly; subsequent initialization does not reset
it. Terminal and log-file writes are serialized within an instance; regex changes and log-file
replacement are protected while worker threads log. Existing regex filtering and
log destinations are retained. Warnings and errors sent through `log_printf()` remain outside the
debug format and are not suppressed by the domain filter. Debug output can contain
filenames, paths, and metadata; review it before sharing.

Temporary developer tools remain available, but calls must not be committed.
Ordinary logging must not suppress compiler format diagnostics.

Log-window delivery uses one idle source and a thread-safe bounded queue. Each
callback handles at most 64 messages and 64 KiB, combining adjacent messages
with the same display style into one text insertion. The queue holds at
most 1,024 messages or 1 MiB; messages exceeding 64 KiB are omitted from the
window. Overflow is reported in the window with the number omitted. These limits
do not affect terminal or file output. Pending window messages are discarded
when the application exits.

Window history retains at most the configured line count, capped at 99,999 lines,
and 262,144 Unicode characters. A nonpositive configured line count uses the
10,000-line safety limit. History collected before the window opens is limited
to 1 MiB and the same maximum number of records.

### Operation lifecycle records

Include `debug-operation.h` and zero-initialize a `DebugOperation` for work that
can overlap, be queued, or be cancelled. Use `DEBUG_OPERATION_START()` and
`DEBUG_OPERATION_FINISH()` with `COMPLETED`, `CANCELLED`, or `FAILED`. Operation
names must outlive the job; use string literals. All access to a particular state
belongs to its owner thread or must use the owner's existing lock. IDs themselves
are allocated atomically and are unique across subsystems within a process.

Records add `[operation:N]`, the operation name, its lifecycle state, and elapsed
monotonic seconds. Start and terminal records share the ID; finishing again is a
no-op. Beginning an already active operation preserves its ID, so deferred and
actual starts can share it. Explicitly finish a cancelled operation before reuse.
Tracking continues while output is filtered, but format arguments are evaluated
only when a record is enabled. Failures use level 1 and retain category filtering.
The macros compile away in builds without debug logging.

Image loads (including preloads and thumbnail decoder jobs) use level 3, directory
scans level 2, and duplicate searches level 1. Image timing includes deferred and
worker queue waits through result delivery. Releasing a loader before delivery
records cancellation after waiting for its worker. Duplicate-search completion
includes populating the results; restarts and stops close the old operation.
Decoder, rendering, and per-file detail messages are not all operation-tagged yet.

### Explicit message severity

Include `log.h` and use `log_info()`, `log_warning()`, or `log_error()` for new
operational messages. All three retain compiler printf-format checking and are
available in builds without debug logging. Translate the message, not the stable
`[info]`, `[warning]`, or `[error]` label added automatically to each line. A final
newline is supplied automatically. For example:

```cpp
log_warning(_("Failed to open help: %s"), error->message);
```

Info messages go to standard output and respect the regex filter. Warnings and
errors go to standard error and bypass that filter. All three are independent of
debug categories and verbosity, append to the configured log file, and use the
bounded window queue and history. Queue overflow can still omit window messages;
terminal and file output are unaffected. Window colors follow the explicit
severity even when the translated message starts with another word.

Existing `log_printf()` calls retain their historical output destinations,
filtering, and text-based warning/error classification. Migrate them gradually;
remove redundant severity prefixes when changing a caller.

### DEBUG_0()

Use `DEBUG_0()` only for temporary debugging i.e. not in code in the repository.
The user will then not see irrelevant debug output when the default
`debug level = 0` is used.

### log_printf()

If the first word of the message is "error" or "warning" (case insensitive) the message will be color-coded appropriately.

- Note that these messages are output in the idle loop.

### print_term()

`print_term(gboolean err, const gchar *text_utf8)`

- If `err` is TRUE output is to STDERR, otherwise to STDOUT

### DEBUG_NAME(widget)

For use with the [GTKInspector](https://wiki.gnome.org/action/show/Projects/GTK/Inspector?action=show&redirect=Projects%2FGTK%2B%2FInspector) to provide a visual indication of where objects are declared.

Sample command line call:  
`GTK_DEBUG=interactive src/geeqie`

### DEBUG_BT()

Prints a backtrace.
Use only for temporary debugging i.e. not in code in the repository

### DEBUG_FD()

Prints a dump of the FileData hash list as a ref. count followed by the full path of the item.
Use only for temporary debugging i.e. not in code in the repository

### DEBUG_RU()

Prints memory usage and runtime from `getrusage`.
Use only for temporary debugging i.e. not in code in the repository

### Log Window

When the log text has focus, the configurable `Run command on selected log text` shortcut (F1 by default) executes the command specified in `Edit/Preferences/Behavior/Log Window command` with the selected text as a parameter.
If no text is selected, the entire line is passed to the command.
This feature may be used to open an editor at a file location in the text string.

---

## SPDX license

In all new files include a header, like this:

```c
/* SPDX-License-Identifier: GPL-2.0-or-later */

```

---

## git change-log

If referencing a Geeqie GitHub issue, include the issue number in the summary line and a hyperlink to the GitHub issue webpage in the message body. Start with a short summary in the first line (without a dot at the end) followed by a empty line.

Use whole sentences beginning with Capital letter. For each modification use a new line. Or you can write the theme, colon and then every change on new line, begin with "- ".

See also [A Note About Git Commit Messages](https://tbaggery.com/2008/04/19/a-note-about-git-commit-messages.html)

Example:

```text
I did some bugfixes

There was the bug that something was wrong. I fixed it.

Library:
- the interface was modified
- new functions were added`
```

Also please use your full name and a working e-mail address as author for any contribution.

---

## Source Code Style

Indentation: tabs at 4 spaces

Names:

- of variables & functions: small\_letters  
- of defines: CAPITAL\_LETTERS

Try to use explicit variable and function names.  
Try not to use macros.  
Use **either** "struct foo" OR "foo"; never both

Conditions, cycles:  

```c
if (<cond>)
    {
    <command>;
    ...
    <command>;
    }
else
    {
    <command>;
    ...
    <command>;
    }

if (<cond_very_very_very_very_very_very_very_very_very_long> &&
<cond2very_very_very_very_very_very_very_very_very_long>)
<the_only_command>;

switch (<var>)
    {
    case 0:
        <command>;
        <command>;
        break;
    case 1:
        <command>; break;
    }

for (i = 0; i <= 10; i++)
    {
    <command>;
    ...
    <command>;
    }
```

Functions:

```c
gint bar(<var_def>, <var_def>, <var_def>)
{
    <command>;
    ...
    <command>;

    return 0; // i.e. SUCCESS; if error, you must return minus <err_no> @FIXME
}

void bar2(void)
{
    <command>;
    ...
    <command>;
}
```

Pragma: (Indentation 2 spaces)

```c
#ifdef ENABLE_NLS
#  undef _
#  define _(String) (String)
#endif /* ENABLE_NLS */
```

Headers:

```c
#ifndef FILENAME_H
```

Use [Names and Order of Includes](https://google.github.io/styleguide/cppguide.html#Names_and_Order_of_Includes) for headers include order.

Use spaces around every operator (except `.`, `->`, `++` and `--`).  
Unary operator `*` and `&` are missing the space from right, (and also unary `-`).

As you can see above, parentheses are closed to inside, i.e. ` (blah blah) `  
In `function(<var>)` there is no space before the `(`.  
You *may* use more tabs/spaces than you *ought to* (according to this CodingStyle), if it makes your code nicer in being vertically indented.  
Don’t introduce a variable before you need to use it (C++ Core Guidelines rule [ES.21](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#es21-dont-introduce-a-variable-or-constant-before-you-need-to-use-it)).  

Use glib functions when possible (i.e. `g_ascii_isspace()` instead of `isspace()`).  
Check if used functions are not deprecated.  

### C++ style

In the case that both the following lines are syntactically correct, in order to emphasise the use of a pointer use:

```c
auto *var = function();
```

and not:

```c
auto var = function();
```

---

## Shell Script Style

Use `/bin/sh` as the interpreter directive.  
Ensure the script is POSIX compliant.  
Use `printf` rather than `echo` except for plain text.  
There are several versions of `mktemp`. Using the following definition helps portability (note that `template` is not optional):

```sh
mktemp [-d] [-q] template …
```

and use for example this style:

```sh
mktemp  "${TMPDIR:-/tmp}/geeqie.XXXXXXXXXX"
```

---

## External Software Tools

### astyle

There is no code format program that exactly matches the above style, but if you are writing new code the following command line program formats code to a fairly close level:

```sh
astyle --options=<options file>
```

Where the options file might contain:

```text
style=vtk
indent=force-tab
pad-oper
pad-header
unpad-paren
align-pointer=name
align-reference=name
```

### cppcheck

A lint-style program may be used, e.g.

```sh
cppcheck --language=c --library=gtk --enable=all --force  -USA_SIGINFO -UZD_EXPORT -Ugettext_noop -DG_KEY_FILE_DESKTOP_GROUP --template=gcc -I .. --quiet --suppressions-list=<suppressions file>
```

Where the suppressions file might contain:

```text
missingIncludeSystem
variableScope
unusedFunction
unmatchedSuppression
```

### markdownlint

Markdown documents may be validated with e.g. [markdownlint](https://github.com/markdownlint/markdownlint).

```sh
mdl --style <style file>`
```

Where the style file might contain:

```text
all
rule 'MD007', :indent => 4
rule 'MD009', :br_spaces => 2
rule 'MD010', :code_blocks => true
exclude_rule 'MD013'
```

### shellcheck

Shell scripts may also be validated, e.g.

```sh
shellcheck --enable=add-default-case,avoid-nullary-conditions,check-unassigned-uppercase,deprecate-which,quote-safe-variables
```

### shfmt

Shell scripts may formatted to some extent with [shfmt](https://github.com/mvdan/sh). At the time of writing it does not format `if`, `for` or `while` statements in the style used by Geeqie.  
However the following script can be used to achieve that:

```sh
#!/bin/sh

shfmt -s -p -ci -sr -fn | awk '
    {if ($0 ~ /; then/)
        {
        match($0, /^\t*/);
        printf('%s\n', substr($0, 0, length($0) - 6));
        printf('%s, substr("\t\t\t\t\t\t\t\t\t\t", 1, RLENGTH))
        print("then")
        }
    else if ($0 ~ /; do/)
        {
        match($0, /^\t*/);
        printf('%s\n', substr($0, 0, length($0) - 4));
        printf('%s', substr("\t\t\t\t\t\t\t\t\t\t", 1, RLENGTH))
        print("do")
        }
    else
        {
        print
        }
    }'
```

### xmllint

The .xml Help files may be validated with e.g. `xmllint`.

---

## Geeqie Software Tools

See the shell scripts section in the Doxygen documentation (`File List`, `detail level 3`, except the `src` sublist).

---

## Documentation

The documentation in Doxygen format for the latest release, or later, is [here](https://cclark.uk/geeqie/doxygen/html/index.html).

Use American, rather than British English, spelling. This will facilitate consistent
text searches. User text may be translated via the en_GB.po file.

To avoid confusion between American and British date formats, use ISO format (YYYY-MM-DD) where possible.

To document the code use the following rules to allow extraction with Doxygen.  
Not all comments have to be Doxygen comments.

### C code

- Use C comments in plain C files and use C++ comments in C++ files for one line comments.
- Use `/**` (note the two asterisks) to start comments to be extracted by Doxygen and start every following line with ` *` as shown below.
- Use `@` to indicate Doxygen keywords/commands (see below).
- Use the `@deprecated` command to indicate the function is subject to be deleted or to a  complete rewrite.

To document functions or big structures:

```c
/**
 * @brief This is a short description of the function.
 *
 * This function does ...
 *
 * @param x1 This is the first parameter named x1
 * @param y1 This is the second parameter named y1
 * @return What the function returns
 *    You can extend that return description (or anything else) by indenting the
 *    following lines until the next empty line or the next keyword/command.
 * @see Cross reference
 */
```

To document members of a structure that have to be documented (use it at least
for big structures) use the `/**<` format:  

```c
gint counter; /**< This counter counts images */

```

Document TODO or FIXME comments as:  

```c
/**  
* @todo
```

or

```c
/**  
* @FIXME
```

### Script files

Script files such as .sh, .pl, and .awk should have the file relevant file extension or be symlinked as so.

Doxygen comments should start each line with `##`, and each file should contain:

```sh
#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

## @file
## @brief <one line description>
## <contents description>
##
```

### Markdown

Use CommonMark syntax for project documentation.

Other than in exceptional circumstances do not use extensions such as GitHub Flavored
Markdown (GFM).

Use standard Markdown links or angle-bracket autolinks instead of relying on
automatic linking of bare URLs or email addresses.

GitHub issue-template YAML metadata is exempt from this policy. Extension
syntax may be shown literally inside code spans or code blocks.

The `mdl` linter checks style but does not enforce the exclusion of GFM
extensions; check for these during documentation review.

For a newline use two spaces (a backslash is not interpreted correctly by Doxygen).

## Doxygen

For further documentation about Doxygen see the [Doxygen Manual](https://www.doxygen.nl/index.html).  
For the possible commands you may use, see [Doxygen Special Commands](https://www.doxygen.nl/manual/commands.html).

The file `./tools/doxygen-help.sh` may be used to integrate access to the Doxygen files into a code editor.

Generate documentation from the checkout root with Python 3 and Doxygen installed:

```sh
tools/doxygen.sh /tmp/geeqie-doxygen
```

The destination defaults to `../doxygen`. The script skips generation when the
checkout contents, effective configuration, and generator dependencies match the
last successful build and all recorded output files still exist. It includes
tracked and nonignored files, including uncommitted documentation, and excludes
the destination itself. Ignored build directories are not documentation inputs.

Use `--force` to regenerate unchanged documentation. Use `--html-only` for a
single-pass browser preview:

```sh
tools/doxygen.sh --html-only /tmp/geeqie-doxygen
```

By default, the script also generates `searchdata.xml` for `doxygen-help.sh` in a
separate pass without graphs or additional output formats. `--html-only` does not
generate or update that editor index. Switching modes triggers regeneration;
run without `--html-only` to refresh the editor index afterward. Failed builds
do not retain an up-to-date marker.

Warnings are saved in the destination directory as `doxygen-warnings.log` for
HTML generation and `doxygen-index-warnings.log` for the editor-index pass. Each
pass replaces its own log and prints its path; cached builds retain those logs.

Run a focused documentation check with:

```sh
tools/doxygen.sh --check /tmp/geeqie-doxygen
```

This always checks the current inputs and writes `doxygen-check-warnings.log`.
Malformed commands/markup, incorrect parameter names, and unresolved references
fail the check. Undocumented functions, enum values, and missing parameter
descriptions are not required. Temporary XML output is discarded; the existing
HTML, editor index, and generation cache marker are not replaced. Without the
optional `doxygen-bash.sed` filter, shell/Perl/AWK inputs are skipped with a message;
install the filter to include their documentation.

When Doxygen is available during Meson configuration, this check is also available
as `meson test -C build 'Doxygen comments'` in the analysis suite. The generator's
regression tests run as `meson test -C build 'Doxygen generation'`, including a
malformed-comment rejection test when Doxygen is installed.

The script sets the source/output paths, project name, and version, and respects
`INLINE_SOURCES`, `STRIP_CODE_COMMENTS`, `PLANTUML_JAR_PATH`, and the optional
format/diagram settings in `doc/doxygen.conf`. HTML output is always enabled by the script.

When invoking Doxygen directly, the following environment variables may be set
to personalize its output:

```sh
DOCDIR=<output folder>
SRCDIR=<the top level directory of the project>
PROJECT=
VERSION=
PLANTUML_JAR_PATH=
INLINE_SOURCES=<YES|NO>
STRIP_CODE_COMMENTS=<YES|NO>
```

Ref: [INLINE\_SOURCES](https://www.doxygen.nl/manual/config.html#cfg_inline_sources)  
Ref: [STRIP\_CODE\_COMMENTS](https://www.doxygen.nl/manual/config.html#cfg_strip_code_comments)

For shell scripts to be documented, the file `doxygen-bash.sed` must be in the `$PATH` environment variable.  
It can be download from here:

```sh
wget https://raw.githubusercontent.com/Anvil/bash-doxygen/master/doxygen-bash.sed
chmod +x doxygen-bash.sed
```

To include diagrams in the Doxygen output, the following are required to be installed. The installation process will vary between distributions:

[The PlantUML jar](https://plantuml.com/download)

```sh
sudo apt install default-jre
sudo apt install texlive-font-utils
```

---

But in case just think about that the documentation is for other developers not
for the end user. So keep the focus.
