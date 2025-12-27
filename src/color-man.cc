/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
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

#include "color-man.h"

#include <config.h>

#if HAVE_LCMS
/*** color support enabled ***/

#include <algorithm>
#include <cstring>

#include <glib-object.h>
#include <lcms2.h>

#include "image.h"
#include "intl.h"
#include "layout.h"
#include "options.h"
#include "ui-fileops.h"

namespace
{

#define GQ_RESOURCE_PATH_ICC "/org/geeqie/icc"

struct ColorManCache {
	cmsHPROFILE   profile_in;
	cmsHPROFILE   profile_out;
	cmsHTRANSFORM transform;

	ColorManProfileType profile_in_type;
	gchar *profile_in_file;

	ColorManProfileType profile_out_type;
	gchar *profile_out_file;

	gboolean has_alpha;

	gint refcount;
};


cmsHPROFILE color_man_create_adobe_comp()
{
	/* ClayRGB1998 is AdobeRGB compatible */
	g_autoptr(GBytes) ClayRGB1998_icc_bytes = g_resources_lookup_data(GQ_RESOURCE_PATH_ICC "/ClayRGB1998.icc",
	                                                                  G_RESOURCE_LOOKUP_FLAGS_NONE, nullptr);

	gsize ClayRGB1998_icc_len;
	gconstpointer ClayRGB1998_icc = g_bytes_get_data(ClayRGB1998_icc_bytes, &ClayRGB1998_icc_len);

	return cmsOpenProfileFromMem(ClayRGB1998_icc, ClayRGB1998_icc_len);
}

/**
 * @brief Retrieves the internal scale factor that maps from window coordinates to the actual device pixels
 * @param  -
 * @returns scale factor
 */
gint scale_factor()
{
	LayoutWindow *lw = get_current_layout();

	return gtk_widget_get_scale_factor(lw->window);
}

/*
 *-------------------------------------------------------------------
 * color transform cache
 *-------------------------------------------------------------------
 */

GList *cm_cache_list = nullptr;


void color_man_cache_ref(ColorManCache *cc)
{
	if (!cc) return;

	cc->refcount++;
}

void color_man_cache_unref(ColorManCache *cc)
{
	if (!cc) return;

	cc->refcount--;
	if (cc->refcount < 1)
		{
		if (cc->transform) cmsDeleteTransform(cc->transform);
		if (cc->profile_in) cmsCloseProfile(cc->profile_in);
		if (cc->profile_out) cmsCloseProfile(cc->profile_out);

		g_free(cc->profile_in_file);
		g_free(cc->profile_out_file);

		g_free(cc);
		}
}

cmsHPROFILE color_man_cache_load_profile(ColorManProfileType type, const gchar *file,
                                         const guchar *data, guint data_len)
{
	cmsHPROFILE profile = nullptr;

	switch (type)
		{
		case COLOR_PROFILE_FILE:
			if (file)
				{
				g_autofree gchar *pathl = path_from_utf8(file);
				profile = cmsOpenProfileFromFile(pathl, "r");
				}
			break;
		case COLOR_PROFILE_SRGB:
			profile = cmsCreate_sRGBProfile();
			break;
		case COLOR_PROFILE_ADOBERGB:
			profile = color_man_create_adobe_comp();
			break;
		case COLOR_PROFILE_MEM:
			if (data)
				{
				profile = cmsOpenProfileFromMem(data, data_len);
				}
			break;
		case COLOR_PROFILE_NONE:
		default:
			break;
		}

	return profile;
}

ColorManCache *color_man_cache_new(ColorManProfileType in_type, const gchar *in_file,
                                   const guchar *in_data, guint in_data_len,
                                   ColorManProfileType out_type, const gchar *out_file,
                                   const guchar *out_data, guint out_data_len,
                                   gboolean has_alpha)
{
	ColorManCache *cc;

	cc = g_new0(ColorManCache, 1);
	cc->refcount = 1;

	cc->profile_in_type = in_type;
	cc->profile_in_file = g_strdup(in_file);

	cc->profile_out_type = out_type;
	cc->profile_out_file = g_strdup(out_file);

	cc->has_alpha = has_alpha;

	cc->profile_in = color_man_cache_load_profile(cc->profile_in_type, cc->profile_in_file,
						      in_data, in_data_len);
	cc->profile_out = color_man_cache_load_profile(cc->profile_out_type, cc->profile_out_file,
						       out_data, out_data_len);

	if (!cc->profile_in || !cc->profile_out)
		{
		DEBUG_1("failed to load color profile for %s: %d %s",
				  (!cc->profile_in) ? "input" : "screen",
				  (!cc->profile_in) ? cc->profile_in_type : cc->profile_out_type,
				  (!cc->profile_in) ? cc->profile_in_file : cc->profile_out_file);

		color_man_cache_unref(cc);
		return nullptr;
		}

	cc->transform = cmsCreateTransform(cc->profile_in,
					   (has_alpha) ? TYPE_RGBA_8 : TYPE_RGB_8,
					   cc->profile_out,
					   (has_alpha) ? TYPE_RGBA_8 : TYPE_RGB_8,
					   options->color_profile.render_intent, 0);

	if (!cc->transform)
		{
		DEBUG_1("failed to create color profile transform");

		color_man_cache_unref(cc);
		return nullptr;
		}

	if (cc->profile_in_type != COLOR_PROFILE_MEM && cc->profile_out_type != COLOR_PROFILE_MEM )
		{
		cm_cache_list = g_list_append(cm_cache_list, cc);
		color_man_cache_ref(cc);
		}

	return cc;
}

void color_man_cache_reset()
{
	g_list_free_full(cm_cache_list, reinterpret_cast<GDestroyNotify>(color_man_cache_unref));
}

ColorManCache *color_man_cache_find(ColorManProfileType in_type, const gchar *in_file,
                                    ColorManProfileType out_type, const gchar *out_file,
                                    gboolean has_alpha)
{
	GList *work;

	work = cm_cache_list;
	while (work)
		{
		ColorManCache *cc;
		gboolean match = FALSE;

		cc = static_cast<ColorManCache *>(work->data);
		work = work->next;

		if (cc->profile_in_type == in_type &&
		    cc->profile_out_type == out_type &&
		    cc->has_alpha == has_alpha)
			{
			match = TRUE;
			}

		if (match && cc->profile_in_type == COLOR_PROFILE_FILE)
			{
			match = (cc->profile_in_file && in_file &&
				 strcmp(cc->profile_in_file, in_file) == 0);
			}
		if (match && cc->profile_out_type == COLOR_PROFILE_FILE)
			{
			match = (cc->profile_out_file && out_file &&
				 strcmp(cc->profile_out_file, out_file) == 0);
			}

		if (match) return cc;
		}

	return nullptr;
}

ColorManCache *color_man_cache_get(ColorManProfileType in_type, const gchar *in_file,
                                   const guchar *in_data, guint in_data_len,
                                   ColorManProfileType out_type, const gchar *out_file,
                                   const guchar *out_data, guint out_data_len,
                                   gboolean has_alpha)
{
	ColorManCache *cc;

	cc = color_man_cache_find(in_type, in_file, out_type, out_file, has_alpha);
	if (cc)
		{
		color_man_cache_ref(cc);
		return cc;
		}

	return color_man_cache_new(in_type, in_file, in_data, in_data_len,
				   out_type, out_file, out_data, out_data_len, has_alpha);
}

} // namespace


/*
 *-------------------------------------------------------------------
 * color manager
 *-------------------------------------------------------------------
 */

void color_man_correct_region(ColorMan *cm, GdkPixbuf *pixbuf, gint x, gint y, gint w, gint h)
{
	ColorManCache *cc;
	guchar *pix;
	gint rs;
	gint i;
	gint pixbuf_width;
	gint pixbuf_height;


	pixbuf_width = gdk_pixbuf_get_width(pixbuf);
	pixbuf_height = gdk_pixbuf_get_height(pixbuf);

	cc = static_cast<ColorManCache *>(cm->profile);

	pix = gdk_pixbuf_get_pixels(pixbuf);
	rs = gdk_pixbuf_get_rowstride(pixbuf);

	/** @FIXME: x,y expected to be = 0. Maybe this is not the right place for scaling */
	w = w * scale_factor();
	h = h * scale_factor();

	w = std::min(w, pixbuf_width - x);
	h = std::min(h, pixbuf_height - y);

	pix += x * ((cc->has_alpha) ? 4 : 3);
	for (i = 0; i < h; i++)
		{
		guchar *pbuf;

		pbuf = pix + ((y + i) * rs);

		cmsDoTransform(cc->transform, pbuf, pbuf, w);
		}

}

static ColorMan *color_man_new_real(ImageWindow *imd, GdkPixbuf *pixbuf,
                                    ColorManProfileType input_type, const gchar *input_file,
                                    const guchar *input_data, guint input_data_len,
                                    ColorManProfileType screen_type, const gchar *screen_file,
                                    const guchar *screen_data, guint screen_data_len)
{
	if (imd) pixbuf = image_get_pixbuf(imd);

	ColorManCache *profile = color_man_cache_get(input_type, input_file, input_data, input_data_len,
	                                             screen_type, screen_file, screen_data, screen_data_len,
	                                             pixbuf ? gdk_pixbuf_get_has_alpha(pixbuf) : FALSE);
	if (!profile) return nullptr;

	auto *cm = g_new0(ColorMan, 1);
	cm->imd = imd;
	cm->pixbuf = pixbuf ? g_object_ref(pixbuf) : nullptr;
	cm->profile = profile;

	return cm;
}

ColorMan *color_man_new(ImageWindow *imd, GdkPixbuf *pixbuf,
                        ColorManProfileType input_type, const gchar *input_file,
                        ColorManProfileType screen_type, const gchar *screen_file,
                        const guchar *screen_data, guint screen_data_len)
{
	return color_man_new_real(imd, pixbuf,
				  input_type, input_file, nullptr, 0,
				  screen_type, screen_file, screen_data, screen_data_len);
}

ColorMan *color_man_new_embedded(ImageWindow *imd, GdkPixbuf *pixbuf,
                                 const guchar *input_data, guint input_data_len,
                                 ColorManProfileType screen_type, const gchar *screen_file,
                                 const guchar *screen_data, guint screen_data_len)
{
	return color_man_new_real(imd, pixbuf,
				  COLOR_PROFILE_MEM, nullptr, input_data, input_data_len,
				  screen_type, screen_file, screen_data, screen_data_len);
}

static std::string color_man_get_profile_name(ColorManProfileType type, cmsHPROFILE profile)
{
	switch (type)
		{
		case COLOR_PROFILE_SRGB:
			return _("sRGB");
		case COLOR_PROFILE_ADOBERGB:
			return _("Adobe RGB compatible");
		case COLOR_PROFILE_MEM:
		case COLOR_PROFILE_FILE:
			if (profile)
				{
				char buffer[20];
				buffer[0] = '\0';
				cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", buffer, 20);
				buffer[19] = '\0'; /* Just to be sure */
				return buffer;
				}
			return _("Custom profile");
		case COLOR_PROFILE_NONE:
		default:
			return "";
		}
}

std::optional<ColorManStatus> color_man_get_status(const ColorMan *cm)
{
	if (!cm) return {};

	auto *cc = static_cast<ColorManCache *>(cm->profile);

	ColorManStatus status = {
	    color_man_get_profile_name(cc->profile_in_type, cc->profile_in),
	    color_man_get_profile_name(cc->profile_out_type, cc->profile_out)
	};
	return status;
}

void color_man_free(ColorMan *cm)
{
	if (!cm) return;

	if (cm->idle_id) g_source_remove(cm->idle_id);
	if (cm->pixbuf) g_object_unref(cm->pixbuf);

	color_man_cache_unref(static_cast<ColorManCache *>(cm->profile));

	g_free(cm);
}

void color_man_update()
{
	color_man_cache_reset();
}

const gchar *get_profile_name(const guchar *profile_data, guint profile_len)
{
	cmsHPROFILE profile = cmsOpenProfileFromMem(profile_data, profile_len);
	if (!profile) return nullptr;

	static cmsUInt8Number profileID[17];
	memset(profileID, 0, sizeof(profileID));

	cmsGetHeaderProfileID(profile, profileID);
	cmsCloseProfile(profile);

	return reinterpret_cast<gchar *>(profileID);
}

#else /* define HAVE_LCMS */
/*** color support not enabled ***/


ColorMan *color_man_new(ImageWindow *, GdkPixbuf *,
                        ColorManProfileType, const gchar *,
                        ColorManProfileType, const gchar *,
                        const guchar *, guint)
{
	/* no op */
	return nullptr;
}

ColorMan *color_man_new_embedded(ImageWindow *, GdkPixbuf *,
                                 const guchar *, guint,
                                 ColorManProfileType, const gchar *,
                                 const guchar *, guint)
{
	/* no op */
	return nullptr;
}

void color_man_free(ColorMan *)
{
	/* no op */
}

void color_man_update()
{
	/* no op */
}

void color_man_correct_region(ColorMan *, GdkPixbuf *, gint, gint, gint, gint)
{
	/* no op */
}

std::optional<ColorManStatus> color_man_get_status(const ColorMan *cm)
{
	return {};
}

const gchar *get_profile_name(const guchar *, guint)
{
	/* no op */
	return nullptr;
}

#endif /* define HAVE_LCMS */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
