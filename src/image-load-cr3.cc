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

#include "image-load-cr3.h"

#include <cstring>

#include <glib.h>

#include "image-load-jpeg.h"
#include "image-load.h"

namespace
{

struct ImageLoaderCr3 : public ImageLoaderJpeg
{
public:
	gboolean write(const guchar *buf, gsize &chunk_size, gsize count, GError **error) override;
	gchar *get_format_name() override;
	gchar **get_format_mime_types() override;
};

gboolean ImageLoaderCr3::write(const guchar *buf, gsize &chunk_size, gsize count, GError **error)
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

	gboolean ret = ImageLoaderJpeg::write(buf + n + 12, chunk_size, i, error);
	if (ret)
		{
		chunk_size = count;
		}
	return ret;
}

gchar *ImageLoaderCr3::get_format_name()
{
	return g_strdup("cr3");
}

gchar **ImageLoaderCr3::get_format_mime_types()
{
	static const gchar *mime[] = {"image/x-canon-cr3", nullptr};
	return g_strdupv(const_cast<gchar **>(mime));
}

} // namespace

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_cr3()
{
	return std::make_unique<ImageLoaderCr3>();
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
