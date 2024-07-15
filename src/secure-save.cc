/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Laurent Monin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "secure-save.h"

#include <cerrno>
#include <cstdarg>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>

#include <glib/gprintf.h>

#include <config.h>

#include "debug.h"
#include "intl.h"

/**
 * @file secure-save.cc
 * 
 * ABOUT SECURE SAVE \n
 * This code was borrowed from the ELinks project (http://elinks.cz)
 * It was originally written by me (Laurent Monin aka Zas) and heavily
 * modified and improved by all ELinks contributors.
 * This code was released under the GPLv2 licence.
 * It was modified to be included in geeqie on 2008/04/05
 * If ssi->secure_save is TRUE:
 *
 * A call to secure_open("/home/me/.confdir/filename", mask) will open a file
 * named "filename.tmp_XXXXXX" in /home/me/.confdir/ and return a pointer to a
 * structure SecureSaveInfo on success or NULL on error.
 *
 * filename.tmp_XXXXXX can't conflict with any file since it's created using
 * mkstemp(). XXXXXX is a random string.
 *
 * Subsequent write operations are done using returned SecureSaveInfo FILE *
 * field named fp.
 *
 * If an error is encountered, SecureSaveInfo int field named err is set
 * (automatically if using secure_fp*() functions or by programmer)
 *
 * When secure_close() is called, "filename.tmp_XXXXXX" is flushed and closed,
 * and if SecureSaveInfo err field has a value of zero, "filename.tmp_XXXXXX"
 * is renamed to "filename". If this succeeded, then secure_close() returns 0.
 *
 * WARNING: since rename() is used, any symlink called "filename" may be
 * replaced by a regular file. If destination file isn't a regular file,
 * then secsave is disabled for that file.
 *
 * If ssi->secure_save is FALSE:
 *
 * No temporary file is created, "filename" is truncated, all operations are
 * done on it, no rename nor flush occur, symlinks are preserved.
 *
 * In both cases:
 *
 * Access rights are affected by secure_open() mask parameter.
 */

/**
 * @file
 * @FIXME locking system on files about to be rewritten ?
 * @FIXME Low risk race conditions about ssi->file_name.
 */

SecureSaveErrno secsave_errno = SS_ERR_NONE;


/** Open a file for writing in a secure way. @returns a pointer to a
 * structure secure_save_info on success, or NULL on failure. */
static SecureSaveInfo *
secure_open_umask(const gchar *file_name)
{
	struct stat st;

	secsave_errno = SS_ERR_NONE;

	g_autofree auto *ssi = g_new0(SecureSaveInfo, 1);
	if (!ssi) {
		secsave_errno = SS_ERR_OUT_OF_MEM;
		return nullptr;
	}

	ssi->secure_save = TRUE;
	ssi->preserve_perms = TRUE;
	ssi->unlink_on_error = TRUE;

	ssi->file_name = g_strdup(file_name);
	if (!ssi->file_name) {
		secsave_errno = SS_ERR_OUT_OF_MEM;
		return nullptr;
	}
	g_autofree gchar *file_name_watcher = ssi->file_name;

	/* Check properties of final file. */
#ifndef NO_UNIX_SOFTLINKS
	if (lstat(ssi->file_name, &st)) {
#else
	if (stat(ssi->file_name, &st)) {
#endif
		/* We ignore error caused by file inexistence. */
		if (errno != ENOENT) {
			/* lstat() error. */
			ssi->err = errno;
			secsave_errno = SS_ERR_STAT;
			return nullptr;
		}
	} else {
		if (!S_ISREG(st.st_mode)) {
			/* Not a regular file, secure_save is disabled. */
			ssi->secure_save = FALSE;
		} else {
#if HAVE_ACCESS
			/* XXX: access() do not work with setuid programs. */
			if (access(ssi->file_name, R_OK | W_OK) < 0) {
				ssi->err = errno;
				secsave_errno = SS_ERR_ACCESS;
				return nullptr;
			}
#else
			FILE *f1;

			/* We still have a race condition here between
			 * [l]stat() and fopen() */

			f1 = fopen(ssi->file_name, "rb+");
			if (f1) {
				fclose(f1);
			} else {
				ssi->err = errno;
				secsave_errno = SS_ERR_OPEN_READ;
				return nullptr;
			}
#endif
		}
	}

	if (ssi->secure_save) {
		/* We use a random name for temporary file, mkstemp() opens
		 * the file and return a file descriptor named fd, which is
		 * then converted to FILE * using fdopen().
		 */
		gint fd;
		g_autofree gchar *randname = g_strconcat(ssi->file_name, ".tmp_XXXXXX", NULL);

		if (!randname) {
			secsave_errno = SS_ERR_OUT_OF_MEM;
			return nullptr;
		}

		/* No need to use safe_mkstemp() here. --Zas */
		fd = g_mkstemp(randname);
		if (fd == -1) {
			secsave_errno = SS_ERR_MKSTEMP;
			return nullptr;
		}

		ssi->fp = fdopen(fd, "wb");
		if (!ssi->fp) {
			secsave_errno = SS_ERR_OPEN_WRITE;
			ssi->err = errno;
			return nullptr;
		}

		ssi->tmp_file_name = g_steal_pointer(&randname);
	} else {
		/* No need to create a temporary file here. */
		ssi->fp = fopen(ssi->file_name, "wb");
		if (!ssi->fp) {
			secsave_errno = SS_ERR_OPEN_WRITE;
			ssi->err = errno;
			return nullptr;
		}
	}

	file_name_watcher = nullptr;
	return g_steal_pointer(&ssi);
}

SecureSaveInfo *
secure_open(const gchar *file_name)
{
	SecureSaveInfo *ssi;
	mode_t saved_mask;
#ifdef CONFIG_OS_WIN32
	/* There is neither S_IRWXG nor S_IRWXO under crossmingw32-gcc */
	const mode_t mask = 0177;
#else
	const mode_t mask = S_IXUSR | S_IRWXG | S_IRWXO;
#endif

	saved_mask = umask(mask);
	ssi = secure_open_umask(file_name);
	umask(saved_mask);

	return ssi;
}

/** Close a file opened with secure_open(). Rreturns 0 on success,
 * errno or -1 on failure.
 */
gint
secure_close(SecureSaveInfo *ssi)
{
	if (!ssi) return -1;

	auto ssi_free = [](SecureSaveInfo *ssi, gint ret)
	{
		if (ssi->tmp_file_name)
			{
			if (ret && ssi->unlink_on_error) unlink(ssi->tmp_file_name);
			g_free(ssi->tmp_file_name);
			}
		if (ssi->file_name) g_free(ssi->file_name);
		g_free(ssi);
		return ret;
	};

	if (!ssi->fp) return ssi_free(ssi, -1);

	if (ssi->err) {	/* Keep previous errno. */
		fclose(ssi->fp); /* Close file */
		return ssi_free(ssi, ssi->err);
	}

	/* Ensure data is effectively written to disk, we first flush libc buffers
	 * using fflush(), then fsync() to flush kernel buffers, and finally call
	 * fclose() (which call fflush() again, but the first one is needed since
	 * it doesn't make much sense to flush kernel buffers and then libc buffers,
	 * while closing file releases file descriptor we need to call fsync(). */
#if HAVE_FFLUSH || HAVE_FSYNC
	if (ssi->secure_save) {
		gboolean fail = FALSE;

#if HAVE_FFLUSH
		fail = (fflush(ssi->fp) == EOF);
#endif

#if HAVE_FSYNC
		if (!fail) fail = fsync(fileno(ssi->fp));
#endif

		if (fail) {
			gint ret = errno;
			secsave_errno = SS_ERR_OTHER;

			fclose(ssi->fp); /* Close file, ignore errors. */
			return ssi_free(ssi, ret);
		}
	}
#endif

	/* Close file. */
	if (fclose(ssi->fp) == EOF) {
		secsave_errno = SS_ERR_OTHER;
		return ssi_free(ssi, errno);
	}

	if (ssi->secure_save && ssi->file_name && ssi->tmp_file_name) {
		struct stat st;

		/**
		 * @FIXME Race condition on ssi->file_name. The file
		 * named ssi->file_name may have changed since
		 * secure_open() call (where we stat() file and
		 * more..).  */
#ifndef NO_UNIX_SOFTLINKS
		if (lstat(ssi->file_name, &st) == 0)
#else
		if (stat(ssi->file_name, &st) == 0)
#endif
			{
			/* set the dest file attributes to that of source (ignoring errors) */
			if (ssi->preserve_perms)
				{
				if (chown(ssi->tmp_file_name, st.st_uid, st.st_gid) != 0) log_printf("chown('%s', %d, %d) failed", ssi->tmp_file_name, st.st_uid, st.st_gid);
				if (chmod(ssi->tmp_file_name, st.st_mode) != 0) log_printf("chmod('%s', %o) failed", ssi->tmp_file_name, st.st_mode);
				}

			if (ssi->preserve_mtime)
				{
				struct utimbuf tb;

				tb.actime = st.st_atime;
				tb.modtime = st.st_mtime;
				utime(ssi->tmp_file_name, &tb);
				}
			}
		if (rename(ssi->tmp_file_name, ssi->file_name) == -1) {
			secsave_errno = SS_ERR_RENAME;
			return ssi_free(ssi, errno);
		}
	}

	return ssi_free(ssi, 0); /* Success. */
}


/** fputs() wrapper, set ssi->err to errno on error. If ssi->err is set when
 * called, it immediately returns EOF.
 */
gint
secure_fputs(SecureSaveInfo *ssi, const gchar *s)
{
	gint ret;

	if (!ssi || !ssi->fp || ssi->err) return EOF;

	ret = fputs(s, ssi->fp);
	if (ret == EOF) {
		secsave_errno = SS_ERR_OTHER;
		ssi->err = errno;
	}

	return ret;
}


/** fputc() wrapper, set ssi->err to errno on error. If ssi->err is set when
 * called, it immediately returns EOF.
 */
gint
secure_fputc(SecureSaveInfo *ssi, gint c)
{
	gint ret;

	if (!ssi || !ssi->fp || ssi->err) return EOF;

	ret = fputc(c, ssi->fp);
	if (ret == EOF) {
		ssi->err = errno;
		secsave_errno = SS_ERR_OTHER;
	}

	return ret;
}

/** fprintf() wrapper, set ssi->err to errno on error and return a negative
 * value. If ssi->err is set when called, it immediately returns -1.
 */
gint
secure_fprintf(SecureSaveInfo *ssi, const gchar *format, ...)
{
	va_list ap;
	gint ret;

	if (!ssi || !ssi->fp || ssi->err) return -1;

	va_start(ap, format);
	ret = g_vfprintf(ssi->fp, format, ap);
	va_end(ap);

	return ret;
}

/** fwrite() wrapper, set ssi->err to errno on error and return a value less than
 * the number of elements to write. If ssi->err is set when called, it immediately returns 0.
 */
size_t
secure_fwrite(gconstpointer ptr, size_t size, size_t nmemb, SecureSaveInfo *ssi)
{
	size_t ret;

	if (!ssi || !ssi->fp || ssi->err) return 0;

	ret = fwrite(ptr, size, nmemb, ssi->fp);
	if (ret < nmemb)
		{
		ssi->err = errno;
		secsave_errno = SS_ERR_OTHER;
		}

	return ret;
}

gchar *
secsave_strerror(SecureSaveErrno secsave_error)
{
	switch (secsave_error) {
	case SS_ERR_OPEN_READ:
		return _("Cannot read the file");
	case SS_ERR_STAT:
		return _("Cannot get file status");
	case SS_ERR_ACCESS:
		return _("Cannot access the file");
	case SS_ERR_MKSTEMP:
		return _("Cannot create temp file");
	case SS_ERR_RENAME:
		return _("Cannot rename the file");
	case SS_ERR_DISABLED:
		return _("File saving disabled by option");
	case SS_ERR_OUT_OF_MEM:
		return _("Out of memory");
	case SS_ERR_OPEN_WRITE:
		return _("Cannot write the file");
	case SS_ERR_NONE: /* Impossible. */
	case SS_ERR_OTHER:
	default:
		return _("Secure file saving error");
	}
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
