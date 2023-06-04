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
#include "filefilter.h"

#include "cache.h"
#include "ui-fileops.h"
#include "rcfile.h"

/*
 *-----------------------------------------------------------------------------
 * file filtering
 *-----------------------------------------------------------------------------
 */

static GList *filter_list = NULL;
static GList *extension_list = NULL;
static GList *sidecar_ext_list = NULL;

static GList *file_class_extension_list[FILE_FORMAT_CLASSES];

static GList *file_writable_list = NULL; /* writable files */
static GList *file_sidecar_list = NULL; /* files with allowed sidecar */


static FilterEntry *filter_entry_new(const gchar *key, const gchar *description,
				     const gchar *extensions, FileFormatClass file_class,
				     gboolean writable, gboolean allow_sidecar, gboolean enabled)
{
	FilterEntry *fe;

	fe = g_new0(FilterEntry, 1);
	fe->key = g_strdup(key);
	fe->description = g_strdup(description);
	fe->extensions = g_strdup(extensions);
	fe->enabled = enabled;
	fe->file_class = file_class;
	fe->writable = writable;
	fe->allow_sidecar = allow_sidecar;

	return fe;
}

static void filter_entry_free(FilterEntry *fe)
{
	if (!fe) return;

	g_free(fe->key);
	g_free(fe->description);
	g_free(fe->extensions);
	g_free(fe);
}

GList *filter_get_list(void)
{
	return filter_list;
}

void filter_remove_entry(FilterEntry *fe)
{
	if (!g_list_find(filter_list, fe)) return;

	filter_list = g_list_remove(filter_list, fe);
	filter_entry_free(fe);
}

static FilterEntry *filter_get_by_key(const gchar *key)
{
	GList *work;

	if (!key) return NULL;

	work = filter_list;
	while (work)
		{
		auto fe = static_cast<FilterEntry *>(work->data);
		work = work->next;

		if (strcmp(fe->key, key) == 0) return fe;
		}

	return NULL;
}

static gboolean filter_key_exists(const gchar *key)
{
	return (filter_get_by_key(key) != NULL);
}

void filter_add(const gchar *key, const gchar *description, const gchar *extensions, FileFormatClass file_class, gboolean writable, gboolean allow_sidecar, gboolean enabled)
{
	filter_list = g_list_append(filter_list, filter_entry_new(key, description, extensions, file_class, writable, allow_sidecar, enabled));
}

void filter_add_unique(const gchar *description, const gchar *extensions, FileFormatClass file_class, gboolean writable, gboolean allow_sidecar, gboolean enabled)
{
	gchar *key;
	guint n;

	key = g_strdup("user0");
	n = 1;
	while (filter_key_exists(key))
		{
		g_free(key);
		if (n > 999) return;
		key = g_strdup_printf("user%d", n);
		n++;
		}

	filter_add(key, description, extensions, file_class, writable, allow_sidecar, enabled);
	g_free(key);
}

static void filter_add_if_missing(const gchar *key, const gchar *description, const gchar *extensions, FileFormatClass file_class, gboolean writable, gboolean allow_sidecar, gboolean enabled)
{
	GList *work;

	if (!key) return;

	work = filter_list;
	while (work)
		{
		auto fe = static_cast<FilterEntry *>(work->data);
		work = work->next;
		if (fe->key && strcmp(fe->key, key) == 0)
			{
			if (fe->file_class == FORMAT_CLASS_UNKNOWN)
				fe->file_class = file_class;	/* for compatibility */

			if (fe->writable && fe->allow_sidecar)
				{
				fe->writable = writable;
				fe->allow_sidecar = allow_sidecar;
				}
			return;
			}
		}

	filter_add(key, description, extensions, file_class, writable, allow_sidecar, enabled);
}

void filter_reset(void)
{
	GList *work;

	work = filter_list;
	while (work)
		{
		auto fe = static_cast<FilterEntry *>(work->data);
		work = work->next;
		filter_entry_free(fe);
		}

	g_list_free(filter_list);
	filter_list = NULL;
}

void filter_add_defaults(void)
{
	/* formats supported by custom loaders */
	filter_add_if_missing("dds", "DirectDraw Surface", ".dds", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);
#ifdef HAVE_PDF
	filter_add_if_missing("pdf", "Portable Document Format", ".pdf", FORMAT_CLASS_DOCUMENT, FALSE, FALSE, TRUE);
#endif
#ifdef HAVE_HEIF
	filter_add_if_missing("heif/avif", "HEIF/AVIF Image", ".heif;.heic;.avif", FORMAT_CLASS_IMAGE, FALSE, TRUE, TRUE);
#endif
#ifdef HAVE_WEBP
	filter_add_if_missing("webp", "WebP Format", ".webp", FORMAT_CLASS_IMAGE, TRUE, FALSE, TRUE);
#endif
#ifdef HAVE_DJVU
	filter_add_if_missing("djvu", "DjVu Format", ".djvu;.djv", FORMAT_CLASS_DOCUMENT, FALSE, FALSE, TRUE);
#endif
#ifdef HAVE_J2K
	filter_add_if_missing("jp2", "JPEG 2000", ".jp2", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);
#endif
#ifdef HAVE_ARCHIVE
	filter_add_if_missing("zip", "Archive files", ".zip;.rar;.tar;.tar.gz;.tar.bz2;.tar.xz;.tgz;.tbz;.txz;.cbr;.cbz;.gz;.bz2;.xz;.lzh;.lza;.7z", FORMAT_CLASS_ARCHIVE, FALSE, FALSE, TRUE);
#endif
	filter_add_if_missing("scr", "ZX Spectrum screen Format", ".scr", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);
	filter_add_if_missing("psd", "Adobe Photoshop Document", ".psd", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);
	filter_add_if_missing("apng", "Animated Portable Network Graphic", ".apng", FORMAT_CLASS_IMAGE, FALSE, FALSE, TRUE);

	/* formats supported by gdk-pixbuf */
	GSList *list, *work;

	list = gdk_pixbuf_get_formats();
	work = list;
	while (work)
		{
		GdkPixbufFormat *format;
		gchar *name;
		gchar *desc;
		gchar **extensions;
		GString *filter = NULL;
		guint i;

		format = static_cast<GdkPixbufFormat *>(work->data);
		work = work->next;

		name = gdk_pixbuf_format_get_name(format);

		if (strcmp(name, "Digital camera RAW") == 0)
			{
			DEBUG_1("Skipped '%s' from loader", name);
			g_free(name);
			continue;
			}

		desc = gdk_pixbuf_format_get_description(format);
		extensions = gdk_pixbuf_format_get_extensions(format);

		i = 0;
		while (extensions[i])
			{
			if (!filter)
				{
				filter = g_string_new(".");
				filter = g_string_append(filter, extensions[i]);
				}
			else
				{
				filter = g_string_append(filter, ";.");
				filter = g_string_append(filter, extensions[i]);
				}
			i++;
			}

		DEBUG_1("loader reported [%s] [%s] [%s]", name, desc, filter->str);

		filter_add_if_missing(name, desc, filter->str, FORMAT_CLASS_IMAGE, TRUE, FALSE, TRUE);

		g_free(name);
		g_free(desc);
		g_strfreev(extensions);
		g_string_free(filter, TRUE);
		}
	g_slist_free(list);

	/* add defaults even if gdk-pixbuf does not have them, but disabled */
	filter_add_if_missing("jpeg", "JPEG group", ".jpg;.jpeg;.jpe", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("png", "Portable Network Graphic", ".png", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("tiff", "Tiff", ".tif;.tiff", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("pnm", "Packed Pixel formats", ".pbm;.pgm;.pnm;.ppm", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("gif", "Graphics Interchange Format", ".gif", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("xbm", "X bitmap", ".xbm", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("xpm", "X pixmap", ".xpm", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("bmp", "Bitmap", ".bmp", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("ico", "Icon file", ".ico;.cur", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("ras", "Raster", ".ras", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);
	filter_add_if_missing("svg", "Scalable Vector Graphics", ".svg", FORMAT_CLASS_IMAGE, TRUE, FALSE, FALSE);

	/* special formats for stereo */
	filter_add_if_missing("jps", "Stereo side-by-side jpeg", ".jps", FORMAT_CLASS_IMAGE, TRUE, FALSE, TRUE);
	filter_add_if_missing("mpo", "Stereo multi-image jpeg", ".mpo", FORMAT_CLASS_IMAGE, FALSE, TRUE, TRUE);

	/* non-image files that might be desirable to show */
	filter_add_if_missing("xmp", "XMP sidecar", ".xmp", FORMAT_CLASS_META, TRUE, FALSE, TRUE);
	filter_add_if_missing("meta", "GQview legacy metadata", GQ_CACHE_EXT_METADATA, FORMAT_CLASS_META, TRUE, FALSE, TRUE);
	filter_add_if_missing("gqv", GQ_APPNAME " image collection", GQ_COLLECTION_EXT, FORMAT_CLASS_COLLECTION, FALSE, TRUE, TRUE);
	filter_add_if_missing("pto", "Panorama script file", ".pto", FORMAT_CLASS_META, FALSE, FALSE, TRUE);

	/* These are the raw camera formats with embedded jpeg/exif.
	 * (see format-raw.cc and/or exiv2.cc)
	 */
	filter_add_if_missing("arw", "Sony raw format", ".arw;.srf;.sr2", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("crw", "Canon raw format", ".crw;.cr2;.cr3", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("kdc", "Kodak raw format", ".kdc;.dcr;.k25", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("raf", "Fujifilm raw format", ".raf", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("mef", "Mamiya raw format", ".mef;.mos", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("mrw", "Minolta raw format", ".mrw", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("nef", "Nikon raw format", ".nef", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("orf", "Olympus raw format", ".orf", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("pef", "Pentax or Samsung raw format", ".pef;.ptx", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("dng", "Adobe Digital Negative raw format", ".dng", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("x3f", "Sigma raw format", ".x3f", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("raw", "Panasonic raw format", ".raw", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("r3d", "Red raw format", ".r3d", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("3fr", "Hasselblad raw format", ".3fr", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("erf", "Epson raw format", ".erf", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("srw", "Samsung raw format", ".srw", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);
	filter_add_if_missing("rw2", "Panasonic raw format", ".rw2", FORMAT_CLASS_RAWIMAGE, FALSE, TRUE, TRUE);

	/* video files */
	filter_add_if_missing("mp4", "MP4 video file", ".mp4;.m4v;.3gp;.3g2", FORMAT_CLASS_VIDEO, FALSE, FALSE, FALSE);
	filter_add_if_missing("3gp", "3GP video file", ".3gp;.3g2", FORMAT_CLASS_VIDEO, FALSE, FALSE, FALSE);
	filter_add_if_missing("mov", "MOV video file", ".mov;.qt", FORMAT_CLASS_VIDEO, FALSE, FALSE, FALSE);
	filter_add_if_missing("avi", "AVI video file", ".avi", FORMAT_CLASS_VIDEO, FALSE, FALSE, FALSE);
	filter_add_if_missing("mpg", "MPG video file", ".mpg;.mpeg;.mts;.m2ts", FORMAT_CLASS_VIDEO, FALSE, FALSE, FALSE);
	filter_add_if_missing("mkv", "Matroska video file", ".mkv;.webm", FORMAT_CLASS_VIDEO, FALSE, FALSE, FALSE);
	filter_add_if_missing("wmv", "Windows Media Video file", ".wmv;.asf", FORMAT_CLASS_VIDEO, FALSE, FALSE, FALSE);
	filter_add_if_missing("flv", "Flash Video file", ".flv", FORMAT_CLASS_VIDEO, FALSE, FALSE, FALSE);
}

GList *filter_to_list(const gchar *extensions)
{
	GList *list = NULL;
	const gchar *p;

	if (!extensions) return NULL;

	p = extensions;
	while (*p != '\0')
		{
		const gchar *b;
		gchar *ext;
		gint file_class = -1;
		guint l = 0;

		b = p;
		while (*p != '\0' && *p != ';')
			{
			p++;
			l++;
			}

		ext = g_strndup(b, l);

		if (g_ascii_strcasecmp(ext, "%image") == 0) file_class = FORMAT_CLASS_IMAGE;
		else if (g_ascii_strcasecmp(ext, "%raw") == 0) file_class = FORMAT_CLASS_RAWIMAGE;
		else if (g_ascii_strcasecmp(ext, "%meta") == 0) file_class = FORMAT_CLASS_META;
		else if (g_ascii_strcasecmp(ext, "%unknown") == 0) file_class = FORMAT_CLASS_UNKNOWN;

		if (file_class == -1)
			{
			list = g_list_append(list, ext);
			}
		else
			{
			list = g_list_concat(list, string_list_copy(file_class_extension_list[file_class]));
			g_free(ext);
			}

		if (*p == ';') p++;
		}

	return list;
}

static gint filter_sort_ext_len_cb(gconstpointer a, gconstpointer b)
{
	auto sa = (gchar *)a;
	auto sb = (gchar *)b;

	gint len_a = strlen(sa);
	gint len_b = strlen(sb);

	if (len_a > len_b) return -1;
	if (len_a < len_b) return 1;
	return 0;
}


void filter_rebuild(void)
{
	GList *work;
	guint i;

	string_list_free(extension_list);
	extension_list = NULL;

	string_list_free(file_writable_list);
	file_writable_list = NULL;

	string_list_free(file_sidecar_list);
	file_sidecar_list = NULL;

	for (i = 0; i < FILE_FORMAT_CLASSES; i++)
		{
		string_list_free(file_class_extension_list[i]);
		file_class_extension_list[i] = NULL;
		}

	work = filter_list;
	while (work)
		{
		FilterEntry *fe;

		fe = static_cast<FilterEntry *>(work->data);
		work = work->next;

		if (fe->enabled)
			{
			GList *ext;

			ext = filter_to_list(fe->extensions);
			if (ext) extension_list = g_list_concat(extension_list, ext);

			if (fe->file_class < FILE_FORMAT_CLASSES)
				{
				ext = filter_to_list(fe->extensions);
				if (ext) file_class_extension_list[fe->file_class] = g_list_concat(file_class_extension_list[fe->file_class], ext);
				}
			else
				{
				log_printf("WARNING: invalid file class %d\n", fe->file_class);
				}

			if (fe->writable)
				{
				ext = filter_to_list(fe->extensions);
				if (ext) file_writable_list = g_list_concat(file_writable_list, ext);
				}

			if (fe->allow_sidecar)
				{
				ext = filter_to_list(fe->extensions);
				if (ext) file_sidecar_list = g_list_concat(file_sidecar_list, ext);
				}

			}
		}

	/* make sure registered_extension_from_path finds the longer match first */
	extension_list = g_list_sort(extension_list, filter_sort_ext_len_cb);
	sidecar_ext_parse(options->sidecar.ext); /* this must be updated after changed file extensions */
}

/* return the extension part of the name or NULL */
static const gchar *filter_name_find(GList *filter, const gchar *name)
{
	GList *work;
	guint ln;

	ln = strlen(name);
	work = filter;
	while (work)
		{
		auto filter = static_cast<gchar *>(work->data);
		guint lf = strlen(filter);

		if (ln >= lf)
			{
			/** @FIXME utf8 */
			if (g_ascii_strncasecmp(name + ln - lf, filter, lf) == 0) return name + ln - lf;
			}
		work = work->next;
		}

	return NULL;
}
const gchar *registered_extension_from_path(const gchar *name)
{
	return filter_name_find(extension_list, name);
}

gboolean filter_name_exists(const gchar *name)
{
	if (!extension_list || options->file_filter.disable) return TRUE;

	return !!filter_name_find(extension_list, name);
}

gboolean filter_file_class(const gchar *name, FileFormatClass file_class)
{
	if (file_class >= FILE_FORMAT_CLASSES)
		{
		log_printf("WARNING: invalid file class %d\n", file_class);
		return FALSE;
		}

	return !!filter_name_find(file_class_extension_list[file_class], name);
}

FileFormatClass filter_file_get_class(const gchar *name)
{
	if (filter_file_class(name, FORMAT_CLASS_IMAGE)) return FORMAT_CLASS_IMAGE;
	if (filter_file_class(name, FORMAT_CLASS_RAWIMAGE)) return FORMAT_CLASS_RAWIMAGE;
	if (filter_file_class(name, FORMAT_CLASS_META)) return FORMAT_CLASS_META;
	if (filter_file_class(name, FORMAT_CLASS_VIDEO)) return FORMAT_CLASS_VIDEO;
	if (filter_file_class(name, FORMAT_CLASS_COLLECTION)) return FORMAT_CLASS_COLLECTION;
	if (filter_file_class(name, FORMAT_CLASS_DOCUMENT)) return FORMAT_CLASS_DOCUMENT;
	if (filter_file_class(name, FORMAT_CLASS_ARCHIVE)) return FORMAT_CLASS_ARCHIVE;
	return FORMAT_CLASS_UNKNOWN;
}

gboolean filter_name_is_writable(const gchar *name)
{
	return !!filter_name_find(file_writable_list, name);
}

gboolean filter_name_allow_sidecar(const gchar *name)
{
	return !!filter_name_find(file_sidecar_list, name);
}

void filter_write_list(GString *outstr, gint indent)
{
	GList *work;

	WRITE_NL(); WRITE_STRING("<filter>");
	indent++;

	work = filter_list;
	while (work)
		{
		auto fe = static_cast<FilterEntry *>(work->data);
		work = work->next;

		WRITE_NL(); WRITE_STRING("<file_type ");
		WRITE_CHAR(*fe, key);
		WRITE_BOOL(*fe, enabled);
		WRITE_CHAR(*fe, extensions);
		WRITE_CHAR(*fe, description);
		WRITE_UINT(*fe, file_class);
		WRITE_BOOL(*fe, writable);
		WRITE_BOOL(*fe, allow_sidecar);
		WRITE_STRING("/>");
		}
	indent--;
	WRITE_NL(); WRITE_STRING("</filter>");
}

void filter_load_file_type(const gchar **attribute_names, const gchar **attribute_values)
{
	FilterEntry fe;
	FilterEntry *old_fe;
	memset(&fe, 0, sizeof(fe));
	while (*attribute_names)
		{
		const gchar *option = *attribute_names++;
		const gchar *value = *attribute_values++;

		if (READ_CHAR(fe, key)) continue;
		if (READ_BOOL(fe, enabled)) continue;
		if (READ_CHAR(fe, extensions)) continue;
		if (READ_CHAR(fe, description)) continue;
		if (READ_UINT_ENUM(fe, file_class)) continue;
		if (READ_BOOL(fe, writable)) continue;
		if (READ_BOOL(fe, allow_sidecar)) continue;

		log_printf("unknown attribute %s = %s\n", option, value);
		}
	if (fe.file_class >= FILE_FORMAT_CLASSES) fe.file_class = FORMAT_CLASS_UNKNOWN;

	if (fe.key && fe.key[0] != 0)
		{
		old_fe = filter_get_by_key(fe.key);

		if (old_fe != NULL) filter_remove_entry(old_fe);
		filter_add(fe.key, fe.description, fe.extensions, fe.file_class, fe.writable, fe.allow_sidecar, fe.enabled);
		}
	g_free(fe.key);
	g_free(fe.extensions);
	g_free(fe.description);
}


/*
 *-----------------------------------------------------------------------------
 * sidecar extension list
 *-----------------------------------------------------------------------------
 */

GList *sidecar_ext_get_list(void)
{
	return sidecar_ext_list;
}

static void sidecar_ext_free_list(void)
{
	GList *work;

	work = sidecar_ext_list;
	while (work)
		{
		auto ext = static_cast<gchar *>(work->data);
		work = work->next;
		g_free(ext);
		}
	g_list_free(sidecar_ext_list);
	sidecar_ext_list = NULL;
}

void sidecar_ext_parse(const gchar *text)
{
	sidecar_ext_free_list();
	if (text == NULL) return;

	sidecar_ext_list = filter_to_list(text);
}


/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
