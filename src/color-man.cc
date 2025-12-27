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

struct ColorMan::Cache {
	Cache() = default;
	~Cache();
	Cache(const Cache &) = delete;
	Cache(Cache &&) = delete;
	Cache &operator=(const Cache &) = delete;
	Cache &operator=(Cache &&) = delete;

	cmsHPROFILE   profile_in;
	cmsHPROFILE   profile_out;
	cmsHTRANSFORM transform;

	ColorManProfileType profile_in_type;
	gchar *profile_in_file;

	ColorManProfileType profile_out_type;
	gchar *profile_out_file;

	gboolean has_alpha;
};

ColorMan::Cache::~Cache()
{
	if (transform) cmsDeleteTransform(transform);
	if (profile_in) cmsCloseProfile(profile_in);
	if (profile_out) cmsCloseProfile(profile_out);

	g_free(profile_in_file);
	g_free(profile_out_file);
}

using ColorManCachePtr = std::shared_ptr<ColorMan::Cache>;

namespace
{

#define GQ_RESOURCE_PATH_ICC "/org/geeqie/icc"

G_DEFINE_AUTO_CLEANUP_FREE_FUNC(cmsHPROFILE, cmsCloseProfile, nullptr)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(cmsHTRANSFORM, cmsDeleteTransform, nullptr)

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

std::vector<ColorManCachePtr> cm_cache_list;


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

ColorManCachePtr color_man_cache_new(ColorManProfileType in_type, const gchar *in_file,
                                     const guchar *in_data, guint in_data_len,
                                     ColorManProfileType out_type, const gchar *out_file,
                                     const guchar *out_data, guint out_data_len,
                                     gboolean has_alpha)
{
	g_auto(cmsHPROFILE) profile_in = color_man_cache_load_profile(in_type, in_file, in_data, in_data_len);
	if (!profile_in)
		{
		DEBUG_1("failed to load color profile for input: %d %s", in_type, in_file);
		return nullptr;
		}

	g_auto(cmsHPROFILE) profile_out = color_man_cache_load_profile(out_type, out_file, out_data, out_data_len);
	if (!profile_out)
		{
		DEBUG_1("failed to load color profile for screen: %d %s", out_type, out_file);
		return nullptr;
		}

	const cmsUInt32Number format = has_alpha ? TYPE_RGBA_8 : TYPE_RGB_8;
	g_auto(cmsHTRANSFORM) transform = cmsCreateTransform(profile_in, format,
	                                                     profile_out, format,
	                                                     options->color_profile.render_intent, 0);
	if (!transform)
		{
		DEBUG_1("failed to create color profile transform");
		return nullptr;
		}

	auto cc = std::make_shared<ColorMan::Cache>();

	cc->profile_in = g_steal_pointer(&profile_in);
	cc->profile_out = g_steal_pointer(&profile_out);

	cc->transform = g_steal_pointer(&transform);

	cc->profile_in_type = in_type;
	cc->profile_in_file = g_strdup(in_file);

	cc->profile_out_type = out_type;
	cc->profile_out_file = g_strdup(out_file);

	cc->has_alpha = has_alpha;

	if (cc->profile_in_type != COLOR_PROFILE_MEM && cc->profile_out_type != COLOR_PROFILE_MEM)
		{
		cm_cache_list.push_back(cc);
		}

	return cc;
}

void color_man_cache_reset()
{
	cm_cache_list.clear();
}

ColorManCachePtr color_man_cache_find(ColorManProfileType in_type, const gchar *in_file,
                                      ColorManProfileType out_type, const gchar *out_file,
                                      gboolean has_alpha)
{
	const auto match_cache = [in_type, in_file, out_type, out_file, has_alpha](const ColorManCachePtr &cc)
	{
		bool match = (cc->profile_in_type == in_type &&
		              cc->profile_out_type == out_type &&
		              cc->has_alpha == has_alpha);

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

		return match;
	};
	auto it = std::find_if(cm_cache_list.begin(), cm_cache_list.end(), match_cache);

	return (it != cm_cache_list.end()) ? *it : nullptr;
}

ColorManCachePtr color_man_cache_get(ColorManProfileType in_type, const gchar *in_file,
                                     const guchar *in_data, guint in_data_len,
                                     ColorManProfileType out_type, const gchar *out_file,
                                     const guchar *out_data, guint out_data_len,
                                     gboolean has_alpha)
{
	ColorManCachePtr cc = color_man_cache_find(in_type, in_file, out_type, out_file, has_alpha);
	if (cc) return cc;

	return color_man_cache_new(in_type, in_file, in_data, in_data_len,
				   out_type, out_file, out_data, out_data_len, has_alpha);
}

} // namespace


/*
 *-------------------------------------------------------------------
 * color manager
 *-------------------------------------------------------------------
 */

void color_man_correct_region(const ColorMan *cm, GdkPixbuf *pixbuf, GdkRectangle region)
{
	/** @FIXME: region x,y expected to be = 0. Maybe this is not the right place for scaling */
	const gint scale = scale_factor();
	region.width = std::min(region.width * scale, gdk_pixbuf_get_width(pixbuf) - region.x);
	region.height = std::min(region.height * scale, gdk_pixbuf_get_height(pixbuf) - region.y);

	ColorMan::Cache *cc = cm->profile.get();
	const int step = cc->has_alpha ? 4 : 3;
	guchar *pix = gdk_pixbuf_get_pixels(pixbuf) + (region.x * step);

	const gint rs = gdk_pixbuf_get_rowstride(pixbuf);

	for (int i = 0; i < region.height; i++)
		{
		guchar *pbuf = pix + ((region.y + i) * rs);

		cmsDoTransform(cc->transform, pbuf, pbuf, region.width);
		}
}

static ColorMan *color_man_new_real(ImageWindow *imd, GdkPixbuf *pixbuf,
                                    ColorManProfileType input_type, const gchar *input_file,
                                    const guchar *input_data, guint input_data_len,
                                    ColorManProfileType screen_type, const gchar *screen_file,
                                    const guchar *screen_data, guint screen_data_len)
{
	if (imd)
		{
		pixbuf = image_get_pixbuf(imd);
		}

	ColorManCachePtr profile = color_man_cache_get(input_type, input_file, input_data, input_data_len,
	                                               screen_type, screen_file, screen_data, screen_data_len,
	                                               pixbuf ? gdk_pixbuf_get_has_alpha(pixbuf) : FALSE);
	if (!profile) return nullptr;

	auto *cm = new ColorMan();
	cm->imd = imd;
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

	ColorMan::Cache *cc = cm->profile.get();

	ColorManStatus status = {
	    color_man_get_profile_name(cc->profile_in_type, cc->profile_in),
	    color_man_get_profile_name(cc->profile_out_type, cc->profile_out)
	};
	return status;
}

void color_man_free(ColorMan *cm)
{
	delete cm;
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

void color_man_correct_region(const ColorMan *, GdkPixbuf *, GdkRectangle)
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
