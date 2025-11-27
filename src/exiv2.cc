/*
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: Vladimir Nadvornik
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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <config.h>

#include <exiv2/exiv2.hpp>
#include <glib.h>
#ifdef ENABLE_NLS
#  include <libintl.h>
#endif

#include "filedata.h"
#include "filefilter.h"
#include "metadata.h"
#include "misc.h"
#include "options.h"
#include "ui-fileops.h"

struct ExifItem;

#if EXIV2_TEST_VERSION(0,27,0)
#define HAVE_EXIV2_ERROR_CODE
#endif

#if EXIV2_TEST_VERSION(0,27,0)
#define EXV_PACKAGE "exiv2"
#endif

#if EXIV2_TEST_VERSION(0,28,0)
#define AnyError Error
#define AutoPtr UniquePtr
#endif

struct AltKey
{
	const gchar *xmp_key;
	const gchar *exif_key;
	const gchar *iptc_key;
};

/* this is a list of keys that should be converted, even with the older Exiv2 which does not support it directly */
static constexpr AltKey alt_keys[] = {
	{"Xmp.tiff.Orientation",		"Exif.Image.Orientation", 	nullptr},
	{"Xmp.dc.title",			nullptr,				"Iptc.Application2.ObjectName"		},
	{"Xmp.photoshop.Urgency",		nullptr,				"Iptc.Application2.Urgency"		},
	{"Xmp.photoshop.Category",		nullptr,				"Iptc.Application2.Category"		},
	{"Xmp.photoshop.SupplementalCategory",	nullptr,				"Iptc.Application2.SuppCategory"	},
	{"Xmp.dc.subject",			nullptr,				"Iptc.Application2.Keywords"		},
	{"Xmp.iptc.Location",			nullptr,				"Iptc.Application2.LocationName"	},
	{"Xmp.photoshop.Instruction",		nullptr,				"Iptc.Application2.SpecialInstructions"	},
	{"Xmp.photoshop.DateCreated",		nullptr,				"Iptc.Application2.DateCreated"		},
	{"Xmp.dc.creator",			nullptr,				"Iptc.Application2.Byline"		},
	{"Xmp.photoshop.AuthorsPosition",	nullptr,				"Iptc.Application2.BylineTitle"		},
	{"Xmp.photoshop.City",			nullptr,				"Iptc.Application2.City"		},
	{"Xmp.photoshop.State",			nullptr,				"Iptc.Application2.ProvinceState"	},
	{"Xmp.iptc.CountryCode",		nullptr,				"Iptc.Application2.CountryCode"		},
	{"Xmp.photoshop.Country",		nullptr,				"Iptc.Application2.CountryName"		},
	{"Xmp.photoshop.TransmissionReference",	nullptr,				"Iptc.Application2.TransmissionReference"},
	{"Xmp.photoshop.Headline",		nullptr,				"Iptc.Application2.Headline"		},
	{"Xmp.photoshop.Credit",		nullptr,				"Iptc.Application2.Credit"		},
	{"Xmp.photoshop.Source",		nullptr,				"Iptc.Application2.Source"		},
	{"Xmp.dc.rights",			nullptr,				"Iptc.Application2.Copyright"		},
	{"Xmp.dc.description",			nullptr,				"Iptc.Application2.Caption"		},
	{"Xmp.photoshop.CaptionWriter",		nullptr,				"Iptc.Application2.Writer"		},
	};

#ifdef DEBUG
static void _debug_exception(const char* file,
                             int line,
                             const char* func,
                             Exiv2::AnyError& e)
{
	g_autofree gchar *str = g_locale_from_utf8(e.what(), -1, nullptr, nullptr, nullptr);
	DEBUG_1("%s:%d:%s:Exiv2: %s", file, line, func, str);
}

#  define debug_exception(e) _debug_exception(__FILE__, __LINE__, __func__, e)
#else
#  define debug_exception(e)
#endif

struct ExifData
{
	Exiv2::ExifData::const_iterator exifIter; /* for exif_get_next_item */
	Exiv2::IptcData::const_iterator iptcIter; /* for exif_get_next_item */
	Exiv2::XmpData::const_iterator xmpIter; /* for exif_get_next_item */

	virtual ~ExifData() = default;

	virtual void writeMetadata(gchar * = nullptr)
	{
		g_critical("Unsupported method of writing metadata");
	}

	virtual ExifData *original()
	{
		return nullptr;
	}

	virtual Exiv2::Image *image() = 0;

	virtual Exiv2::ExifData &exifData() = 0;

	virtual Exiv2::IptcData &iptcData() = 0;

	virtual Exiv2::XmpData &xmpData() = 0;

	virtual void add_jpeg_color_profile(unsigned char *cp_data, guint cp_length) = 0;

	virtual guchar *get_jpeg_color_profile(guint *data_len) = 0;

	[[nodiscard]] virtual std::string image_comment() const = 0;

	virtual void set_image_comment(const std::string& comment) = 0;
};

// This allows read-only access to the original metadata
struct ExifDataOriginal : public ExifData
{
protected:
	Exiv2::Image::AutoPtr image_;

	/* the icc profile in jpeg is not technically exif - store it here */
	unsigned char *cp_data_;
	guint cp_length_;
	gboolean valid_;

	Exiv2::ExifData emptyExifData_;
	Exiv2::IptcData emptyIptcData_;
	Exiv2::XmpData emptyXmpData_;

public:
	ExifDataOriginal(const gchar *path)
	{
		cp_data_ = nullptr;
		cp_length_ = 0;
		valid_ = TRUE;

		try
			{
			g_autofree gchar *pathl = path_from_utf8(path);

			image_ = Exiv2::ImageFactory::open(pathl);
			image_->readMetadata();

			if (image_->mimeType() == "application/rdf+xml")
				{
				//Exiv2 sidecar converts xmp to exif and iptc, we don't want it.
				image_->clearExifData();
				image_->clearIptcData();
				}

			if (image_->mimeType() == "image/jpeg")
				{
				/* try to get jpeg color profile */
				Exiv2::BasicIo &io = image_->io();
				gint open = io.isopen();
				if (!open) io.open();
				if (io.isopen())
					{
					auto mapped = static_cast<unsigned char*>(io.mmap());
					if (mapped) exif_jpeg_parse_color(this, mapped, io.size());
					io.munmap();
					}
				if (!open) io.close();
				}
			}
		catch (Exiv2::AnyError& e)
			{
			valid_ = FALSE;
			}
	}

	~ExifDataOriginal() override
	{
		g_free(cp_data_);
	}

	Exiv2::Image *image() override
	{
		if (!valid_) return nullptr;
		return image_.get();
	}

	Exiv2::ExifData &exifData () override
	{
		if (!valid_) return emptyExifData_;
		return image_->exifData();
	}

	Exiv2::IptcData &iptcData () override
	{
		if (!valid_) return emptyIptcData_;
		return image_->iptcData();
	}

	Exiv2::XmpData &xmpData () override
	{
		if (!valid_) return emptyXmpData_;
		return image_->xmpData();
	}

	void add_jpeg_color_profile(unsigned char *cp_data, guint cp_length) override
	{
		g_free(cp_data_);
		cp_data_ = cp_data;
		cp_length_ = cp_length;
	}

	guchar *get_jpeg_color_profile(guint *data_len) override
	{
		if (cp_data_)
		{
			if (data_len) *data_len = cp_length_;
#if GLIB_CHECK_VERSION(2,68,0)
			return static_cast<unsigned char *>(g_memdup2(cp_data_, cp_length_));
#else
			return static_cast<unsigned char *>(g_memdup(cp_data_, cp_length_));
#endif
		}
		return nullptr;
	}

	[[nodiscard]] std::string image_comment() const override
	{
		return image_.get() ? image_->comment() : "";
	}

	void set_image_comment(const std::string& comment) override
	{
		if (image_.get())
			image_->setComment(comment);
	}
};

static void _ExifDataProcessed_update_xmp(gpointer key, gpointer value, gpointer data);

// This allows read-write access to the metadata
struct ExifDataProcessed : public ExifData
{
protected:
	std::unique_ptr<ExifDataOriginal> imageData_;
	std::unique_ptr<ExifDataOriginal> sidecarData_;

	Exiv2::ExifData exifData_;
	Exiv2::IptcData iptcData_;
	Exiv2::XmpData xmpData_;

public:
	ExifDataProcessed(gchar *path, gchar *sidecar_path, GHashTable *modified_xmp)
	{
		imageData_ = std::make_unique<ExifDataOriginal>(path);
		sidecarData_ = nullptr;
		if (sidecar_path)
			{
			sidecarData_ = std::make_unique<ExifDataOriginal>(sidecar_path);
			xmpData_ = sidecarData_->xmpData();
			}
		else
			{
			xmpData_ = imageData_->xmpData();
			}

		exifData_ = imageData_->exifData();
		iptcData_ = imageData_->iptcData();
		try
			{
			syncExifWithXmp(exifData_, xmpData_);
			}
		catch (...)
			{
			DEBUG_1("Exiv2: Catching bug\n");
			}
		if (modified_xmp)
			{
			g_hash_table_foreach(modified_xmp, _ExifDataProcessed_update_xmp, this);
			}
	}

	ExifData *original() override
	{
		return imageData_.get();
	}

	void writeMetadata(gchar *path = nullptr) override
	{
		if (!path)
			{
			if (options->metadata.save_legacy_IPTC)
				copyXmpToIptc(xmpData_, iptcData_);
			else
				iptcData_.clear();

			copyXmpToExif(xmpData_, exifData_);
			Exiv2::Image *image = imageData_->image();

			if (!image)
#ifdef HAVE_EXIV2_ERROR_CODE
#  if EXIV2_TEST_VERSION(0,28,0)
				throw Exiv2::Error(Exiv2::ErrorCode::kerInputDataReadFailed);
#  else
				throw Exiv2::Error(Exiv2::kerInputDataReadFailed);
#  endif
#else
				throw Exiv2::Error(21);
#endif
			image->setExifData(exifData_);
			image->setIptcData(iptcData_);
			image->setXmpData(xmpData_);
			image->writeMetadata();
			}
		else
			{
			g_autofree gchar *pathl = path_from_utf8(path);;

			auto sidecar = Exiv2::ImageFactory::create(Exiv2::ImageType::xmp, pathl);

			sidecar->setXmpData(xmpData_);
			sidecar->writeMetadata();
			}
	}

	Exiv2::Image *image() override
	{
		return imageData_->image();
	}

	Exiv2::ExifData &exifData () override
	{
		return exifData_;
	}

	Exiv2::IptcData &iptcData () override
	{
		return iptcData_;
	}

	Exiv2::XmpData &xmpData () override
	{
		return xmpData_;
	}

	void add_jpeg_color_profile(unsigned char *cp_data, guint cp_length) override
	{
		imageData_->add_jpeg_color_profile(cp_data, cp_length);
	}

	guchar *get_jpeg_color_profile(guint *data_len) override
	{
		return imageData_->get_jpeg_color_profile(data_len);
	}

	[[nodiscard]] std::string image_comment() const override
	{
		return imageData_->image_comment();
	}

	void set_image_comment(const std::string& comment) override
	{
		imageData_->set_image_comment(comment);
	}
};






void exif_init()
{
#ifdef EXV_ENABLE_NLS
	bind_textdomain_codeset (EXV_PACKAGE, "UTF-8");
#endif

#if !EXIV2_TEST_VERSION(0,28,3)
#ifdef EXV_ENABLE_BMFF
	Exiv2::enableBMFF(true);
#endif
#endif
}



static void _ExifDataProcessed_update_xmp(gpointer key, gpointer value, gpointer data)
{
	exif_update_metadata(static_cast<ExifData *>(data), static_cast<gchar *>(key), static_cast<GList *>(value));
}

ExifData *exif_read(gchar *path, gchar *sidecar_path, GHashTable *modified_xmp)
{
	DEBUG_1("exif read %s, sidecar: %s", path, sidecar_path ? sidecar_path : "-");
	try {
		return new ExifDataProcessed(path, sidecar_path, modified_xmp);
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}

}

gboolean exif_write(ExifData *exif)
{
	try {
		exif->writeMetadata();
		return TRUE;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return FALSE;
	}
}

gboolean exif_write_sidecar(ExifData *exif, gchar *path)
{
	try {
		exif->writeMetadata(path);
		return TRUE;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return FALSE;
	}

}


void exif_free(ExifData *exif)
{
	if (!exif) return;
	g_assert(dynamic_cast<ExifDataProcessed *>(exif)); // this should not be called on ExifDataOriginal
	delete exif;
}

ExifData *exif_get_original(ExifData *exif)
{
	return exif->original();
}


ExifItem *exif_get_item(ExifData *exif, const gchar *key)
{
	try {
		Exiv2::Metadatum *item = nullptr;
		try {
			Exiv2::ExifKey ekey(key);
			auto pos = exif->exifData().findKey(ekey);
			if (pos == exif->exifData().end()) return nullptr;
			item = &*pos;
		}
		catch (Exiv2::AnyError& e) {
			try {
				Exiv2::IptcKey ekey(key);
				auto pos = exif->iptcData().findKey(ekey);
				if (pos == exif->iptcData().end()) return nullptr;
				item = &*pos;
			}
			catch (Exiv2::AnyError& e) {
				Exiv2::XmpKey ekey(key);
				auto pos = exif->xmpData().findKey(ekey);
				if (pos == exif->xmpData().end()) return nullptr;
				item = &*pos;
			}
		}
		return reinterpret_cast<ExifItem *>(item);
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}


ExifItem *exif_get_first_item(ExifData *exif)
{
	try {
		exif->exifIter = exif->exifData().begin();
		exif->iptcIter = exif->iptcData().begin();
		exif->xmpIter = exif->xmpData().begin();
		if (exif->exifIter != exif->exifData().end())
			{
			const Exiv2::Metadatum *item = &*exif->exifIter;
			++exif->exifIter;
			return (ExifItem *)item;
			}
		if (exif->iptcIter != exif->iptcData().end())
			{
			const Exiv2::Metadatum *item = &*exif->iptcIter;
			++exif->iptcIter;
			return (ExifItem *)item;
			}
		if (exif->xmpIter != exif->xmpData().end())
			{
			const Exiv2::Metadatum *item = &*exif->xmpIter;
			++exif->xmpIter;
			return (ExifItem *)item;
			}
		return nullptr;

	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

ExifItem *exif_get_next_item(ExifData *exif)
{
	try {
		if (exif->exifIter != exif->exifData().end())
			{
			const Exiv2::Metadatum *item = &*exif->exifIter;
			++exif->exifIter;
			return (ExifItem *)item;
		}
		if (exif->iptcIter != exif->iptcData().end())
			{
			const Exiv2::Metadatum *item = &*exif->iptcIter;
			++exif->iptcIter;
			return (ExifItem *)item;
		}
		if (exif->xmpIter != exif->xmpData().end())
			{
			const Exiv2::Metadatum *item = &*exif->xmpIter;
			++exif->xmpIter;
			return (ExifItem *)item;
		}
		return nullptr;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

char *exif_item_get_tag_name(ExifItem *item)
{
	try {
		if (!item) return nullptr;
		return g_strdup((reinterpret_cast<Exiv2::Metadatum *>(item))->key().c_str());
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

guint exif_item_get_tag_id(ExifItem *item)
{
	try {
		if (!item) return 0;
		return (reinterpret_cast<Exiv2::Metadatum *>(item))->tag();
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return 0;
	}
}

guint exif_item_get_elements(ExifItem *item)
{
	try {
		if (!item) return 0;
		return (reinterpret_cast<Exiv2::Metadatum *>(item))->count();
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return 0;
	}
}

char *exif_item_get_data(ExifItem *item, guint *data_len)
{
	try {
		if (!item) return nullptr;
		auto md = reinterpret_cast<Exiv2::Metadatum *>(item);
		if (data_len) *data_len = md->size();
		auto data = static_cast<char *>(g_malloc(md->size()));
		auto res = md->copy(reinterpret_cast<Exiv2::byte *>(data), Exiv2::littleEndian /* should not matter */);
		g_assert(res == md->size());
		return data;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

char *exif_item_get_description(ExifItem *item)
{
	try {
		if (!item) return nullptr;
		return utf8_validate_or_convert((reinterpret_cast<Exiv2::Metadatum *>(item))->tagLabel().c_str());
	}
	catch (std::exception& e) {
		return nullptr;
	}
}

guint exif_item_get_format_id(ExifItem *item)
{
	/*
	invalidTypeId, unsignedByte, asciiString, unsignedShort,
	  unsignedLong, unsignedRational, signedByte, undefined,
	  signedShort, signedLong, signedRational, string,
	  date, time, comment, directory,
	  xmpText, xmpAlt, xmpBag, xmpSeq,
	  langAlt, lastTypeId
	*/
	static constexpr std::array<guint, 19> format_id_trans_tbl{
		EXIF_FORMAT_UNKNOWN,
		EXIF_FORMAT_BYTE_UNSIGNED,
		EXIF_FORMAT_STRING,
		EXIF_FORMAT_SHORT_UNSIGNED,
		EXIF_FORMAT_LONG_UNSIGNED,
		EXIF_FORMAT_RATIONAL_UNSIGNED,
		EXIF_FORMAT_BYTE,
		EXIF_FORMAT_UNDEFINED,
		EXIF_FORMAT_SHORT,
		EXIF_FORMAT_LONG,
		EXIF_FORMAT_RATIONAL,
		EXIF_FORMAT_STRING,
		EXIF_FORMAT_STRING,
		EXIF_FORMAT_STRING,
		EXIF_FORMAT_UNDEFINED,
		EXIF_FORMAT_STRING,
		EXIF_FORMAT_STRING,
		EXIF_FORMAT_STRING,
		EXIF_FORMAT_STRING
	};

	try {
		if (!item) return EXIF_FORMAT_UNKNOWN;
		guint id = (reinterpret_cast<Exiv2::Metadatum *>(item))->typeId();
		return format_id_trans_tbl.at(id);
	}
	catch (Exiv2::AnyError &e) {
		debug_exception(e);
		return EXIF_FORMAT_UNKNOWN;
	}
	catch (const std::out_of_range &) {
		return EXIF_FORMAT_UNKNOWN;
	}
}

const char *exif_item_get_format_name(ExifItem *item, gboolean)
{
	try {
		if (!item) return nullptr;
		return (reinterpret_cast<Exiv2::Metadatum *>(item))->typeName();
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}


gchar *exif_item_get_data_as_text(ExifItem *item, ExifData *exif)
{
	try {
		if (!item) return nullptr;
		auto metadatum = reinterpret_cast<Exiv2::Metadatum *>(item);
		return utf8_validate_or_convert(metadatum->print(&exif->exifData()).c_str());
	}
	catch (Exiv2::AnyError& e) {
		return nullptr;
	}
}

gchar *exif_item_get_string(ExifItem *item, int idx)
{
	try {
		if (!item) return nullptr;
		auto em = reinterpret_cast<Exiv2::Metadatum *>(item);
		std::string str = em->toString(idx);
		if (idx == 0 && str.empty()) str = em->toString();
		if (str.length() > 5 && str.substr(0, 5) == "lang=")
			{
			std::string::size_type pos = str.find_first_of(' ');
			if (pos != std::string::npos) str = str.substr(pos+1);
			}

		return utf8_validate_or_convert(str.c_str());
	}
	catch (Exiv2::AnyError& e) {
		return nullptr;
	}
}


gint exif_item_get_integer(ExifItem *item, gint *value)
{
	try {
		if (!item || exif_item_get_elements(item) == 0) return 0;

#if EXIV2_TEST_VERSION(0,28,0)
        *value = ((Exiv2::Metadatum *)item)->toInt64();
#else
		*value = (reinterpret_cast<Exiv2::Metadatum *>(item))->toLong();
#endif
		return 1;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return 0;
	}
}

ExifRational *exif_item_get_rational(ExifItem *item, gint *sign, guint n)
{
	try {
		if (!item) return nullptr;
		if (n >= exif_item_get_elements(item)) return nullptr;
		Exiv2::Rational v = (reinterpret_cast<Exiv2::Metadatum *>(item))->toRational(n);
		static ExifRational ret;
		ret.num = v.first;
		ret.den = v.second;
		if (sign) *sign = ((reinterpret_cast<Exiv2::Metadatum *>(item))->typeId() == Exiv2::signedRational);
		return &ret;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

gchar *exif_get_tag_description_by_key(const gchar *key)
{
	try {
		Exiv2::ExifKey ekey(key);
		return utf8_validate_or_convert(ekey.tagLabel().c_str());
	}
	catch (Exiv2::AnyError& e) {
		try {
			Exiv2::IptcKey ikey(key);
			return utf8_validate_or_convert(ikey.tagLabel().c_str());
		}
		catch (Exiv2::AnyError& e) {
			try {
				Exiv2::XmpKey xkey(key);
				return utf8_validate_or_convert(xkey.tagLabel().c_str());
			}
			catch (Exiv2::AnyError& e) {
				debug_exception(e);
				return nullptr;
			}
		}
	}
	return nullptr;
}

static const AltKey *find_alt_key(const gchar *xmp_key)
{
	for (const auto& k : alt_keys)
		if (strcmp(xmp_key, k.xmp_key) == 0) return &k;
	return nullptr;
}

static gint exif_update_metadata_simple(ExifData *exif, const gchar *key, const GList *values)
{
	try {
		const GList *work = values;

		try {
			Exiv2::ExifKey ekey(key);

			auto pos = exif->exifData().findKey(ekey);
			while (pos != exif->exifData().end())
				{
				exif->exifData().erase(pos);
				pos = exif->exifData().findKey(ekey);
				}

			while (work)
				{
				exif->exifData()[key] = static_cast<gchar *>(work->data);
				work = work->next;
				}
		}
		catch (Exiv2::AnyError& e) {
			try
			{
				Exiv2::IptcKey ekey(key);
				auto pos = exif->iptcData().findKey(ekey);
				while (pos != exif->iptcData().end())
					{
					exif->iptcData().erase(pos);
					pos = exif->iptcData().findKey(ekey);
					}

				while (work)
					{
					exif->iptcData()[key] = static_cast<gchar *>(work->data);
					work = work->next;
					}
			}
			catch (Exiv2::AnyError& e) {
				Exiv2::XmpKey ekey(key);
				auto pos = exif->xmpData().findKey(ekey);
				while (pos != exif->xmpData().end())
					{
					exif->xmpData().erase(pos);
					pos = exif->xmpData().findKey(ekey);
					}

				while (work)
					{
					exif->xmpData()[key] = static_cast<gchar *>(work->data);
					work = work->next;
					}
			}
		}
		return 1;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return 0;
	}
}

gint exif_update_metadata(ExifData *exif, const gchar *key, const GList *values)
{
	gint ret = exif_update_metadata_simple(exif, key, values);

	if (
	    !values || /* deleting item */
	    !ret  /* writing to the explicitly given xmp tag failed */
	    )
		{
		/* deleted xmp metadatum can't be converted, we have to delete also the corresponding legacy tag */
		/* if we can't write xmp, update at least the legacy tag */
		const AltKey *alt_key = find_alt_key(key);
		if (alt_key && alt_key->iptc_key)
			ret = exif_update_metadata_simple(exif, alt_key->iptc_key, values);

		if (alt_key && alt_key->exif_key)
			ret = exif_update_metadata_simple(exif, alt_key->exif_key, values);
		}
	return ret;
}


static GList *exif_add_value_to_glist(GList *list, Exiv2::Metadatum &item, MetadataFormat format, const Exiv2::ExifData *metadata)
{
	Exiv2::TypeId id = item.typeId();
	if (format == METADATA_FORMATTED ||
	    id == Exiv2::asciiString ||
	    id == Exiv2::undefined ||
	    id == Exiv2::string ||
	    id == Exiv2::date ||
	    id == Exiv2::time ||
	    id == Exiv2::xmpText ||
	    id == Exiv2::langAlt ||
	    id == Exiv2::comment
	    )
		{
		/* read as a single entry */
		std::string str;

		if (format == METADATA_FORMATTED)
			{
			str = item.print(metadata);
			if (str.length() > 1024)
				{
				/* truncate very long strings, they cause problems in gui */
				str.erase(1024);
				str.append("...");
				}
			}
		else
			{
			str = item.toString();
			}
		if (str.length() > 5 && str.substr(0, 5) == "lang=")
			{
			std::string::size_type pos = str.find_first_of(' ');
			if (pos != std::string::npos) str = str.substr(pos+1);
			}
		list = g_list_append(list, utf8_validate_or_convert(str.c_str()));
		}
	else
		{
		/* read as a list */
#if EXIV2_TEST_VERSION(0,28,0)
		size_t i;
#else
		long i;
#endif
		for (i = 0; i < item.count(); i++)
			list = g_list_append(list, utf8_validate_or_convert(item.toString(i).c_str()));
		}
	return list;
}

static GList *exif_get_metadata_simple(ExifData *exif, const gchar *key, MetadataFormat format)
{
	GList *list = nullptr;
	try {
		try {
			Exiv2::ExifKey ekey(key);
			auto pos = exif->exifData().findKey(ekey);
			if (pos != exif->exifData().end())
				list = exif_add_value_to_glist(list, *pos, format, &exif->exifData());

		}
		catch (Exiv2::AnyError& e) {
			try {
				Exiv2::IptcKey ekey(key);
				auto pos = exif->iptcData().begin();
				while (pos != exif->iptcData().end())
					{
					if (pos->key() == key)
						list = exif_add_value_to_glist(list, *pos, format, nullptr);
					++pos;
					}

			}
			catch (Exiv2::AnyError& e) {
				Exiv2::XmpKey ekey(key);
				auto pos = exif->xmpData().findKey(ekey);
				if (pos != exif->xmpData().end())
					list = exif_add_value_to_glist(list, *pos, format, nullptr);
			}
		}
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
	}
	return list;
}

GList *exif_get_metadata(ExifData *exif, const gchar *key, MetadataFormat format)
{
	GList *list = nullptr;

	if (!key) return nullptr;

	if (format == METADATA_FORMATTED)
		{
		gchar *text;
		gint key_valid;
		text = exif_get_formatted_by_key(exif, key, &key_valid);
		if (key_valid) return g_list_append(nullptr, text);
		}

	list = exif_get_metadata_simple(exif, key, format);

	/* the following code can be ifdefed out as soon as Exiv2 supports it */
	if (!list)
		{
		const AltKey *alt_key = find_alt_key(key);
		if (alt_key && alt_key->iptc_key)
			list = exif_get_metadata_simple(exif, alt_key->iptc_key, format);
		}
	return list;
}


void exif_add_jpeg_color_profile(ExifData *exif, unsigned char *cp_data, guint cp_length)
{
	exif->add_jpeg_color_profile(cp_data, cp_length);
}

guchar *exif_get_color_profile(ExifData *exif, guint *data_len)
{
	guchar *ret = exif->get_jpeg_color_profile(data_len);
	if (ret) return ret;

	ExifItem *prof_item = exif_get_item(exif, "Exif.Image.InterColorProfile");
	if (prof_item && exif_item_get_format_id(prof_item) == EXIF_FORMAT_UNDEFINED)
		ret = reinterpret_cast<guchar *>(exif_item_get_data(prof_item, data_len));
	return ret;
}

gchar* exif_get_image_comment(FileData* fd)
{
	if (!fd || !fd->exif)
		return g_strdup("");

	return g_strdup(fd->exif->image_comment().c_str());
}

void exif_set_image_comment(FileData* fd, const gchar* comment)
{
	if (!fd || !fd->exif)
		return;

	fd->exif->set_image_comment(comment ? comment : "");
}


guchar *exif_get_preview(ExifData *exif, guint *data_len, gint requested_width, gint requested_height)
{
	if (!exif) return nullptr;

	if (!exif->image()) return nullptr;

	std::string const path = exif->image()->io().path();
	/* given image pathname, first do simple (and fast) file extension test */
	gboolean is_raw = filter_file_class(path.c_str(), FORMAT_CLASS_RAWIMAGE);

	if (!is_raw && requested_width == 0) return nullptr;

	try {

		Exiv2::PreviewManager pm(*exif->image());

		Exiv2::PreviewPropertiesList list = pm.getPreviewProperties();

		if (!list.empty())
			{
			Exiv2::PreviewPropertiesList::iterator pos;
			auto last = --list.end();

			if (requested_width == 0)
				{
				pos = last; // the largest
				}
			else
				{
				pos = list.begin();
				while (pos != last)
					{
					if (pos->width_ >= static_cast<uint32_t>(requested_width) &&
					    pos->height_ >= static_cast<uint32_t>(requested_height)) break;
					++pos;
					}

				// we are not interested in smaller thumbnails in normal image formats - we can use full image instead
				if (!is_raw)
					{
					if (pos->width_ < static_cast<uint32_t>(requested_width) || pos->height_ < static_cast<uint32_t>(requested_height)) return nullptr;
					}
				}

			Exiv2::PreviewImage image = pm.getPreviewImage(*pos);

			// Let's not touch data_len until we finish copy.
			// Just in case we run into OOM.
			size_t img_sz = image.size();
			auto* b = new Exiv2::byte[img_sz];
			std::copy_n(image.pData(), img_sz, b);
			*data_len = img_sz;
			return b;
			}
		return nullptr;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return nullptr;
	}
}

void exif_free_preview(const guchar *buf)
{
	delete[] static_cast<const Exiv2::byte*>(buf);
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
