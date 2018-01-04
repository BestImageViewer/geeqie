/*
 * Copyright (C) 2004 John Ellis
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
#include "print.h"

#include "filedata.h"
#include "filefilter.h"
#include "image.h"
#include "image-load.h"
#include "pixbuf_util.h"
#include "thumb.h"
#include "utilops.h"
#include "ui_bookmark.h"
#include "ui_menu.h"
#include "ui_misc.h"
#include "ui_utildlg.h"
#include "ui_fileops.h"
#include "ui_spinner.h"
#include "ui_tabcomp.h"

#include <locale.h>
#include <signal.h>
#include <glib/gprintf.h>

#define PRINT_LPR_COMMAND "lpr"
#define PRINT_LPR_CUSTOM  "lpr -P %s"
#define PRINT_LPR_QUERY   "lpstat -p"

#define PRINT_DLG_WIDTH 600
#define PRINT_DLG_HEIGHT 400

#define PRINT_DLG_PREVIEW_WIDTH 270
#define PRINT_DLG_PREVIEW_HEIGHT -1

/* these are in point units */
#define PRINT_MIN_WIDTH 100
#define PRINT_MIN_HEIGHT 100
#define PRINT_MAX_WIDTH 4000
#define PRINT_MAX_HEIGHT 4000

#define PRINT_MARGIN_DEFAULT 36

#define PRINT_PROOF_MIN_SIZE 8
#define PRINT_PROOF_MAX_SIZE 720
#define PRINT_PROOF_DEFAULT_SIZE 144
#define PRINT_PROOF_MARGIN 5

/* default page size */
#define PAGE_LAYOUT_WIDTH 850
#define PAGE_LAYOUT_HEIGHT 1100

/* preview uses 1 pixel = PRINT_PREVIEW_SCALE points */
#define PRINT_PREVIEW_SCALE 4

/* default dpi to use for printing ps image data */
#define PRINT_PS_DPI_DEFAULT 300.0
#define PRINT_PS_DPI_MIN 150.0
/* method to use when scaling down image data */
#define PRINT_PS_MAX_INTERP GDK_INTERP_BILINEAR
/* color to use as mask when printing transparent images */
#define PRINT_PS_MASK_R 255
#define PRINT_PS_MASK_G 255
#define PRINT_PS_MASK_B 255

/* padding between objects */
#define PRINT_TEXT_PADDING 3.0

/* locale for postscript portability */
#define POSTSCRIPT_LOCALE "C"


/* group and keys for saving prefs */
#define PRINT_PREF_GROUP	"print_settings"

#define PRINT_PREF_SAVE		"save_settings"

#define PRINT_PREF_OUTPUT	"output"
#define PRINT_PREF_FORMAT	"format"
#define PRINT_PREF_DPI		"dpi"
#define PRINT_PREF_UNITS	"units"
#define PRINT_PREF_SIZE		"size"
#define PRINT_PREF_ORIENTATION	"orientation"

#define PRINT_PREF_CUSTOM_WIDTH		"custom_width"
#define PRINT_PREF_CUSTOM_HEIGHT	"custom_height"
#define PRINT_PREF_MARGIN_LEFT		"margin_left"
#define PRINT_PREF_MARGIN_RIGHT		"margin_right"
#define PRINT_PREF_MARGIN_TOP		"margin_top"
#define PRINT_PREF_MARGIN_BOTTOM	"margin_bottom"
#define PRINT_PREF_PROOF_WIDTH		"proof_width"
#define PRINT_PREF_PROOF_HEIGHT		"proof_height"

#define PRINT_PREF_PRINTERC	"custom_printer"

#define PRINT_PREF_TEXT			"text"
#define PRINT_PREF_TEXTSIZE		"textsize"
#define PRINT_PREF_TEXTCOLOR_R		"textcolor_r"
#define PRINT_PREF_TEXTCOLOR_G		"textcolor_g"
#define PRINT_PREF_TEXTCOLOR_B		"textcolor_b"

#define PRINT_PREF_SOURCE		"source"
#define PRINT_PREF_LAYOUT		"layout"

#define PRINT_PREF_IMAGE_SCALE		"image_scale"

typedef enum {
	PRINT_SOURCE_IMAGE = 0,
	PRINT_SOURCE_SELECTION,
	PRINT_SOURCE_ALL,
	PRINT_SOURCE_COUNT
} PrintSource;

const gchar *print_source_text[] = {
	N_("Image"),
	N_("Selection"),
	N_("All"),
	NULL
};

typedef enum {
	PRINT_LAYOUT_IMAGE = 0,
	PRINT_LAYOUT_PROOF,
	PRINT_LAYOUT_COUNT
} PrintLayout;

const gchar *print_layout_text[] = {
	N_("One image per page"),
	N_("Proof sheet"),
	NULL
};

typedef enum {
	PRINT_OUTPUT_PS_LPR = 0,
	PRINT_OUTPUT_PS_CUSTOM,
	PRINT_OUTPUT_PS_FILE,
	PRINT_OUTPUT_RGB_FILE,
	PRINT_OUTPUT_COUNT
} PrintOutput;

const gchar *print_output_text[] = {
	N_("Default printer"),
	N_("Custom printer"),
	N_("PostScript file"),
	N_("Image file"),
	NULL,
	NULL
};

typedef enum {
	PRINT_FILE_JPG_LOW = 0,
	PRINT_FILE_JPG_NORMAL,
	PRINT_FILE_JPG_HIGH,
	PRINT_FILE_PNG,
	PRINT_FILE_COUNT
} PrintFileFormat;

const gchar *print_file_format_text[] = {
	N_("jpeg, low quality"),
	N_("jpeg, normal quality"),
	N_("jpeg, high quality"),
	"png",
	NULL
};

typedef enum {
	RENDER_FORMAT_PREVIEW,
	RENDER_FORMAT_RGB,
	RENDER_FORMAT_PS
} RenderFormat;

typedef enum {
	TEXT_INFO_FILENAME = 1 << 0,
	TEXT_INFO_FILEDATE = 1 << 1,
	TEXT_INFO_FILESIZE = 1 << 2,
	TEXT_INFO_DIMENSIONS = 1 << 3,
	TEXT_INFO_FILEPATH = 1 << 4
} TextInfo;

typedef enum {
	PAPER_UNIT_POINTS = 0,
	PAPER_UNIT_MM,
	PAPER_UNIT_CM,
	PAPER_UNIT_INCH,
	PAPER_UNIT_PICAS,
	PAPER_UNIT_COUNT
} PaperUnits;

typedef enum {
	PAPER_ORIENTATION_PORTRAIT = 0,
	PAPER_ORIENTATION_LANDSCAPE,
	PAPER_ORIENTATION_COUNT
} PaperOrientation;


typedef struct _PrintWindow PrintWindow;
struct _PrintWindow
{
	GenericDialog *dialog;

	FileData *source_fd;
	GList *source_selection;
	GList *source_list;

	PrintSource source;
	PrintLayout layout;
	PrintOutput output;

	gchar *output_path;
	gchar *output_custom;

	PrintFileFormat output_format;

	gdouble max_dpi;

	GtkWidget *notebook;

	GtkWidget *path_entry;
	GtkWidget *custom_entry;
	GtkWidget *path_format_menu;
	GtkWidget *max_dpi_menu;

	ImageWindow *layout_image;
	gdouble layout_width;
	gdouble layout_height;

	guint layout_idle_id; /* event source id */

	gdouble image_scale;

	GtkWidget *image_scale_spin;

	gdouble proof_width;
	gdouble proof_height;
	gint proof_columns;
	gint proof_rows;
	GList *proof_point;
	gint proof_position;
	gint proof_page;

	GtkWidget *proof_group;
	GtkWidget *proof_width_spin;
	GtkWidget *proof_height_spin;

	GtkWidget *paper_menu;
	GtkWidget *paper_width_spin;
	GtkWidget *paper_height_spin;
	GtkWidget *paper_units_menu;
	GtkWidget *paper_orientation_menu;

	GtkWidget *margin_left_spin;
	GtkWidget *margin_right_spin;
	GtkWidget *margin_top_spin;
	GtkWidget *margin_bottom_spin;

	PaperUnits paper_units;
	gint paper_size;
	gdouble paper_width;
	gdouble paper_height;
	PaperOrientation paper_orientation;

	gdouble margin_left;
	gdouble margin_right;
	gdouble margin_top;
	gdouble margin_bottom;

	GtkWidget *button_back;
	GtkWidget *button_next;
	GtkWidget *page_label;
	GtkWidget *print_button;

	gdouble single_scale;
	gdouble single_x;
	gdouble single_y;

	GtkWidget *single_scale_spin;

	TextInfo	text_fields;
	gint		text_points;
	guint8		text_r;
	guint8		text_g;
	guint8		text_b;

	gint save_settings;

	/* job printing */

	GenericDialog	*job_dialog;
	GtkWidget	*job_progress;
	GtkWidget	*job_progress_label;

	RenderFormat	 job_format;
	PrintOutput	 job_output;

	FILE		*job_file;
	FILE 		*job_pipe;
	gchar		*job_path;

	GdkPixbuf	*job_pixbuf;

	gint		 job_page;
	ImageLoader	*job_loader;
};


static void print_job_throw_error(PrintWindow *pw, const gchar *message);
static gint print_job_start(PrintWindow *pw, RenderFormat format, PrintOutput output);
static void print_job_close(PrintWindow *pw, gint error);
static void print_window_close(PrintWindow *pw);


/* misc utils */

static gboolean clip_region(gdouble x1, gdouble y1, gdouble w1, gdouble h1,
			    gdouble x2, gdouble y2, gdouble w2, gdouble h2,
			    gdouble *rx, gdouble *ry, gdouble *rw, gdouble *rh)
{
	if (x2 + w2 <= x1 || x2 >= x1 + w1 ||
	    y2 + h2 <= y1 || y2 >= y1 + h1)
		{
		return FALSE;
		}

	*rx = MAX(x1, x2);
	*rw = MIN((x1 + w1), (x2 + w2)) - *rx;

	*ry = MAX(y1, y2);
	*rh = MIN((y1 + h1), (y2 + h2)) - *ry;

	return TRUE;
}

static const gchar *print_output_name(PrintOutput output)
{
	if (output >= PRINT_OUTPUT_COUNT) return "";

	return _(print_output_text[output]);
}


/*
 *-----------------------------------------------------------------------------
 * data
 *-----------------------------------------------------------------------------
 */


typedef struct _PaperSize PaperSize;
struct _PaperSize {
	gchar *description;
	gint width;
	gint height;
	PaperOrientation orientation;
};

const gchar *print_paper_units[] = {
	N_("points"),
	N_("millimeters"),
	N_("centimeters"),
	N_("inches"),
	N_("picas"),
	NULL
};

const gchar *print_paper_orientation[] = {
	N_("Portrait"),
	N_("Landscape"),
	NULL
};

PaperSize print_paper_sizes[] = {
	{ N_("Custom"),		360,	720,	PAPER_ORIENTATION_PORTRAIT },
	{ N_("Letter"),		612,	792,	PAPER_ORIENTATION_PORTRAIT },	/* in 8.5 x 11 */
	{ N_("Legal"),		612,	1008,	PAPER_ORIENTATION_PORTRAIT },	/* in 8.5 x 14 */
	{ N_("Executive"),	522,	756,	PAPER_ORIENTATION_PORTRAIT },	/* in 7.25x 10.5 */
	{ "A0",			2384,	3370,	PAPER_ORIENTATION_PORTRAIT },	/* mm 841 x 1189 */
	{ "A1",			1684,	2384,	PAPER_ORIENTATION_PORTRAIT },	/* mm 594 x 841 */
	{ "A2",			1191,	1684,	PAPER_ORIENTATION_PORTRAIT },	/* mm 420 x 594 */
	{ "A3",			842,	1191,	PAPER_ORIENTATION_PORTRAIT },	/* mm 297 x 420 */
	{ "A4",			595,	842,	PAPER_ORIENTATION_PORTRAIT },	/* mm 210 x 297 */
	{ "A5",			420,	595,	PAPER_ORIENTATION_PORTRAIT },	/* mm 148 x 210 */
	{ "A6",			298,	420,	PAPER_ORIENTATION_PORTRAIT },	/* mm 105 x 148 */
	{ "B3",			1001,	1417,	PAPER_ORIENTATION_PORTRAIT },	/* mm 353 x 500 */
	{ "B4",			709,	1001,	PAPER_ORIENTATION_PORTRAIT },	/* mm 250 x 353 */
	{ "B5",			499,	709,	PAPER_ORIENTATION_PORTRAIT },	/* mm 176 x 250 */
	{ "B6",			354,	499,	PAPER_ORIENTATION_PORTRAIT },	/* mm 125 x 176 */
	{ N_("Envelope #10"),	297,	684,	PAPER_ORIENTATION_LANDSCAPE },	/* in 4.125 x 9.5 */
	{ N_("Envelope #9"),	279,	639,	PAPER_ORIENTATION_LANDSCAPE },	/* in 3.875 x 8.875 */
	{ N_("Envelope C4"),	649,	918,	PAPER_ORIENTATION_LANDSCAPE },	/* mm 229 x 324 */
	{ N_("Envelope C5"),	459,	649,	PAPER_ORIENTATION_LANDSCAPE },	/* mm 162 x 229 */
	{ N_("Envelope C6"),	323,	459,	PAPER_ORIENTATION_LANDSCAPE },	/* mm 114 x 162 */
	{ N_("Photo 6x4"),	432,	288,	PAPER_ORIENTATION_PORTRAIT },	/* in 6   x 4 */
	{ N_("Photo 8x10"),	576,	720,	PAPER_ORIENTATION_PORTRAIT },	/* in 8   x 10 */
	{ N_("Postcard"),	284,	419,	PAPER_ORIENTATION_LANDSCAPE },	/* mm 100 x 148 */
	{ N_("Tabloid"),	792,	1224,	PAPER_ORIENTATION_PORTRAIT },	/* in 11  x 17 */
	{ NULL, 0, 0, 0 }
};


static PaperSize *print_paper_size_nth(gint n)
{
	PaperSize *ps = NULL;
	gint i = 0;

	while (i <= n && print_paper_sizes[i].description)
		{
		ps = &print_paper_sizes[i];
		i++;
		}

	return ps;
}

static gint print_paper_size_lookup(gint n, gdouble *width, gdouble *height)
{
	PaperSize *ps;
	gdouble w, h;

	ps = print_paper_size_nth(n);
	if (!ps) return FALSE;

	if (ps->orientation == PAPER_ORIENTATION_PORTRAIT)
		{
		w = ps->width;
		h = ps->height;
		}
	else
		{
		h = ps->width;
		w = ps->height;
		}

	if (width) *width = w;
	if (height) *height = h;

	return TRUE;
}

static gdouble print_paper_size_convert_units(gdouble value, PaperUnits src, PaperUnits dst)
{
	gdouble ret;

	if (src == dst) return value;

	switch (src)
		{
		case PAPER_UNIT_MM:
			ret = value / 25.4 * 72.0;
			break;
		case PAPER_UNIT_CM:
			ret = value / 2.54 * 72.0;
			break;
		case PAPER_UNIT_INCH:
			ret = value * 72.0;
			break;
		case PAPER_UNIT_PICAS:
			ret = value * 12.0;
			break;
		case PAPER_UNIT_POINTS:
		default:
			ret = value;
			break;
		}

	switch (dst)
		{
		case PAPER_UNIT_MM:
			ret = ret / 72.0 * 25.4;
			break;
		case PAPER_UNIT_CM:
			ret = ret / 72.0 * 2.54;
			break;
		case PAPER_UNIT_INCH:
			ret = ret / 72.0;
			break;
		case PAPER_UNIT_PICAS:
			ret = ret / 12.0;
			break;
		case PAPER_UNIT_POINTS:
		default:
			break;
		}

	return ret;
}

static PaperUnits paper_unit_default(void)
{
	const gchar *result;
#if 0
	/* this is not used because it is not even slightly portable */
	#include <langinfo.h>

	result = nl_langinfo(_NL_MEASUREMENT_MEASUREMENT);
	if (result[0] == '2') return PAPER_UNIT_INCH;
#endif

#ifdef LC_MEASUREMENT
	result = setlocale(LC_MEASUREMENT, NULL);
#else
	result = setlocale(LC_ALL, NULL);
#endif
	if (result &&
	    (strstr(result, "_US") || strstr(result, "_PR")) )
		{
		return PAPER_UNIT_INCH;
		}

	return PAPER_UNIT_CM;
}

/*
 *-----------------------------------------------------------------------------
 * the layout window
 *-----------------------------------------------------------------------------
 */

static gint print_layout_page_count(PrintWindow *pw);


static gint print_preview_unit(gdouble points)
{
	return (gint)(points / PRINT_PREVIEW_SCALE);
}

static void print_proof_size(PrintWindow *pw, gdouble *width, gdouble *height)
{
	if (width) *width = pw->proof_width + PRINT_PROOF_MARGIN * 2;
	if (height)
		{
		gdouble h;

		h = pw->proof_height + PRINT_PROOF_MARGIN * 2;
		if (pw->text_fields != 0) h += PRINT_TEXT_PADDING;
		if (pw->text_fields & TEXT_INFO_FILENAME) h+= (gdouble)pw->text_points * 1.25;
		if (pw->text_fields & TEXT_INFO_DIMENSIONS) h+= (gdouble)pw->text_points * 1.25;
		if (pw->text_fields & TEXT_INFO_FILEDATE) h+= (gdouble)pw->text_points * 1.25;
		if (pw->text_fields & TEXT_INFO_FILESIZE) h+= (gdouble)pw->text_points * 1.25;
		*height = h;
		}
}

static void print_window_layout_status(PrintWindow *pw)
{
	gint total;
	gchar *buf;

	total = print_layout_page_count(pw);
	pw->proof_page = CLAMP(pw->proof_page, 0, total - 1);

	buf = g_strdup_printf(_("page %d of %d"), pw->proof_page + 1, (total > 0) ? total : 1);
	gtk_label_set_text(GTK_LABEL(pw->page_label), buf);
	g_free(buf);

	gtk_widget_set_sensitive(pw->page_label, (total > 0));

	gtk_widget_set_sensitive(pw->button_back, (pw->proof_page > 0));
	gtk_widget_set_sensitive(pw->button_next, (pw->proof_page < total - 1));

	gtk_widget_set_sensitive(pw->print_button, total > 0);
}

static void print_window_layout_render_stop(PrintWindow *pw)
{
	if (pw->layout_idle_id)
		{
		g_source_remove(pw->layout_idle_id);
		pw->layout_idle_id = 0;
		}
}

static gboolean print_window_layout_render_idle(gpointer data)
{
	PrintWindow *pw = data;

	print_job_close(pw, FALSE);
	print_job_start(pw, RENDER_FORMAT_PREVIEW, 0);

	pw->layout_idle_id = 0;
	return FALSE;
}

static void print_window_layout_render(PrintWindow *pw)
{
	gdouble proof_w, proof_h;

	print_proof_size(pw, &proof_w, &proof_h);
	pw->proof_columns = (pw->layout_width - pw->margin_left - pw->margin_right) / proof_w;
	pw->proof_rows = (pw->layout_height - pw->margin_top - pw->margin_bottom) / proof_h;

	print_window_layout_status(pw);

	if (!pw->layout_idle_id)
		{
		pw->layout_idle_id = g_idle_add(print_window_layout_render_idle, pw);
		}
}

static void print_window_layout_size(PrintWindow *pw)
{
	GdkPixbuf *pixbuf;
	gdouble width;
	gdouble height;
	gint sw, sh;

	if (!pw->layout_image) return;

	if (pw->paper_orientation == PAPER_ORIENTATION_LANDSCAPE)
		{
		width = pw->paper_height;
		height = pw->paper_width;
		}
	else
		{
		width = pw->paper_width;
		height = pw->paper_height;
		}

	pw->layout_width = width;
	pw->layout_height = height;

	sw = print_preview_unit(width);
	sh = print_preview_unit(height);
	pixbuf = image_get_pixbuf(pw->layout_image);
	if (!pixbuf ||
	    gdk_pixbuf_get_width(pixbuf) != sw ||
	    gdk_pixbuf_get_height(pixbuf) != sh)
		{
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, sw, sh);
		image_change_pixbuf(pw->layout_image, pixbuf, 0.0, FALSE);
		g_object_unref(pixbuf);
		}

	print_window_layout_render(pw);
	print_window_layout_status(pw);
}

static gint print_layout_page_count(PrintWindow *pw)
{
	gint images;
	gint images_per_page;
	gint pages;

	if (pw->layout_width - pw->margin_left - pw->margin_right <= 0.0 ||
	    pw->layout_height - pw->margin_top - pw->margin_bottom <= 0.0)
		{
		return 0;
		}

	switch (pw->source)
		{
		case PRINT_SOURCE_ALL:
			images = g_list_length(pw->source_list);
			break;
		case PRINT_SOURCE_SELECTION:
			images = g_list_length(pw->source_selection);
			break;
		case PRINT_SOURCE_IMAGE:
		default:
			images = (pw->source_fd) ? 1 : 0;
			break;
		}

	switch (pw->layout)
		{
		case PRINT_LAYOUT_PROOF:
			images_per_page = pw->proof_columns * pw->proof_rows;
			break;
		case PRINT_LAYOUT_IMAGE:
		default:
			images_per_page = 1;
			break;
		}

	if (images < 1 || images_per_page < 1) return 0;

	pages = images / images_per_page;
	if (pages * images_per_page < images) pages++;

	return pages;
}

static void print_layout_page_step(PrintWindow *pw, gint step)
{
	gint max;
	gint page;

	max = print_layout_page_count(pw);
	page = pw->proof_page + step;

	if (page >= max) page = max - 1;
	if (page < 0) page = 0;

	if (page == pw->proof_page) return;

	pw->proof_page = page;
	print_window_layout_size(pw);
}

static void print_layout_page_back_cb(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;

	print_layout_page_step(pw, -1);
}

static void print_layout_page_next_cb(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;

	print_layout_page_step(pw, 1);
}

static void print_layout_zoom_in_cb(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;
	image_zoom_adjust(pw->layout_image, 0.25);
}

static void print_layout_zoom_out_cb(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;
	image_zoom_adjust(pw->layout_image, -0.25);
}

static void print_layout_zoom_original_cb(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;
	gdouble zoom;

	zoom = image_zoom_get(pw->layout_image);
	image_zoom_set(pw->layout_image, (zoom == 1.0) ? 0.0 : 1.0);
}

static GtkWidget *print_window_layout_setup(PrintWindow *pw, GtkWidget *box)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *group;
	GtkWidget *button;

	vbox = pref_box_new(box, TRUE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	group = pref_frame_new(vbox, TRUE, _("Preview"), GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	pw->layout_idle_id = 0;

	pw->layout_image = image_new(FALSE);
	gtk_widget_set_size_request(pw->layout_image->widget, PRINT_DLG_PREVIEW_WIDTH, PRINT_DLG_PREVIEW_HEIGHT);

	gtk_box_pack_start(GTK_BOX(group), pw->layout_image->widget, TRUE, TRUE, 0);
	gtk_widget_show(pw->layout_image->widget);

	hbox = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);
	pw->button_back = pref_button_new(hbox, GTK_STOCK_GO_BACK, NULL, TRUE,
					  G_CALLBACK(print_layout_page_back_cb), pw);
	pw->button_next = pref_button_new(hbox, GTK_STOCK_GO_FORWARD, NULL, TRUE,
					  G_CALLBACK(print_layout_page_next_cb), pw);
	pw->page_label = pref_label_new(hbox, "");

	button = pref_button_new(NULL, GTK_STOCK_ZOOM_OUT, NULL, TRUE,
				 G_CALLBACK(print_layout_zoom_out_cb), pw);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	button = pref_button_new(NULL, GTK_STOCK_ZOOM_IN, NULL, TRUE,
				 G_CALLBACK(print_layout_zoom_in_cb), pw);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);
	button = pref_button_new(NULL, GTK_STOCK_ZOOM_100, NULL, TRUE,
				 G_CALLBACK(print_layout_zoom_original_cb), pw);
	gtk_box_pack_end(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	print_window_layout_size(pw);

	return vbox;
}

static void print_window_spin_set(GtkSpinButton *spin, gpointer block_data,
				  gdouble value, gdouble min, gdouble max,
				  gdouble step, gdouble page, gint digits)
{
	if (block_data) g_signal_handlers_block_matched(G_OBJECT(spin), G_SIGNAL_MATCH_DATA,
							0, 0, NULL, NULL, block_data);
	gtk_spin_button_set_digits(spin, digits);
	gtk_spin_button_set_increments(spin, step, page);
	gtk_spin_button_set_range(spin, min, max);
	gtk_spin_button_set_value(spin, value);

	if (block_data) g_signal_handlers_unblock_matched(G_OBJECT(spin), G_SIGNAL_MATCH_DATA,
							  0, 0, NULL, NULL, block_data);
}

static void print_window_layout_sync_layout(PrintWindow *pw)
{
	gtk_widget_set_sensitive(pw->image_scale_spin, (pw->layout == PRINT_LAYOUT_IMAGE));
	gtk_widget_set_sensitive(pw->proof_group, (pw->layout == PRINT_LAYOUT_PROOF));
}

static void print_window_layout_sync_paper(PrintWindow *pw)
{
	gdouble width, height;
	gint digits;
	gdouble step;
	gdouble page;

	gtk_widget_set_sensitive(pw->paper_width_spin, (pw->paper_size == 0));
	gtk_widget_set_sensitive(pw->paper_height_spin, (pw->paper_size == 0));

	width = print_paper_size_convert_units((gdouble)pw->paper_width, PAPER_UNIT_POINTS, pw->paper_units);
	height = print_paper_size_convert_units((gdouble)pw->paper_height, PAPER_UNIT_POINTS, pw->paper_units);

	switch (pw->paper_units)
		{
		case PAPER_UNIT_MM:
			digits = 1;
			step = 1.0;
			page = 10.0;
			break;
		case PAPER_UNIT_CM:
			digits = 2;
			step = 0.5;
			page = 1.0;
			break;
		case PAPER_UNIT_INCH:
			digits = 3;
			step = 0.25;
			page = 1.0;
			break;
		case PAPER_UNIT_PICAS:
			digits = 2;
			step = 1.0;
			page = 6.0;
			break;
		case PAPER_UNIT_POINTS:
		default:
			digits = 1;
			step = 1.0;
			page = 10.0;
			break;
		}

	print_window_spin_set(GTK_SPIN_BUTTON(pw->paper_width_spin), pw, width,
			      print_paper_size_convert_units(PRINT_MIN_WIDTH, PAPER_UNIT_POINTS, pw->paper_units),
			      print_paper_size_convert_units(PRINT_MAX_WIDTH, PAPER_UNIT_POINTS, pw->paper_units),
			      step, page, digits);

	print_window_spin_set(GTK_SPIN_BUTTON(pw->paper_height_spin), pw, height,
			      print_paper_size_convert_units(PRINT_MIN_HEIGHT, PAPER_UNIT_POINTS, pw->paper_units),
			      print_paper_size_convert_units(PRINT_MAX_HEIGHT, PAPER_UNIT_POINTS, pw->paper_units),
			      step, page, digits);

	print_window_spin_set(GTK_SPIN_BUTTON(pw->margin_left_spin), pw,
			      print_paper_size_convert_units(pw->margin_left, PAPER_UNIT_POINTS, pw->paper_units),
			      0.0,
			      print_paper_size_convert_units(PRINT_MAX_WIDTH, PAPER_UNIT_POINTS, pw->paper_units),
			      step, page, digits);

	print_window_spin_set(GTK_SPIN_BUTTON(pw->margin_right_spin), pw,
			      print_paper_size_convert_units(pw->margin_right, PAPER_UNIT_POINTS, pw->paper_units),
			      0.0,
			      print_paper_size_convert_units(PRINT_MAX_WIDTH, PAPER_UNIT_POINTS, pw->paper_units),
			      step, page, digits);

	print_window_spin_set(GTK_SPIN_BUTTON(pw->margin_top_spin), pw,
			      print_paper_size_convert_units(pw->margin_top, PAPER_UNIT_POINTS, pw->paper_units),
			      0.0,
			      print_paper_size_convert_units(PRINT_MAX_HEIGHT, PAPER_UNIT_POINTS, pw->paper_units),
			      step, page, digits);

	print_window_spin_set(GTK_SPIN_BUTTON(pw->margin_bottom_spin), pw,
			      print_paper_size_convert_units(pw->margin_bottom, PAPER_UNIT_POINTS, pw->paper_units),
			      0.0,
			      print_paper_size_convert_units(PRINT_MAX_HEIGHT, PAPER_UNIT_POINTS, pw->paper_units),
			      step, page, digits);

	print_window_spin_set(GTK_SPIN_BUTTON(pw->proof_width_spin), pw,
			      print_paper_size_convert_units(pw->proof_width, PAPER_UNIT_POINTS, pw->paper_units),
			      print_paper_size_convert_units(PRINT_PROOF_MIN_SIZE, PAPER_UNIT_POINTS, pw->paper_units),
			      print_paper_size_convert_units(PRINT_PROOF_MAX_SIZE, PAPER_UNIT_POINTS, pw->paper_units),
			      step, page, digits);

	print_window_spin_set(GTK_SPIN_BUTTON(pw->proof_height_spin), pw,
			      print_paper_size_convert_units(pw->proof_height, PAPER_UNIT_POINTS, pw->paper_units),
			      print_paper_size_convert_units(PRINT_PROOF_MIN_SIZE, PAPER_UNIT_POINTS, pw->paper_units),
			      print_paper_size_convert_units(PRINT_PROOF_MAX_SIZE, PAPER_UNIT_POINTS, pw->paper_units),
			      step, page, digits);
}

static void print_window_layout_set_size(PrintWindow *pw, gdouble width, gdouble height)
{
	pw->paper_width = width;
	pw->paper_height = height;

	print_window_layout_sync_paper(pw);

	print_window_layout_size(pw);
}

static void print_window_layout_set_orientation(PrintWindow *pw, PaperOrientation o)
{
	if (pw->paper_orientation == o) return;

	pw->paper_orientation = o;

	print_window_layout_size(pw);
}

/*
 *-----------------------------------------------------------------------------
 * list printers
 *-----------------------------------------------------------------------------
 */

static GList *print_window_list_printers(void)
{
	FILE *p;
	GList *list = NULL;
	gchar buffer[2048];

	p = popen(PRINT_LPR_QUERY, "r");
	if (!p) return NULL;

	while (fgets(buffer, sizeof(buffer), p) != NULL)
		{
		gchar *ptr;
		gchar *end;

		ptr = buffer;
		if (strncmp(ptr, "printer ", 8) != 0) continue;
		if (strstr(ptr, "enabled") == NULL) continue;
		ptr += 8;
		end = ptr;
		while (*end != '\0' && *end != '\n' && *end != ' ' && *end != '\t') end++;
		*end = '\0';
		list = g_list_append(list, g_strdup(ptr));
		DEBUG_1("adding printer: %s", ptr);
		}

	pclose(p);

	return list;
}

/*
 *-----------------------------------------------------------------------------
 * print ps
 *-----------------------------------------------------------------------------
 */

typedef struct _PipeError PipeError;
struct _PipeError {
	struct sigaction old_action;
	sig_atomic_t *error;
};

static sig_atomic_t pipe_handler_error = FALSE;
static PipeError *pipe_handler_data = NULL;

static void pipe_handler_sigpipe_cb(gint fd)
{
	pipe_handler_error = TRUE;
}

static PipeError *pipe_handler_new(void)
{
	struct sigaction new_action;
	PipeError *pe;

	if (pipe_handler_data)
		{
		log_printf("warning SIGPIPE handler already in use\n");
		return NULL;
		}

	pe = g_new0(PipeError, 1);

	pipe_handler_error = FALSE;
	pe->error = &pipe_handler_error;

	new_action.sa_handler = pipe_handler_sigpipe_cb;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;

	/* setup our signal handler */
	sigaction(SIGPIPE, &new_action, &pe->old_action);

	pipe_handler_data = pe;
	return pe;
}

static void pipe_handler_free(PipeError *pe)
{
	if (!pe) return;
	if (pe != pipe_handler_data) log_printf("warning SIGPIPE handler not closing same data\n");

	/* restore the original signal handler */
	sigaction(SIGPIPE, &pe->old_action, NULL);

	pipe_handler_data = NULL;
	g_free(pe);
}

static gboolean pipe_handler_check(PipeError *pe)
{
	if (!pe) return FALSE;
	return !!(*pe->error);
}

static FILE *print_job_ps_fd(PrintWindow *pw)
{
	if (pw->job_file) return pw->job_file;
	if (pw->job_pipe) return pw->job_pipe;
	return NULL;
}

static gboolean print_job_ps_init(PrintWindow *pw)
{
	FILE *f;
	PipeError *pe;
	const gchar *cmd = NULL;
	const gchar *path = NULL;
	gchar *lc_pointer;
	gboolean ret;

	if (pw->job_file != NULL || pw->job_pipe != NULL) return FALSE;

	switch (pw->job_output)
		{
		case PRINT_OUTPUT_PS_LPR:
			cmd = PRINT_LPR_COMMAND;
			break;
		case PRINT_OUTPUT_PS_CUSTOM:
			cmd = pw->output_custom;
			break;
		case PRINT_OUTPUT_PS_FILE:
			path = pw->output_path;
			break;
		default:
			return FALSE;
			break;
		}

	if (cmd)
		{
		pw->job_pipe = popen(cmd, "w");

		if (!pw->job_pipe)
			{
			gchar *buf;

			buf = g_strdup_printf(_("Unable to open pipe for writing.\n\"%s\""), cmd);
			print_job_throw_error(pw, buf);
			g_free(buf);

			return FALSE;
			}
		}
	else if (path)
		{
		gchar *pathl;

		if (isfile(path))
			{
			gchar *buf;

			buf = g_strdup_printf(_("A file with name %s already exists."), path);
			print_job_throw_error(pw, buf);
			g_free(buf);

			return FALSE;
			}

		pathl = path_from_utf8(path);
		pw->job_file = fopen(pathl, "w");
		g_free(pathl);

		if (!pw->job_file)
			{
			gchar *buf;

			buf = g_strdup_printf(_("Failure writing to file %s"), path);
			print_job_throw_error(pw, buf);
			g_free(buf);

			return FALSE;
			}

		g_free(pw->job_path);
		pw->job_path = g_strdup(path);
		}

	f = print_job_ps_fd(pw);
	if (!f) return FALSE;

	lc_pointer = g_strdup(setlocale(LC_NUMERIC, NULL));
	setlocale(LC_NUMERIC, POSTSCRIPT_LOCALE);

	pe = pipe_handler_new();

	/* comments, etc. */
	g_fprintf(f, "%%!PS-Adobe-3.0\n");
	g_fprintf(f, "%%%%Creator: %s Version %s\n", GQ_APPNAME, VERSION);
	g_fprintf(f, "%%%%CreationDate: \n");
	g_fprintf(f, "%%%%LanguageLevel 2\n");
	g_fprintf(f, "%%%%DocumentMedia: \n");
	g_fprintf(f, "%%%%Orientation: %s\n",
		(pw->paper_orientation == PAPER_ORIENTATION_PORTRAIT) ? "Portrait" : "Landscape");
	g_fprintf(f, "%%%%BoundingBox: %f %f %f %f\n",
		0.0, 0.0, pw->paper_width, pw->paper_height);
	g_fprintf(f, "%%%%Pages: %d\n", print_layout_page_count(pw));
	g_fprintf(f, "%%%%PageOrder: Ascend\n");
	g_fprintf(f, "%%%%Title:\n");

	/* setup page size, coordinates (do we really need this?) */
	/* enabled for 1.0beta2  https://bugzilla.redhat.com/222639 */
#if 1
	g_fprintf(f, "<<\n");
	g_fprintf(f, "/PageSize [%f %f]\n", pw->layout_width, pw->layout_height);
	g_fprintf(f, "/ImagingBBox [%f %f %f %f]\n", /* l b r t */
		pw->margin_left, pw->margin_bottom,
		pw->layout_width - pw->margin_right, pw->layout_height - pw->margin_top);
	g_fprintf(f, "/Orientation %d\n",
		(pw->paper_orientation == PAPER_ORIENTATION_PORTRAIT) ? 0 : 1);
	g_fprintf(f, ">> setpagedevice\n");
#endif

	ret = !pipe_handler_check(pe);
	pipe_handler_free(pe);

	if (lc_pointer)
		{
		setlocale(LC_NUMERIC, lc_pointer);
		g_free(lc_pointer);
		}

	if (!ret) print_job_throw_error(pw, _("SIGPIPE error writing to printer."));

	return ret;
}

static gboolean print_job_ps_page_new(PrintWindow *pw, gint page)
{
	FILE *f;
	PipeError *pe;
	gchar *lc_pointer;
	gboolean ret;

	f= print_job_ps_fd(pw);
	if (!f) return FALSE;

	lc_pointer = g_strdup(setlocale(LC_NUMERIC, NULL));
	setlocale(LC_NUMERIC, POSTSCRIPT_LOCALE);

	pe = pipe_handler_new();

	g_fprintf(f, "%%%% page %d\n", page + 1);

	if (pw->paper_orientation == PAPER_ORIENTATION_LANDSCAPE)
		{
		g_fprintf(f, "/pagelevel save def\n");
		g_fprintf(f, "%d 0 translate 90 rotate\n", (gint)pw->layout_height);
		}

	ret = !pipe_handler_check(pe);
	pipe_handler_free(pe);

	if (lc_pointer)
		{
		setlocale(LC_NUMERIC, lc_pointer);
		g_free(lc_pointer);
		}

	if (!ret) print_job_throw_error(pw, _("SIGPIPE error writing to printer."));

	return ret;
}

static gboolean print_job_ps_page_done(PrintWindow *pw)
{
	FILE *f;
	PipeError *pe;
	gchar *lc_pointer;
	gboolean ret;

	f = print_job_ps_fd(pw);
	if (!f) return FALSE;

	lc_pointer = g_strdup(setlocale(LC_NUMERIC, NULL));
	setlocale(LC_NUMERIC, POSTSCRIPT_LOCALE);

	pe = pipe_handler_new();

	if (pw->paper_orientation == PAPER_ORIENTATION_LANDSCAPE)
		{
		g_fprintf(f, "pagelevel restore\n");
		}

	g_fprintf(f, "showpage\n");

	ret = !pipe_handler_check(pe);
	pipe_handler_free(pe);

	if (lc_pointer)
		{
		setlocale(LC_NUMERIC, lc_pointer);
		g_free(lc_pointer);
		}

	if (!ret) print_job_throw_error(pw, _("SIGPIPE error writing to printer."));

	return ret;
}

static void print_job_ps_page_image_pixel(FILE *f, guchar *pix)
{
	static gchar hex_digits[] = "0123456789abcdef";
	gchar text[8];
	gint i;

	for (i = 0; i < 3; i++)
		{
		text[i*2] = hex_digits[pix[i] >> 4];
		text[i*2+1] = hex_digits[pix[i] & 0xf];
		}
	text[6] = '\0';

	g_fprintf(f, "%s", text);
}
static gboolean print_job_ps_page_image(PrintWindow *pw, GdkPixbuf *pixbuf,
				        gdouble x, gdouble y, gdouble w, gdouble h,
				        gdouble offx, gdouble offy)
{
	FILE *f;
	PipeError *pe;
	gchar *lc_pointer;
	gint sw, sh;
	gint bps;
	gint rowstride;
	guchar *pix;
	gint i, j;
	gint c;
	guchar *p;
	guchar bps_buf[3];
	gboolean ret;

	if (!pixbuf) return TRUE;

	f = print_job_ps_fd(pw);
	if (!f) return FALSE;

	sw = gdk_pixbuf_get_width(pixbuf);
	sh = gdk_pixbuf_get_height(pixbuf);

	if (pw->max_dpi >= PRINT_PS_DPI_MIN &&
	    sw / pw->max_dpi > w / 72.0)
		{
		pixbuf = gdk_pixbuf_scale_simple(pixbuf,
						(gint)(w / 72.0 * pw->max_dpi),
						(gint)(h / 72.0 * pw->max_dpi),
						PRINT_PS_MAX_INTERP);
		sw = gdk_pixbuf_get_width(pixbuf);
		sh = gdk_pixbuf_get_height(pixbuf);
		}
	else
		{
		g_object_ref(G_OBJECT(pixbuf));
		}

	bps = (gdk_pixbuf_get_has_alpha(pixbuf)) ? 4 : 3;
	rowstride = gdk_pixbuf_get_rowstride(pixbuf);
	pix = gdk_pixbuf_get_pixels(pixbuf);

	lc_pointer = g_strdup(setlocale(LC_NUMERIC, NULL));
	setlocale(LC_NUMERIC, POSTSCRIPT_LOCALE);

	pe = pipe_handler_new();

	g_fprintf(f, "gsave\n");
	g_fprintf(f, "[%f 0 0 %f %f %f] concat\n", w, h, x, pw->layout_height - h - y);
	g_fprintf(f, "/buf %d string def\n", sw * 3);
	g_fprintf(f, "%d %d %d\n", sw, sh, 8);
	g_fprintf(f, "[%d 0 0 -%d 0 %d]\n", sw, sh, sh);
	g_fprintf(f, "{ currentfile buf readhexstring pop }\n");
	g_fprintf(f, "false %d colorimage\n", 3);

	c = 0;
	for (j = 0; j < sh; j++)
		{
		p = pix + j * rowstride;
		for (i = 0; i < sw; i++)
			{
			if (bps == 3)
				{
				print_job_ps_page_image_pixel(f, p);
				}
			else
				{
				bps_buf[0] = (p[0] * p[3] + PRINT_PS_MASK_R * (256 - p[3])) >> 8;
				bps_buf[1] = (p[1] * p[3] + PRINT_PS_MASK_G * (256 - p[3])) >> 8;
				bps_buf[2] = (p[2] * p[3] + PRINT_PS_MASK_B * (256 - p[3])) >> 8;
				print_job_ps_page_image_pixel(f, bps_buf);
				}
			p+=bps;
			c++;
			if (c > 11)
				{
				g_fprintf(f, "\n");
				c = 0;
				}
			}
		}
	if (c > 0) g_fprintf(f, "\n");
	g_fprintf(f, "grestore\n");

	ret = !pipe_handler_check(pe);
	pipe_handler_free(pe);

	if (lc_pointer)
		{
		setlocale(LC_NUMERIC, lc_pointer);
		g_free(lc_pointer);
		}

	g_object_unref(G_OBJECT(pixbuf));

	if (!ret) print_job_throw_error(pw, _("SIGPIPE error writing to printer."));

	return ret;
}

static gdouble convert_pango_dpi(gdouble points);

static gboolean print_job_ps_page_text(PrintWindow *pw, const gchar *text, gdouble point_size,
				       gdouble x, gdouble y, gdouble width,
				       guint8 r, guint8 g, guint8 b)
{
	PangoLayout *layout;
	PangoFontDescription *desc;
	GdkPixbuf *pixbuf;
	gint lw, lh;
	gboolean ret;
	gdouble scale_to_max_dpi = (pw->max_dpi >= PRINT_PS_DPI_MIN) ? pw->max_dpi / 72.0 : 1200.0 / 72.0;

	layout = gtk_widget_create_pango_layout(pw->dialog->dialog, NULL);

	desc = pango_font_description_new();
	pango_font_description_set_size(desc, convert_pango_dpi(point_size) * PANGO_SCALE * scale_to_max_dpi);
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, text, -1);

	pango_layout_get_pixel_size(layout, &lw, &lh);
	x = x - (gdouble)lw / 2.0 / scale_to_max_dpi;

	pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, lw, lh);
	gdk_pixbuf_fill(pixbuf, 0xffffffff);
	pixbuf_draw_layout(pixbuf, layout, pw->dialog->dialog, 0, 0, r, g, b, 255);
	g_object_unref(G_OBJECT(layout));

	ret = print_job_ps_page_image(pw, pixbuf, x, y,
				       /* do not allow rescaling of the pixbuf due to rounding errors */
	                              ((gdouble)lw + 0.01) / scale_to_max_dpi,
	                              ((gdouble)lh + 0.01) / scale_to_max_dpi,
	                              0, 0);

	g_object_unref(G_OBJECT(pixbuf));

	return ret;
}

static gboolean print_job_ps_end(PrintWindow *pw)
{
	FILE *f;
	PipeError *pe;
	gchar *lc_pointer;
	gboolean ret;

	f = print_job_ps_fd(pw);
	if (!f) return FALSE;

	lc_pointer = g_strdup(setlocale(LC_NUMERIC, NULL));
	setlocale(LC_NUMERIC, POSTSCRIPT_LOCALE);

	pe = pipe_handler_new();

	g_fprintf(f, "%%%%EOF\n");

	ret = !pipe_handler_check(pe);
	pipe_handler_free(pe);

	if (lc_pointer)
		{
		setlocale(LC_NUMERIC, lc_pointer);
		g_free(lc_pointer);
		}

	if (!ret) print_job_throw_error(pw, _("SIGPIPE error writing to printer."));

	return ret;
}

/*
 *-----------------------------------------------------------------------------
 * print rgb
 *-----------------------------------------------------------------------------
 */

static gboolean print_job_rgb_page_new(PrintWindow *pw, gint page)
{
	gint total;

	if (pw->job_pixbuf)
		{
		pixbuf_set_rect_fill(pw->job_pixbuf, 0, 0,
				     gdk_pixbuf_get_width(pw->job_pixbuf),
				     gdk_pixbuf_get_height(pw->job_pixbuf),
				     255, 255, 255, 255);
		}

	g_free(pw->job_path);
	pw->job_path = NULL;

	total = print_layout_page_count(pw);

	if (!pw->output_path ||
	    page < 0 || page >= total) return FALSE;

	if (total > 1)
		{
		const gchar *ext;
		gchar *base;

		ext = registered_extension_from_path(pw->output_path);

		if (ext)
			{
			base = g_strndup(pw->output_path, ext - pw->output_path);
			}
		else
			{
			base = g_strdup(pw->output_path);
			ext = "";
			}
		pw->job_path = g_strdup_printf("%s_%03d%s", base, page + 1, ext);
		g_free(base);
		}
	else
		{
		pw->job_path = g_strdup(pw->output_path);
		}

	if (isfile(pw->job_path))
		{
		gchar *buf;

		buf = g_strdup_printf(_("A file with name %s already exists."), pw->job_path);
		print_job_throw_error(pw, buf);
		g_free(buf);

		g_free(pw->job_path);
		pw->job_path = NULL;
		}

	return (pw->job_path != NULL);
}

static gboolean print_job_rgb_page_done(PrintWindow *pw)
{
	gchar *pathl;
	gboolean ret = FALSE;

	if (!pw->job_pixbuf) return FALSE;

	pathl = path_from_utf8(pw->job_path);

	if (pw->output_format == PRINT_FILE_PNG)
		{
		ret = pixbuf_to_file_as_png(pw->job_pixbuf, pathl);
		}
	else
		{
		gint quality = 0;

		switch (pw->output_format)
			{
			case PRINT_FILE_JPG_LOW:
				quality = 65;
				break;
			case PRINT_FILE_JPG_NORMAL:
				quality = 80;
				break;
			case PRINT_FILE_JPG_HIGH:
				quality = 95;
				break;
			default:
				break;
			}

		if (quality > 0)
			{
			ret = pixbuf_to_file_as_jpg(pw->job_pixbuf, pathl, quality);
			}
		}

	g_free(pathl);

	if (!ret)
		{
		gchar *buf;

		buf = g_strdup_printf(_("Failure writing to file %s"), pw->job_path);
		print_job_throw_error(pw, buf);
		g_free(buf);
		}

	return ret;
}

static gboolean print_job_rgb_page_image(PrintWindow *pw, GdkPixbuf *pixbuf,
				         gdouble x, gdouble y, gdouble w, gdouble h,
				         gdouble offx, gdouble offy)
{
	gdouble sw, sh;
	gdouble dw, dh;
	gdouble rx, ry, rw, rh;

	if (!pw->job_pixbuf) return FALSE;
	if (!pixbuf) return TRUE;

	sw = (gdouble)gdk_pixbuf_get_width(pixbuf);
	sh = (gdouble)gdk_pixbuf_get_height(pixbuf);

	dw = (gdouble)gdk_pixbuf_get_width(pw->job_pixbuf);
	dh = (gdouble)gdk_pixbuf_get_height(pw->job_pixbuf);

	if (clip_region(x, y, w, h,
			0.0, 0.0, dw, dh,
			&rx, &ry, &rw, &rh))
		{
		gdk_pixbuf_composite(pixbuf, pw->job_pixbuf, rx, ry, rw, rh,
				     x + offx, y + offy,
				     w / sw, h / sh,
				     (w / sw < 0.01 || h / sh < 0.01) ? GDK_INTERP_NEAREST : GDK_INTERP_BILINEAR, 255);
		}

	return TRUE;
}

static gdouble convert_pango_dpi(gdouble points)
{
	static gdouble dpi = 0.0;

	if (dpi == 0.0)
		{
		GtkSettings *settings;
		GObjectClass *klass;

		settings = gtk_settings_get_default();
		klass = G_OBJECT_CLASS(GTK_SETTINGS_GET_CLASS(settings));
		if (g_object_class_find_property(klass, "gtk-xft-dpi"))
			{
			gint int_dpi;
			g_object_get(settings, "gtk-xft-dpi", &int_dpi, NULL);
			dpi = (gdouble)int_dpi / PANGO_SCALE;
			}

		if (dpi < 25.0)
			{
			static gboolean warned = FALSE;
			gdouble fallback_dpi = 96.0;

			if (!warned)
				{
				if (dpi == 0.0)
					{
					log_printf("pango dpi unknown, assuming %.0f\n", fallback_dpi);
					}
				else
					{
					log_printf("pango dpi reported as %.0f ignored, assuming %.0f\n", dpi, fallback_dpi);
					}
				warned = TRUE;
				}

			dpi = fallback_dpi;
			}
		}

	if (dpi == 0) return points;
	return points * 72.0 / dpi;
}

static gboolean print_job_rgb_page_text(PrintWindow *pw, const gchar *text, gdouble point_size,
				        gdouble x, gdouble y, gdouble width,
				        guint8 r, guint8 g, guint8 b)
{
	PangoLayout *layout;
	PangoFontDescription *desc;
	gint lw, lh;

	if (!pw->job_pixbuf) return FALSE;

	layout = gtk_widget_create_pango_layout(pw->dialog->dialog, NULL);

	desc = pango_font_description_new();
	pango_font_description_set_size(desc, convert_pango_dpi(point_size) * PANGO_SCALE);
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, text, -1);

	pango_layout_get_pixel_size(layout, &lw, &lh);
	x = x - (gdouble)lw / 2.0;

	pixbuf_draw_layout(pw->job_pixbuf, layout, pw->dialog->dialog, x, y, r, g, b, 255);
	g_object_unref(G_OBJECT(layout));

	return TRUE;
}

static gboolean print_job_rgb_init(PrintWindow *pw)
{
	if (pw->job_pixbuf) g_object_unref(pw->job_pixbuf);
	pw->job_pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
					(gint)pw->layout_width, (gint)pw->layout_height);

	return print_job_rgb_page_new(pw, pw->job_page);
}

/*
 *-----------------------------------------------------------------------------
 * print preview
 *-----------------------------------------------------------------------------
 */

static gboolean print_job_preview_page_new(PrintWindow *pw, gint page)
{
	GdkPixbuf *pixbuf;
	gint w, h;
	gint l, r, t, b;

	pixbuf = pw->job_pixbuf;
	if (!pixbuf) return FALSE;

	w = print_preview_unit(pw->layout_width);
	h = print_preview_unit(pw->layout_height);
	l = print_preview_unit(pw->margin_left);
	r = print_preview_unit(pw->margin_right);
	t = print_preview_unit(pw->margin_top);
	b = print_preview_unit(pw->margin_bottom);

	/* fill background */
	pixbuf_set_rect_fill(pixbuf, 0, 0, w, h,
			     255, 255, 255, 255);

	/* draw cm or inch grid */
	if (TRUE)
		{
		gdouble i;
		gdouble grid;
		PaperUnits units;

		units = (pw->paper_units == PAPER_UNIT_MM ||
			 pw->paper_units == PAPER_UNIT_CM) ? PAPER_UNIT_CM : PAPER_UNIT_INCH;

		grid = print_paper_size_convert_units(1.0, units, PAPER_UNIT_POINTS);
		for (i = grid ; i < pw->layout_width; i += grid)
			{
			pixbuf_draw_rect_fill(pixbuf, print_preview_unit(i), 0, 1, h, 0, 0, 0, 16);
			}
		for (i = grid; i < pw->layout_height; i += grid)
			{
			pixbuf_draw_rect_fill(pixbuf, 0, print_preview_unit(i), w, 1, 0, 0, 0, 16);
			}
		}

	/* proof sheet grid */
	if (pw->layout == PRINT_LAYOUT_PROOF)
		{
		gdouble i, j;
		gdouble proof_w, proof_h;
		gint uw, uh;

		print_proof_size(pw, &proof_w, &proof_h);
		uw = print_preview_unit(proof_w + PRINT_PREVIEW_SCALE - 0.1);
		uh = print_preview_unit(proof_h + PRINT_PREVIEW_SCALE - 0.1);

		for (i = 0; i < pw->proof_columns; i++)
		    for (j = 0; j < pw->proof_rows; j++)
			{
			gint x, y;

			x = pw->margin_left + (pw->layout_width - pw->margin_left - pw->margin_right - (pw->proof_columns * proof_w)) / 2 + i * proof_w;
			y = pw->margin_top + j * proof_h;

			pixbuf_draw_rect(pixbuf, print_preview_unit(x), print_preview_unit(y), uw, uh,
					 255, 0, 0, 64, 1, 1, 1, 1);
			}
		}

	/* non-printable region (margins) */
	pixbuf_draw_rect(pixbuf, 0, 0, w, h,
			 0, 0, 0, 16,
			 l, r, t, b);

	/* margin lines */
	pixbuf_draw_rect(pixbuf, l, 0, w - l - r, h,
			 0, 0, 255, 128,
			 1, 1, 0, 0);
	pixbuf_draw_rect(pixbuf, 0, t, w, h - t - b,
			 0, 0, 255, 128,
			 0, 0, 1, 1);

	/* border */
	pixbuf_draw_rect(pixbuf, 0, 0, w, h,
			 0, 0, 0, 255,
			 1, 1, 1, 1);

	image_area_changed(pw->layout_image, 0, 0, w, h);

	return TRUE;
}

static gboolean print_job_preview_page_done(PrintWindow *pw)
{
	return TRUE;
}

static gboolean print_job_preview_page_image(PrintWindow *pw, GdkPixbuf *pixbuf,
					     gdouble x, gdouble y, gdouble w, gdouble h,
					     gdouble offx, gdouble offy)
{
	gdouble sw, sh;
	gdouble dw, dh;
	gdouble rx, ry, rw, rh;

	if (!pw->job_pixbuf) return FALSE;
	if (!pixbuf) return TRUE;

	sw = (gdouble)gdk_pixbuf_get_width(pixbuf);
	sh = (gdouble)gdk_pixbuf_get_height(pixbuf);

	dw = (gdouble)gdk_pixbuf_get_width(pw->job_pixbuf);
	dh = (gdouble)gdk_pixbuf_get_height(pw->job_pixbuf);

	x = print_preview_unit(x);
	y = print_preview_unit(y);
	w = print_preview_unit(w);
	h = print_preview_unit(h);
	offx = print_preview_unit(offx);
	offy = print_preview_unit(offy);

	if (clip_region(x, y, w, h,
			0.0, 0.0, dw, dh,
			&rx, &ry, &rw, &rh))
		{
		gdk_pixbuf_composite(pixbuf, pw->job_pixbuf, rx, ry, rw, rh,
				     x + offx, y + offy,
				     w / sw, h / sh,
				     (w / sw < 0.01 || h / sh < 0.01) ? GDK_INTERP_NEAREST : GDK_INTERP_BILINEAR, 255);

		image_area_changed(pw->layout_image, rx, ry, rw, rh);
		}

	return TRUE;
}

static gboolean print_job_preview_page_text(PrintWindow *pw, const gchar *text, gdouble point_size,
					    gdouble x, gdouble y, gdouble width,
					    guint8 r, guint8 g, guint8 b)
{
	PangoLayout *layout;
	PangoFontDescription *desc;
	gint lw, lh;
	GdkPixbuf *pixbuf;

	if (!pw->job_pixbuf) return FALSE;

	layout = gtk_widget_create_pango_layout(pw->dialog->dialog, NULL);

	desc = pango_font_description_new();
	pango_font_description_set_size(desc, convert_pango_dpi(point_size) * PANGO_SCALE);
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	pango_layout_set_text(layout, text, -1);

	pango_layout_get_pixel_size(layout, &lw, &lh);
	x = x - (gdouble)lw / 2.0;

	pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, lw, lh);
	pixbuf_set_rect_fill(pixbuf, 0, 0, lw, lh, 0, 0, 0, 0);
	pixbuf_draw_layout(pixbuf, layout, pw->dialog->dialog, 0, 0, r, g, b, 255);
	g_object_unref(G_OBJECT(layout));

	print_job_preview_page_image(pw, pixbuf, x, y, (gdouble)lw, (gdouble)lh, 0, 0);
	g_object_unref(pixbuf);

	return TRUE;
}

static gboolean print_job_preview_init(PrintWindow *pw)
{
	if (pw->job_pixbuf) g_object_unref(pw->job_pixbuf);
	pw->job_pixbuf = image_get_pixbuf(pw->layout_image);
	g_object_ref(pw->job_pixbuf);

	return print_job_preview_page_new(pw, pw->job_page);
}


/*
 *-----------------------------------------------------------------------------
 * wrappers
 *-----------------------------------------------------------------------------
 */

static gboolean print_job_page_new(PrintWindow *pw)
{
	switch (pw->job_format)
		{
		case RENDER_FORMAT_RGB:
			return print_job_rgb_page_new(pw, pw->job_page);
		case RENDER_FORMAT_PS:
			return print_job_ps_page_new(pw, pw->job_page);
		case RENDER_FORMAT_PREVIEW:
			return print_job_preview_page_new(pw, pw->job_page);
		}

	return FALSE;
}

static gboolean print_job_page_done(PrintWindow *pw)
{
	switch (pw->job_format)
		{
		case RENDER_FORMAT_RGB:
			return print_job_rgb_page_done(pw);
		case RENDER_FORMAT_PS:
			return print_job_ps_page_done(pw);
		case RENDER_FORMAT_PREVIEW:
			return print_job_preview_page_done(pw);
		}

	return FALSE;
}

static gboolean print_job_page_image(PrintWindow *pw, GdkPixbuf *pixbuf,
				     gdouble x, gdouble y, gdouble w, gdouble h,
				     gdouble offx, gdouble offy)
{
	gboolean success = FALSE;

	if (w <= 0.0 || h <= 0.0) return TRUE;

	switch (pw->job_format)
		{
		case RENDER_FORMAT_RGB:
			success = print_job_rgb_page_image(pw, pixbuf, x, y, w, h, offx, offy);
			break;
		case RENDER_FORMAT_PS:
			success = print_job_ps_page_image(pw, pixbuf, x, y, w, h, offx, offy);
			break;
		case RENDER_FORMAT_PREVIEW:
			success = print_job_preview_page_image(pw, pixbuf, x, y, w, h, offx, offy);
			break;
		}

	return success;
}

static gboolean print_job_page_text(PrintWindow *pw, const gchar *text, gdouble point_size,
				    gdouble x, gdouble y, gdouble width,
				    guint8 r, guint8 g, guint8 b)
{
	gboolean success = TRUE;

	if (!text) return TRUE;

	switch (pw->job_format)
		{
		case RENDER_FORMAT_RGB:
			success = print_job_rgb_page_text(pw, text, point_size, x, y, width, r, g, b);
			break;
		case RENDER_FORMAT_PS:
			success = print_job_ps_page_text(pw, text, point_size, x, y, width, r, g, b);
			break;
		case RENDER_FORMAT_PREVIEW:
			success = print_job_preview_page_text(pw, text, point_size, x, y, width, r, g, b);
			break;
		}

	return success;
}

/*
 *-----------------------------------------------------------------------------
 * print ?
 *-----------------------------------------------------------------------------
 */

static gboolean print_job_render_image(PrintWindow *pw);
static gboolean print_job_render_proof(PrintWindow *pw);


static void print_job_status(PrintWindow *pw)
{
	gdouble value;
	gint page;
	gint total;
	gchar *buf;

	if (!pw->job_progress) return;

	page = pw->job_page;
	total = print_layout_page_count(pw);

	if (pw->layout == PRINT_LAYOUT_PROOF && pw->proof_point)
		{
		GList *start;

		start = g_list_first(pw->proof_point);
		value = (gdouble)g_list_position(start, pw->proof_point) / g_list_length(start);
		}
	else
		{
		value = (total > 0) ? (gdouble)page / total : 0.0;
		}

	buf = g_strdup_printf(_("Page %d"), page + 1);
	gtk_progress_bar_set_text(GTK_PROGRESS_BAR(pw->job_progress), buf);
	g_free(buf);

	if (pw->job_path && pw->job_progress_label)
		{
		gtk_label_set_text(GTK_LABEL(pw->job_progress_label), pw->job_path);
		}

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pw->job_progress), value);
}

static void print_job_throw_error(PrintWindow *pw, const gchar *message)
{
	GenericDialog *gd;
	GtkWidget *parent = NULL;
	GtkWidget *group;
	GtkWidget *label;
	gchar *buf;

	if (gtk_widget_get_visible(pw->dialog->dialog)) parent = pw->dialog->dialog;

	gd = generic_dialog_new(_("Printing error"), "print_warning",
				parent, TRUE, NULL, NULL);
	generic_dialog_add_button(gd, GTK_STOCK_OK, NULL, NULL, TRUE);

	buf = g_strdup_printf(_("An error occurred printing to %s."), print_output_name(pw->output));
	generic_dialog_add_message(gd, GTK_STOCK_DIALOG_ERROR, _("Printing error"), buf, TRUE);
	g_free(buf);

	group = pref_group_new(gd->vbox, FALSE, _("Details"), GTK_ORIENTATION_VERTICAL);
	label = pref_label_new(group, message);
	gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);

	gtk_widget_show(gd->dialog);
}

static void print_job_done(PrintWindow *pw)
{
	print_job_close(pw, FALSE);
}

static gboolean print_job_text_image(PrintWindow *pw, const gchar *path,
				     gdouble x, gdouble y, gdouble width,
				     gint sw, gint sh, gint proof)
{
	GString *string;
	gboolean space = FALSE;
	gboolean newline = FALSE;
	gboolean ret;

	if (pw->text_fields == 0) return TRUE;

	string = g_string_new("");
	path = image_loader_get_fd(pw->job_loader)->path;

	if (pw->text_fields & TEXT_INFO_FILENAME)
		{
		if (pw->text_fields & TEXT_INFO_FILEPATH)
			g_string_append(string, path);
		else
			g_string_append(string, filename_from_path(path));
		newline = TRUE;
		}
	else if (pw->text_fields & TEXT_INFO_FILEPATH)
		{
		gchar *dirname = g_path_get_dirname(path);

		g_string_append_printf(string, "%s%s", dirname, G_DIR_SEPARATOR_S);
		g_free(dirname);
		newline = TRUE;
		}
	if (pw->text_fields & TEXT_INFO_DIMENSIONS)
		{
		if (newline) g_string_append(string, "\n");
		g_string_append_printf(string, "%d x %d", (gint)sw, (gint)sh);
		newline = proof;
		space = !proof;
		}
	if (pw->text_fields & TEXT_INFO_FILEDATE)
		{
		if (newline)  g_string_append(string, "\n");
		if (space) g_string_append(string, " - ");
		g_string_append(string, text_from_time(filetime(image_loader_get_fd(pw->job_loader)->path)));
		newline = proof;
		space = !proof;
		}
	if (pw->text_fields & TEXT_INFO_FILESIZE)
		{
		gchar *size;

		if (newline)  g_string_append(string, "\n");
		if (space) g_string_append(string, " - ");
		size = text_from_size_abrev(filesize(image_loader_get_fd(pw->job_loader)->path));
		g_string_append(string, size);
		g_free(size);
		}

	ret = print_job_page_text(pw, string->str, pw->text_points, x, y, width,
				  pw->text_r, pw->text_g, pw->text_b);

	g_string_free(string, TRUE);

	return ret;
}

static void print_job_render_image_loader_done(ImageLoader *il, gpointer data)
{
	PrintWindow *pw = data;
	GdkPixbuf *pixbuf;
	gboolean success = TRUE;

	pixbuf = image_loader_get_pixbuf(il);
	if (pixbuf)
		{
		gdouble sw, sh;
		gdouble dw, dh;
		gdouble x, y, w, h;
		gdouble offx, offy;

		sw = (gdouble)gdk_pixbuf_get_width(pixbuf);
		sh = (gdouble)gdk_pixbuf_get_height(pixbuf);

		dw = pw->layout_width - pw->margin_left - pw->margin_right;
		dh = pw->layout_height - pw->margin_top - pw->margin_bottom;

		if (dw / sw < dh / sh)
			{
			w = dw;
			h = dw / sw * sh;
			}
		else
			{
			h = dh;
			w = dh / sh *sw;
			}

		if (pw->image_scale >= 5.0)
			{
			w = w * pw->image_scale / 100.0;
			h = h * pw->image_scale / 100.0;
			}

		x = pw->margin_left + (dw / 2) - (w / 2);
		y = pw->margin_top + (dh / 2) - (h / 2);

		offx = offy = 0;

		if (x < 0)
			{
			w += x;
			offx = x;
			x = 0;
			}
		if (x + w >= pw->layout_width) w = pw->layout_width - x;

		if (y < 0)
			{
			h += y;
			offy = y;
			y = 0;
			}
		if (y + h >= pw->layout_height) h = pw->layout_height - y;

		success = (success &&
			   print_job_page_image(pw, pixbuf, x, y, w, h, offx, offy));

		x = x + w / 2;
		y = y + h + PRINT_TEXT_PADDING;

		success = (success &&
			   print_job_text_image(pw, image_loader_get_fd(pw->job_loader)->path, x, y, dw, sw, sh, FALSE));
		}

	image_loader_free(pw->job_loader);
	pw->job_loader = NULL;

	if (pw->job_format == RENDER_FORMAT_PREVIEW)
		{
		print_job_done(pw);
		return;
		}

	success = (success && print_job_page_done(pw));
	if (!success)
		{
		print_job_close(pw, TRUE);
		return;
		}

	pw->job_page++;
	print_job_status(pw);

	if (print_job_render_image(pw))
		{
		if (!print_job_page_new(pw)) print_job_close(pw, TRUE);
		}
	else
		{
		print_job_done(pw);
		}
}

static gboolean print_job_render_image(PrintWindow *pw)
{
	FileData *fd = NULL;

	switch (pw->source)
		{
		case PRINT_SOURCE_SELECTION:
			fd = g_list_nth_data(pw->source_selection, pw->job_page);
			break;
		case PRINT_SOURCE_ALL:
			fd = g_list_nth_data(pw->source_list, pw->job_page);
			break;
		case PRINT_SOURCE_IMAGE:
		default:
			if (pw->job_page == 0) fd = pw->source_fd;
			break;
		}

	image_loader_free(pw->job_loader);
	pw->job_loader = NULL;

	if (!fd) return FALSE;

	pw->job_loader = image_loader_new(fd);
	g_signal_connect(G_OBJECT(pw->job_loader), "done", (GCallback)print_job_render_image_loader_done, pw);
	if (!image_loader_start(pw->job_loader))
		{
		image_loader_free(pw->job_loader);
		pw->job_loader= NULL;
		}

	return TRUE;
}

static void print_job_render_proof_loader_done(ImageLoader *il, gpointer data)
{
	PrintWindow *pw = data;
	GdkPixbuf *pixbuf;
	gdouble x, y;
	gdouble w, h;
	gdouble proof_w, proof_h;
	gdouble icon_w, icon_h;
	gboolean success = TRUE;

	if (pw->proof_columns < 1 || pw->proof_rows < 1)
		{
		image_loader_free(pw->job_loader);
		pw->job_loader = NULL;

		print_job_done(pw);

		return;
		}

	pixbuf = image_loader_get_pixbuf(il);

	if (options->image.exif_proof_rotate_enable == TRUE) {
		pixbuf = pixbuf_apply_orientation(pixbuf, il->fd->exif_orientation);
	}

	w = gdk_pixbuf_get_width(pixbuf);
	h = gdk_pixbuf_get_height(pixbuf);

	if (pw->proof_width / w < pw->proof_height / h)
		{
		icon_w = pw->proof_width;
		icon_h = pw->proof_width / w * h;
		}
	else
		{
		icon_h = pw->proof_height;
		icon_w = pw->proof_height / h * w;
		}

	y = pw->proof_position / pw->proof_columns;
	x = pw->proof_position - (y * pw->proof_columns);

	print_proof_size(pw, &proof_w, &proof_h);

	x *= proof_w;
	y *= proof_h;
	x += pw->margin_left + (pw->layout_width - pw->margin_left - pw->margin_right - (pw->proof_columns * proof_w)) / 2 + (proof_w - icon_w) / 2;
	y += pw->margin_top + PRINT_PROOF_MARGIN + (pw->proof_height - icon_h) / 2;

	success = (success &&
		   print_job_page_image(pw, pixbuf, x, y, icon_w, icon_h, 0, 0));

	x = x + icon_w / 2;
	y = y + icon_h + (pw->proof_height - icon_h) / 2 + PRINT_TEXT_PADDING;

	success = (success &&
		   print_job_text_image(pw, image_loader_get_fd(pw->job_loader)->path, x, y, icon_w + PRINT_PROOF_MARGIN * 2, w, h, TRUE));

	if (!success)
		{
		print_job_close(pw, TRUE);
		return;
		}

	if (pw->proof_point) pw->proof_point = pw->proof_point->next;

	pw->proof_position++;
	if (pw->proof_position >= pw->proof_columns * pw->proof_rows)
		{
		if (pw->job_format == RENDER_FORMAT_PREVIEW)
			{
			print_job_done(pw);
			return;
			}

		if (!print_job_page_done(pw))
			{
			print_job_close(pw, TRUE);
			return;
			}

		pw->proof_position = 0;
		pw->job_page++;
		if (print_job_render_proof(pw))
			{
			if (!print_job_page_new(pw))
				{
				print_job_close(pw, TRUE);
				return;
				}
			print_job_status(pw);
			}
		else
			{
			print_job_done(pw);
			}
		}
	else
		{
		if (print_job_render_proof(pw))
			{
			print_job_status(pw);
			}
		else
			{
			if (print_job_page_done(pw))
				{
				print_job_done(pw);
				}
			else
				{
				print_job_close(pw, TRUE);
				}
			}
		}
}

static gboolean print_job_render_proof(PrintWindow *pw)
{
	FileData *fd = NULL;

	if (pw->proof_columns < 1 || pw->proof_rows < 1) return FALSE;

	if (!pw->proof_point && pw->proof_position == 0 && pw->source == PRINT_SOURCE_IMAGE)
		{
		fd = pw->source_fd;
		}
	else if (pw->proof_point &&
		 pw->proof_position < pw->proof_columns * pw->proof_rows)
		{
		fd = pw->proof_point->data;
		}

	if (!fd) return FALSE;

	image_loader_free(pw->job_loader);
	pw->job_loader = image_loader_new(fd);
	g_signal_connect(G_OBJECT(pw->job_loader), "done", (GCallback)print_job_render_proof_loader_done, pw);
	if (!image_loader_start(pw->job_loader))
		{
		image_loader_free(pw->job_loader);
		pw->job_loader = NULL;
		}

	return TRUE;
}

static void print_job_render(PrintWindow *pw)
{
	gdouble proof_w, proof_h;
	gboolean finished;

	pw->proof_position = 0;

	switch (pw->source)
		{
		case PRINT_SOURCE_SELECTION:
			pw->proof_point = pw->source_selection;
			break;
		case PRINT_SOURCE_ALL:
			pw->proof_point = pw->source_list;
			break;
		case PRINT_SOURCE_IMAGE:
		default:
			pw->proof_point = NULL;
			break;
		}

	print_proof_size(pw, &proof_w, &proof_h);
	pw->proof_columns = (pw->layout_width - pw->margin_left - pw->margin_right) / proof_w;
	pw->proof_rows = (pw->layout_height - pw->margin_top - pw->margin_bottom) / proof_h;

	if (pw->job_format == RENDER_FORMAT_PREVIEW)
		{
		gint total;

		total = print_layout_page_count(pw);
		if (pw->job_page < 0 || pw->job_page >= total)
			{
			print_job_done(pw);
			return;
			}

		if (pw->proof_point && pw->job_page > 0)
			{
			pw->proof_point = g_list_nth(pw->proof_point, pw->job_page * pw->proof_columns * pw->proof_rows);
			}
		}

	if (!print_job_page_new(pw))
		{
		print_job_close(pw, TRUE);
		return;
		}

	if (pw->layout == PRINT_LAYOUT_IMAGE)
		{
		finished = !print_job_render_image(pw);
		}
	else
		{
		finished = !print_job_render_proof(pw);
		}

	if (finished) print_job_done(pw);
}

static gboolean print_job_init(PrintWindow *pw)
{
	gboolean success = FALSE;

	pw->job_page = 0;

	switch (pw->job_format)
		{
		case RENDER_FORMAT_RGB:
			success = print_job_rgb_init(pw);
			break;
		case RENDER_FORMAT_PS:
			success = print_job_ps_init(pw);
			break;
		case RENDER_FORMAT_PREVIEW:
			pw->job_page = pw->proof_page;
			success = print_job_preview_init(pw);
			break;
		}

	return success;
}

static gboolean print_job_finish(PrintWindow *pw)
{
	gboolean success = FALSE;

	switch (pw->job_format)
		{
		case RENDER_FORMAT_RGB:
			success = TRUE;
			break;
		case RENDER_FORMAT_PS:
			print_job_ps_end(pw);
			break;
		case RENDER_FORMAT_PREVIEW:
			success = TRUE;
			break;
		}

	return success;
}

static void print_job_close_file(PrintWindow *pw)
{
	if (pw->job_file)
		{
		fclose(pw->job_file);
		pw->job_file = NULL;
		}

	if (pw->job_pipe)
		{
		PipeError *pe;

		pe = pipe_handler_new();
		pclose(pw->job_pipe);
		pipe_handler_free(pe);

		pw->job_pipe = NULL;
		}
}

static gboolean print_job_close_finish_cb(gpointer data)
{
	PrintWindow *pw = data;

	print_window_close(pw);
	return FALSE;
}

static void print_job_close(PrintWindow *pw, gint error)
{
	if (!error) print_job_finish(pw);

	print_job_close_file(pw);
	g_free(pw->job_path);
	pw->job_path = NULL;

	if (pw->job_dialog)
		{
		generic_dialog_close(pw->job_dialog);
		pw->job_dialog = NULL;
		pw->job_progress = NULL;
		}

	image_loader_free(pw->job_loader);
	pw->job_loader = NULL;

	if (pw->job_pixbuf)
		{
		g_object_unref(pw->job_pixbuf);
		pw->job_pixbuf = NULL;
		}

	if (pw->dialog && !gtk_widget_get_visible(pw->dialog->dialog))
		{
		g_idle_add_full(G_PRIORITY_HIGH_IDLE, print_job_close_finish_cb, pw, NULL);
		}
}

static void print_job_cancel_cb(GenericDialog *gd, gpointer data)
{
	PrintWindow *pw = data;

	print_job_close(pw, FALSE);
}

static void print_pref_store(PrintWindow *pw)
{

	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_SAVE, pw->save_settings);

	if (!pw->save_settings) return;

	/* only store values that are actually used in this print job, hence the if()s */

	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_OUTPUT, pw->output);

	if (pw->output == PRINT_OUTPUT_RGB_FILE)
		{
		pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_FORMAT, pw->output_format);
		}

	if (pw->job_format == RENDER_FORMAT_PS)
		{
		pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_DPI, pw->max_dpi);
		}

	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_UNITS, pw->paper_units);
	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_SIZE, pw->paper_size);
	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_ORIENTATION, pw->paper_orientation);

	if (pw->paper_size == 0)
		{
		pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_CUSTOM_WIDTH, pw->paper_width);
		pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_CUSTOM_HEIGHT, pw->paper_height);
		}

	pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_MARGIN_LEFT, pw->margin_left);
	pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_MARGIN_RIGHT, pw->margin_right);
	pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_MARGIN_TOP, pw->margin_top);
	pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_MARGIN_BOTTOM, pw->margin_bottom);

	if (pw->layout == PRINT_LAYOUT_PROOF)
		{
		pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_PROOF_WIDTH, pw->proof_width);
		pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_PROOF_HEIGHT, pw->proof_height);
		}

	if (pw->output == PRINT_OUTPUT_PS_CUSTOM)
		{
		pref_list_string_set(PRINT_PREF_GROUP, PRINT_PREF_PRINTERC, pw->output_custom);
		}

	if (pw->output == PRINT_OUTPUT_RGB_FILE ||
	    pw->output == PRINT_OUTPUT_PS_FILE)
		{
		tab_completion_append_to_history(pw->path_entry, pw->output_path);
		}

	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_TEXT, pw->text_fields);
	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_TEXTSIZE, pw->text_points);
	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_TEXTCOLOR_R, pw->text_r);
	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_TEXTCOLOR_G, pw->text_g);
	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_TEXTCOLOR_B, pw->text_b);

	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_SOURCE, pw->source);
	pref_list_int_set(PRINT_PREF_GROUP, PRINT_PREF_LAYOUT, pw->layout);

	pref_list_double_set(PRINT_PREF_GROUP, PRINT_PREF_IMAGE_SCALE, pw->image_scale);
}

static gboolean print_job_start(PrintWindow *pw, RenderFormat format, PrintOutput output)
{
	GtkWidget *hbox;
	GtkWidget *spinner;
	gchar *msg;

	if (pw->job_dialog) return FALSE;

	pw->job_format = format;
	pw->job_output = output;

	if (!print_job_init(pw))
		{
		print_job_close(pw, TRUE);
		return FALSE;
		}

	if (format == RENDER_FORMAT_PREVIEW)
		{
		print_job_render(pw);
		return TRUE;
		}

	print_pref_store(pw);

	gtk_widget_hide(pw->dialog->dialog);

	pw->job_dialog = file_util_gen_dlg(_("Print"), "print_job_dialog",
					   (GtkWidget *)gtk_window_get_transient_for(GTK_WINDOW(pw->dialog->dialog)), FALSE,
					   print_job_cancel_cb, pw);

	msg = g_strdup_printf(_("Printing %d pages to %s."), print_layout_page_count(pw), print_output_name(pw->output));
	generic_dialog_add_message(pw->job_dialog, NULL, msg, NULL, TRUE);
	g_free(msg);

	if (pw->job_output == PRINT_OUTPUT_PS_FILE ||
	    pw->job_output == PRINT_OUTPUT_RGB_FILE)
		{
		hbox = pref_box_new(pw->job_dialog->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
		pref_label_new(hbox, _("Filename:"));

		pw->job_progress_label = pref_label_new(hbox, "");
		}
	else
		{
		pw->job_progress_label = NULL;
		}

	pref_spacer(pw->job_dialog->vbox, PREF_PAD_SPACE);

	hbox = pref_box_new(pw->job_dialog->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);

	pw->job_progress = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(hbox), pw->job_progress, TRUE, TRUE, 0);
	gtk_widget_show(pw->job_progress);

	spinner = spinner_new(NULL, SPINNER_SPEED);
	gtk_box_pack_start(GTK_BOX(hbox), spinner, FALSE, FALSE, 0);
	gtk_widget_show(spinner);

	gtk_widget_show(pw->job_dialog->dialog);

	print_job_render(pw);
	print_job_status(pw);

	return TRUE;
}

static void print_window_print_start(PrintWindow *pw)
{
	RenderFormat format;

	switch (pw->output)
		{
		case PRINT_OUTPUT_RGB_FILE:
			format = RENDER_FORMAT_RGB;
			break;
		case PRINT_OUTPUT_PS_FILE:
		case PRINT_OUTPUT_PS_CUSTOM:
		case PRINT_OUTPUT_PS_LPR:
		default:
			format = RENDER_FORMAT_PS;
			break;
		}

	print_job_start(pw, format, pw->output);
}


/*
 *-----------------------------------------------------------------------------
 * combo box util
 *-----------------------------------------------------------------------------
 */

static GtkWidget *print_combo_menu(const gchar *text[], gint count, gint preferred,
				   GCallback func, gpointer data)
{
	GtkWidget *combo;
	gint i;

	combo = gtk_combo_box_text_new();

	for (i = 0 ; i < count; i++)
		{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _(text[i]));
		}

	if (preferred >= 0 && preferred < count)
		{
		gtk_combo_box_set_active(GTK_COMBO_BOX(combo), preferred);
		}

	if (func) g_signal_connect(G_OBJECT(combo), "changed", func, data);

	return combo;
}


/*
 *-----------------------------------------------------------------------------
 * paper selection
 *-----------------------------------------------------------------------------
 */

static GtkWidget *print_paper_menu(GtkWidget *table, gint column, gint row,
				   PaperOrientation preferred, GCallback func, gpointer data)
{
	GtkWidget *combo;
	gint i;

	pref_table_label(table, column, row, (_("Format:")), 1.0);

	combo = gtk_combo_box_text_new();

	i = 0;
	while (print_paper_sizes[i].description)
		{
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _(print_paper_sizes[i].description));
		i++;
		}

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), preferred);
	if (func) g_signal_connect(G_OBJECT(combo), "changed", func, data);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(combo);

	return combo;
}

static void print_paper_select_cb(GtkWidget *combo, gpointer data)
{
	PrintWindow *pw = data;
	PaperSize *ps;
	gint n;

	n = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
	ps = print_paper_size_nth(n);

	if (!ps) return;

	pw->paper_size = n;

	if (pw->paper_size == 0)
		{
		print_window_layout_sync_paper(pw);
		return;
		}

	if (ps->orientation == PAPER_ORIENTATION_PORTRAIT)
		{
		print_window_layout_set_size(pw, ps->width, ps->height);
		}
	else
		{
		print_window_layout_set_size(pw, ps->height, ps->width);
		}
}

static void print_paper_size_cb(GtkWidget *spin, gpointer data)
{
	PrintWindow *pw = data;
	gdouble value;

	value = print_paper_size_convert_units(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)),
					       pw->paper_units, PAPER_UNIT_POINTS);

	if (spin == pw->paper_width_spin)
		{
		pw->paper_width = value;
		}
	else
		{
		pw->paper_height = value;
		}

	print_window_layout_set_size(pw, pw->paper_width, pw->paper_height);
}

static GtkWidget *print_paper_units_menu(GtkWidget *table, gint column, gint row,
					 PaperUnits units, GCallback func, gpointer data)
{
	GtkWidget *combo;

	pref_table_label(table, column, row, (_("Units:")), 1.0);

	combo = print_combo_menu(print_paper_units, PAPER_UNIT_COUNT, units, func, data);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(combo);

	return combo;
}

static void print_paper_units_set(PrintWindow *pw, PaperUnits units)
{
	PaperUnits old_units;

	if (units >= PAPER_UNIT_COUNT) return;

	old_units = pw->paper_units;
	pw->paper_units = units;
	print_window_layout_sync_paper(pw);

	if ((units == PAPER_UNIT_MM || units == PAPER_UNIT_CM) !=
	    (old_units == PAPER_UNIT_MM || old_units == PAPER_UNIT_CM))
		{
		print_window_layout_render(pw);
		}
}

static void print_paper_units_cb(GtkWidget *combo, gpointer data)
{
	PrintWindow *pw = data;
	PaperUnits units;

	units = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	print_paper_units_set(pw, units);
}

static GtkWidget *print_paper_orientation_menu(GtkWidget *table, gint column, gint row,
					       PaperOrientation preferred,
					       GCallback func, gpointer data)
{
	GtkWidget *combo;

	pref_table_label(table, column, row, (_("Orientation:")), 1.0);

	combo = print_combo_menu(print_paper_orientation, PAPER_ORIENTATION_COUNT, preferred, func, data);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(combo);

	return combo;
}

static void print_paper_orientation_cb(GtkWidget *combo, gpointer data)
{
	PrintWindow *pw = data;
	PaperOrientation o;

	o = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	print_window_layout_set_orientation(pw, o);
}

static void print_paper_margin_cb(GtkWidget *spin, gpointer data)
{
	PrintWindow *pw = data;
	gdouble value;

	value = print_paper_size_convert_units(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)),
					       pw->paper_units, PAPER_UNIT_POINTS);

	if (spin == pw->margin_left_spin)
		{
		pw->margin_left = CLAMP(value, 0.0, pw->paper_width);
		}
	else if (spin == pw->margin_right_spin)
		{
		pw->margin_right = CLAMP(value, 0.0, pw->paper_width);
		}
	else if (spin == pw->margin_top_spin)
		{
		pw->margin_top = CLAMP(value, 0.0, pw->paper_height);
		}
	else if (spin == pw->margin_bottom_spin)
		{
		pw->margin_bottom = CLAMP(value, 0.0, pw->paper_height);
		}

	print_window_layout_set_size(pw, pw->paper_width, pw->paper_height);
}

static GtkWidget *print_misc_menu(GtkWidget *parent_box, gint preferred,
				  const gchar *title, const gchar *key,
				  gint count, const gchar **text,
				  GCallback func, gpointer data)
{
	GtkWidget *box;
	GtkWidget *button = NULL;
	gint i;

	box = pref_group_new(parent_box, FALSE, title, GTK_ORIENTATION_VERTICAL);

	for (i = 0; i < count; i++)
		{
		button = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(button), _(text[i]));
		if (i == preferred)
			{
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
			}
		g_object_set_data(G_OBJECT(button), key, GINT_TO_POINTER(i));
		if (func) g_signal_connect(G_OBJECT(button), "clicked", func, data);
		gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
		gtk_widget_show(button);
		}

	return box;
}

static void print_source_select_cb(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;

	pw->source = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "print_source"));
	print_window_layout_size(pw);
}

static void print_layout_select_cb(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;

	pw->layout = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "print_layout"));

	print_window_layout_sync_layout(pw);
	print_window_layout_size(pw);
}

static void print_image_scale_cb(GtkWidget *spin, gpointer data)
{
	PrintWindow *pw = data;

	pw->image_scale = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin));

	print_window_layout_set_size(pw, pw->paper_width, pw->paper_height);
}

static void print_proof_size_cb(GtkWidget *spin, gpointer data)
{
	PrintWindow *pw = data;
	gdouble value;

	value = print_paper_size_convert_units(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)),
					       pw->paper_units, PAPER_UNIT_POINTS);

	if (spin == pw->proof_width_spin)
		{
		pw->proof_width = value;
		}
	else
		{
		pw->proof_height = value;
		}

	print_window_layout_render(pw);
}

static GtkWidget *print_output_menu(GtkWidget *table, gint column, gint row,
				    PrintOutput preferred, GCallback func, gpointer data)
{
	GtkWidget *combo;

	pref_table_label(table, column, row, (_("Destination:")), 1.0);

	combo = print_combo_menu(print_output_text, PRINT_OUTPUT_COUNT, preferred, func, data);

	gtk_table_attach(GTK_TABLE(table), combo, column + 1, column + 2, row, row + 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(combo);

	return combo;
}

static void print_custom_entry_set(PrintWindow *pw, GtkWidget *combo)
{
	GtkListStore *store;
	const gchar *text;
	GList *list;
	GList *work;

	store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(combo)));
	gtk_list_store_clear(store);

	list = print_window_list_printers();
	work = list;
	while (work)
		{
		gchar *name;
		gchar *buf;

		name = work->data;
		work = work->next;

		buf = g_strdup_printf(PRINT_LPR_CUSTOM, name);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), buf);
		g_free(buf);
		}
	string_list_free(list);

	if (pref_list_string_get(PRINT_PREF_GROUP, PRINT_PREF_PRINTERC, &text))
		{
		gtk_entry_set_text(GTK_ENTRY(pw->custom_entry), text);
		}
	else
		{
		text = gtk_entry_get_text(GTK_ENTRY(pw->custom_entry));
		if (!text || strlen(text) == 0)
			{
			gchar *buf;

			buf = g_strdup_printf(PRINT_LPR_CUSTOM, _("<printer name>"));
			gtk_entry_set_text(GTK_ENTRY(pw->custom_entry), buf);
			g_free(buf);
			}
		}
}

static void print_output_set(PrintWindow *pw, PrintOutput output)
{
	gboolean use_file = FALSE;
	gboolean use_custom = FALSE;
	gboolean use_format = FALSE;

	pw->output = output;

	switch (pw->output)
		{
		case PRINT_OUTPUT_RGB_FILE:
			use_file = TRUE;
			use_format = TRUE;
			break;
		case PRINT_OUTPUT_PS_FILE:
			use_file = TRUE;
			break;
		case PRINT_OUTPUT_PS_CUSTOM:
			use_custom = TRUE;
			break;
		case PRINT_OUTPUT_PS_LPR:
		default:
			break;
		}

	gtk_widget_set_sensitive(gtk_widget_get_parent(pw->path_entry), use_file);
	gtk_widget_set_sensitive(gtk_widget_get_parent(pw->custom_entry), use_custom);
	gtk_widget_set_sensitive(pw->path_format_menu, use_format);
	gtk_widget_set_sensitive(pw->max_dpi_menu, !use_format);
}

static void print_output_cb(GtkWidget *combo, gpointer data)
{
	PrintWindow *pw = data;
	PrintOutput output;

	output = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));

	print_output_set(pw, output);
}

static GtkWidget *print_output_format_menu(GtkWidget * table, gint column, gint row,
					   PrintFileFormat preferred, GCallback func, gpointer data)
{
	GtkWidget *combo;

	combo = print_combo_menu(print_file_format_text, PRINT_FILE_COUNT, preferred, func, data);

	gtk_table_attach(GTK_TABLE(table), combo, column, column + 1, row, row + 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(combo);

	return combo;
}

static void print_output_format_cb(GtkWidget *combo, gpointer data)
{
	PrintWindow *pw = data;

	pw->output_format = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
}

static GtkWidget *print_output_dpi_menu(GtkWidget * table, gint column, gint row,
					gdouble dpi, GCallback func, gpointer data)
{
	static gint dpilist[] = { 150, 300, 600, 1200, 0, -1};
	GtkWidget *combo;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	gint current = 1;
	gint i;

	store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);

	i = 0;
	while (dpilist[i] != -1)
		{
		GtkTreeIter iter;
		gchar *text;

		if (dpilist[i] == 0)
			{
			text = g_strdup(_("Unlimited"));
			}
		else
			{
			text = g_strdup_printf("%d", dpilist[i]);
			}

		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 0, text, 1, dpilist[i], -1);
		g_free(text);

		if (dpi == (gdouble)dpilist[i]) current = i;

		i++;
		}

	combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));
	g_object_unref(store);

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), current);
	if (func) g_signal_connect(G_OBJECT(combo), "changed", func, data);

	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), renderer, "text", 0, NULL);

	gtk_table_attach(GTK_TABLE(table), combo, column, column + 1, row, row + 1,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(combo);

	return combo;
}

static void print_output_dpi_cb(GtkWidget *combo, gpointer data)
{
	PrintWindow *pw = data;
	GtkTreeModel *store;
	GtkTreeIter iter;
	gint n = -1;

	store = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo), &iter)) return;
	gtk_tree_model_get(store, &iter, 1, &n, -1);

	pw->max_dpi = (gdouble)n;
}

static void print_text_field_set(PrintWindow *pw, TextInfo field, gboolean active)
{
	if (active)
		{
		pw->text_fields |= field;
		}
	else
		{
		pw->text_fields &= ~field;
		}

	print_window_layout_render(pw);
}

static void print_text_cb_name(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;
	gboolean active;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	print_text_field_set(pw, TEXT_INFO_FILENAME, active);
}

static void print_text_cb_path(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;
	gboolean active;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	print_text_field_set(pw, TEXT_INFO_FILEPATH, active);
}

static void print_text_cb_date(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;
	gboolean active;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	print_text_field_set(pw, TEXT_INFO_FILEDATE, active);
}

static void print_text_cb_size(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;
	gboolean active;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	print_text_field_set(pw, TEXT_INFO_FILESIZE, active);
}

static void print_text_cb_dims(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;
	gboolean active;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	print_text_field_set(pw, TEXT_INFO_DIMENSIONS, active);
}

static void print_text_cb_points(GtkWidget *widget, gpointer data)
{
	PrintWindow *pw = data;

	pw->text_points = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
	print_window_layout_render(pw);
}

static void print_text_menu(GtkWidget *box, PrintWindow *pw)
{
	GtkWidget *group;

	group = pref_group_new(box, FALSE, _("Show"), GTK_ORIENTATION_VERTICAL);

	pref_checkbox_new(group, _("Name"), (pw->text_fields & TEXT_INFO_FILENAME),
			  G_CALLBACK(print_text_cb_name), pw);
	pref_checkbox_new(group, _("Path"), (pw->text_fields & TEXT_INFO_FILEPATH),
			  G_CALLBACK(print_text_cb_path), pw);
	pref_checkbox_new(group, _("Date"), (pw->text_fields & TEXT_INFO_FILEDATE),
			  G_CALLBACK(print_text_cb_date), pw);
	pref_checkbox_new(group, _("Size"), (pw->text_fields & TEXT_INFO_FILESIZE),
			  G_CALLBACK(print_text_cb_size), pw);
	pref_checkbox_new(group, _("Dimensions"), (pw->text_fields & TEXT_INFO_DIMENSIONS),
			  G_CALLBACK(print_text_cb_dims), pw);

	group = pref_group_new(box, FALSE, _("Font"), GTK_ORIENTATION_VERTICAL);

	pref_spin_new(group, _("Size:"), _("points"),
		      8.0, 100.0, 1.0, 0, pw->text_points,
		      G_CALLBACK(print_text_cb_points), pw);
}

/*
 *-----------------------------------------------------------------------------
 * print window
 *-----------------------------------------------------------------------------
 */

static void print_window_close(PrintWindow *pw)
{
	print_window_layout_render_stop(pw);

	generic_dialog_close(pw->dialog);
	pw->dialog = NULL;

	print_job_close(pw, FALSE);

	file_data_unref(pw->source_fd);
	filelist_free(pw->source_selection);
	filelist_free(pw->source_list);

	g_free(pw->output_path);
	g_free(pw->output_custom);

	g_free(pw);
}

static void print_window_print_cb(GenericDialog *gd, gpointer data)
{
	PrintWindow *pw = data;

	switch (pw->output)
		{
		case PRINT_OUTPUT_RGB_FILE:
		case PRINT_OUTPUT_PS_FILE:
			g_free(pw->output_path);
			pw->output_path = g_strdup(gtk_entry_get_text(GTK_ENTRY(pw->path_entry)));
			break;
		case PRINT_OUTPUT_PS_CUSTOM:
			g_free(pw->output_custom);
			pw->output_custom = g_strdup(gtk_entry_get_text(GTK_ENTRY(pw->custom_entry)));
			break;
		case PRINT_OUTPUT_PS_LPR:
		default:
			break;
		}

	print_window_print_start(pw);
}

static void print_window_cancel_cb(GenericDialog *gd, gpointer data)
{
	PrintWindow *pw = data;

	print_window_close(pw);
}

static gint print_pref_int(const gchar *key, gint fallback)
{
	gint value;

	if (pref_list_int_get(PRINT_PREF_GROUP, key, &value)) return value;
	return fallback;
}

static gdouble print_pref_double(const gchar *key, gdouble fallback)
{
	gdouble value;

	if (pref_list_double_get(PRINT_PREF_GROUP, key, &value)) return value;
	return fallback;
}

void print_window_new(FileData *fd, GList *selection, GList *list, GtkWidget *parent)
{
	PrintWindow *pw;
	GdkGeometry geometry;
	GtkWidget *main_box;
	GtkWidget *vbox;
	GtkWidget *label;
	GtkWidget *combo;
	GtkWidget *box;
	GtkWidget *table;

	pw = g_new0(PrintWindow, 1);

	pw->source_fd = file_data_ref(fd);
	pw->source_selection = file_data_process_groups_in_selection(selection, FALSE, NULL);
	pw->source_list = list;

	pw->source = print_pref_int(PRINT_PREF_SOURCE, PRINT_SOURCE_SELECTION);
	pw->layout = print_pref_int(PRINT_PREF_LAYOUT, PRINT_LAYOUT_IMAGE);

	pw->image_scale = print_pref_double(PRINT_PREF_IMAGE_SCALE, 100.0);

	pw->output = print_pref_int(PRINT_PREF_OUTPUT, PRINT_OUTPUT_PS_LPR);
	pw->output_format = print_pref_int(PRINT_PREF_FORMAT, PRINT_FILE_JPG_NORMAL);

	pw->max_dpi = print_pref_double(PRINT_PREF_DPI, PRINT_PS_DPI_DEFAULT);

	pw->paper_units = print_pref_int(PRINT_PREF_UNITS, paper_unit_default());
	pw->paper_size = print_pref_int(PRINT_PREF_SIZE, 1);
	if (pw->paper_size == 0 ||
	    !print_paper_size_lookup(pw->paper_size, &pw->paper_width, &pw->paper_height))
		{
		pw->paper_width = print_pref_double(PRINT_PREF_CUSTOM_WIDTH, 360.0);
		pw->paper_height = print_pref_double(PRINT_PREF_CUSTOM_HEIGHT, 720.0);
		}
	pw->paper_orientation = print_pref_int(PRINT_PREF_ORIENTATION, PAPER_ORIENTATION_PORTRAIT);

	pw->margin_left = print_pref_double(PRINT_PREF_MARGIN_LEFT, PRINT_MARGIN_DEFAULT);
	pw->margin_right = print_pref_double(PRINT_PREF_MARGIN_RIGHT, PRINT_MARGIN_DEFAULT);
	pw->margin_top = print_pref_double(PRINT_PREF_MARGIN_TOP, PRINT_MARGIN_DEFAULT);
	pw->margin_bottom = print_pref_double(PRINT_PREF_MARGIN_BOTTOM, PRINT_MARGIN_DEFAULT);

	pw->proof_width = print_pref_double(PRINT_PREF_PROOF_WIDTH, PRINT_PROOF_DEFAULT_SIZE);
	pw->proof_height = print_pref_double(PRINT_PREF_PROOF_HEIGHT, PRINT_PROOF_DEFAULT_SIZE);

	pw->text_fields = print_pref_int(PRINT_PREF_TEXT, TEXT_INFO_FILENAME);
	pw->text_points = print_pref_int(PRINT_PREF_TEXTSIZE, 10);
	pw->text_r = print_pref_int(PRINT_PREF_TEXTCOLOR_R, 0);
	pw->text_g = print_pref_int(PRINT_PREF_TEXTCOLOR_G, 0);
	pw->text_b = print_pref_int(PRINT_PREF_TEXTCOLOR_B, 0);

	pw->save_settings = print_pref_int(PRINT_PREF_SAVE, TRUE);

	pw->dialog = file_util_gen_dlg(_("Print"), "print_dialog",
				       parent, FALSE,
				       print_window_cancel_cb, pw);

	geometry.min_width = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.min_height = DEFAULT_MINIMAL_WINDOW_SIZE;
	geometry.base_width = PRINT_DLG_WIDTH;
	geometry.base_height = PRINT_DLG_HEIGHT;
	gtk_window_set_geometry_hints(GTK_WINDOW(pw->dialog->dialog), NULL, &geometry,
				      GDK_HINT_MIN_SIZE | GDK_HINT_BASE_SIZE);

	pw->print_button = generic_dialog_add_button(pw->dialog, GTK_STOCK_PRINT, NULL, print_window_print_cb, TRUE);

	main_box = pref_box_new(pw->dialog->vbox, TRUE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_GAP);

	pw->notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(pw->notebook), GTK_POS_TOP);
	gtk_box_pack_start(GTK_BOX(main_box), pw->notebook, FALSE, FALSE, 0);

	/* layout tab */

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), PREF_PAD_BORDER);
	gtk_widget_show(vbox);
	label = gtk_label_new(_("Layout"));
	gtk_notebook_append_page(GTK_NOTEBOOK(pw->notebook), vbox, label);

	print_misc_menu(vbox, pw->source, _("Source"), "print_source",
			PRINT_SOURCE_COUNT, print_source_text,
			G_CALLBACK(print_source_select_cb), pw);

	box = print_misc_menu(vbox, pw->layout, _("Layout"), "print_layout",
			      PRINT_LAYOUT_COUNT, print_layout_text,
			      G_CALLBACK(print_layout_select_cb), pw);

	pref_spacer(box, PREF_PAD_GROUP);

	table = pref_table_new(box, 2, 2, FALSE, FALSE);

	pw->image_scale_spin = pref_table_spin(table, 0, 0, _("Image size:"), "%",
					       5.0, 100.0, 1.0, 0, pw->image_scale,
					       G_CALLBACK(print_image_scale_cb), pw);

	label = pref_table_label(table, 0, 1, _("Proof size:"), 1.0);
	pw->proof_group = pref_table_box(table, 1, 1, GTK_ORIENTATION_HORIZONTAL, NULL);
	pref_link_sensitivity(label, pw->proof_group);

	pw->proof_width_spin = pref_spin_new(pw->proof_group, NULL, NULL,
					     0.0, 50.0, 0.1, 3, 0.0,
					     G_CALLBACK(print_proof_size_cb), pw);
	pw->proof_height_spin = pref_spin_new(pw->proof_group, "x", NULL,
					      0.0, 50.0, 0.1, 3, 0.0,
					      G_CALLBACK(print_proof_size_cb), pw);

	/* text tab */

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), PREF_PAD_BORDER);
	gtk_widget_show(vbox);
	label = gtk_label_new(_("Text"));
	gtk_notebook_append_page(GTK_NOTEBOOK(pw->notebook), vbox, label);

	print_text_menu(vbox, pw);

	/* paper tab */

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), PREF_PAD_BORDER);
	gtk_widget_show(vbox);
	label = gtk_label_new(_("Paper"));
	gtk_notebook_append_page(GTK_NOTEBOOK(pw->notebook), vbox, label);

	table = pref_table_new(vbox, 2, 4, FALSE, FALSE);

	print_paper_menu(table, 0, 0, pw->paper_size, G_CALLBACK(print_paper_select_cb), pw);

	label = pref_table_label(table, 0, 1, (_("Size:")), 1.0);
	box = pref_table_box(table, 1, 1, GTK_ORIENTATION_HORIZONTAL, NULL);
	pw->paper_width_spin = pref_spin_new(box, NULL, NULL,
					     1.0, 10000.0, 1.0, 2, 66,
					     G_CALLBACK(print_paper_size_cb), pw);
	pw->paper_height_spin = pref_spin_new(box, "x", NULL,
					      1.0, 10000.0, 1.0, 2, 66,
					      G_CALLBACK(print_paper_size_cb), pw);
	pref_link_sensitivity(label, pw->paper_width_spin);

	pw->paper_units_menu = print_paper_units_menu(table, 0, 2, pw->paper_units,
					G_CALLBACK(print_paper_units_cb), pw);

	print_paper_orientation_menu(table, 0, 3, pw->paper_orientation,
				     G_CALLBACK(print_paper_orientation_cb), pw);

	box = pref_group_new(vbox, FALSE, _("Margins"), GTK_ORIENTATION_VERTICAL);
	table = pref_table_new(box, 4, 2, FALSE, FALSE);
	pw->margin_left_spin = pref_table_spin(table, 0, 0, _("Left:"), NULL,
					0.0, 50.0, 0.1, 3, 0.0,
					G_CALLBACK(print_paper_margin_cb), pw);
	pw->margin_right_spin = pref_table_spin(table, 2, 0, _("Right:"), NULL,
					0.0, 50.0, 0.1, 3, 0.0,
					G_CALLBACK(print_paper_margin_cb), pw);
	pw->margin_top_spin = pref_table_spin(table, 0, 1, _("Top:"), NULL,
					0.0, 50.0, 0.1, 3, 0.0,
					G_CALLBACK(print_paper_margin_cb), pw);
	pw->margin_bottom_spin = pref_table_spin(table, 2, 1, _("Bottom:"), NULL,
					0.0, 50.0, 0.1, 3, 0.0,
					G_CALLBACK(print_paper_margin_cb), pw);

	/* printer tab */

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), PREF_PAD_BORDER);
	gtk_widget_show(vbox);
	label = gtk_label_new(_("Printer"));
	gtk_notebook_append_page(GTK_NOTEBOOK(pw->notebook), vbox, label);

	table = pref_table_new(vbox, 2, 5, FALSE, FALSE);
	print_output_menu(table, 0, 0, pw->output, G_CALLBACK(print_output_cb), pw);

	label = pref_table_label(table, 0, 1, _("Custom printer:"), 1.0);
	combo = history_combo_new(&pw->custom_entry, NULL, "print_custom", -1);
	print_custom_entry_set(pw, combo);
	gtk_table_attach(GTK_TABLE(table), combo, 1, 2, 1, 2,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(combo);

	pref_link_sensitivity(label, combo);

	label = pref_table_label(table, 0, 2, _("File:"), 1.0);
	combo = tab_completion_new_with_history(&pw->path_entry, NULL, "print_path", -1, NULL, pw);
	tab_completion_add_select_button(pw->path_entry, NULL, FALSE);
	gtk_table_attach(GTK_TABLE(table), combo, 1, 2, 2, 3,
			 GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
	gtk_widget_show(combo);

	pref_link_sensitivity(label, combo);

	label = pref_table_label(table, 0, 3, _("File format:"), 1.0);
	pw->path_format_menu = print_output_format_menu(table, 1, 3, pw->output_format,
							G_CALLBACK(print_output_format_cb), pw);
	pref_link_sensitivity(label, pw->path_format_menu);

	label = pref_table_label(table, 0, 4, _("DPI:"), 1.0);
	pw->max_dpi_menu = print_output_dpi_menu(table, 1, 4, pw->max_dpi,
						 G_CALLBACK(print_output_dpi_cb), pw);
	pref_link_sensitivity(label, pw->max_dpi_menu);

	print_output_set(pw, pw->output);

	vbox = print_window_layout_setup(pw, main_box);
	pref_checkbox_new_int(vbox, _("Remember print settings"), pw->save_settings, &pw->save_settings);

	print_window_layout_sync_layout(pw);
	print_window_layout_sync_paper(pw);

	gtk_widget_show(pw->notebook);
	gtk_widget_show(pw->dialog->dialog);
}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
