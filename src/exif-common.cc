/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
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

#include "exif.h"

#include <config.h>

#ifdef __linux__
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#endif

#include <sys/stat.h>
#include <sys/types.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <glib.h>

#include "cache.h"
#include "color-man-heif.h"
#include "color-man.h"
#include "filecache.h"
#include "filedata.h"
#include "filefilter.h"
#include "glua.h"
#include "intl.h"
#include "jpeg-parser.h"
#include "main-defines.h"
#include "misc.h"
#include "third-party/zonedetect.h"
#include "ui-fileops.h"

namespace
{

struct ExifFormattedText
{
	const gchar *key;
	const gchar *description;
	gchar *(*build_func)(ExifData *exif);
};

} // namespace

static gdouble exif_rational_to_double(ExifRational *r, gint sign)
{
	if (!r || r->den == 0.0) return 0.0;

	if (sign) return static_cast<gdouble>(static_cast<gint>(r->num)) / static_cast<gdouble>(static_cast<gint>(r->den));
	return static_cast<gdouble>(r->num) / r->den;
}

static gdouble exif_get_rational_as_double(ExifData *exif, const gchar *key)
{
	ExifRational *r;
	gint sign;

	r = exif_get_rational(exif, key, &sign);
	return exif_rational_to_double(r, sign);
}

static gchar *remove_common_prefix(gchar *s, gchar *t)
{
	gint i;

	if (!s || !t) return t;

	for (i = 0; s[i] && t[i] && s[i] == t[i]; i++)
		;
	if (!i)
		return t;
	if (s[i-1] == ' ' || !s[i])
		{
		while (t[i] == ' ')
			i++;
		return t + i;
		}
	return s;
}

static gdouble get_crop_factor(ExifData *exif)
{
	gdouble res_unit_tbl[] = {0.0, 25.4, 25.4, 10.0, 1.0, 0.001 };
	gdouble xres = exif_get_rational_as_double(exif, "Exif.Photo.FocalPlaneXResolution");
	gdouble yres = exif_get_rational_as_double(exif, "Exif.Photo.FocalPlaneYResolution");
	gint res_unit;
	gint w;
	gint h;
	gdouble xsize;
	gdouble ysize;
	gdouble size;
	gdouble ratio;

	if (xres == 0.0 || yres == 0.0) return 0.0;

	if (!exif_get_integer(exif, "Exif.Photo.FocalPlaneResolutionUnit", &res_unit)) return 0.0;
	if (res_unit < 1 || res_unit > 5) return 0.0;

	if (!exif_get_integer(exif, "Exif.Photo.PixelXDimension", &w)) return 0.0;
	if (!exif_get_integer(exif, "Exif.Photo.PixelYDimension", &h)) return 0.0;

	xsize = w * res_unit_tbl[res_unit] / xres;
	ysize = h * res_unit_tbl[res_unit] / yres;

	ratio = xsize / ysize;

	if (ratio < 0.5 || ratio > 2.0) return 0.0; /* reasonable ratio */

	size = hypot(xsize, ysize);

	if (size < 1.0 || size > 100.0) return 0.0; /* reasonable sensor size in mm */

	return hypot(36, 24) / size;
}

static gboolean remove_suffix(gchar *str, const gchar *suffix, gint suffix_len)
{
	gint str_len = strlen(str);

	if (suffix_len < 0) suffix_len = strlen(suffix);
	if (str_len < suffix_len) return FALSE;

	if (strcmp(str + str_len - suffix_len, suffix) != 0) return FALSE;
	str[str_len - suffix_len] = '\0';

	return TRUE;
}

static gchar *exif_build_formatted_Camera(ExifData *exif)
{
	g_autofree gchar *make = exif_get_data_as_text(exif, "Exif.Image.Make");
	g_autofree gchar *model = exif_get_data_as_text(exif, "Exif.Image.Model");
	g_autofree gchar *software = exif_get_data_as_text(exif, "Exif.Image.Software");
	gchar *model2;
	gchar *software2;

	if (make)
		{
		g_strstrip(make);

		if (remove_suffix(make, " CORPORATION", 12)) { /* Nikon */ }
		else if (remove_suffix(make, " Corporation", 12)) { /* Pentax */ }
		else if (remove_suffix(make, " OPTICAL CO.,LTD", 16)) { /* OLYMPUS */ };
		}

	if (model)
		g_strstrip(model);

	if (software)
		{
		gint i;
		gint j;

		g_strstrip(software);

		/* remove superfluous spaces (pentax K100D) */
		for (i = 0, j = 0; software[i]; i++, j++)
			{
			if (software[i] == ' ' && software[i + 1] == ' ')
				i++;
			if (i != j) software[j] = software[i];
			}
		software[j] = '\0';
		}

	model2 = remove_common_prefix(make, model);
	software2 = remove_common_prefix(model2, software);

	return g_strdup_printf("%s%s%s%s%s%s",
	                       make ? make : "",
	                       (make && model2) ? " " : "",
	                       model2 ? model2 : "",
	                       (software2 && (make || model2)) ? " (" : "",
	                       software2 ? software2 : "",
	                       (software2 && (make || model2)) ? ")" : "");
}

static gchar *exif_build_formatted_DateTime(ExifData *exif, const gchar *text_key, const gchar *subsec_key)
{
	g_autofree gchar *subsec = nullptr;

	gchar *text = exif_get_data_as_text(exif, text_key);
	if (text)
		{
		subsec = exif_get_data_as_text(exif, subsec_key);
		}
	else
		{
		text = exif_get_data_as_text(exif, "Exif.Image.DateTime");
		if (!text) return nullptr;

		subsec = exif_get_data_as_text(exif, "Exif.Photo.SubSecTime");
		}

	/* Convert the stuff into a tm struct */
	std::tm tm{}; /* Uh, strptime could let garbage in tm! */
	if (strptime(text, "%Y:%m:%d %H:%M:%S", &tm))
		{
		gchar buf[128];
		const gint buflen = strftime(buf, sizeof(buf), "%x %X", &tm);
		if (buflen > 0)
			{
			g_autoptr(GError) error = nullptr;
			g_autofree gchar *tmp = g_locale_to_utf8(buf, buflen, nullptr, nullptr, &error);
			if (error)
				{
				log_printf("Error converting locale strftime to UTF-8: %s\n", error->message);
				}
			else
				{
				std::swap(text, tmp);
				}
			}
		}

	if (subsec)
		{
		g_autofree gchar *tmp = text;
		text = g_strconcat(tmp, ".", subsec, NULL);
		}

	return text;
}

static gchar *exif_build_formatted_DateTime(ExifData *exif)
{
	return exif_build_formatted_DateTime(exif, "Exif.Photo.DateTimeOriginal", "Exif.Photo.SubSecTimeOriginal");
}

static gchar *exif_build_formatted_DateTimeDigitized(ExifData *exif)
{
	return exif_build_formatted_DateTime(exif, "Exif.Photo.DateTimeDigitized", "Exif.Photo.SubSecTimeDigitized");
}

static gchar *exif_build_formatted_ShutterSpeed(ExifData *exif)
{
	ExifRational *r;

	r = exif_get_rational(exif, "Exif.Photo.ExposureTime", nullptr);
	if (r && r->num && r->den)
		{
		gdouble n = static_cast<gdouble>(r->den) / static_cast<gdouble>(r->num);
		return g_strdup_printf("%s%.0fs", n > 1.0 ? "1/" : "",
						  n > 1.0 ? n : 1.0 / n);
		}
	r = exif_get_rational(exif, "Exif.Photo.ShutterSpeedValue", nullptr);
	if (r && r->num  && r->den)
		{
		gdouble n = pow(2.0, exif_rational_to_double(r, TRUE));

		/* Correct exposure time to avoid values like 1/91s (seen on Minolta DImage 7) */
		if (n > 1.0 && static_cast<gint>(n) - (static_cast<gint>(n/10))*10 == 1) n--;

		return g_strdup_printf("%s%.0fs", n > 1.0 ? "1/" : "",
						  n > 1.0 ? floor(n) : 1.0 / n);
		}
	return nullptr;
}

static gchar *exif_build_formatted_Aperture(ExifData *exif)
{
	gdouble n;

	n = exif_get_rational_as_double(exif, "Exif.Photo.FNumber");
	if (n == 0.0) n = exif_get_rational_as_double(exif, "Exif.Photo.ApertureValue");
	if (n == 0.0) return nullptr;

	return g_strdup_printf("f/%.1f", n);
}

static gchar *exif_build_formatted_ExposureBias(ExifData *exif)
{
	ExifRational *r;
	gint sign;
	gdouble n;

	r = exif_get_rational(exif, "Exif.Photo.ExposureBiasValue", &sign);
	if (!r) return nullptr;

	n = exif_rational_to_double(r, sign);
	return g_strdup_printf("%+.1f", n);
}

static gchar *exif_build_formatted_FocalLength(ExifData *exif)
{
	gdouble n;

	n = exif_get_rational_as_double(exif, "Exif.Photo.FocalLength");
	if (n == 0.0) return nullptr;
	return g_strdup_printf("%.0f mm", n);
}

static gchar *exif_build_formatted_FocalLength35mmFilm(ExifData *exif)
{
	gint n;
	gdouble f;
	gdouble c;

	if (exif_get_integer(exif, "Exif.Photo.FocalLengthIn35mmFilm", &n) && n != 0)
		{
		return g_strdup_printf("%d mm", n);
		}

	f = exif_get_rational_as_double(exif, "Exif.Photo.FocalLength");
	if (f == 0.0) return nullptr;

	c = get_crop_factor(exif);
	if (c == 0.0) return nullptr;

	return g_strdup_printf("%.0f mm", f * c);
}

static gchar *exif_build_formatted_ISOSpeedRating(ExifData *exif)
{
	gchar *text;

	text = exif_get_data_as_text(exif, "Exif.Photo.ISOSpeedRatings");
	/* old canon may set this instead */
	if (!text) text = exif_get_data_as_text(exif, "Exif.CanonSi.ISOSpeed");
	/* kodak may set this instead */
	if (!text) text = exif_get_data_as_text(exif, "Exif.Photo.ExposureIndex");
	return text;
}

static gchar *exif_build_formatted_SubjectDistance(ExifData *exif)
{
	ExifRational *r;
	gint sign;
	gdouble n;

	r = exif_get_rational(exif, "Exif.Photo.SubjectDistance", &sign);
	if (!r) return nullptr;

	if (static_cast<glong>(r->num) == static_cast<glong>(0xffffffff)) return g_strdup(_("infinity"));
	if (static_cast<glong>(r->num) == 0) return g_strdup(_("unknown"));

	n = exif_rational_to_double(r, sign);
	if (n == 0.0) return _("unknown");
	return g_strdup_printf("%.3f m", n);
}

static gchar *exif_build_formatted_Flash(ExifData *exif)
{
	/* grr, flash is a bitmask... */
	GString *string;
	gint n;
	gint v;

	if (!exif_get_integer(exif, "Exif.Photo.Flash", &n)) return nullptr;

	/* Exif 2.1 only defines first 3 bits */
	if (n <= 0x07) return exif_get_data_as_text(exif, "Exif.Photo.Flash");

	/* must be Exif 2.2 */
	string = g_string_new("");

	/* flash fired (bit 0) */
	string = g_string_append(string, (n & 0x01) ? _("yes") : _("no"));

	/* flash mode (bits 3, 4) */
	v = (n >> 3) & 0x03;
	if (v) g_string_append_printf(string, ", %s", _("mode:"));
	switch (v)
		{
		case 1:
			string = g_string_append(string, _("on"));
			break;
		case 2:
			string = g_string_append(string, _("off"));
			break;
		case 3:
			string = g_string_append(string, _("auto"));
			break;
		default:
			break;
		}

	/* return light (bits 1, 2) */
	v = (n >> 1) & 0x03;
	if (v == 2) g_string_append_printf(string, ", %s", _("not detected by strobe"));
	if (v == 3) g_string_append_printf(string, ", %s", _("detected by strobe"));

	/* we ignore flash function (bit 5) */

	/* red-eye (bit 6) */
	if ((n >> 5) & 0x01) g_string_append_printf(string, ", %s", _("red-eye reduction"));

	return g_string_free(string, FALSE);
}

static gchar *exif_build_formatted_Resolution(ExifData *exif)
{
	ExifRational *rx;
	ExifRational *ry;

	rx = exif_get_rational(exif, "Exif.Image.XResolution", nullptr);
	ry = exif_get_rational(exif, "Exif.Image.YResolution", nullptr);
	if (!rx || !ry) return nullptr;

	g_autofree gchar *units = exif_get_data_as_text(exif, "Exif.Image.ResolutionUnit");
	return g_strdup_printf("%0.f x %0.f (%s/%s)",
	                       rx->den ? static_cast<gdouble>(rx->num) / rx->den : 1.0,
	                       ry->den ? static_cast<gdouble>(ry->num) / ry->den : 1.0,
	                       _("dot"),
	                       units ? units : _("unknown"));
}

static gchar *exif_build_formatted_ColorProfile(ExifData *exif)
{
	const gchar *name = "";
	const gchar *source = "";
	guint profile_len;

	g_autofree guchar *profile_data = exif_get_color_profile(exif, &profile_len);
	if (!profile_data)
		{
		gint cs;

		/* ColorSpace == 1 specifies sRGB per EXIF 2.2 */
		if (!exif_get_integer(exif, "Exif.Photo.ColorSpace", &cs)) cs = 0;

		g_autofree gchar *interop_index = exif_get_data_as_text(exif, "Exif.Iop.InteroperabilityIndex");

		if (cs == 1)
			{
			name = _("sRGB");
			source = "ColorSpace";
			}
		else if (cs == 2 || (interop_index && !strcmp(interop_index, "R03")))
			{
			name = _("AdobeRGB");
			source = (cs == 2) ? "ColorSpace" : "Iop";
			}
		}
	else
		{
		const gchar *profile_name = get_profile_name(profile_data, profile_len);
		if (profile_name) name = profile_name;

		source = _("embedded");
		}

	if (name[0] == 0 && source[0] == 0) return nullptr;
	return g_strdup_printf("%s (%s)", name, source);
}

static gchar *exif_build_formatted_GPSPosition(ExifData *exif)
{
	GString *string = g_string_new("");

	const auto build = [exif, string](const gchar *item_key, const gchar *ref_key)
	{
		ExifItem *item = exif_get_item(exif, item_key);
		if (!item) return;

		g_autofree gchar *ref = exif_get_data_as_text(exif, ref_key);
		if (!ref) return;

		gdouble p = 0;
		for (guint i = 0, elements = exif_item_get_elements(item); i < elements; i++)
			{
			ExifRational *value = exif_item_get_rational(item, nullptr, i);
			if (value && value->num && value->den)
				p += static_cast<gdouble>(value->num) / static_cast<gdouble>(value->den) / pow(60.0, static_cast<gdouble>(i));
			}

		const gulong p1 = static_cast<gint>(p);
		const gulong p2 = static_cast<gint>((p - p1)*60);
		const gdouble p3 = ((p - p1)*60 - p2)*60;

		if (string->len > 0)
			g_string_append(string, ", ");

		g_string_append_printf(string, "%0lu° %0lu' %0.2f\" %.1s", p1, p2, p3, ref);
	};

	build("Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef");
	build("Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef");

	return g_string_free(string, FALSE);
}

static gchar *exif_build_formatted_GPSAltitude(ExifData *exif)
{
	ExifRational *r;
	ExifItem *item;
	gdouble alt;
	gint ref;

	item = exif_get_item(exif, "Exif.GPSInfo.GPSAltitudeRef");
	r = exif_get_rational(exif, "Exif.GPSInfo.GPSAltitude", nullptr);

	if (!r || !item) return nullptr;

	alt = exif_rational_to_double(r, 0);
	exif_item_get_integer(item, &ref);

	return g_strdup_printf("%0.f m %s", alt, (ref==0)?_("Above Sea Level"):_("Below Sea Level"));
}

/**
 * @brief Extracts timezone data from a ZoneDetect search structure
 * @param[in] results ZoneDetect search structure
 * @param[out] timezone in the form "Europe/London"
 * @param[out] countryname in the form "United Kingdom"
 * @param[out] countryalpha2 in the form "GB"
 * 
 * Refer to https://github.com/BertoldVdb/ZoneDetect
 * for structure details
 */
static void zd_tz(ZoneDetectResult *results, gchar **timezone, gchar **countryname, gchar **countryalpha2)
{
	g_autofree gchar *timezone_pre = nullptr;
	g_autofree gchar *timezone_id = nullptr;
	unsigned int index = 0;

	while(results[index].lookupResult != ZD_LOOKUP_END)
		{
		if(results[index].data)
			{
			for(unsigned int i=0; i<results[index].numFields; i++)
				{
				if (g_strstr_len(results[index].fieldNames[i], -1, "TimezoneIdPrefix"))
					{
					timezone_pre = g_strdup(results[index].data[i]);
					}
				if (g_strstr_len(results[index].fieldNames[i], -1, "TimezoneId"))
					{
					timezone_id = g_strdup(results[index].data[i]);
					}
				if (g_strstr_len(results[index].fieldNames[i], -1, "CountryName"))
					{
					*countryname = g_strdup(results[index].data[i]);
					}
				if (g_strstr_len(results[index].fieldNames[i], -1, "CountryAlpha2"))
					{
					*countryalpha2 = g_strdup(results[index].data[i]);
					}
				}
			}
		index++;
		}

	*timezone = g_strconcat(timezone_pre, timezone_id, NULL);
}

static void ZoneDetect_onError(int errZD, int errNative)
{
	log_printf("Error: ZoneDetect %s (0x%08X)\n", ZDGetErrorString(errZD), (unsigned)errNative);
}

/**
 * @brief Gets timezone data from an exif structure
 * @param[in] exif
 * @returns TRUE if timezone data found AND GPS date and time found
 * @param[out] exif_date_time exif date/time in the form 2018:11:30:17:05:04
 * @param[out] timezone in the form "Europe/London"
 * @param[out] countryname in the form "United Kingdom"
 * @param[out] countryalpha2 in the form "GB"
 *
 *
 */
static gboolean exif_build_tz_data(ExifData *exif, gchar **exif_date_time, gchar **timezone, gchar **countryname, gchar **countryalpha2)
{
	gfloat latitude;
	gfloat longitude;
	gchar *lat_deg;
	gchar *lat_min;
	gchar *lon_deg;
	gchar *lon_min;
	ZoneDetect *cd;
	ZoneDetectResult *results;
	gboolean ret = FALSE;

	g_autofree gchar *text_latitude = exif_get_data_as_text(exif, "Exif.GPSInfo.GPSLatitude");
	g_autofree gchar *text_longitude = exif_get_data_as_text(exif, "Exif.GPSInfo.GPSLongitude");
	g_autofree gchar *text_latitude_ref = exif_get_data_as_text(exif, "Exif.GPSInfo.GPSLatitudeRef");
	g_autofree gchar *text_longitude_ref = exif_get_data_as_text(exif, "Exif.GPSInfo.GPSLongitudeRef");

	if (text_latitude && text_longitude && text_latitude_ref && text_longitude_ref)
		{
		lat_deg = strtok(text_latitude, "deg'");
		lat_min = strtok(nullptr, "deg'");
		if (!lat_deg || !lat_min)
			{
			return FALSE;
			}
		latitude = atof(lat_deg) + atof(lat_min) / 60;
		if (!g_strcmp0(text_latitude_ref, "South"))
			{
			latitude = -latitude;
			}
		lon_deg = strtok(text_longitude, "deg'");
		lon_min = strtok(nullptr, "deg'");
		if (!lon_deg || !lon_min)
			{
			return FALSE;
			}
		longitude = atof(lon_deg) + atof(lon_min) / 60;
		if (!g_strcmp0(text_longitude_ref, "West"))
			{
			longitude = -longitude;
			}

		g_autofree gchar *timezone_path = g_build_filename(get_rc_dir(), TIMEZONE_DATABASE_FILE, NULL);
		if (g_file_test(timezone_path, G_FILE_TEST_EXISTS))
			{
			ZDSetErrorHandler(ZoneDetect_onError);
			cd = ZDOpenDatabase(timezone_path);
			if (cd)
				{
				results = ZDLookup(cd, latitude, longitude, nullptr);
				if (results)
					{
					zd_tz(results, timezone, countryname, countryalpha2);
					ret = TRUE;
					}
				}
			else
				{
				log_printf("Error: Init of timezone database %s failed\n", timezone_path);
				}
			ZDCloseDatabase(cd);
			}
		}

	if (ret)
		{
		g_autofree gchar *text_date = exif_get_data_as_text(exif, "Exif.GPSInfo.GPSDateStamp");
		g_autofree gchar *text_time = exif_get_data_as_text(exif, "Exif.GPSInfo.GPSTimeStamp");

		if (text_date && text_time)
			{
			*exif_date_time = g_strconcat(text_date, ":", text_time, NULL);
			}
		else
			{
			ret = FALSE;
			}
		}

	return ret;
}

/**
 * @brief Creates local time from GPS lat/long
 * @param[in] exif
 * @returns Localised time and date
 *
 * GPS lat/long is translated to timezone using ZoneDetect.
 * GPS UTC is converted to Unix time stamp (seconds since 1970).
 * The TZ environment variable is set to the relevant timezone
 * and the Unix timestamp converted to local time using locale.
 * If the conversion fails, unformatted UTC is returned.
 */
static gchar *exif_build_formatted_localtime(ExifData *exif)
{
	gchar buf[128];
	gint buflen;
	struct tm *tm_local;
	struct tm tm_utc;
	time_t stamp;
	gchar *exif_date_time = nullptr;
	g_autofree gchar *timezone = nullptr;
	g_autofree gchar *countryname = nullptr;
	g_autofree gchar *countryalpha2 = nullptr;

	if (exif_build_tz_data(exif, &exif_date_time, &timezone, &countryname, &countryalpha2))
		{
		g_autofree gchar *time_zone_image = g_strconcat("TZ=", timezone, NULL);
		g_autofree gchar *time_zone_org = g_strconcat("TZ=", getenv("TZ"), NULL);
		setenv("TZ", "UTC", TRUE);

		memset(&tm_utc, 0, sizeof(tm_utc));
		if (exif_date_time && strptime(exif_date_time, "%Y:%m:%d:%H:%M:%S", &tm_utc))
			{
			stamp = mktime(&tm_utc);	// Convert the struct to a Unix timestamp
			putenv(time_zone_image);	// Switch to destination time zone

			tm_local = localtime(&stamp);

			/* Convert to localtime using locale */
			buflen = strftime(buf, sizeof(buf), "%x %X", tm_local);
			if (buflen > 0)
				{
				g_autoptr(GError) error = nullptr;
				g_autofree gchar *tmp = g_locale_to_utf8(buf, buflen, nullptr, nullptr, &error);
				if (error)
					{
					log_printf("Error converting locale strftime to UTF-8: %s\n", error->message);
					}
				else
					{
					std::swap(exif_date_time, tmp);
					}
				}
			}

		putenv(time_zone_org);
		}

	return exif_date_time;
}

/**
 * @brief Gets timezone from GPS lat/long
 * @param[in] exif
 * @returns Timezone string in the form Europe/London
 *
 *
 */
static gchar *exif_build_formatted_timezone(ExifData *exif)
{
	g_autofree gchar *exif_date_time = nullptr;
	gchar *timezone = nullptr;
	g_autofree gchar *countryname = nullptr;
	g_autofree gchar *countryalpha2 = nullptr;

	exif_build_tz_data(exif, &exif_date_time, &timezone, &countryname, &countryalpha2);

	return timezone;
}

/**
 * @brief Gets countryname from GPS lat/long
 * @param[in] exif
 * @returns Countryname string
 *
 *
 */
static gchar *exif_build_formatted_countryname(ExifData *exif)
{
	g_autofree gchar *exif_date_time = nullptr;
	g_autofree gchar *timezone = nullptr;
	gchar *countryname = nullptr;
	g_autofree gchar *countryalpha2 = nullptr;

	exif_build_tz_data(exif, &exif_date_time, &timezone, &countryname, &countryalpha2);

	return countryname;
}

/**
 * @brief Gets two-letter country code from GPS lat/long
 * @param[in] exif
 * @returns Countryalpha2 string
 *
 *
 */
static gchar *exif_build_formatted_countrycode(ExifData *exif)
{
	g_autofree gchar *exif_date_time = nullptr;
	g_autofree gchar *timezone = nullptr;
	g_autofree gchar *countryname = nullptr;
	gchar *countryalpha2 = nullptr;

	exif_build_tz_data(exif, &exif_date_time, &timezone, &countryname, &countryalpha2);

	return countryalpha2;
}

static gchar *exif_build_formatted_star_rating(ExifData *exif)
{
	gint n = 0;

	exif_get_integer(exif, "Xmp.xmp.Rating", &n);

	return convert_rating_to_stars(n);
}

/* List of custom formatted pseudo-exif tags */
#define EXIF_FORMATTED() "formatted."
#define EXIF_FORMATTED_LEN (sizeof(EXIF_FORMATTED()) - 1)
#define EXIF_FORMATTED_TAG(name, label) { EXIF_FORMATTED()#name, label, exif_build_formatted##_##name }

/**< the list of specially formatted keys, for human readable output */
static const ExifFormattedText ExifFormattedList[] = {
	EXIF_FORMATTED_TAG(Camera,		N_("Camera")),
	EXIF_FORMATTED_TAG(DateTime,		N_("Date")),
	EXIF_FORMATTED_TAG(DateTimeDigitized,	N_("DateDigitized")),
	EXIF_FORMATTED_TAG(ShutterSpeed,	N_("Shutter speed")),
	EXIF_FORMATTED_TAG(Aperture,		N_("Aperture")),
	EXIF_FORMATTED_TAG(ExposureBias,	N_("Exposure bias")),
	EXIF_FORMATTED_TAG(ISOSpeedRating,	N_("ISO sensitivity")),
	EXIF_FORMATTED_TAG(FocalLength,		N_("Focal length")),
	EXIF_FORMATTED_TAG(FocalLength35mmFilm,	N_("Focal length 35mm")),
	EXIF_FORMATTED_TAG(SubjectDistance,	N_("Subject distance")),
	EXIF_FORMATTED_TAG(Flash,		N_("Flash")),
	EXIF_FORMATTED_TAG(Resolution,		N_("Resolution")),
	EXIF_FORMATTED_TAG(ColorProfile,	N_("Color profile")),
	EXIF_FORMATTED_TAG(GPSPosition,		N_("GPS position")),
	EXIF_FORMATTED_TAG(GPSAltitude,		N_("GPS altitude")),
	EXIF_FORMATTED_TAG(localtime,		N_("Local time")),
	EXIF_FORMATTED_TAG(timezone,		N_("Time zone")),
	EXIF_FORMATTED_TAG(countryname,		N_("Country name")),
	EXIF_FORMATTED_TAG(countrycode,		N_("Country code")),
	EXIF_FORMATTED_TAG(star_rating,		N_("Star rating")),
	{"file.size",				N_("File size"), 	nullptr},
	{"file.date",				N_("File date"), 	nullptr},
	{"file.mode",				N_("File mode"), 	nullptr},
	{"file.ctime",				N_("File ctime"), 	nullptr},
	{"file.owner",				N_("File owner"), 	nullptr},
	{"file.group",				N_("File group"), 	nullptr},
	{"file.link",				N_("File link"), 	nullptr},
	{"file.class",				N_("File class"), 	nullptr},
	{"file.page_no",			N_("Page no."), 	nullptr},
	{"lua.lensID",				N_("Lens"), 		nullptr},
};

GHashTable *exif_get_formatted(ExifData *exif)
{
	GHashTable *formatted = g_hash_table_new_full(g_str_hash, g_str_equal, nullptr, g_free);

	for (const auto &eft : ExifFormattedList)
		{
		auto text = exif_get_formatted_by_key(exif, eft.key);
		if (text && text.value())
			{
			g_hash_table_insert(formatted, const_cast<gchar *>(eft.key), text.value());
			}
		}

	return formatted;
}

std::optional<gchar *> exif_get_formatted_by_key(ExifData *exif, const gchar *key)
{
	if (strncmp(key, EXIF_FORMATTED(), EXIF_FORMATTED_LEN) != 0) return {};

	key += EXIF_FORMATTED_LEN;

	for (const auto &eft : ExifFormattedList)
		if (eft.build_func && strcmp(key, eft.key + EXIF_FORMATTED_LEN) == 0)
			return eft.build_func(exif);

	return {};
}

gchar *exif_get_description_by_key(const gchar *key)
{
	if (!key) return nullptr;

	if (strncmp(key, EXIF_FORMATTED(), EXIF_FORMATTED_LEN) == 0 || strncmp(key, "file.", 5) == 0 || strncmp(key, "lua.", 4) == 0)
		{
		for (const auto &eft : ExifFormattedList)
			if (strcmp(key, eft.key) == 0)
				return g_strdup(_(eft.description));
		}

	return exif_get_tag_description_by_key(key);
}

gint exif_get_integer(ExifData *exif, const gchar *key, gint *value)
{
	ExifItem *item;

	item = exif_get_item(exif, key);
	return exif_item_get_integer(item, value);
}

ExifRational *exif_get_rational(ExifData *exif, const gchar *key, gint *sign)
{
	ExifItem *item;

	item = exif_get_item(exif, key);
	return exif_item_get_rational(item, sign, 0);
}

gchar *exif_get_data_as_text(ExifData *exif, const gchar *key)
{
	if (!key) return nullptr;

	if (auto text = exif_get_formatted_by_key(exif, key); text.has_value()) return text.value();

	ExifItem *item = exif_get_item(exif, key);
	if (item) return exif_item_get_data_as_text(item, exif);

	return nullptr;
}


static void exif_release_cb(FileData *fd)
{
	g_clear_pointer(&fd->exif, exif_free);
}

ExifData *exif_read_fd(FileData *fd)
{
	if (!fd) return nullptr;

	static FileCacheData *exif_cache = file_cache_new(exif_release_cb, 4);

	if (file_cache_get(exif_cache, fd)) return fd->exif;
	g_assert(fd->exif == nullptr);

	/* CACHE_TYPE_XMP_METADATA file should exist only if the metadata are
	 * not writable directly, thus it should contain the most up-to-date version */
	g_autofree gchar *sidecar_path = nullptr;

#if HAVE_EXIV2
	/* we are not able to handle XMP sidecars without exiv2 */
	sidecar_path = cache_find_location(CACHE_TYPE_XMP_METADATA, fd->path);

	if (!sidecar_path) sidecar_path = file_data_get_sidecar_path(fd, TRUE);
#endif

	fd->exif = exif_read(fd->path, sidecar_path, fd->modified_xmp);

	file_cache_put(exif_cache, fd, 1);
	return fd->exif;
}


void exif_free_fd(FileData *fd, ExifData *exif)
{
	if (!fd) return;
	g_assert(fd->exif == exif);
}

ColorManMemData exif_get_color_profile(FileData *fd, ColorManProfileType &color_profile_from_image)
{
	color_profile_from_image = COLOR_PROFILE_NONE;

	ExifData *exif = exif_read_fd(fd);
	if (!exif) return {};

	ColorManMemData profile{};

	if (g_strcmp0(fd->format_name, "heif") == 0)
		{
		profile.ptr.reset(heif_color_profile(fd->path, profile.len));
		}

	if (!profile.ptr)
		{
		profile.ptr.reset(exif_get_color_profile(exif, &profile.len));
		}

	if (profile.ptr)
		{
		DEBUG_1("Found embedded color profile");
		color_profile_from_image = COLOR_PROFILE_MEM;
		}
	else
		{
		g_autofree gchar *interop_index = exif_get_data_as_text(exif, "Exif.Iop.InteroperabilityIndex");

		if (interop_index)
			{
			/* Exif 2.21 specification */
			if (!strcmp(interop_index, "R98"))
				{
				color_profile_from_image = COLOR_PROFILE_SRGB;
				DEBUG_1("Found EXIF 2.21 ColorSpace of sRGB");
				}
			else if (!strcmp(interop_index, "R03"))
				{
				color_profile_from_image = COLOR_PROFILE_ADOBERGB;
				DEBUG_1("Found EXIF 2.21 ColorSpace of AdobeRGB");
				}
			}
		else
			{
			gint cs;

			/* ColorSpace == 1 specifies sRGB per EXIF 2.2 */
			if (!exif_get_integer(exif, "Exif.Photo.ColorSpace", &cs)) cs = 0;
			if (cs == 1)
				{
				color_profile_from_image = COLOR_PROFILE_SRGB;
				DEBUG_1("Found EXIF 2.2 ColorSpace of sRGB");
				}
			else if (cs == 2)
				{
				/* non-standard way of specifying AdobeRGB (used by some software) */
				color_profile_from_image = COLOR_PROFILE_ADOBERGB;
				DEBUG_1("Found EXIF 2.2 ColorSpace of AdobeRGB");
				}
			}
		}

	exif_free_fd(fd, exif);

	return profile;
}

/* embedded icc in jpeg */

gboolean exif_jpeg_parse_color(ExifData *exif, guchar *data, guint size)
{
	guint seg_offset = 0;
	guint seg_length = 0;
	guint chunk_offset[255];
	guint chunk_length[255];
	guint chunk_count = 0;

	/* For jpeg/jfif, ICC color profile data can be in more than one segment.
	   the data is in APP2 data segments that start with "ICC_PROFILE\x00\xNN\xTT"
	   NN = segment number for data
	   TT = total number of ICC segments (TT in each ICC segment should match)
	 */

	while (jpeg_segment_find(data + seg_offset + seg_length,
	                         size - seg_offset - seg_length,
	                         JPEG_MARKER_APP2,
	                         "ICC_PROFILE\x00", 12,
	                         seg_offset, seg_length))
		{
		guchar chunk_num;
		guchar chunk_tot;

		if (seg_length < 14) return FALSE;

		chunk_num = data[seg_offset + 12];
		chunk_tot = data[seg_offset + 13];

		if (chunk_num == 0 || chunk_tot == 0) return FALSE;

		if (chunk_count == 0)
			{
			guint i;

			chunk_count = static_cast<guint>(chunk_tot);
			for (i = 0; i < chunk_count; i++) chunk_offset[i] = 0;
			for (i = 0; i < chunk_count; i++) chunk_length[i] = 0;
			}

		if (chunk_tot != chunk_count ||
		    chunk_num > chunk_count) return FALSE;

		chunk_num--;
		chunk_offset[chunk_num] = seg_offset + 14;
		chunk_length[chunk_num] = seg_length - 14;
		}

	if (chunk_count > 0)
		{
		guint cp_length = 0;
		guint i;

		for (i = 0; i < chunk_count; i++) cp_length += chunk_length[i];

		g_autofree auto *cp_data = static_cast<guchar *>(g_malloc(cp_length));

		for (i = 0; i < chunk_count; i++)
			{
			if (chunk_offset[i] == 0)
				{
				/* error, we never saw this chunk */
				return FALSE;
				}
			memcpy(cp_data, data + chunk_offset[i], chunk_length[i]);
			}
		DEBUG_1("Found embedded icc profile in jpeg");
		exif_add_jpeg_color_profile(exif, g_steal_pointer(&cp_data), cp_length);

		return TRUE;
		}

	return FALSE;
}

/*
 *-------------------------------------------------------------------
 * file info
 * it is here because it shares tag neming infrastructure with exif
 * we should probably not invest too much effort into this because
 * new exiv2 will support the same functionality
 * https://dev.exiv2.org/issues/505
 *-------------------------------------------------------------------
 */

static gchar *mode_number(mode_t m)
{
	gint mb;
	gint mu;
	gint mg;
	gint mo;
	gchar pbuf[12];

	mb = mu = mg = mo = 0;

	if (m & S_ISUID) mb |= 4;
	if (m & S_ISGID) mb |= 2;
	if (m & S_ISVTX) mb |= 1;

	if (m & S_IRUSR) mu |= 4;
	if (m & S_IWUSR) mu |= 2;
	if (m & S_IXUSR) mu |= 1;

	if (m & S_IRGRP) mg |= 4;
	if (m & S_IWGRP) mg |= 2;
	if (m & S_IXGRP) mg |= 1;

	if (m & S_IROTH) mo |= 4;
	if (m & S_IWOTH) mo |= 2;
	if (m & S_IXOTH) mo |= 1;

	pbuf[0] = (m & S_IRUSR) ? 'r' : '-';
	pbuf[1] = (m & S_IWUSR) ? 'w' : '-';
	pbuf[2] = (m & S_IXUSR) ? 'x' : '-';
	pbuf[3] = (m & S_IRGRP) ? 'r' : '-';
	pbuf[4] = (m & S_IWGRP) ? 'w' : '-';
	pbuf[5] = (m & S_IXGRP) ? 'x' : '-';
	pbuf[6] = (m & S_IROTH) ? 'r' : '-';
	pbuf[7] = (m & S_IWOTH) ? 'w' : '-';
	pbuf[8] = (m & S_IXOTH) ? 'x' : '-';
	pbuf[9] = '\0';

	return g_strdup_printf("%s (%d%d%d%d)", pbuf, mb, mu, mg, mo);
}

gchar *metadata_file_info(FileData *fd, const gchar *key, MetadataFormat)
{
	gchar *page_n_of_m;

	if (strcmp(key, "file.size") == 0)
		{
		return g_strdup_printf("%ld", static_cast<long>(fd->size));
		}
	if (strcmp(key, "file.date") == 0)
		{
		return g_strdup(text_from_time(fd->date));
		}
	if (strcmp(key, "file.mode") == 0)
		{
		return mode_number(fd->mode);
		}
	if (strcmp(key, "file.ctime") == 0)
		{
		return g_strdup(text_from_time(fd->cdate));
		}
	if (strcmp(key, "file.class") == 0)
		{
		return g_strdup(format_class_list[fd->format_class]);
		}
	if (strcmp(key, "file.owner") == 0)
		{
		return get_file_owner(fd->path);
		}
	if (strcmp(key, "file.group") == 0)
		{
		return get_file_group(fd->path);
		}
	if (strcmp(key, "file.link") == 0)
		{
		return get_symbolic_link(fd->path);
		}
	if (strcmp(key, "file.page_no") == 0)
		{
		if (fd->page_total > 1)
			{
			page_n_of_m = g_strdup_printf("[%d/%d]", fd->page_num + 1, fd->page_total);
			return page_n_of_m;
			}

		return nullptr;
		}
	return g_strdup("");
}

#if HAVE_LUA
gchar *metadata_lua_info(FileData *fd, const gchar *key, MetadataFormat)
{
	g_autofree gchar *script_name_utf8 = g_strdup(key + 4);
	g_autofree gchar *script_name = path_from_utf8(script_name_utf8);

	g_autofree gchar *raw_data = lua_callvalue(fd, script_name, nullptr);
	g_autofree gchar *valid_data = g_utf8_make_valid(raw_data, -1);

	return g_utf8_substring(valid_data, 0, 150);
}
#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
