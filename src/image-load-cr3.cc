/*
 * Copyright (C) 2020 The Geeqie Team
 *
 * Authors: Colin Clark
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

/** This is a Will Not Fix */
#pragma GCC diagnostic ignored "-Wclobbered"

#include <config.h>

#if HAVE_JPEG && !HAVE_RAW
#include "image-load-cr3.h"

#include <cstring>

#include <glib.h>

#include "image-load-jpeg.h"
#include "image-load.h"

namespace
{

gboolean image_loader_cr3_load(gpointer loader, const guchar *buf, gsize count, GError **error)
{
	/** @FIXME Just start search at where full size jpeg should be,
	 * then search through the file looking for a jpeg end-marker
	 */
	gboolean found = FALSE;
	gint i;
	guint n;

	n = 0;
	while (n < count - 4 && !found)
		{
		if (memcmp(&buf[n], "mdat", 4) == 0)
			{
			if (memcmp(&buf[n + 12], "\xFF\xD8", 2) == 0)
				{
				i = 0;
				while (!found )
					{
					if (memcmp(&buf[n + 12 + i], "\xFF\xD9", 2) == 0)
						{
						found = TRUE;
						}
					i++;
					}
				}
			else
				{
				break;
				}
			}
		else
			{
			n++;
			}
		}

	if (!found)
		{
		return FALSE;
		}

	return image_loader_jpeg_load(loader, buf + n + 12, i, error);
}

gchar* image_loader_cr3_get_format_name(gpointer)
{
	return g_strdup("cr3");
}

gchar** image_loader_cr3_get_format_mime_types(gpointer)
{
	static const gchar *mime[] = {"image/x-canon-cr3", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

} // namespace

void image_loader_backend_set_cr3(ImageLoaderBackend *funcs)
{
	image_loader_backend_set_jpeg(funcs);

	funcs->load = image_loader_cr3_load;

	funcs->get_format_name = image_loader_cr3_get_format_name;
	funcs->get_format_mime_types = image_loader_cr3_get_format_mime_types;
}

#endif // HAVE_JPEG && !HAVE_RAW

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
