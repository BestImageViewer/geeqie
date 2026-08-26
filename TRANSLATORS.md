# Geeqie Readme for translators

## Creating a new translation

If you wish to contribute a new language, you must first create the appropriate language file - for example `de.po` is the language file for the German language.

The file prefix is determined by your locale. You can see your locale by executing the command `locale` from a command line window.

A complete list of language locales is in [https://en.wikipedia.org/wiki/List_of_ISO_639_language_codes](https://en.wikipedia.org/wiki/List_of_ISO_639_language_codes).

Regional subscripts, if required, are listed in  [https://simplelocalize.io/data/locales/](https://simplelocalize.io/data/locales/) .

You should first create a blank language file by executing, for example, `./po/touch gv.po`.

## Updating a translation file or populating a new translation file

To update (or to populate from new) a language file from the current sources, execute for example `./tools/update-translation.sh gv.po`.

Then edit the language file using a suitable tool.

When complete, you should create a pull request on [https://github.com/BestImageViewer/geeqie/pulls](https://github.com/BestImageViewer/geeqie/pulls).

## Which translation tool to use

The program `poedit` is one of the programs suitable for making translations.

## Required program files

`xgettext` and `itstool` are required.

## Other files

The script `./tools/gen-translations-stats.sh` generates statistics about translations.

## Creating a GitHub Pull Request

This guide explains how to make a pull request - you should change the text where necessary to suit the translation you are making.

Go to [https://github.com](https://github.com) and click on Sign Up to create a new account.

Go to the Geeqie GitHub page:
   [https://github.com/BestImageViewer/geeqie](https://github.com/BestImageViewer/geeqie)

Towards the top right corner, click the **Fork** button. This will create your own copy of the project under your GitHub account.

Open a terminal on your computer and clone your fork locally:

```bash
cd <a work area>
git clone https://github.com/<your-github-username>/geeqie.git
cd geeqie
git checkout -b new-translation-branch
git add .
```

Edit the .po file, as described above.

When complete, commit the changes locally:

`
git commit -m "New language translation"
`

To create the Pull Request, go to your fork on GitHub and click on the Compare & pull request button.
