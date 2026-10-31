/*
 * Copyright (C) 2018 The Geeqie Team
 *
 * Author: Colin Clark
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

#include "print.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include "exif.h"
#include "filedata.h"
#include "image-load.h"
#include "intl.h"
#include "main-defines.h"
#include "misc.h"
#include "options.h"
#include "osd.h"
#include "pixbuf-util.h"
#include "ui-fileops.h"
#include "ui-misc.h"

#define PRINT_SETTINGS "print_settings" // filename save printer settings
#define PAGE_SETUP "page_setup" // filename save page setup

/* padding between objects */
#define PRINT_TEXT_PADDING 3.0

/* method to use when scaling down image data */
#define PRINT_MAX_INTERP GDK_INTERP_BILINEAR

constexpr gdouble PRINT_PROOF_GAP = 5.0;

PrintProofLayout print_proof_layout(gdouble width, gdouble height, gdouble proof_width, gdouble proof_height)
{
	width = std::max(1.0, width);
	height = std::max(1.0, height);
	const gdouble cell_width = std::clamp(proof_width, 1.0, width);
	const gdouble cell_height = std::clamp(proof_height, 1.0, height);
	return {std::max(1, static_cast<gint>((width + PRINT_PROOF_GAP) / (cell_width + PRINT_PROOF_GAP))),
	        std::max(1, static_cast<gint>((height + PRINT_PROOF_GAP) / (cell_height + PRINT_PROOF_GAP))),
	        cell_width, cell_height};
}

gint print_proof_page_count(gint image_count, const PrintProofLayout &layout)
{
	const gint per_page = layout.columns * layout.rows;
	return (image_count / per_page) + (image_count % per_page != 0);
}

namespace
{

#if !PANGO_VERSION_CHECK(1,49,4)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PangoFontDescription, pango_font_description_free)
#endif

struct PrintWindow
{
	GtkWidget *vbox;
	GtkWidget *options_window;
	GList *source_selection;
	GtkWidget *proof_controls;
	GtkWidget *scale_control;
	PrintProofLayout proof_layout;
	gdouble page_text_height;
	gdouble page_width;
	gdouble page_height;

	gint job_page;
	GtkTextBuffer *page_text;
	GtkWidget *parent;
	ImageLoader *job_loader;

	GList *print_pixbuf_queue;
	gboolean job_render_finished;
	std::array<GtkWidget *, 4> image_group{};
	std::array<GtkWidget *, 4> page_group{};
};

constexpr gint PRE_FORMATTED_COLUMNS = 4;

gint print_layout_page_count(const PrintWindow *pw)
{
	const gint images = g_list_length(pw->source_selection);
	return options->printer.proof_sheet ? print_proof_page_count(images, pw->proof_layout) : images;
}

gboolean print_job_render_image(PrintWindow *pw);

void print_job_render_image_loader_done(ImageLoader *il, gpointer data)
{
	auto pw = static_cast<PrintWindow *>(data);
	GdkPixbuf *pixbuf;

	pixbuf = image_loader_get_pixbuf(il);

	GdkPixbuf *queued = pixbuf ? GDK_PIXBUF(g_object_ref(pixbuf)) : nullptr;
	if (queued && options->printer.proof_sheet)
		{
		// Proof sheets need only enough pixels for their small cells at 300 DPI.
		const gdouble limit = std::max(pw->proof_layout.cell_width, pw->proof_layout.cell_height) * 300.0 / 72.0;
		const gint width = gdk_pixbuf_get_width(queued);
		const gint height = gdk_pixbuf_get_height(queued);
		const gdouble scale = std::min(1.0, limit / std::max(width, height));
		if (scale < 1.0)
			{
			auto *scaled = gdk_pixbuf_scale_simple(queued, std::max(1, static_cast<gint>(width * scale)),
			                                       std::max(1, static_cast<gint>(height * scale)), PRINT_MAX_INTERP);
			g_object_unref(queued);
			queued = scaled;
			}
		}
	pw->print_pixbuf_queue = g_list_append(pw->print_pixbuf_queue, queued);

	g_clear_object(&pw->job_loader);

	pw->job_page++;

	if (!print_job_render_image(pw))
		{
		pw->job_render_finished = TRUE;
		}
}

gboolean print_job_render_image(PrintWindow *pw)
{
	while (auto *fd = static_cast<FileData *>(g_list_nth_data(pw->source_selection, pw->job_page)))
		{
		pw->job_loader = image_loader_new(fd);
		g_signal_connect(pw->job_loader, "done", G_CALLBACK(print_job_render_image_loader_done), pw);
		g_signal_connect(pw->job_loader, "error", G_CALLBACK(print_job_render_image_loader_done), pw);
		if (image_loader_start(pw->job_loader)) return TRUE;
		g_clear_object(&pw->job_loader);
		pw->print_pixbuf_queue = g_list_append(pw->print_pixbuf_queue, nullptr);
		pw->job_page++;
		}
	return FALSE;
}

void font_dialog_done(GObject *source, GAsyncResult *result, gpointer option)
{
	g_autoptr(PangoFontDescription) desc = gtk_font_dialog_choose_font_finish(GTK_FONT_DIALOG(source), result, nullptr);
	if (desc)
		{
		auto **font = static_cast<gchar **>(option);
		g_free(*font);
		*font = pango_font_description_to_string(desc);
		}
}

template<bool is_image>
void print_set_font_cb(GtkWidget *widget, gpointer data)
{
	g_autoptr(GtkFontDialog) dialog = gtk_font_dialog_new();
	gtk_font_dialog_set_title(dialog, is_image ? _("Image text font") : _("Page text font"));
	gtk_font_dialog_set_modal(dialog, TRUE);

	const gchar *font = *static_cast<gchar **>(data);
	g_autoptr(PangoFontDescription) desc = pango_font_description_from_string(font);

	gtk_font_dialog_choose_font(dialog, GTK_WINDOW(widget_get_toplevel(widget)),
	                            desc, nullptr, font_dialog_done, data);
}

gint set_toggle(const std::array<GtkWidget *, 4> &group, TextPosition pos)
{
	gint new_pos = - 1;

	GtkWidget *current_sel = group[pos];
	if (current_sel && gtk_check_button_get_active(GTK_CHECK_BUTTON(current_sel)))
		{
		new_pos = (pos - 1);
		if (new_pos < 0)
			{
			new_pos = HEADER_1;
			}
		GtkWidget *new_sel = group[new_pos];
		if (new_sel) gtk_check_button_set_active(GTK_CHECK_BUTTON(new_sel), TRUE);
		}
	return new_pos;
}

template<TextPosition pos>
void image_text_position_cb(GtkWidget *widget, gpointer data)
{
	if (!gtk_check_button_get_active(GTK_CHECK_BUTTON(widget))) return;

	auto pw = static_cast<PrintWindow *>(data);
	gint new_set = set_toggle(pw->page_group, pos);
	if (new_set >= 0)
		{
		options->printer.page_text_position = static_cast<TextPosition>(new_set);
		}

	options->printer.image_text_position = pos;
}

template<TextPosition pos>
void page_text_position_cb(GtkWidget *widget, gpointer data)
{
	if (!gtk_check_button_get_active(GTK_CHECK_BUTTON(widget))) return;

	auto pw = static_cast<PrintWindow *>(data);
	gint new_set = set_toggle(pw->image_group, pos);
	if (new_set >= 0)
		{
		options->printer.image_text_position = static_cast<TextPosition>(new_set);
		}

	options->printer.page_text_position = pos;
}

void image_text_template_view_changed_cb(GtkWidget *, gpointer data)
{
	g_free(options->printer.template_string);
	options->printer.template_string = text_widget_text_pull(static_cast<GtkWidget *>(data), TRUE);
}

void print_text_menu(GtkWidget *box, PrintWindow *pw)
{
	GtkWidget *group;
	GtkWidget *hbox;
	GtkWidget *button1;
	GtkWidget *button2;
	GtkWidget *image_text_button;
	GtkWidget *page_text_button;
	GtkWidget *subgroup;
	GtkWidget *page_text_view;
	GtkWidget *image_text_template_view;
	GtkWidget *scrolled_pre_formatted;
	GtkTextBuffer *buffer;

	group = pref_group_new(box, FALSE, _("Image text"), GTK_ORIENTATION_VERTICAL);

	image_text_button = pref_checkbox_new_int(group, _("Show image text"),
										options->printer.show_image_text, &options->printer.show_image_text);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);

	pref_checkbox_link_sensitivity(image_text_button, subgroup);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(subgroup), hbox);

	/* order is important */
	button1 = pref_radiobutton_new(hbox, nullptr,  _("Header 1"),
	                               options->printer.image_text_position == HEADER_1,
	                               G_CALLBACK(image_text_position_cb<HEADER_1>), pw);
	pw->image_group[HEADER_1] = button1;
	button1 = pref_radiobutton_new(hbox, button1,  _("Header 2"),
	                               options->printer.image_text_position == HEADER_2,
	                               G_CALLBACK(image_text_position_cb<HEADER_2>), pw);
	pw->image_group[HEADER_2] = button1;
	button1 = pref_radiobutton_new(hbox, button1, _("Footer 1"),
	                               options->printer.image_text_position == FOOTER_1,
	                               G_CALLBACK(image_text_position_cb<FOOTER_1>), pw);
	pw->image_group[FOOTER_1] = button1;
	button1 = pref_radiobutton_new(hbox, button1, _("Footer 2"),
	                               options->printer.image_text_position == FOOTER_2,
	                               G_CALLBACK(image_text_position_cb<FOOTER_2>), pw);
	pw->image_group[FOOTER_2] = button1;

	image_text_template_view = gtk_text_view_new();

	scrolled_pre_formatted = osd_new(PRE_FORMATTED_COLUMNS, image_text_template_view);
	gtk_box_append(GTK_BOX(subgroup), scrolled_pre_formatted);

	gtk_widget_set_tooltip_markup(image_text_template_view,
					_("Extensive formatting options are shown in the Help file"));

	GtkWidget *scrolled = gtk_scrolled_window_new();
	gtk_widget_set_size_request(scrolled, 200, 50);
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
									GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(subgroup))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(subgroup))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	if (gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(subgroup))) == GTK_ORIENTATION_HORIZONTAL)
		{
		gtk_widget_set_margin_end(scrolled, 5);
		}
	else
		{
		gtk_widget_set_margin_bottom(scrolled, 5);
		}
	gtk_box_append(GTK_BOX(subgroup), scrolled);

	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), image_text_template_view);

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(image_text_template_view));
	if (options->printer.template_string) gtk_text_buffer_set_text(buffer, options->printer.template_string, -1);
	g_signal_connect(G_OBJECT(buffer), "changed",
			 G_CALLBACK(image_text_template_view_changed_cb), image_text_template_view);

	hbox = pref_box_new(subgroup, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	pref_button_new(hbox, GQ_ICON_SELECT_FONT, _("Font"),
	                G_CALLBACK(print_set_font_cb<true>), &options->printer.image_font);

	pref_spacer(group, PREF_PAD_GAP);

	group = pref_group_new(box, FALSE, _("Page text"), GTK_ORIENTATION_VERTICAL);

	page_text_button = pref_checkbox_new_int(group, _("Show page text"),
					  options->printer.show_page_text, &options->printer.show_page_text);

	subgroup = pref_box_new(group, FALSE, GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	pref_checkbox_link_sensitivity(page_text_button, subgroup);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append(GTK_BOX(subgroup), hbox);

	/* order is important */
	button2 = pref_radiobutton_new(hbox, nullptr, _("Header 1"),
	                               options->printer.page_text_position == HEADER_1,
	                               G_CALLBACK(page_text_position_cb<HEADER_1>), pw);
	pw->page_group[HEADER_1] = button2;
	button2 = pref_radiobutton_new(hbox, button2,  _("Header 2"),
	                               options->printer.page_text_position == HEADER_2,
	                               G_CALLBACK(page_text_position_cb<HEADER_2>), pw);
	pw->page_group[HEADER_2] = button2;
	button2 = pref_radiobutton_new(hbox, button2, _("Footer 1"),
	                               options->printer.page_text_position == FOOTER_1,
	                               G_CALLBACK(page_text_position_cb<FOOTER_1>), pw);
	pw->page_group[FOOTER_1] = button2;
	button2 = pref_radiobutton_new(hbox, button2, _("Footer 2"),
	                               options->printer.page_text_position == FOOTER_2,
	                               G_CALLBACK(page_text_position_cb<FOOTER_2>), pw);
	pw->page_group[FOOTER_2] = button2;

	scrolled = gtk_scrolled_window_new();
	gtk_widget_set_size_request(scrolled, 50, 50);
	gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled), true);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_hexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(subgroup))) == GTK_ORIENTATION_HORIZONTAL ? TRUE : FALSE);
	gtk_widget_set_vexpand(scrolled, gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(subgroup))) == GTK_ORIENTATION_VERTICAL ? TRUE : FALSE);
	if (gtk_orientable_get_orientation(GTK_ORIENTABLE(GTK_BOX(subgroup))) == GTK_ORIENTATION_HORIZONTAL)
		{
		gtk_widget_set_margin_end(scrolled, 5);
		}
	else
		{
		gtk_widget_set_margin_bottom(scrolled, 5);
		}
	gtk_box_append(GTK_BOX(subgroup), scrolled);

	page_text_view = gtk_text_view_new();
	pw->page_text = gtk_text_view_get_buffer(GTK_TEXT_VIEW(page_text_view ));
	gtk_text_buffer_set_text(GTK_TEXT_BUFFER(pw->page_text), options->printer.page_text ? options->printer.page_text : "", -1);
	g_object_ref(pw->page_text);

	gtk_widget_set_tooltip_markup(page_text_view, (_("Text shown on each page of a single or multi-page print job")));
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), page_text_view);

	hbox = pref_box_new(subgroup, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_BUTTON_GAP);

	pref_button_new(hbox, GQ_ICON_SELECT_FONT, _("Font"),
	                G_CALLBACK(print_set_font_cb<false>), &options->printer.page_font);
}

gboolean paginate_cb(GtkPrintOperation *, GtkPrintContext *, gpointer data)
{
	auto pw = static_cast<PrintWindow *>(data);

	return pw->job_render_finished;
}

gchar *form_image_text(const gchar *template_string, FileData *fd, PrintWindow *pw, gint page_nr)
{
	if (!fd) return nullptr;

	OsdTemplate vars;

	const gchar *window_title = gtk_window_get_title(GTK_WINDOW(pw->parent));
	gchar *delimiter = g_strstr_len(window_title, -1, " - Collection - ");
	if (delimiter)
		{
		g_autofree gchar *collection_name = g_strndup(window_title, delimiter - window_title);
		osd_template_insert(vars, "collection", collection_name);
		}

	osd_template_insert(vars, "number", std::to_string(page_nr + 1).c_str());
	osd_template_insert(vars, "total", std::to_string(print_layout_page_count(pw)).c_str());
	osd_template_insert(vars, "name", fd->name);
	osd_template_insert(vars, "date", text_from_time(fd->date));

	g_autofree gchar *size_str = text_from_size_abrev(fd->size);
	osd_template_insert(vars, "size", size_str);

	if (fd->pixbuf)
		{
		gint w;
		gint h;
		w = gdk_pixbuf_get_width(fd->pixbuf);
		h = gdk_pixbuf_get_height(fd->pixbuf);

		osd_template_insert(vars, "width", std::to_string(w).c_str());
		osd_template_insert(vars, "height", std::to_string(h).c_str());

		g_autofree gchar *res_str = g_strdup_printf("%d × %d", w, h);
		osd_template_insert(vars, "res", res_str);
 		}
	else
		{
		osd_template_insert(vars, "width", nullptr);
		osd_template_insert(vars, "height", nullptr);
		osd_template_insert(vars, "res", nullptr);
		}

	return image_osd_mkinfo(template_string, fd, vars);
}

gchar *print_get_page_text(const PrintWindow *pw)
{
	return text_buffer_get_text(pw->page_text, FALSE);
}

PangoLayout *print_create_text_layout(cairo_t *cr, const gchar *text, const gchar *font, gdouble width, gdouble &height)
{
	height = 0;
	if (!text || !*text) return nullptr;
	auto *layout = pango_cairo_create_layout(cr);
	pango_cairo_context_set_resolution(pango_layout_get_context(layout), 72.0);
	g_autoptr(PangoFontDescription) desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	pango_layout_set_text(layout, text, -1);
	pango_layout_set_width(layout, static_cast<gint>(width * PANGO_SCALE));
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	gint text_height;
	pango_layout_get_pixel_size(layout, nullptr, &text_height);
	height = text_height + (PRINT_TEXT_PADDING * 2);
	return layout;
}

void print_draw_image(PrintWindow *pw, cairo_t *cr, gint index, gint page_nr,
                      gdouble x, gdouble y, gdouble width, gdouble height, bool page_text)
{
	auto *fd = static_cast<FileData *>(g_list_nth_data(pw->source_selection, index));
	auto *pixbuf = static_cast<GdkPixbuf *>(g_list_nth_data(pw->print_pixbuf_queue, index));
	g_autoptr(GdkPixbuf) rotated = nullptr;
	if (pixbuf && fd->exif_orientation != EXIF_ORIENTATION_TOP_LEFT)
		{
		rotated = pixbuf_apply_orientation(pixbuf, fd->exif_orientation);
		pixbuf = rotated;
		}
	gdouble image_text_height = 0;
	gdouble page_text_height = 0;
	g_autofree gchar *image_caption = options->printer.show_image_text ? form_image_text(options->printer.template_string, fd, pw, page_nr) : nullptr;
	g_autofree gchar *page_caption = page_text && options->printer.show_page_text ? print_get_page_text(pw) : nullptr;
	g_autoptr(PangoLayout) image_layout = print_create_text_layout(cr, image_caption, options->printer.image_font, width, image_text_height);
	g_autoptr(PangoLayout) page_layout = print_create_text_layout(cr, page_caption, options->printer.page_font, width, page_text_height);

	cairo_save(cr);
	cairo_rectangle(cr, x, y, width, height);
	cairo_clip(cr);
	gdouble top = y;
	gdouble bottom = y + height;
	const auto draw_caption = [cr, x, &top, &bottom](PangoLayout *layout, gdouble caption_height, TextPosition position)
		{
		if (!layout) return;
		const bool header = position >= HEADER_2;
		if (!header) bottom -= caption_height;
		cairo_move_to(cr, x, (header ? top : bottom) + PRINT_TEXT_PADDING);
		pango_cairo_show_layout(cr, layout);
		if (header) top += caption_height;
		};
	// Draw the outer header/footer first, then reserve the inner caption.
	const auto outer = [](TextPosition position) { return position == HEADER_1 || position == FOOTER_2; };
	if (outer(options->printer.page_text_position)) draw_caption(page_layout, page_text_height, options->printer.page_text_position);
	draw_caption(image_layout, image_text_height, options->printer.image_text_position);
	if (!outer(options->printer.page_text_position)) draw_caption(page_layout, page_text_height, options->printer.page_text_position);

	if (pixbuf && bottom > top)
		{
		const gdouble image_width = gdk_pixbuf_get_width(pixbuf);
		const gdouble image_height = gdk_pixbuf_get_height(pixbuf);
		const gdouble scale = std::min(width / image_width, (bottom - top) / image_height) * (options->printer.proof_sheet ? 1.0 : options->printer.image_scale / 100.0);
		cairo_translate(cr, x + ((width - (image_width * scale)) / 2), top + ((bottom - top - (image_height * scale)) / 2));
		cairo_scale(cr, scale, scale);
		cairo_surface_t *surface = pixbuf_to_cairo_surface(pixbuf);
		if (surface)
			{
			cairo_set_source_surface(cr, surface, 0, 0);
			cairo_paint(cr);
			cairo_surface_destroy(surface);
			}
		}
	cairo_restore(cr);
}

void draw_page(GtkPrintOperation *, GtkPrintContext *context, gint page_nr, gpointer data)
{
	auto *pw = static_cast<PrintWindow *>(data);
	auto *cr = gtk_print_context_get_cairo_context(context);
	const gdouble width = gtk_print_context_get_width(context);
	const gdouble height = gtk_print_context_get_height(context);
	if (!options->printer.proof_sheet)
		{
		print_draw_image(pw, cr, page_nr, page_nr, 0, 0, width, height, true);
		return;
		}

	gdouble caption_height = 0;
	g_autofree gchar *caption = options->printer.show_page_text ? print_get_page_text(pw) : nullptr;
	g_autoptr(PangoLayout) caption_layout = print_create_text_layout(cr, caption, options->printer.page_font, width, caption_height);
	const bool header = options->printer.page_text_position >= HEADER_2;
	if (caption_layout)
		{
		cairo_move_to(cr, 0, (header ? 0 : height - caption_height) + PRINT_TEXT_PADDING);
		pango_cairo_show_layout(cr, caption_layout);
		}
	const auto &grid = pw->proof_layout;
	const gint per_page = grid.columns * grid.rows;
	const gint count = g_list_length(pw->source_selection);
	const gdouble offset_x = (width - grid.columns * grid.cell_width - (grid.columns - 1) * PRINT_PROOF_GAP) / 2;
	const gdouble offset_y = (header ? caption_height : 0) + ((height - caption_height - (grid.rows * grid.cell_height) - ((grid.rows - 1) * PRINT_PROOF_GAP)) / 2);
	for (gint slot = 0; slot < per_page && (page_nr * per_page) + slot < count; ++slot)
		{
		const gint row = slot / grid.columns;
		print_draw_image(pw, cr, (page_nr * per_page) + slot, page_nr,
		                 offset_x + ((slot % grid.columns) * (grid.cell_width + PRINT_PROOF_GAP)),
		                 offset_y + (row * (grid.cell_height + PRINT_PROOF_GAP)),
		                 grid.cell_width, grid.cell_height, false);
		}
}

void begin_print(GtkPrintOperation *operation, GtkPrintContext *context, gpointer user_data)
{
	auto *pw = static_cast<PrintWindow *>(user_data);
	pw->page_width = gtk_print_context_get_width(context);
	pw->page_height = gtk_print_context_get_height(context);
	g_autofree gchar *caption = options->printer.show_page_text ? print_get_page_text(pw) : nullptr;
	g_autoptr(PangoLayout) caption_layout = print_create_text_layout(gtk_print_context_get_cairo_context(context), caption,
	                                                              options->printer.page_font, pw->page_width, pw->page_text_height);
	pw->proof_layout = print_proof_layout(pw->page_width, pw->page_height - pw->page_text_height,
	                                     options->printer.proof_width, options->printer.proof_height);
	gtk_print_operation_set_n_pages(operation, print_layout_page_count(pw));
	pw->job_render_finished = !print_job_render_image(pw);
}

void print_layout_changed_cb(GtkDropDown *drop_down, GParamSpec *, gpointer data)
{
	auto *pw = static_cast<PrintWindow *>(data);
	options->printer.proof_sheet = gtk_drop_down_get_selected(drop_down) == 1;
	gtk_widget_set_sensitive(pw->proof_controls, options->printer.proof_sheet);
	gtk_widget_set_sensitive(pw->scale_control, !options->printer.proof_sheet);
}

void print_layout_menu(GtkWidget *box, PrintWindow *pw)
{
	auto *group = pref_group_new(box, FALSE, _("Layout"), GTK_ORIENTATION_VERTICAL);
	auto *row = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(row, _("Layout:"));
	const gchar *layouts[] = {_("One image per page"), _("Proof sheet"), nullptr};
	auto *layout_menu = gtk_drop_down_new_from_strings(layouts);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(layout_menu), options->printer.proof_sheet ? 1 : 0);
	gtk_box_append(GTK_BOX(row), layout_menu);
	pw->scale_control = pref_spin_new_int(group, _("Image size:"), "%", 5, 100, 1, options->printer.image_scale, &options->printer.image_scale);
	gtk_widget_set_sensitive(pw->scale_control, !options->printer.proof_sheet);
	pw->proof_controls = pref_box_new(group, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_spin_new_int(pw->proof_controls, _("Proof width:"), _("points"), 8, 720, 1, options->printer.proof_width, &options->printer.proof_width);
	pref_spin_new_int(pw->proof_controls, _("Height:"), _("points"), 8, 720, 1, options->printer.proof_height, &options->printer.proof_height);
	gtk_widget_set_sensitive(pw->proof_controls, options->printer.proof_sheet);
	g_signal_connect(layout_menu, "notify::selected", G_CALLBACK(print_layout_changed_cb), pw);
}


GtkPrintSettings *load_print_settings()
{
	GtkPrintSettings *settings = gtk_print_settings_new();

	const gchar *dir = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
	if (dir == nullptr)
		{
		dir = g_get_home_dir();
		}
	g_autofree gchar *uri = g_build_filename("file:/", dir, "geeqie-file.pdf", NULL);
	gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_URI, uri);

	g_autofree gchar *print_settings_path = g_build_filename(get_rc_dir(), PRINT_SETTINGS, NULL);
	g_autoptr(GError) error = nullptr;
	gtk_print_settings_load_file(settings, print_settings_path, &error);
	if (error)
		{
		log_printf("Error: Printer settings load failed:\n%s", error->message); // NOTRANSLATE: Diagnostic message.
		}

	return settings;
}

void save_print_settings(GtkPrintOperation *operation)
{
	GtkPrintSettings *print_settings = gtk_print_operation_get_print_settings(operation);
	g_autofree gchar *print_settings_path = g_build_filename(get_rc_dir(), PRINT_SETTINGS, NULL);

	g_autoptr(GError) error = nullptr;
	gtk_print_settings_to_file(print_settings, print_settings_path, &error);
	if (error)
		{
		log_printf("Error: Print settings save failed:\n%s", error->message); // NOTRANSLATE: Diagnostic message.
		}
}

GtkPageSetup *load_page_setup()
{
	GtkPageSetup *page_setup = gtk_page_setup_new();

	g_autofree gchar *page_setup_path = g_build_filename(get_rc_dir(), PAGE_SETUP, NULL);
	g_autoptr(GError) error = nullptr;
	gtk_page_setup_load_file(page_setup, page_setup_path, &error);
	if (error)
		{
		log_printf("Error: Print page setup load failed:\n%s", error->message); // NOTRANSLATE: Diagnostic message.
		}

	return page_setup;
}

void save_page_setup(GtkPrintOperation *operation)
{
	GtkPageSetup *page_setup = gtk_print_operation_get_default_page_setup(operation);
	g_autofree gchar *page_setup_path = g_build_filename(get_rc_dir(), PAGE_SETUP, NULL);

	g_autoptr(GError) error = nullptr;
	gtk_page_setup_to_file(page_setup, page_setup_path, &error);
	if (error)
		{
		log_printf("Error: Print page setup save failed:\n%s", error->message); // NOTRANSLATE: Diagnostic message.
		}
}

void print_window_free(PrintWindow *pw)
{
	if (!pw) return;

	for (GList *work = pw->print_pixbuf_queue; work; work = work->next)
		{
		auto *pixbuf = static_cast<GdkPixbuf *>(work->data);
		if (pixbuf)
			{
			g_object_unref(pixbuf);
			}
		}

	g_list_free(pw->print_pixbuf_queue);
	g_clear_object(&pw->job_loader);
	file_data_list_free(pw->source_selection);
	if (pw->options_window) gtk_window_destroy(GTK_WINDOW(pw->options_window));
	g_clear_object(&pw->vbox);
	g_clear_object(&pw->parent);
	g_object_unref(pw->page_text);
	g_free(pw);
}

void end_print_cb(GtkPrintOperation *operation, GtkPrintContext *, gpointer data)
{
	save_print_settings(operation);
	save_page_setup(operation);

	auto *pw = static_cast<PrintWindow *>(data);

	g_free(options->printer.page_text);
	options->printer.page_text = print_get_page_text(pw);

}

void print_done_cb(GtkPrintOperation *operation, GtkPrintOperationResult result, gpointer data)
{
	auto *pw = static_cast<PrintWindow *>(data);
	if (result == GTK_PRINT_OPERATION_RESULT_ERROR)
		{
		g_autoptr(GError) error = nullptr;
		gtk_print_operation_get_error(operation, &error);
		if (error)
			{
			g_autoptr(GtkAlertDialog) dialog = gtk_alert_dialog_new("%s", error->message);
			gtk_alert_dialog_show(dialog, GTK_WINDOW(pw->parent));
			}
		}
	print_window_free(pw);
	g_object_unref(operation);
}

void print_options_cancel_cb(GtkButton *, gpointer data)
{
	print_window_free(static_cast<PrintWindow *>(data));
}

gboolean print_options_close_cb(GtkWindow *, gpointer data)
{
	print_window_free(static_cast<PrintWindow *>(data));
	return TRUE;
}

gboolean print_options_key_press_cb(GtkEventControllerKey *, guint keyval, guint, GdkModifierType, gpointer data)
{
	if (keyval != GDK_KEY_Escape) return FALSE;

	print_options_cancel_cb(nullptr, data);
	return TRUE;
}

void print_options_continue_cb(GtkButton *, gpointer data)
{
	auto *pw = static_cast<PrintWindow *>(data);
	gtk_widget_set_visible(pw->options_window, FALSE);
	GtkPrintOperation *operation = gtk_print_operation_new();

	gtk_print_operation_set_use_full_page(operation, FALSE);
	gtk_print_operation_set_unit(operation, GTK_UNIT_POINTS);
	gtk_print_operation_set_embed_page_setup(operation, TRUE);
	gtk_print_operation_set_allow_async (operation, TRUE);

	g_autoptr(GtkPrintSettings) settings = load_print_settings();
	gtk_print_operation_set_print_settings(operation, settings);

	g_autoptr(GtkPageSetup) page_setup = load_page_setup();
	gtk_print_operation_set_default_page_setup(operation, page_setup);

	g_signal_connect (G_OBJECT (operation), "begin-print",
					G_CALLBACK (begin_print), pw);
	g_signal_connect (G_OBJECT (operation), "draw-page",
					G_CALLBACK (draw_page), pw);
	g_signal_connect (G_OBJECT (operation), "end-print",
					G_CALLBACK (end_print_cb), pw);
	g_signal_connect (G_OBJECT (operation), "paginate",
					G_CALLBACK (paginate_cb), pw);

	g_signal_connect(operation, "done", G_CALLBACK(print_done_cb), pw);

	g_autoptr(GError) error = nullptr;
	gtk_print_operation_run(operation, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
												GTK_WINDOW (pw->parent), &error);

}

} // namespace

/**
 * @brief Print the supplied selection.
 * Takes ownership of the selection.
 */
void print_window_new(GList *selection, GtkWidget *parent)
{
	selection = file_data_process_groups_in_selection(selection, FALSE, nullptr);
	if (!selection) return;

	auto *pw = g_new0(PrintWindow, 1);
	pw->source_selection = selection;
	pw->parent = GTK_WIDGET(g_object_ref(parent));

	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_margin_top(vbox, PREF_PAD_BORDER);
	gtk_widget_set_margin_bottom(vbox, PREF_PAD_BORDER);
	gtk_widget_set_margin_start(vbox, PREF_PAD_BORDER);
	gtk_widget_set_margin_end(vbox, PREF_PAD_BORDER);

	print_layout_menu(vbox, pw);
	print_text_menu(vbox, pw);
	auto *scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 400);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scrolled), 600);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), vbox);
	pw->vbox = GTK_WIDGET(g_object_ref_sink(scrolled));

	pw->print_pixbuf_queue = nullptr;
	pw->job_render_finished = FALSE;
	pw->job_page = 0;

	pw->options_window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(pw->options_window), _("Print options"));
	gtk_window_set_transient_for(GTK_WINDOW(pw->options_window), GTK_WINDOW(parent));
	gtk_window_set_modal(GTK_WINDOW(pw->options_window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(pw->options_window), 700, 600);
	g_signal_connect(pw->options_window, "close-request", G_CALLBACK(print_options_close_cb), pw);
	auto *key_controller = gtk_event_controller_key_new();
	g_signal_connect(key_controller, "key-pressed", G_CALLBACK(print_options_key_press_cb), pw);
	gtk_widget_add_controller(pw->options_window, key_controller);
	auto *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, PREF_PAD_GAP);
	gtk_window_set_child(GTK_WINDOW(pw->options_window), box);
	gtk_widget_set_vexpand(pw->vbox, TRUE);
	gtk_box_append(GTK_BOX(box), pw->vbox);
	auto *buttons = pref_box_new(box, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_widget_set_margin_end(buttons, PREF_PAD_BORDER);
	gtk_widget_set_margin_bottom(buttons, PREF_PAD_BORDER);
	auto *cancel = gtk_button_new_with_mnemonic(_("_Cancel"));
	g_signal_connect(cancel, "clicked", G_CALLBACK(print_options_cancel_cb), pw);
	gtk_box_append(GTK_BOX(buttons), cancel);
	auto *continue_button = gtk_button_new_with_mnemonic(_("_Continue"));
	g_signal_connect(continue_button, "clicked", G_CALLBACK(print_options_continue_cb), pw);
	gtk_box_append(GTK_BOX(buttons), continue_button);
	gtk_window_set_default_widget(GTK_WINDOW(pw->options_window), continue_button);
	gtk_window_set_focus(GTK_WINDOW(pw->options_window), continue_button);
	gtk_window_present(GTK_WINDOW(pw->options_window));

}
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
