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

#include "color-man-heif.h"

#include <config.h>

#if HAVE_LCMS && HAVE_HEIF
/*** color support enabled ***/

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include <glib-object.h>
#include <lcms2.h>
#include <libheif/heif_cxx.h>

namespace
{

G_DEFINE_AUTOPTR_CLEANUP_FUNC(cmsToneCurve, cmsFreeToneCurve)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(cmsHPROFILE, cmsCloseProfile, nullptr) // NOLINT(readability-non-const-parameter)

G_DEFINE_AUTOPTR_CLEANUP_FUNC(heif_color_profile_nclx, heif_nclx_color_profile_free)

cmsToneCurve *colorspaces_create_transfer(int32_t size, double (*fct)(double))
{
	std::vector<float> values;
	values.reserve(size);
	for(int32_t i = 0; i < size; ++i)
		{
		const double x = static_cast<float>(i) / (size - 1);
		const double y = std::min(fct(x), 1.0);
		values.push_back(static_cast<float>(y));
		}

	return cmsBuildTabulatedToneCurveFloat(nullptr, size, values.data());
}

// https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2100-2-201807-I!!PDF-F.pdf
// Hybrid Log-Gamma
double HLG_fct(double x)
{
	static const double Beta  = 0.04;
	static const double RA    = 5.591816309728916; // 1.0 / A where A = 0.17883277
	static const double B     = 0.28466892; // 1.0 - 4.0 * A
	static const double C     = 0.5599107295; // 0.5 - A * ln(4.0 * A)

	double e = std::max((x * (1.0 - Beta)) + Beta, 0.0);

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

double PQ_fct(double x)
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
	const double num = std::max(xpo - C1, 0.0);
	const double den = C2 - (C3 * xpo);
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
guchar *nclx_to_lcms_profile(const heif_color_profile_nclx *nclx, guint &profile_len)
{
	if (!nclx || nclx->color_primaries == heif_color_primaries_unspecified) return nullptr;

	[[maybe_unused]] const gchar *primaries_name = "";
	switch (nclx->color_primaries)
		{
		case heif_color_primaries_ITU_R_BT_709_5:
			primaries_name = "BT.709";
			break;
		case heif_color_primaries_ITU_R_BT_470_6_System_M:
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
			return nullptr;
		}

	DEBUG_1("nclx primaries: %s: ", primaries_name);

	using GetCurve = std::function<cmsToneCurve *()>;
	const auto get_profile = [nclx](const GetCurve &get_curve)
	{
		cmsCIExyY whitepoint;
		whitepoint.x = nclx->color_primary_white_x;
		whitepoint.y = nclx->color_primary_white_y;
		whitepoint.Y = 1.0F;

		cmsCIExyYTRIPLE primaries;
		primaries.Red.x = nclx->color_primary_red_x;
		primaries.Red.y = nclx->color_primary_red_y;
		primaries.Red.Y = 1.0F;

		primaries.Green.x = nclx->color_primary_green_x;
		primaries.Green.y = nclx->color_primary_green_y;
		primaries.Green.Y = 1.0F;

		primaries.Blue.x = nclx->color_primary_blue_x;
		primaries.Blue.y = nclx->color_primary_blue_y;
		primaries.Blue.Y = 1.0F;

		g_autoptr(cmsToneCurve) curve = get_curve();
		cmsToneCurve *transfer_function[3] = {curve, curve, curve};

		return cmsCreateRGBProfile(&whitepoint, &primaries, transfer_function);
	};

	g_auto(cmsHPROFILE) profile = nullptr;
	[[maybe_unused]] const gchar *trc_name = "";
	switch (nclx->transfer_characteristics)
		{
		case heif_transfer_characteristic_ITU_R_BT_709_5:
			profile = get_profile([]()
			{
				cmsFloat64Number rec709_parameters[5] =
				{ 2.2, 1.0 / 1.099,  0.099 / 1.099, 1.0 / 4.5, 0.081 };

				return cmsBuildParametricToneCurve(nullptr, 4, rec709_parameters);
			});
			trc_name = "Rec709 RGB";
			break;
		case heif_transfer_characteristic_ITU_R_BT_470_6_System_M:
			profile = get_profile([](){ return cmsBuildGamma(nullptr, 2.2F); });
			trc_name = "Gamma2.2 RGB";
			break;
		case heif_transfer_characteristic_ITU_R_BT_470_6_System_B_G:
			profile = get_profile([](){ return cmsBuildGamma(nullptr, 2.8F); });
			trc_name = "Gamma2.8 RGB";
			break;
		case heif_transfer_characteristic_linear:
			profile = get_profile([](){ return cmsBuildGamma(nullptr, 1.0F); });
			trc_name = "linear RGB";
			break;
		case heif_transfer_characteristic_ITU_R_BT_2100_0_HLG:
			profile = get_profile([](){ return colorspaces_create_transfer(4096, HLG_fct); });
			trc_name = "HLG Rec2020 RGB";
			break;
		case heif_transfer_characteristic_ITU_R_BT_2100_0_PQ:
			profile = get_profile([](){ return colorspaces_create_transfer(4096, PQ_fct); });
			trc_name = "PQ Rec2020 RGB";
			break;
		case heif_transfer_characteristic_IEC_61966_2_1: /* same as default */
		default:
			profile = get_profile([]()
			{
				cmsFloat64Number srgb_parameters[5] =
				{ 2.4, 1.0 / 1.055,  0.055 / 1.055, 1.0 / 12.92, 0.04045 };

				return cmsBuildParametricToneCurve(nullptr, 4, srgb_parameters);
			});
			trc_name = "sRGB-TRC RGB";
			break;
		}

	DEBUG_1("nclx transfer characteristic: %s", trc_name);

	if (!profile) return nullptr;

	cmsUInt32Number size;
	if (!cmsSaveProfileToMem(profile, nullptr, &size)) return nullptr;

	auto *data = static_cast<guchar *>(g_malloc(size));
	if (cmsSaveProfileToMem(profile, data, &size))
		{
		profile_len = size;
		}

	return data;
}

} // namespace

guchar *heif_color_profile(const gchar *path, guint &profile_len)
{
	heif::Context ctx{};

	try
		{
		ctx.read_from_file(path);

		heif::ImageHandle image_handle = ctx.get_primary_image_handle();
		heif_image_handle *handle = image_handle.get_raw_image_handle();

		if (heif_image_handle_get_color_profile_type(handle) == heif_color_profile_type_prof)
			{
			profile_len = heif_image_handle_get_raw_color_profile_size(handle);
			auto *data = static_cast<guint8 *>(g_malloc0(profile_len));

			heif_error error = heif_image_handle_get_raw_color_profile(handle, data);
			if (error.code) throw heif::Error(error);

			DEBUG_1("heif color profile type: prof");

			return static_cast<guchar *>(data);
			}

		g_autoptr(heif_color_profile_nclx) nclx_cp = heif_nclx_color_profile_alloc();

		heif_error error = heif_image_handle_get_nclx_color_profile(handle, &nclx_cp);
		if (error.code) throw heif::Error(error);

		return nclx_to_lcms_profile(nclx_cp, profile_len);
		}
	catch (const heif::Error &error)
		{
		if (error.get_code() != heif_error_Color_profile_does_not_exist)
			{
			log_printf("warning: heif reader error: %d (%s)\n",
			           error.get_code(), error.get_message().c_str());
			}
		}

	return nullptr;
}

#else /* define HAVE_LCMS && HAVE_HEIF */
/*** color support not enabled ***/

guchar *heif_color_profile(const gchar *, guint &)
{
	return nullptr;
}

#endif /* define HAVE_LCMS && HAVE_HEIF */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
