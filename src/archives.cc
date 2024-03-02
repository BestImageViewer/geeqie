/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Authors: Vladimir Nadvornik, Laurent Monin
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

#include "archives.h"

#include <config.h>

#include "debug.h"
#include "intl.h"

#if HAVE_ARCHIVE
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

#include <archive.h>
#include <archive_entry.h>

#include "filedata.h"
#include "main-defines.h"
#include "main.h"
#include "ui-fileops.h"

/* Copied from the libarchive .repo. examples */

namespace
{

int verbose = 0;

void msg(const char *m)
{
	log_printf("Open Archive - libarchive error: %s \n", m);
}

void errmsg(const char *m)
{
	if (m == nullptr)
		{
		m = "Error: No error description provided.\n";
		}
	msg(m);
}

int copy_data(struct archive *ar, struct archive *aw)
{
	int r;
	const void *buff;
	size_t size;
	int64_t offset;

	for (;;)
		{
		r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF)
			return (ARCHIVE_OK);
		if (r != ARCHIVE_OK)
			{
			errmsg(archive_error_string(ar));
			return (r);
			}
		r = archive_write_data_block(aw, buff, size, offset);
		if (r != ARCHIVE_OK)
			{
			errmsg(archive_error_string(ar));
			return (r);
			}
		}
}

gboolean extract(const char *filename, bool do_extract, int flags)
{
	struct archive *a;
	struct archive *ext;
	struct archive_entry *entry;
	int r;

	a = archive_read_new();
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	if (filename != nullptr && strcmp(filename, "-") == 0)
		{
		filename = nullptr;
		}
	if ((r = archive_read_open_filename(a, filename, 10240)))
		{
		errmsg(archive_error_string(a));
		errmsg("\n");
		return(FALSE);
		}
	for (;;)
		{
		int needcr = 0;

		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_EOF)
			{
			break;
			}
		if (r != ARCHIVE_OK)
			{
			errmsg(archive_error_string(a));
			errmsg("\n");
			return(FALSE);
			}
		if (verbose && do_extract)
			{
			msg("x ");
			}
		if (verbose || !do_extract)
			{
			msg(archive_entry_pathname(entry));
			msg(" ");
			needcr = 1;
			}
		if (do_extract)
			{
			r = archive_write_header(ext, entry);
			if (r != ARCHIVE_OK)
				{
				errmsg(archive_error_string(a));
				needcr = 1;
				}
			else
				{
				r = copy_data(a, ext);
				if (r != ARCHIVE_OK)
					{
					needcr = 1;
					}
				}
			}
		if (needcr)
			{
			msg("\n");
			}
		}
	archive_read_close(a);
	archive_read_free(a);

	archive_write_close(ext);
	archive_write_free(ext);
	return(TRUE);
}

} // namespace

gchar *open_archive(const FileData *fd)
{
	int flags;
	gchar *current_dir;
	gchar *destination_dir;
	gboolean success;
	gint error;

	destination_dir = g_build_filename(g_get_tmp_dir(), GQ_ARCHIVE_DIR, instance_identifier, fd->path, NULL);

	if (!recursive_mkdir_if_not_exists(destination_dir, 0755))
		{
		log_printf("%s%s%s", _("Open Archive - Cannot create directory: "), destination_dir, "\n");
		g_free(destination_dir);
		return nullptr;
		}

	current_dir = g_get_current_dir();
	error = chdir(destination_dir);
	if (error)
		{
		log_printf("%s%s%s%s%s", _("Open Archive - Cannot change directory to: "), destination_dir, _("\n  Error code: "), strerror(errno), "\n");
		g_free(destination_dir);
		g_free(current_dir);
		return nullptr;
		}

	flags = ARCHIVE_EXTRACT_TIME;
	success = extract(fd->path, true, flags);

	error = chdir(current_dir);
	if (error)
		{
		log_printf("%s%s%s%s%s", _("Open Archive - Cannot change directory to: "), current_dir, _("\n  Error code: "), strerror(errno), "\n");
		g_free(destination_dir);
		g_free(current_dir);
		return nullptr;
		}
	g_free(current_dir);

	if (!success)
		{
		g_free(destination_dir);
		destination_dir = nullptr;
		}

	return destination_dir;
}
#else
gchar *open_archive(const FileData *)
{
	log_printf("%s", _("Warning: libarchive not installed"));
	return nullptr;
}
#endif /* HAVE_ARCHIVE */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
