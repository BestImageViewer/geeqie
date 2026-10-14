/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "image-crop.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include <config.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "color-man.h"
#include "exif.h"
#include "filedata.h"
#include "filefilter.h"
#include "image.h"
#include "intl.h"
#include "main-defines.h"
#include "pixbuf-util.h"
#include "ui-file-chooser.h"
#include "ui-fileops.h"
#include "ui-misc.h"
#include "ui-utildlg.h"

namespace
{

struct CropDialog
{
	GenericDialog *dialog;
	FileData *source;
	GdkPixbuf *crop;
	gchar *profile;
	GtkWidget *format;
	GtkWidget *quality_widget;
	gint quality = 90;
	gboolean metadata = TRUE;
};

bool is_source_file(const gchar *source, const gchar *destination)
{
	g_autoptr(GFile) source_file = g_file_new_for_path(source);
	g_autoptr(GFile) destination_file = g_file_new_for_path(destination);
	if (g_file_equal(source_file, destination_file)) return true;

	struct stat source_stat;
	struct stat destination_stat;
	return g_stat(source, &source_stat) == 0 && g_stat(destination, &destination_stat) == 0 &&
	       source_stat.st_dev == destination_stat.st_dev && source_stat.st_ino == destination_stat.st_ino;
}

void crop_dialog_free(CropDialog *data)
{
	generic_dialog_close(data->dialog);
	file_data_unref(data->source);
	g_object_unref(data->crop);
	g_free(data->profile);
	delete data;
}

void crop_cancel_cb(GenericDialog *, gpointer data)
{
	crop_dialog_free(static_cast<CropDialog *>(data));
}

void crop_format_changed_cb(GObject *, GParamSpec *, gpointer data)
{
	auto *crop = static_cast<CropDialog *>(data);
	gtk_widget_set_sensitive(crop->quality_widget, gtk_drop_down_get_selected(GTK_DROP_DOWN(crop->format)) == 0);
}

void crop_file_selected_cb(GFile *file, gpointer data)
{
	auto *crop = static_cast<CropDialog *>(data);
	gtk_widget_set_sensitive(crop->dialog->dialog, TRUE);
	if (!file) return;

	g_autofree gchar *path = g_file_get_path(file);
	g_autoptr(GError) error = nullptr;
	const gboolean png = gtk_drop_down_get_selected(GTK_DROP_DOWN(crop->format)) == 1;
	if (!path)
		g_set_error_literal(&error, G_FILE_ERROR, G_FILE_ERROR_INVAL, _("Choose a local file for the cropped copy."));
	else
		image_crop_save(crop->source, crop->crop, path, png, crop->quality, crop->profile, crop->metadata, &error);
	if (error)
		{
		warning_dialog(_("Unable to save cropped copy"), error->message, GQ_ICON_DIALOG_ERROR, crop->dialog->dialog);
		return;
		}
	crop_dialog_free(crop);
}

void crop_save_cb(GenericDialog *, gpointer data)
{
	auto *crop = static_cast<CropDialog *>(data);
	const gboolean png = gtk_drop_down_get_selected(GTK_DROP_DOWN(crop->format)) == 1;
	g_autofree gchar *stem = remove_extension_from_path(crop->source->name);
	g_autofree gchar *name = g_strdup_printf("%s-crop.%s", stem, png ? "png" : "jpg");
	g_autofree gchar *directory = remove_level_from_path(crop->source->path);
	FileDialogData chooser = {};
	chooser.action = FileDialogAction::SAVE;
	chooser.callback = crop_file_selected_cb;
	chooser.data = crop;
	chooser.parent = GTK_WINDOW(crop->dialog->dialog);
	chooser.title = _("Save cropped copy");
	chooser.accept_text = _("Save");
	chooser.suggested_name = name;
	chooser.filename = directory;
	chooser.filter = png ? "*.png" : "*.jpg;*.jpeg";
	chooser.filter_description = png ? _("PNG images") : _("JPEG images");
	gtk_widget_set_sensitive(crop->dialog->dialog, FALSE);
	file_dialog_show(chooser);
}

} // namespace

GdkPixbuf *image_crop_pixbuf(GdkPixbuf *source, const GdkRectangle &rectangle, gint orientation, GError **error)
{
	if (!source || rectangle.x < 0 || rectangle.y < 0 || rectangle.width < 1 || rectangle.height < 1 ||
	    rectangle.x > gdk_pixbuf_get_width(source) - rectangle.width ||
	    rectangle.y > gdk_pixbuf_get_height(source) - rectangle.height)
		{
		g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, _("The crop rectangle is outside the loaded image."));
		return nullptr;
		}
	g_autoptr(GdkPixbuf) region = gdk_pixbuf_new_subpixbuf(source, rectangle.x, rectangle.y, rectangle.width, rectangle.height);
	if (orientation >= EXIF_ORIENTATION_TOP_RIGHT && orientation <= EXIF_ORIENTATION_LEFT_BOTTOM)
		return pixbuf_apply_orientation(region, orientation);
	return gdk_pixbuf_copy(region);
}

gboolean image_crop_save(FileData *source, GdkPixbuf *crop, const gchar *destination,
                         gboolean png, gint quality, const gchar *profile, gboolean metadata, GError **error)
{
	if (is_source_file(source->path, destination))
		{
		g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, _("Choose a different filename. The original image must be preserved."));
		return FALSE;
		}
	const gchar *extension = strrchr(destination, '.');
	if (!extension || (png ? g_ascii_strcasecmp(extension, ".png") != 0
	    : g_ascii_strcasecmp(extension, ".jpg") != 0 && g_ascii_strcasecmp(extension, ".jpeg") != 0))
		{
		g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, _("The filename extension must match the selected JPEG or PNG format."));
		return FALSE;
		}

	g_autofree gchar *directory = g_path_get_dirname(destination);
	g_autofree gchar *temporary = g_build_filename(directory, ".geeqie-crop-XXXXXX", nullptr);
	const gint descriptor = g_mkstemp(temporary);
	if (descriptor < 0)
		{
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "%s", g_strerror(errno));
		return FALSE;
		}
	close(descriptor);
	g_autoptr(GdkPixbuf) opaque = nullptr;
	GdkPixbuf *output = crop;
	if (!png && gdk_pixbuf_get_has_alpha(crop))
		{
		opaque = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, gdk_pixbuf_get_width(crop), gdk_pixbuf_get_height(crop));
		gdk_pixbuf_fill(opaque, 0xffffffff);
		gdk_pixbuf_composite(crop, opaque, 0, 0, gdk_pixbuf_get_width(crop), gdk_pixbuf_get_height(crop),
		                     0, 0, 1, 1, GDK_INTERP_NEAREST, 255);
		output = opaque;
		}
	g_autofree gchar *quality_text = g_strdup_printf("%d", std::clamp(quality, 1, 100));
	const gchar *keys[] = {png ? "compression" : "quality", profile ? "icc-profile" : nullptr, nullptr};
	const gchar *values[] = {png ? "6" : quality_text, profile, nullptr};
	gboolean saved = gdk_pixbuf_savev(output, temporary, png ? "png" : "jpeg",
	                                  const_cast<gchar **>(keys), const_cast<gchar **>(values), error);
	if (saved && metadata)
		saved = exif_copy_crop_metadata(source, temporary, gdk_pixbuf_get_width(output), gdk_pixbuf_get_height(output), error);
	if (saved && g_rename(temporary, destination) != 0)
		{
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "%s", g_strerror(errno));
		saved = FALSE;
		}
	g_unlink(temporary);
	return saved;
}

void image_crop_dialog_show(ImageWindow *imd, GtkWidget *parent)
{
	GdkRectangle rectangle;
	if (!imd || !imd->image_fd || !image_get_selected_rectangle(imd, rectangle))
		{
		warning_dialog(_("Save cropped copy"), _("Select a rectangle on the image first."), GQ_ICON_DIALOG_INFO, parent);
		return;
		}
	if (!imd->completed)
		{
		warning_dialog(_("Save cropped copy"), _("Wait for the image to finish loading before saving a cropped copy."), GQ_ICON_DIALOG_INFO, parent);
		return;
		}
	g_autoptr(GError) error = nullptr;
	GdkPixbuf *source = image_get_pixbuf(imd);
	GdkPixbuf *pixbuf = image_crop_pixbuf(source, rectangle, imd->orientation, &error);
	if (!pixbuf)
		{
		warning_dialog(_("Unable to crop image"), error->message, GQ_ICON_DIALOG_ERROR, parent);
		return;
		}
	auto *crop = new CropDialog{};
	crop->source = file_data_ref(imd->image_fd);
	crop->crop = pixbuf;
	crop->profile = g_strdup(gdk_pixbuf_get_option(source, "icc-profile"));
	if (!crop->profile)
		{
		ColorManProfileType type;
		auto profile = exif_get_color_profile(crop->source, type);
		if (profile.ptr) crop->profile = g_base64_encode(profile.ptr.get(), profile.len);
		}
	crop->dialog = generic_dialog_new(_("Save cropped copy"), "crop_output", parent, FALSE, crop_cancel_cb, crop);
	gtk_window_set_modal(GTK_WINDOW(crop->dialog->dialog), TRUE);
	g_autofree gchar *dimensions = g_strdup_printf(_("Cropped copy: %d × %d pixels"), gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
	gtk_box_append(GTK_BOX(crop->dialog->vbox), gtk_label_new(dimensions));
	if (crop->source->format_class == FORMAT_CLASS_RAWIMAGE)
		{
		gtk_box_append(GTK_BOX(crop->dialog->vbox), gtk_label_new(_("This copy uses the loaded RAW preview, not the full sensor data.")));
		}
	const gchar *formats[] = {_("JPEG"), _("PNG"), nullptr};
	auto *format_row = pref_box_new(crop->dialog->vbox, FALSE, GTK_ORIENTATION_HORIZONTAL, PREF_PAD_SPACE);
	pref_label_new(format_row, _("Format:"));
	crop->format = gtk_drop_down_new_from_strings(formats);
	gtk_widget_set_tooltip_text(crop->format, _("Output format"));
	gtk_accessible_update_property(GTK_ACCESSIBLE(crop->format), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Output format"), -1);
	gtk_box_append(GTK_BOX(format_row), crop->format);
	crop->quality_widget = pref_spin_new_int(crop->dialog->vbox, _("JPEG quality:"), nullptr, 1, 100, 1, 90, &crop->quality);
	g_signal_connect(crop->format, "notify::selected", G_CALLBACK(crop_format_changed_cb), crop);
	if (gdk_pixbuf_get_has_alpha(pixbuf))
		gtk_box_append(GTK_BOX(crop->dialog->vbox), gtk_label_new(_("PNG preserves transparency. JPEG uses a white background.")));
#if HAVE_EXIV2
	pref_checkbox_new_int(crop->dialog->vbox, _("Preserve metadata"), TRUE, &crop->metadata);
#else
	crop->metadata = FALSE;
#endif
	GtkWidget *save = generic_dialog_add_button(crop->dialog, GQ_ICON_SAVE, _("Save cropped copy…"), crop_save_cb, TRUE);
	gtk_window_set_focus(GTK_WINDOW(crop->dialog->dialog), save);
	gtk_window_present(GTK_WINDOW(crop->dialog->dialog));
}
