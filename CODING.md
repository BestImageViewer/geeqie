# Coding and Documentation Style

[Error Logging](#error-logging)  
[GPL header](#gpl-header)  
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

When the Log Window has focus, the F1 key executes the action specified in `Edit/Preferences/Behavior/Log Window F1 Command` with the selected text as a parameter.
If no text is selected, the entire line is passed to the command.
This feature may be used to open an editor at a file location in the text string.

---

## SPDX license

In all new files include a header, like this:

```c
/* SPDX-License-Identifier: LGPL-2.1-or-later */

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
Variables declarations should be followed by a blank line and should always be at the start of the block.  

Use glib types when possible (ie. gint and gchar instead of int and char).  
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
mktemp [-d] [-q] template ...
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

For a newline use two spaces (a backslash is not interpreted correctly by Doxygen).

## Doxygen

For further documentation about Doxygen see the [Doxygen Manual](https://www.doxygen.nl/index.html).  
For the possible commands you may use, see [Doxygen Special Commands](https://www.doxygen.nl/manual/commands.html).

The file `./tools/doxygen-help.sh` may be used to integrate access to the Doxygen files into a code editor.

The following environment variables may be set to personalize the Doxygen output:

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
