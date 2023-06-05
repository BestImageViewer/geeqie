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

#include <config.h>

#ifdef HAVE_EXIV2

// Don't include the <exiv2/version.hpp> file directly
// Early Exiv2 versions didn't have version.hpp and the macros.
#include <exiv2/exiv2.hpp>
#include <iostream>
#include <string>

// EXIV2_TEST_VERSION is defined in Exiv2 0.15 and newer.
#ifdef EXIV2_VERSION
#ifndef EXIV2_TEST_VERSION
#define EXIV2_TEST_VERSION(major,minor,patch) \
	( EXIV2_VERSION >= EXIV2_MAKE_VERSION(major,minor,patch) )
#endif
#else
#define EXIV2_TEST_VERSION(major,minor,patch) (false)
#endif

#if EXIV2_TEST_VERSION(0,27,0)
#define HAVE_EXIV2_ERROR_CODE
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#if EXIV2_TEST_VERSION(0,27,0)
#define EXV_PACKAGE "exiv2"
#endif

#include <glib.h>

#include "main.h"
#include "exif.h"

#include "filefilter.h"
#include "ui-fileops.h"

#include "misc.h"

#if EXIV2_TEST_VERSION(0,28,0)
#define AnyError Error
#define AutoPtr UniquePtr
#endif

typedef struct _AltKey AltKey;

struct _AltKey
{
	const gchar *xmp_key;
	const gchar *exif_key;
	const gchar *iptc_key;
};

/* this is a list of keys that should be converted, even with the older Exiv2 which does not support it directly */
static const AltKey alt_keys[] = {
	{"Xmp.tiff.Orientation",		"Exif.Image.Orientation", 	NULL},
	{"Xmp.dc.title",			NULL,				"Iptc.Application2.ObjectName"		},
	{"Xmp.photoshop.Urgency",		NULL,				"Iptc.Application2.Urgency"		},
	{"Xmp.photoshop.Category",		NULL,				"Iptc.Application2.Category"		},
	{"Xmp.photoshop.SupplementalCategory",	NULL,				"Iptc.Application2.SuppCategory"	},
	{"Xmp.dc.subject",			NULL,				"Iptc.Application2.Keywords"		},
	{"Xmp.iptc.Location",			NULL,				"Iptc.Application2.LocationName"	},
	{"Xmp.photoshop.Instruction",		NULL,				"Iptc.Application2.SpecialInstructions"	},
	{"Xmp.photoshop.DateCreated",		NULL,				"Iptc.Application2.DateCreated"		},
	{"Xmp.dc.creator",			NULL,				"Iptc.Application2.Byline"		},
	{"Xmp.photoshop.AuthorsPosition",	NULL,				"Iptc.Application2.BylineTitle"		},
	{"Xmp.photoshop.City",			NULL,				"Iptc.Application2.City"		},
	{"Xmp.photoshop.State",			NULL,				"Iptc.Application2.ProvinceState"	},
	{"Xmp.iptc.CountryCode",		NULL,				"Iptc.Application2.CountryCode"		},
	{"Xmp.photoshop.Country",		NULL,				"Iptc.Application2.CountryName"		},
	{"Xmp.photoshop.TransmissionReference",	NULL,				"Iptc.Application2.TransmissionReference"},
	{"Xmp.photoshop.Headline",		NULL,				"Iptc.Application2.Headline"		},
	{"Xmp.photoshop.Credit",		NULL,				"Iptc.Application2.Credit"		},
	{"Xmp.photoshop.Source",		NULL,				"Iptc.Application2.Source"		},
	{"Xmp.dc.rights",			NULL,				"Iptc.Application2.Copyright"		},
	{"Xmp.dc.description",			NULL,				"Iptc.Application2.Caption"		},
	{"Xmp.photoshop.CaptionWriter",		NULL,				"Iptc.Application2.Writer"		},
	{NULL, NULL, NULL}
	};

static void _debug_exception(const char* file,
                             int line,
                             const char* func,
                             Exiv2::AnyError& e)
{
	gchar *str = g_locale_from_utf8(e.what(), -1, NULL, NULL, NULL);
	DEBUG_1("%s:%d:%s:Exiv2: %s", file, line, func, str);
	g_free(str);
}

#define debug_exception(e) _debug_exception(__FILE__, __LINE__, __func__, e)

struct _ExifData
{
	Exiv2::ExifData::const_iterator exifIter; /* for exif_get_next_item */
	Exiv2::IptcData::const_iterator iptcIter; /* for exif_get_next_item */
#if EXIV2_TEST_VERSION(0,16,0)
	Exiv2::XmpData::const_iterator xmpIter; /* for exif_get_next_item */
#endif

	virtual ~_ExifData() = default;

	virtual void writeMetadata(gchar *UNUSED(path) = NULL)
	{
		g_critical("Unsupported method of writing metadata");
	}

	virtual ExifData *original()
	{
		return NULL;
	}

	virtual Exiv2::Image *image() = 0;

	virtual Exiv2::ExifData &exifData() = 0;

	virtual Exiv2::IptcData &iptcData() = 0;

#if EXIV2_TEST_VERSION(0,16,0)
	virtual Exiv2::XmpData &xmpData() = 0;
#endif

	virtual void add_jpeg_color_profile(unsigned char *cp_data, guint cp_length) = 0;

	virtual guchar *get_jpeg_color_profile(guint *data_len) = 0;

	virtual std::string image_comment() const = 0;

	virtual void set_image_comment(const std::string& comment) = 0;
};

// This allows read-only access to the original metadata
struct _ExifDataOriginal : public _ExifData
{
protected:
	Exiv2::Image::AutoPtr image_;

	/* the icc profile in jpeg is not technically exif - store it here */
	unsigned char *cp_data_;
	guint cp_length_;
	gboolean valid_;
	gchar *pathl_;

	Exiv2::ExifData emptyExifData_;
	Exiv2::IptcData emptyIptcData_;
#if EXIV2_TEST_VERSION(0,16,0)
	Exiv2::XmpData emptyXmpData_;
#endif

public:
	_ExifDataOriginal(Exiv2::Image::AutoPtr image)
	{
		cp_data_ = NULL;
		cp_length_ = 0;
        	image_ = std::move(image);
		valid_ = TRUE;
	}

	_ExifDataOriginal(gchar *path)
	{
		cp_data_ = NULL;
		cp_length_ = 0;
		valid_ = TRUE;

		pathl_ = path_from_utf8(path);
		try
			{
			image_ = Exiv2::ImageFactory::open(pathl_);
//			g_assert (image.get() != 0);
			image_->readMetadata();

#if EXIV2_TEST_VERSION(0,16,0)
			if (image_->mimeType() == "application/rdf+xml")
				{
				//Exiv2 sidecar converts xmp to exif and iptc, we don't want it.
				image_->clearExifData();
				image_->clearIptcData();
				}
#endif

#if EXIV2_TEST_VERSION(0,14,0)
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
#endif
			}
		catch (Exiv2::AnyError& e)
			{
			valid_ = FALSE;
			}
	}

	~_ExifDataOriginal() override
	{
		if (cp_data_) g_free(cp_data_);
		if (pathl_) g_free(pathl_);
	}

	Exiv2::Image *image() override
	{
		if (!valid_) return NULL;
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

#if EXIV2_TEST_VERSION(0,16,0)
	Exiv2::XmpData &xmpData () override
	{
		if (!valid_) return emptyXmpData_;
		return image_->xmpData();
	}
#endif

	void add_jpeg_color_profile(unsigned char *cp_data, guint cp_length) override
	{
		if (cp_data_) g_free(cp_data_);
		cp_data_ = cp_data;
		cp_length_ = cp_length;
	}

	guchar *get_jpeg_color_profile(guint *data_len) override
	{
		if (cp_data_)
		{
			if (data_len) *data_len = cp_length_;
#if GLIB_CHECK_VERSION(2,68,0)
			return (unsigned char *) g_memdup2(cp_data_, cp_length_);
#else
			return static_cast<unsigned char *>(g_memdup(cp_data_, cp_length_));
#endif
		}
		return NULL;
	}

	std::string image_comment() const override
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
struct _ExifDataProcessed : public _ExifData
{
protected:
	std::unique_ptr<_ExifDataOriginal> imageData_;
	std::unique_ptr<_ExifDataOriginal> sidecarData_;

	Exiv2::ExifData exifData_;
	Exiv2::IptcData iptcData_;
#if EXIV2_TEST_VERSION(0,16,0)
	Exiv2::XmpData xmpData_;
#endif

public:
	_ExifDataProcessed(gchar *path, gchar *sidecar_path, GHashTable *modified_xmp)
	{
		imageData_ = std::make_unique<_ExifDataOriginal>(path);
		sidecarData_ = NULL;
#if EXIV2_TEST_VERSION(0,16,0)
		if (sidecar_path)
			{
			sidecarData_ = std::make_unique<_ExifDataOriginal>(sidecar_path);
			xmpData_ = sidecarData_->xmpData();
			}
		else
			{
			xmpData_ = imageData_->xmpData();
			}

#endif
		exifData_ = imageData_->exifData();
		iptcData_ = imageData_->iptcData();
#if EXIV2_TEST_VERSION(0,17,0)
		try
			{
			syncExifWithXmp(exifData_, xmpData_);
			}
		catch (...)
			{
			DEBUG_1("Exiv2: Catching bug\n");
			}
#endif
		if (modified_xmp)
			{
			g_hash_table_foreach(modified_xmp, _ExifDataProcessed_update_xmp, this);
			}
	}

	ExifData *original() override
	{
		return imageData_.get();
	}

	void writeMetadata(gchar *path = NULL) override
	{
		if (!path)
			{
#if EXIV2_TEST_VERSION(0,17,0)
			if (options->metadata.save_legacy_IPTC)
				copyXmpToIptc(xmpData_, iptcData_);
			else
				iptcData_.clear();

			copyXmpToExif(xmpData_, exifData_);
#endif
			Exiv2::Image *image = imageData_->image();

#ifdef HAVE_EXIV2_ERROR_CODE
#if EXIV2_TEST_VERSION(0,28,0)
            if (!image) throw Exiv2::Error(Exiv2::ErrorCode::kerInputDataReadFailed);
#else
			if (!image) throw Exiv2::Error(Exiv2::kerInputDataReadFailed);
#endif
#else
			if (!image) throw Exiv2::Error(21);
#endif
			image->setExifData(exifData_);
			image->setIptcData(iptcData_);
#if EXIV2_TEST_VERSION(0,16,0)
			image->setXmpData(xmpData_);
#endif
			image->writeMetadata();
			}
		else
			{
#if EXIV2_TEST_VERSION(0,17,0)
			gchar *pathl = path_from_utf8(path);;

			auto sidecar = Exiv2::ImageFactory::create(Exiv2::ImageType::xmp, pathl);

			g_free(pathl);

			sidecar->setXmpData(xmpData_);
			sidecar->writeMetadata();
#else
#ifdef HAVE_EXIV2_ERROR_CODE
			throw Exiv2::Error(Exiv2::kerNotAnImage, "xmp");
#else
			throw Exiv2::Error(3, "xmp");
#endif
#endif
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

#if EXIV2_TEST_VERSION(0,16,0)
	Exiv2::XmpData &xmpData () override
	{
		return xmpData_;
	}
#endif

	void add_jpeg_color_profile(unsigned char *cp_data, guint cp_length) override
	{
		imageData_->add_jpeg_color_profile(cp_data, cp_length);
	}

	guchar *get_jpeg_color_profile(guint *data_len) override
	{
		return imageData_->get_jpeg_color_profile(data_len);
	}

	std::string image_comment() const override
	{
		return imageData_->image_comment();
	}

	void set_image_comment(const std::string& comment) override
	{
		imageData_->set_image_comment(comment);
	}
};






void exif_init(void)
{
#ifdef EXV_ENABLE_NLS
	bind_textdomain_codeset (EXV_PACKAGE, "UTF-8");
#endif

#ifdef EXV_ENABLE_BMFF
	Exiv2::enableBMFF(TRUE);
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
		return new _ExifDataProcessed(path, sidecar_path, modified_xmp);
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
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
	g_assert(dynamic_cast<_ExifDataProcessed *>(exif)); // this should not be called on ExifDataOriginal
	delete exif;
}

ExifData *exif_get_original(ExifData *exif)
{
	return exif->original();
}


ExifItem *exif_get_item(ExifData *exif, const gchar *key)
{
	try {
		Exiv2::Metadatum *item = NULL;
		try {
			Exiv2::ExifKey ekey(key);
			auto pos = exif->exifData().findKey(ekey);
			if (pos == exif->exifData().end()) return NULL;
			item = &*pos;
		}
		catch (Exiv2::AnyError& e) {
			try {
				Exiv2::IptcKey ekey(key);
				auto pos = exif->iptcData().findKey(ekey);
				if (pos == exif->iptcData().end()) return NULL;
				item = &*pos;
			}
			catch (Exiv2::AnyError& e) {
#if EXIV2_TEST_VERSION(0,16,0)
				Exiv2::XmpKey ekey(key);
				auto pos = exif->xmpData().findKey(ekey);
				if (pos == exif->xmpData().end()) return NULL;
				item = &*pos;
#endif
			}
		}
		return reinterpret_cast<ExifItem *>(item);
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
	}
}

ExifItem *exif_add_item(ExifData *exif, const gchar *key)
{
	try {
		Exiv2::Metadatum *item = NULL;
		try {
			Exiv2::ExifKey ekey(key);
			exif->exifData().add(ekey, NULL);
			auto pos = exif->exifData().end(); // a hack, there should be a better way to get the currently added item
			pos--;
			item = &*pos;
		}
		catch (Exiv2::AnyError& e) {
			try {
				Exiv2::IptcKey ekey(key);
				exif->iptcData().add(ekey, NULL);
				auto pos = exif->iptcData().end();
				pos--;
				item = &*pos;
			}
			catch (Exiv2::AnyError& e) {
#if EXIV2_TEST_VERSION(0,16,0)
				Exiv2::XmpKey ekey(key);
				exif->xmpData().add(ekey, NULL);
				auto pos = exif->xmpData().end();
				pos--;
				item = &*pos;
#endif
			}
		}
		return reinterpret_cast<ExifItem *>(item);
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
	}
}


ExifItem *exif_get_first_item(ExifData *exif)
{
	try {
		exif->exifIter = exif->exifData().begin();
		exif->iptcIter = exif->iptcData().begin();
#if EXIV2_TEST_VERSION(0,16,0)
		exif->xmpIter = exif->xmpData().begin();
#endif
		if (exif->exifIter != exif->exifData().end())
			{
			const Exiv2::Metadatum *item = &*exif->exifIter;
			exif->exifIter++;
			return (ExifItem *)item;
			}
		if (exif->iptcIter != exif->iptcData().end())
			{
			const Exiv2::Metadatum *item = &*exif->iptcIter;
			exif->iptcIter++;
			return (ExifItem *)item;
			}
#if EXIV2_TEST_VERSION(0,16,0)
		if (exif->xmpIter != exif->xmpData().end())
			{
			const Exiv2::Metadatum *item = &*exif->xmpIter;
			exif->xmpIter++;
			return (ExifItem *)item;
			}
#endif
		return NULL;

	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
	}
}

ExifItem *exif_get_next_item(ExifData *exif)
{
	try {
		if (exif->exifIter != exif->exifData().end())
			{
			const Exiv2::Metadatum *item = &*exif->exifIter;
			exif->exifIter++;
			return (ExifItem *)item;
		}
		if (exif->iptcIter != exif->iptcData().end())
			{
			const Exiv2::Metadatum *item = &*exif->iptcIter;
			exif->iptcIter++;
			return (ExifItem *)item;
		}
#if EXIV2_TEST_VERSION(0,16,0)
		if (exif->xmpIter != exif->xmpData().end())
			{
			const Exiv2::Metadatum *item = &*exif->xmpIter;
			exif->xmpIter++;
			return (ExifItem *)item;
		}
#endif
		return NULL;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
	}
}

char *exif_item_get_tag_name(ExifItem *item)
{
	try {
		if (!item) return NULL;
		return g_strdup((reinterpret_cast<Exiv2::Metadatum *>(item))->key().c_str());
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
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
		if (!item) return 0;
		auto md = reinterpret_cast<Exiv2::Metadatum *>(item);
		if (data_len) *data_len = md->size();
		auto data = static_cast<char *>(g_malloc(md->size()));
		long res = md->copy(reinterpret_cast<Exiv2::byte *>(data), Exiv2::littleEndian /* should not matter */);
		g_assert(res == md->size());
		return data;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
	}
}

char *exif_item_get_description(ExifItem *item)
{
	try {
		if (!item) return NULL;
		return utf8_validate_or_convert((reinterpret_cast<Exiv2::Metadatum *>(item))->tagLabel().c_str());
	}
	catch (std::exception& e) {
//		debug_exception(e);
		return NULL;
	}
}

/*
invalidTypeId, unsignedByte, asciiString, unsignedShort,
  unsignedLong, unsignedRational, signedByte, undefined,
  signedShort, signedLong, signedRational, string,
  date, time, comment, directory,
  xmpText, xmpAlt, xmpBag, xmpSeq,
  langAlt, lastTypeId
*/

static guint format_id_trans_tbl [] = {
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



guint exif_item_get_format_id(ExifItem *item)
{
	try {
		if (!item) return EXIF_FORMAT_UNKNOWN;
		guint id = (reinterpret_cast<Exiv2::Metadatum *>(item))->typeId();
		if (id >= (sizeof(format_id_trans_tbl) / sizeof(format_id_trans_tbl[0])) ) return EXIF_FORMAT_UNKNOWN;
		return format_id_trans_tbl[id];
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return EXIF_FORMAT_UNKNOWN;
	}
}

const char *exif_item_get_format_name(ExifItem *item, gboolean UNUSED(brief))
{
	try {
		if (!item) return NULL;
		return (reinterpret_cast<Exiv2::Metadatum *>(item))->typeName();
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
	}
}


gchar *exif_item_get_data_as_text(ExifItem *item)
{
	try {
		if (!item) return NULL;
		auto metadatum = reinterpret_cast<Exiv2::Metadatum *>(item);
#if EXIV2_TEST_VERSION(0,17,0)
		return utf8_validate_or_convert(metadatum->print().c_str());
#else
		std::stringstream str;
		Exiv2::Exifdatum *exifdatum;
		Exiv2::Iptcdatum *iptcdatum;
#if EXIV2_TEST_VERSION(0,16,0)
		Exiv2::Xmpdatum *xmpdatum;
#endif
		if ((exifdatum = dynamic_cast<Exiv2::Exifdatum *>(metadatum)))
			str << *exifdatum;
		else if ((iptcdatum = dynamic_cast<Exiv2::Iptcdatum *>(metadatum)))
			str << *iptcdatum;
#if EXIV2_TEST_VERSION(0,16,0)
		else if ((xmpdatum = dynamic_cast<Exiv2::Xmpdatum *>(metadatum)))
			str << *xmpdatum;
#endif

		return utf8_validate_or_convert(str.str().c_str());
#endif
	}
	catch (Exiv2::AnyError& e) {
		return NULL;
	}
}

gchar *exif_item_get_string(ExifItem *item, int idx)
{
	try {
		if (!item) return NULL;
		auto em = reinterpret_cast<Exiv2::Metadatum *>(item);
#if EXIV2_TEST_VERSION(0,16,0)
		std::string str = em->toString(idx);
#else
		std::string str = em->toString(); /**< @FIXME ?? */
#endif
		if (idx == 0 && str == "") str = em->toString();
		if (str.length() > 5 && str.substr(0, 5) == "lang=")
			{
			std::string::size_type pos = str.find_first_of(' ');
			if (pos != std::string::npos) str = str.substr(pos+1);
			}

		return utf8_validate_or_convert(str.c_str());
	}
	catch (Exiv2::AnyError& e) {
		return NULL;
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
		if (!item) return NULL;
		if (n >= exif_item_get_elements(item)) return NULL;
		Exiv2::Rational v = (reinterpret_cast<Exiv2::Metadatum *>(item))->toRational(n);
		static ExifRational ret;
		ret.num = v.first;
		ret.den = v.second;
		if (sign) *sign = ((reinterpret_cast<Exiv2::Metadatum *>(item))->typeId() == Exiv2::signedRational);
		return &ret;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
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
#if EXIV2_TEST_VERSION(0,16,0)
				Exiv2::XmpKey xkey(key);
				return utf8_validate_or_convert(xkey.tagLabel().c_str());
#endif
			}
			catch (Exiv2::AnyError& e) {
				debug_exception(e);
				return NULL;
			}
		}
	}
	return NULL;
}

static const AltKey *find_alt_key(const gchar *xmp_key)
{
	gint i = 0;

	while (alt_keys[i].xmp_key)
		{
		if (strcmp(xmp_key, alt_keys[i].xmp_key) == 0) return &alt_keys[i];
		i++;
		}
	return NULL;
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
#if EXIV2_TEST_VERSION(0,16,0)
			try
#endif
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
#if EXIV2_TEST_VERSION(0,16,0)
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
#endif
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
#if !EXIV2_TEST_VERSION(0,17,0)
	    TRUE || /* no conversion support */
#endif
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
#if EXIV2_TEST_VERSION(0,16,0)
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
#endif
		/* read as a single entry */
		std::string str;

		if (format == METADATA_FORMATTED)
			{
#if EXIV2_TEST_VERSION(0,17,0)
			str = item.print(
#if EXIV2_TEST_VERSION(0,18,0)
					metadata
#endif
					);
#else
			std::stringstream stream;
			Exiv2::Exifdatum *exifdatum;
			Exiv2::Iptcdatum *iptcdatum;
#if EXIV2_TEST_VERSION(0,16,0)
			Exiv2::Xmpdatum *xmpdatum;
#endif
			if ((exifdatum = dynamic_cast<Exiv2::Exifdatum *>(&item)))
				stream << *exifdatum;
			else if ((iptcdatum = dynamic_cast<Exiv2::Iptcdatum *>(&item)))
				stream << *iptcdatum;
#if EXIV2_TEST_VERSION(0,16,0)
			else if ((xmpdatum = dynamic_cast<Exiv2::Xmpdatum *>(&item)))
				stream << *xmpdatum;
#endif
			str = stream.str();
#endif
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
#if EXIV2_TEST_VERSION(0,16,0)
		}
	else
		{
		/* read as a list */
		gint i;
		for (i = 0; i < item.count(); i++)
			list = g_list_append(list, utf8_validate_or_convert(item.toString(i).c_str()));
		}
#endif
	return list;
}

static GList *exif_get_metadata_simple(ExifData *exif, const gchar *key, MetadataFormat format)
{
	GList *list = NULL;
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
						list = exif_add_value_to_glist(list, *pos, format, NULL);
					++pos;
					}

			}
			catch (Exiv2::AnyError& e) {
#if EXIV2_TEST_VERSION(0,16,0)
				Exiv2::XmpKey ekey(key);
				auto pos = exif->xmpData().findKey(ekey);
				if (pos != exif->xmpData().end())
					list = exif_add_value_to_glist(list, *pos, format, NULL);
#endif
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
	GList *list = NULL;

	if (!key) return NULL;

	if (format == METADATA_FORMATTED)
		{
		gchar *text;
		gint key_valid;
		text = exif_get_formatted_by_key(exif, key, &key_valid);
		if (key_valid) return g_list_append(NULL, text);
		}

	list = exif_get_metadata_simple(exif, key, format);

	/* the following code can be ifdefed out as soon as Exiv2 supports it */
	if (!list)
		{
		const AltKey *alt_key = find_alt_key(key);
		if (alt_key && alt_key->iptc_key)
			list = exif_get_metadata_simple(exif, alt_key->iptc_key, format);

#if !EXIV2_TEST_VERSION(0,17,0)
		/* with older Exiv2 versions exif is not synced */
		if (!list && alt_key && alt_key->exif_key)
			list = exif_get_metadata_simple(exif, alt_key->exif_key, format);
#endif
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


#if EXIV2_TEST_VERSION(0,17,90)

guchar *exif_get_preview(ExifData *exif, guint *data_len, gint requested_width, gint requested_height)
{
	if (!exif) return NULL;

	if (!exif->image()) return NULL;

	std::string const path = exif->image()->io().path();
	/* given image pathname, first do simple (and fast) file extension test */
	gboolean is_raw = filter_file_class(path.c_str(), FORMAT_CLASS_RAWIMAGE);

	if (!is_raw && requested_width == 0) return NULL;

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
					if (pos->width_ < static_cast<uint32_t>(requested_width) || pos->height_ < static_cast<uint32_t>(requested_height)) return NULL;
					}
				}

			Exiv2::PreviewImage image = pm.getPreviewImage(*pos);

			Exiv2::DataBuf buf = image.copy();

#if EXIV2_TEST_VERSION(0,28,0)
                       *data_len = buf.size();
                       auto b = buf.data();
                       buf.reset();
                       return b;
#else
			std::pair<Exiv2::byte*, long> p = buf.release();

			*data_len = p.second;
			return p.first;
#endif
			}
		return NULL;
	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
		return NULL;
	}
}

void exif_free_preview(guchar *buf)
{
	delete[] static_cast<Exiv2::byte*>(buf);
}
#endif

#if !EXIV2_TEST_VERSION(0,17,90)

/* This is a dirty hack to support raw file preview, bassed on
tiffparse.cpp from Exiv2 examples */

class RawFile {
	public:

	RawFile(Exiv2::BasicIo &io);
	~RawFile();

	const Exiv2::Value *find(uint16_t tag, uint16_t group);

	unsigned long preview_offset();

	private:
	int type;
	Exiv2::TiffComponent::AutoPtr rootDir;
	Exiv2::BasicIo &io_;
	const Exiv2::byte *map_data;
	size_t map_len;
	unsigned long offset;
};

typedef struct _UnmapData UnmapData;
struct _UnmapData
{
	guchar *ptr;
	guchar *map_data;
	size_t map_len;
};

static GList *exif_unmap_list = 0;

guchar *exif_get_preview(ExifData *exif, guint *data_len, gint requested_width, gint requested_height)
{
	unsigned long offset;

	if (!exif) return NULL;
	if (!exif->image()) return NULL;

	std::string const path = exif->image()->io().path();

	/* given image pathname, first do simple (and fast) file extension test */
	if (!filter_file_class(path.c_str(), FORMAT_CLASS_RAWIMAGE)) return NULL;

	try {
		struct stat st;
		guchar *map_data;
		size_t map_len;
		UnmapData *ud;
		int fd;

		RawFile rf(exif->image()->io());
		offset = rf.preview_offset();
		DEBUG_1("%s: offset %lu", path.c_str(), offset);

		fd = open(path.c_str(), O_RDONLY);
		if (fd == -1)
			{
			return NULL;
			}

		if (fstat(fd, &st) == -1)
			{
			close(fd);
			return NULL;
			}
		map_len = st.st_size;
		map_data = (guchar *) mmap(0, map_len, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		if (map_data == MAP_FAILED)
			{
			return NULL;
			}
		*data_len = map_len - offset;
		ud = g_new(UnmapData, 1);
		ud->ptr = map_data + offset;
		ud->map_data = map_data;
		ud->map_len = map_len;

		exif_unmap_list = g_list_prepend(exif_unmap_list, ud);
		return ud->ptr;

	}
	catch (Exiv2::AnyError& e) {
		debug_exception(e);
	}
	return NULL;

}

void exif_free_preview(guchar *buf)
{
	GList *work = exif_unmap_list;

	while (work)
		{
		UnmapData *ud = (UnmapData *)work->data;
		if (ud->ptr == buf)
			{
			munmap(ud->map_data, ud->map_len);
			exif_unmap_list = g_list_remove_link(exif_unmap_list, work);
			g_free(ud);
			return;
			}
		work = work->next;
		}
	g_assert_not_reached();
}

using namespace Exiv2;

RawFile::RawFile(BasicIo &io) : io_(io), map_data(NULL), map_len(0), offset(0)
{
/*
	struct stat st;
	if (fstat(fd, &st) == -1)
		{
		throw Error(14);
		}
	map_len = st.st_size;
	map_data = (Exiv2::byte *) mmap(0, map_len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map_data == MAP_FAILED)
		{
		throw Error(14);
		}
*/
        if (io.open() != 0) {
            throw Error(9, io.path(), strError());
        }

        map_data = io.mmap();
        map_len = io.size();


	type = Exiv2::ImageFactory::getType(map_data, map_len);

#if EXIV2_TEST_VERSION(0,16,0)
	std::unique_ptr<TiffHeaderBase> tiffHeader;
#else
	std::unique_ptr<TiffHeade2> tiffHeader;
#endif
	std::unique_ptr<Cr2Header> cr2Header;

	switch (type) {
		case Exiv2::ImageType::tiff:
			tiffHeader = std::make_unique<TiffHeade2>();
			break;
		case Exiv2::ImageType::cr2:
			cr2Header = std::make_unique<Cr2Header>();
			break;
#if EXIV2_TEST_VERSION(0,16,0)
		case Exiv2::ImageType::orf:
			tiffHeader = std::make_unique<OrfHeader>();
			break;
#endif
#if EXIV2_TEST_VERSION(0,13,0)
		case Exiv2::ImageType::raf:
			if (map_len < 84 + 4) throw Error(14);
			offset = getULong(map_data + 84, bigEndian);
			return;
#endif
		case Exiv2::ImageType::crw:
			{
			// Parse the image, starting with a CIFF header component
			auto parseTree = std::make_unique<Exiv2::CiffHeader>();
			parseTree->read(map_data, map_len);
			CiffComponent *entry = parseTree->findComponent(0x2007, 0);
			if (entry) offset =  entry->pData() - map_data;
			return;
			}

		default:
			throw Error(3, "RAW");
	}

	// process tiff-like formats

	TiffCompFactoryFct createFct = TiffCreator::create;

	rootDir = createFct(Tag::root, Group::none);
	if (0 == rootDir.get()) {
		throw Error(1, "No root element defined in TIFF structure");
	}

	if (tiffHeader)
		{
		if (!tiffHeader->read(map_data, map_len)) throw Error(3, "TIFF");
#if EXIV2_TEST_VERSION(0,16,0)
		rootDir->setStart(map_data + tiffHeader->offset());
#else
		rootDir->setStart(map_data + tiffHeader->ifdOffset());
#endif
		}

	if (cr2Header)
		{
		rootDir->setStart(map_data + cr2Header->offset());
		}

	TiffRwState::AutoPtr state(new TiffRwState(tiffHeader ? tiffHeader->byteOrder() : littleEndian, 0, createFct));

	TiffReader reader(map_data,
			  map_len,
			  rootDir.get(),
			  state);

	rootDir->accept(reader);
}

RawFile::~RawFile(void)
{
	io_.munmap();
	io_.close();
}

const Value * RawFile::find(uint16_t tag, uint16_t group)
{
	TiffFinder finder(tag, group);
	rootDir->accept(finder);
	TiffEntryBase* te = dynamic_cast<TiffEntryBase*>(finder.result());
	if (te)
		{
		DEBUG_1("(tag: %04x %04x) ", tag, group);
		return te->pValue();
		}
	else
		return NULL;
}

unsigned long RawFile::preview_offset(void)
{
	const Value *val;
	if (offset) return offset;

	if (type == Exiv2::ImageType::cr2)
		{
		val = find(0x111, Group::ifd0);
#if EXIV2_TEST_VERSION(0,28,0)
		if (val) return val->toInt64();
#else
		if (val) return val->tolong();
#endif
		return 0;
		}

	val = find(0x201, Group::sub0_0);
#if EXIV2_TEST_VERSION(0,28,0)
	if (val) return val->toInt64();
#else
	if (val) return val->tolong();
#endif

	val = find(0x201, Group::ifd0);
#if EXIV2_TEST_VERSION(0,28,0)
	if (val) return val->toInt64();
#else
	if (val) return val->tolong();
#endif

	val = find(0x201, Group::ignr); // for PEF files, originally it was probably ifd2
#if EXIV2_TEST_VERSION(0,28,0)
	if (val) return val->toInt64();
#else
	if (val) return val->tolong();
#endif

	val = find(0x111, Group::sub0_1); // dng
#if EXIV2_TEST_VERSION(0,28,0)
	if (val) return val->toInt64();
#else
	if (val) return val->tolong();
#endif

	return 0;
}


#endif


#endif
/* HAVE_EXIV2 */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
