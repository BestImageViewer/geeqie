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

#include "main.h"
#include "color-man.h"

#include "image.h"
#include "ui-fileops.h"


#ifdef HAVE_LCMS
/*** color support enabled ***/

#ifdef HAVE_LCMS2
#include <lcms2.h>
#else
#include <lcms.h>
#endif


typedef struct _ColorManCache ColorManCache;
struct _ColorManCache {
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

/* pixels to transform per idle call */
#define COLOR_MAN_CHUNK_SIZE 81900


static void color_man_lib_init(void)
{
	static gboolean init_done = FALSE;

	if (init_done) return;
	init_done = TRUE;

#ifndef HAVE_LCMS2
	cmsErrorAction(LCMS_ERROR_IGNORE);
#endif
}

static cmsHPROFILE color_man_create_adobe_comp(void)
{
	/* ClayRGB1998 is AdobeRGB compatible */
#include "ClayRGB1998_icc.h"
	return cmsOpenProfileFromMem(ClayRGB1998_icc, ClayRGB1998_icc_len);
}

/*
 *-------------------------------------------------------------------
 * color transform cache
 *-------------------------------------------------------------------
 */

static GList *cm_cache_list = NULL;


static void color_man_cache_ref(ColorManCache *cc)
{
	if (!cc) return;

	cc->refcount++;
}

static void color_man_cache_unref(ColorManCache *cc)
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

static cmsHPROFILE color_man_cache_load_profile(ColorManProfileType type, const gchar *file,
						guchar *data, guint data_len)
{
	cmsHPROFILE profile = NULL;

	switch (type)
		{
		case COLOR_PROFILE_FILE:
			if (file)
				{
				gchar *pathl;

				pathl = path_from_utf8(file);
				profile = cmsOpenProfileFromFile(pathl, "r");
				g_free(pathl);
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

static ColorManCache *color_man_cache_new(ColorManProfileType in_type, const gchar *in_file,
					  guchar *in_data, guint in_data_len,
					  ColorManProfileType out_type, const gchar *out_file,
					  guchar *out_data, guint out_data_len,
					  gboolean has_alpha)
{
	ColorManCache *cc;

	color_man_lib_init();

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
		return NULL;
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
		return NULL;
		}

	if (cc->profile_in_type != COLOR_PROFILE_MEM && cc->profile_out_type != COLOR_PROFILE_MEM )
		{
		cm_cache_list = g_list_append(cm_cache_list, cc);
		color_man_cache_ref(cc);
		}

	return cc;
}

static void color_man_cache_free(ColorManCache *cc)
{
	if (!cc) return;

	cm_cache_list = g_list_remove(cm_cache_list, cc);
	color_man_cache_unref(cc);
}

static void color_man_cache_reset(void)
{
	while (cm_cache_list)
		{
		ColorManCache *cc;

		cc = static_cast<ColorManCache *>(cm_cache_list->data);
		color_man_cache_free(cc);
		}
}

static ColorManCache *color_man_cache_find(ColorManProfileType in_type, const gchar *in_file,
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

	return NULL;
}

static ColorManCache *color_man_cache_get(ColorManProfileType in_type, const gchar *in_file,
					  guchar *in_data, guint in_data_len,
					  ColorManProfileType out_type, const gchar *out_file,
					  guchar *out_data, guint out_data_len,
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


/*
 *-------------------------------------------------------------------
 * color manager
 *-------------------------------------------------------------------
 */

//static void color_man_done(ColorMan *cm, ColorManReturnType type)
//{
	//if (cm->func_done)
		//{
		//cm->func_done(cm, type, cm->func_done_data);
		//}
//}

void color_man_correct_region(ColorMan *cm, GdkPixbuf *pixbuf, gint x, gint y, gint w, gint h)
{
	ColorManCache *cc;
	guchar *pix;
	gint rs;
	gint i;
	gint pixbuf_width, pixbuf_height;


	pixbuf_width = gdk_pixbuf_get_width(pixbuf);
	pixbuf_height = gdk_pixbuf_get_height(pixbuf);

	cc = static_cast<ColorManCache *>(cm->profile);

	pix = gdk_pixbuf_get_pixels(pixbuf);
	rs = gdk_pixbuf_get_rowstride(pixbuf);

	/** @FIXME: x,y expected to be = 0. Maybe this is not the right place for scaling */
	w = w * scale_factor();
	h = h * scale_factor();

	w = MIN(w, pixbuf_width - x);
	h = MIN(h, pixbuf_height - y);

	pix += x * ((cc->has_alpha) ? 4 : 3);
	for (i = 0; i < h; i++)
		{
		guchar *pbuf;

		pbuf = pix + ((y + i) * rs);

		cmsDoTransform(cc->transform, pbuf, pbuf, w);
		}

}

//static gboolean color_man_idle_cb(gpointer data)
//{
	//ColorMan *cm = static_cast<ColorMan *>(data);
	//gint width, height;
	//gint rh;

	//if (!cm->pixbuf) return FALSE;

	//if (cm->imd &&
	    //cm->pixbuf != image_get_pixbuf(cm->imd))
		//{
		//cm->idle_id = 0;
		//color_man_done(cm, COLOR_RETURN_IMAGE_CHANGED);
		//return FALSE;
		//}

	//width = gdk_pixbuf_get_width(cm->pixbuf);
	//height = gdk_pixbuf_get_height(cm->pixbuf);

	//if (cm->row > height)
		//{
		//if (!cm->incremental_sync && cm->imd)
			//{
			//image_area_changed(cm->imd, 0, 0, width, height);
			//}

		//cm->idle_id = 0;
		//color_man_done(cm, COLOR_RETURN_SUCCESS);
		//return FALSE;
		//}

	//rh = COLOR_MAN_CHUNK_SIZE / width + 1;
	//color_man_correct_region(cm, cm->pixbuf, 0, cm->row, width, rh);
	//if (cm->incremental_sync && cm->imd) image_area_changed(cm->imd, 0, cm->row, width, rh);
	//cm->row += rh;

	//return TRUE;
//}

static ColorMan *color_man_new_real(ImageWindow *imd, GdkPixbuf *pixbuf,
				    ColorManProfileType input_type, const gchar *input_file,
				    guchar *input_data, guint input_data_len,
				    ColorManProfileType screen_type, const gchar *screen_file,
				    guchar *screen_data, guint screen_data_len)
{
	ColorMan *cm;
	gboolean has_alpha;

	if (imd) pixbuf = image_get_pixbuf(imd);

	cm = g_new0(ColorMan, 1);
	cm->imd = imd;
	cm->pixbuf = pixbuf;
	if (cm->pixbuf) g_object_ref(cm->pixbuf);

	has_alpha = pixbuf ? gdk_pixbuf_get_has_alpha(pixbuf) : FALSE;

	cm->profile = color_man_cache_get(input_type, input_file, input_data, input_data_len,
					  screen_type, screen_file, screen_data, screen_data_len, has_alpha);
	if (!cm->profile)
		{
		color_man_free(cm);
		return NULL;
		}

	return cm;
}

ColorMan *color_man_new(ImageWindow *imd, GdkPixbuf *pixbuf,
			ColorManProfileType input_type, const gchar *input_file,
			ColorManProfileType screen_type, const gchar *screen_file,
			guchar *screen_data, guint screen_data_len)
{
	return color_man_new_real(imd, pixbuf,
				  input_type, input_file, NULL, 0,
				  screen_type, screen_file, screen_data, screen_data_len);
}

//void color_man_start_bg(ColorMan *cm, ColorManDoneFunc done_func, gpointer done_data)
//{
	//cm->func_done = done_func;
	//cm->func_done_data = done_data;
	//cm->idle_id = g_idle_add(color_man_idle_cb, cm);
//}

ColorMan *color_man_new_embedded(ImageWindow *imd, GdkPixbuf *pixbuf,
				 guchar *input_data, guint input_data_len,
				 ColorManProfileType screen_type, const gchar *screen_file,
				 guchar *screen_data, guint screen_data_len)
{
	return color_man_new_real(imd, pixbuf,
				  COLOR_PROFILE_MEM, NULL, input_data, input_data_len,
				  screen_type, screen_file, screen_data, screen_data_len);
}

static gchar *color_man_get_profile_name(ColorManProfileType type, cmsHPROFILE profile)
{
	switch (type)
		{
		case COLOR_PROFILE_SRGB:
			return g_strdup(_("sRGB"));
		case COLOR_PROFILE_ADOBERGB:
			return g_strdup(_("Adobe RGB compatible"));
			break;
		case COLOR_PROFILE_MEM:
		case COLOR_PROFILE_FILE:
			if (profile)
				{
#ifdef HAVE_LCMS2
				char buffer[20];
				buffer[0] = '\0';
				cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", buffer, 20);
				buffer[19] = '\0'; /* Just to be sure */
				return g_strdup(buffer);
#else
				return g_strdup(cmsTakeProductName(profile));
#endif
				}
			return g_strdup(_("Custom profile"));
			break;
		case COLOR_PROFILE_NONE:
		default:
			return g_strdup("");
		}
}

gboolean color_man_get_status(ColorMan *cm, gchar **image_profile, gchar **screen_profile)
{
	ColorManCache *cc;
	if (!cm) return FALSE;

	cc = static_cast<ColorManCache *>(cm->profile);

	if (image_profile) *image_profile = color_man_get_profile_name(cc->profile_in_type, cc->profile_in);
	if (screen_profile) *screen_profile = color_man_get_profile_name(cc->profile_out_type, cc->profile_out);
	return TRUE;
}

void color_man_free(ColorMan *cm)
{
	if (!cm) return;

	if (cm->idle_id) g_source_remove(cm->idle_id);
	if (cm->pixbuf) g_object_unref(cm->pixbuf);

	color_man_cache_unref(static_cast<ColorManCache *>(cm->profile));

	g_free(cm);
}

void color_man_update(void)
{
	color_man_cache_reset();
}

#ifdef HAVE_HEIF
#include <libheif/heif.h>
#include <math.h>

static cmsToneCurve* colorspaces_create_transfer(int32_t size, double (*fct)(double))
{
	float *values = static_cast<float *>(g_malloc(sizeof(float) * size));

	for(int32_t i = 0; i < size; ++i)
		{
		const double x = (float)i / (size - 1);
		const double y = MIN(fct(x), 1.0f);
		values[i] = (float)y;
		}

	cmsToneCurve* result = cmsBuildTabulatedToneCurveFloat(NULL, size, values);
	g_free(values);
	return result;
}

// https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-F.pdf
// Hybrid Log-Gamma
static double HLG_fct(double x)
{
	static const double Beta  = 0.04;
	static const double RA    = 5.591816309728916; // 1.0 / A where A = 0.17883277
	static const double B     = 0.28466892; // 1.0 - 4.0 * A
	static const double C     = 0.5599107295; // 0,5 –aln(4a)

	double e = MAX(x * (1.0 - Beta) + Beta, 0.0);

	if(e == 0.0) return 0.0;

	const double sign = e;
	e = fabs(e);

	double res = 0.0;

	if(e <= 0.5)
		{
		res = e * e / 3.0;
		}
	else
		{
		res = (exp((e - C) * RA) + B) / 12.0;
		}

	return copysign(res, sign);
}

static double PQ_fct(double x)
{
	static const double M1 = 2610.0 / 16384.0;
	static const double M2 = (2523.0 / 4096.0) * 128.0;
	static const double C1 = 3424.0 / 4096.0;
	static const double C2 = (2413.0 / 4096.0) * 32.0;
	static const double C3 = (2392.0 / 4096.0) * 32.0;

	if(x == 0.0) return 0.0;
	const double sign = x;
	x = fabs(x);

	const double xpo = pow(x, 1.0 / M2);
	const double num = MAX(xpo - C1, 0.0);
	const double den = C2 - C3 * xpo;
	const double res = pow(num / den, 1.0 / M1);

	return copysign(res, sign);
}

/**
 * @brief
 * @param nclx
 * @param profile_len
 * @returns
 *
 * Copied from: gimp/libgimpcolor/gimpcolorprofile.c
 */
static guchar *nclx_to_lcms_profile(const struct heif_color_profile_nclx *nclx, guint *profile_len)
{
	const gchar *primaries_name = "";
	const gchar *trc_name = "";
	cmsHPROFILE *profile = NULL;
	cmsCIExyY whitepoint;
	cmsCIExyYTRIPLE primaries;
	cmsToneCurve *curve[3];
	cmsUInt32Number size;
	guint8 *data = NULL;

	cmsFloat64Number srgb_parameters[5] =
	{ 2.4, 1.0 / 1.055,  0.055 / 1.055, 1.0 / 12.92, 0.04045 };

	cmsFloat64Number rec709_parameters[5] =
	{ 2.2, 1.0 / 1.099,  0.099 / 1.099, 1.0 / 4.5, 0.081 };

	if (nclx == NULL)
		{
		return NULL;
		}

	if (nclx->color_primaries == heif_color_primaries_unspecified)
		{
		return NULL;
		}

	whitepoint.x = nclx->color_primary_white_x;
	whitepoint.y = nclx->color_primary_white_y;
	whitepoint.Y = 1.0f;

	primaries.Red.x = nclx->color_primary_red_x;
	primaries.Red.y = nclx->color_primary_red_y;
	primaries.Red.Y = 1.0f;

	primaries.Green.x = nclx->color_primary_green_x;
	primaries.Green.y = nclx->color_primary_green_y;
	primaries.Green.Y = 1.0f;

	primaries.Blue.x = nclx->color_primary_blue_x;
	primaries.Blue.y = nclx->color_primary_blue_y;
	primaries.Blue.Y = 1.0f;

	switch (nclx->color_primaries)
		{
		case heif_color_primaries_ITU_R_BT_709_5:
			primaries_name = "BT.709";
			break;
		case   heif_color_primaries_ITU_R_BT_470_6_System_M:
			primaries_name = "BT.470-6 System M";
			break;
		case heif_color_primaries_ITU_R_BT_470_6_System_B_G:
			primaries_name = "BT.470-6 System BG";
			break;
		case heif_color_primaries_ITU_R_BT_601_6:
			primaries_name = "BT.601";
			break;
		case heif_color_primaries_SMPTE_240M:
			primaries_name = "SMPTE 240M";
			break;
		case heif_color_primaries_generic_film:
			primaries_name = "Generic film";
			break;
		case heif_color_primaries_ITU_R_BT_2020_2_and_2100_0:
			primaries_name = "BT.2020";
			break;
		case heif_color_primaries_SMPTE_ST_428_1:
			primaries_name = "SMPTE ST 428-1";
			break;
		case heif_color_primaries_SMPTE_RP_431_2:
			primaries_name = "SMPTE RP 431-2";
			break;
		case heif_color_primaries_SMPTE_EG_432_1:
			primaries_name = "SMPTE EG 432-1 (DCI P3)";
			break;
		case heif_color_primaries_EBU_Tech_3213_E:
			primaries_name = "EBU Tech. 3213-E";
			break;
		default:
			log_printf("nclx unsupported color_primaries value: %d\n", nclx->color_primaries);
			return NULL;
			break;
		}

	DEBUG_1("nclx primaries: %s: ", primaries_name);

	switch (nclx->transfer_characteristics)
		{
		case heif_transfer_characteristic_ITU_R_BT_709_5:
			curve[0] = curve[1] = curve[2] = cmsBuildParametricToneCurve(NULL, 4, rec709_parameters);
			profile = static_cast<cmsHPROFILE *>(cmsCreateRGBProfile(&whitepoint, &primaries, curve));
			cmsFreeToneCurve(curve[0]);
			trc_name = "Rec709 RGB";
			break;
		case heif_transfer_characteristic_ITU_R_BT_470_6_System_M:
			curve[0] = curve[1] = curve[2] = cmsBuildGamma (NULL, 2.2f);
			profile = static_cast<cmsHPROFILE *>(cmsCreateRGBProfile(&whitepoint, &primaries, curve));
			cmsFreeToneCurve(curve[0]);
			trc_name = "Gamma2.2 RGB";
			break;
		case heif_transfer_characteristic_ITU_R_BT_470_6_System_B_G:
			curve[0] = curve[1] = curve[2] = cmsBuildGamma (NULL, 2.8f);
			profile = static_cast<cmsHPROFILE *>(cmsCreateRGBProfile(&whitepoint, &primaries, curve));
			cmsFreeToneCurve(curve[0]);
			trc_name = "Gamma2.8 RGB";
			break;
		case heif_transfer_characteristic_linear:
			curve[0] = curve[1] = curve[2] = cmsBuildGamma (NULL, 1.0f);
			profile = static_cast<cmsHPROFILE *>(cmsCreateRGBProfile(&whitepoint, &primaries, curve));
			cmsFreeToneCurve(curve[0]);
			trc_name = "linear RGB";
			break;
		case heif_transfer_characteristic_ITU_R_BT_2100_0_HLG:
			curve[0] = curve[1] = curve[2] = colorspaces_create_transfer(4096, HLG_fct);
			profile = static_cast<cmsHPROFILE *>(cmsCreateRGBProfile(&whitepoint, &primaries, curve));
			cmsFreeToneCurve(curve[0]);
			trc_name = "HLG Rec2020 RGB";
			break;
		case heif_transfer_characteristic_ITU_R_BT_2100_0_PQ:
			curve[0] = curve[1] = curve[2] = colorspaces_create_transfer(4096, PQ_fct);
			profile = static_cast<cmsHPROFILE *>(cmsCreateRGBProfile(&whitepoint, &primaries, curve));
			cmsFreeToneCurve(curve[0]);
			trc_name = "PQ Rec2020 RGB";
			break;
		case heif_transfer_characteristic_IEC_61966_2_1:
		/* same as default */
		default:
			curve[0] = curve[1] = curve[2] = cmsBuildParametricToneCurve(NULL, 4, srgb_parameters);
			profile = static_cast<cmsHPROFILE *>(cmsCreateRGBProfile(&whitepoint, &primaries, curve));
			cmsFreeToneCurve(curve[0]);
			trc_name = "sRGB-TRC RGB";
			break;
		}

	DEBUG_1("nclx transfer characteristic: %s", trc_name);

	if (profile)
		{
		if (cmsSaveProfileToMem(profile, NULL, &size))
			{
			data = static_cast<guint8 *>(g_malloc(size));
			if (cmsSaveProfileToMem(profile, data, &size))
				{
				*profile_len = size;
				}
			cmsCloseProfile(profile);
			return (guchar *)data;
			}
		else
			{
			cmsCloseProfile(profile);
			return NULL;
			}
		}
	else
		{
		return NULL;
		}
}

guchar *heif_color_profile(FileData *fd, guint *profile_len)
{
	struct heif_context* ctx;
	struct heif_error error_code;
	struct heif_image_handle* handle;
	struct heif_color_profile_nclx *nclxcp;
	gint profile_type;
	guchar *profile;
	cmsUInt32Number size;
	guint8 *data = NULL;

	ctx = heif_context_alloc();
	error_code = heif_context_read_from_file(ctx, fd->path, NULL);

	if (error_code.code)
		{
		log_printf("warning: heif reader error: %s\n", error_code.message);
		heif_context_free(ctx);
		return NULL;
		}

	error_code = heif_context_get_primary_image_handle(ctx, &handle);
	if (error_code.code)
		{
		log_printf("warning: heif reader error: %s\n", error_code.message);
		heif_context_free(ctx);
		return NULL;
		}

	nclxcp = heif_nclx_color_profile_alloc();
	profile_type = heif_image_handle_get_color_profile_type(handle);

	if (profile_type == heif_color_profile_type_prof)
		{
		size = heif_image_handle_get_raw_color_profile_size(handle);
		*profile_len = size;
		data = static_cast<guint8 *>(g_malloc0(size));
		error_code = heif_image_handle_get_raw_color_profile(handle, data);
		if (error_code.code)
			{
			log_printf("warning: heif reader error: %s\n", error_code.message);
			heif_context_free(ctx);
			heif_nclx_color_profile_free(nclxcp);
			return NULL;
			}

		DEBUG_1("heif color profile type: prof");
		heif_context_free(ctx);
		heif_nclx_color_profile_free(nclxcp);

		return (guchar *)data;
		}
	else
		{
		error_code = heif_image_handle_get_nclx_color_profile(handle, &nclxcp);
		if (error_code.code)
			{
			log_printf("warning: heif reader error: %s\n", error_code.message);
			heif_context_free(ctx);
			heif_nclx_color_profile_free(nclxcp);
			return NULL;
			}

		profile = nclx_to_lcms_profile(nclxcp, profile_len);
		}

	heif_context_free(ctx);
	heif_nclx_color_profile_free(nclxcp);

	return (guchar *)profile;
}
#else
guchar *heif_color_profile(FileData *UNUSED(fd), guint *UNUSED(profile_len))
{
	return NULL;
}
#endif

#else /* define HAVE_LCMS */
/*** color support not enabled ***/


ColorMan *color_man_new(ImageWindow *UNUSED(imd), GdkPixbuf *UNUSED(pixbuf),
			ColorManProfileType UNUSED(input_type), const gchar *UNUSED(input_file),
			ColorManProfileType UNUSED(screen_type), const gchar *UNUSED(screen_file),
			guchar *UNUSED(screen_data), guint UNUSED(screen_data_len))
{
	/* no op */
	return nullptr;
}

ColorMan *color_man_new_embedded(ImageWindow *UNUSED(imd), GdkPixbuf *UNUSED(pixbuf),
				 guchar *UNUSED(input_data), guint UNUSED(input_data_len),
				 ColorManProfileType UNUSED(screen_type), const gchar *UNUSED(screen_file),
				 guchar *UNUSED(screen_data), guint UNUSED(screen_data_len))
{
	/* no op */
	return nullptr;
}

void color_man_free(ColorMan *UNUSED(cm))
{
	/* no op */
}

void color_man_update(void)
{
	/* no op */
}

void color_man_correct_region(ColorMan *UNUSED(cm), GdkPixbuf *UNUSED(pixbuf), gint UNUSED(x), gint UNUSED(y), gint UNUSED(w), gint UNUSED(h))
{
	/* no op */
}

void color_man_start_bg(ColorMan *UNUSED(cm), ColorManDoneFunc UNUSED(done_func), gpointer UNUSED(done_data))
{
	/* no op */
}

gboolean color_man_get_status(ColorMan *UNUSED(cm), gchar **UNUSED(image_profile), gchar **UNUSED(screen_profile))
{
	return FALSE;
}

guchar *heif_color_profile(FileData *UNUSED(fd), guint *UNUSED(profile_len))
{
	return nullptr;
}

#endif /* define HAVE_LCMS */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
