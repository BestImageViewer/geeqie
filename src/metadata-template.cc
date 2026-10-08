/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "metadata-template.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <config.h>

#include "exif.h"
#include "filedata.h"
#include "intl.h"
#include "main-defines.h"
#include "metadata.h"
#include "ui-fileops.h"
#include "ui-utildlg.h"

namespace
{

struct TemplateField
{
	const gchar *label;
	const gchar *key;
	bool list = false;
};

constexpr std::array<TemplateField, 6> default_fields{{
	{N_("Description"), COMMENT_KEY},
	{N_("Headline"), "Xmp.photoshop.Headline"},
	{N_("Keywords"), KEYWORD_KEY, true},
	{N_("Copyright"), "Xmp.dc.rights"},
	{N_("Creator"), "Xmp.dc.creator"},
	{N_("Credit"), "Xmp.photoshop.Credit"}
}};

constexpr std::array<TemplateField, 9> suggested_fields{{
	{N_("Title"), "Xmp.dc.title"},
	{N_("City"), "Xmp.photoshop.City"},
	{N_("State/Province"), "Xmp.photoshop.State"},
	{N_("Country"), "Xmp.photoshop.Country"},
	{N_("Country code"), "Xmp.iptc.CountryCode"},
	{N_("Instructions"), "Xmp.photoshop.Instructions"},
	{N_("Source"), "Xmp.photoshop.Source"},
	{N_("Copyright URL"), "Xmp.xmpRights.WebStatement"},
	{N_("Subject code"), "Xmp.iptc.SubjectCode", true}
}};

struct TemplateDialog;

enum class FieldAction : guint
{
	UNCHANGED,
	REPLACE,
	APPEND,
	CLEAR
};

constexpr std::array<const gchar *, 4> action_names{{"unchanged", "replace", "append", "clear"}};

struct TemplateFieldRow
{
	TemplateDialog *owner;
	std::string label;
	std::string key;
	bool list;
	bool custom;
	GtkWidget *row;
	GtkWidget *label_widget;
	GtkWidget *editor;
	GtkWidget *mode;
	GtkTextBuffer *buffer;
};

struct TemplateDialog
{
	GenericDialog *dialog;
	GList *files;
	GtkWidget *templates;
	GtkWidget *name;
	GtkWidget *status;
	GtkWidget *fields_box;
	GtkWidget *field_label;
	GtkWidget *field_key;
	GtkWidget *field_list;
	std::vector<std::unique_ptr<TemplateFieldRow>> fields;
};

bool template_field_key_valid(const gchar *key)
{
	if (!g_str_has_prefix(key, "Xmp.")) return false;
	const gchar *separator = strchr(key + 4, '.');
	if (!separator || separator == key + 4 || !separator[1]) return false;
	for (const gchar *work = key + 4; *work; ++work)
		{
		if (!g_ascii_isalnum(*work) && *work != '_' && *work != '-' && work != separator) return false;
		}
#if HAVE_EXIV2
	g_autofree gchar *description = exif_get_tag_description_by_key(key);
	return description != nullptr;
#else
	return true;
#endif
}

bool template_has_field(TemplateDialog *data, const gchar *key)
{
	return std::any_of(data->fields.begin(), data->fields.end(), [key](const auto &field) { return field->key == key; });
}

void template_remove_field(GtkButton *, gpointer user_data)
{
	auto *field = static_cast<TemplateFieldRow *>(user_data);
	auto *data = field->owner;
	gtk_box_remove(GTK_BOX(data->fields_box), field->row);
	data->fields.erase(std::find_if(data->fields.begin(), data->fields.end(),
	                              [field](const auto &item) { return item.get() == field; }));
	gtk_label_set_text(GTK_LABEL(data->status), _("Field removed. Save the template to keep this change."));
}

void template_field_action_changed(GObject *, GParamSpec *, gpointer user_data)
{
	auto *field = static_cast<TemplateFieldRow *>(user_data);
	const auto action = static_cast<FieldAction>(gtk_drop_down_get_selected(GTK_DROP_DOWN(field->mode)));
	gtk_widget_set_sensitive(field->editor, action == FieldAction::REPLACE || action == FieldAction::APPEND);
}

void template_status_changed(GObject *object, GParamSpec *, gpointer)
{
	gtk_widget_set_visible(GTK_WIDGET(object), *gtk_label_get_text(GTK_LABEL(object)) != '\0');
}

void template_add_row(TemplateDialog *data, const gchar *label, const gchar *key, bool list, bool custom)
{
	auto field = std::make_unique<TemplateFieldRow>();
	field->owner = data;
	field->label = label;
	field->key = key;
	field->list = list;
	field->custom = custom;
	field->row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_append(GTK_BOX(data->fields_box), field->row);
	field->label_widget = gtk_label_new(label);
	gtk_label_set_xalign(GTK_LABEL(field->label_widget), 0.0);
	gtk_label_set_wrap(GTK_LABEL(field->label_widget), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(field->label_widget), 18);
	gtk_widget_set_size_request(field->label_widget, 135, -1);
	gtk_widget_set_tooltip_text(field->label_widget, key);
	gtk_box_append(GTK_BOX(field->row), field->label_widget);
	GtkWidget *view = gtk_text_view_new();
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
	gtk_widget_set_name(view, key);
	gtk_accessible_update_property(GTK_ACCESSIBLE(view), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
	field->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view);
	gtk_widget_set_hexpand(scroll, TRUE);
	gtk_widget_set_size_request(scroll, 250, strcmp(key, COMMENT_KEY) == 0 || list ? 80 : 48);
	gtk_box_append(GTK_BOX(field->row), scroll);
	field->editor = scroll;
	const gchar *modes[] = {_("Leave unchanged"), _("Replace"), _("Append"), _("Clear"), nullptr};
	field->mode = gtk_drop_down_new_from_strings(modes);
	gtk_widget_set_valign(field->mode, GTK_ALIGN_START);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(field->mode), static_cast<guint>(FieldAction::UNCHANGED));
	gtk_widget_set_tooltip_text(field->mode, list ? _("Replace the existing list, or append values without duplicates.")
	                                           : _("Replace existing text, or append after a newline. Applying again repeats the appended text."));
	gtk_accessible_update_property(GTK_ACCESSIBLE(field->mode), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Field action"), -1);
	gtk_box_append(GTK_BOX(field->row), field->mode);
	g_signal_connect(field->mode, "notify::selected", G_CALLBACK(template_field_action_changed), field.get());
	template_field_action_changed(nullptr, nullptr, field.get());
	gtk_widget_set_tooltip_text(view, strcmp(key, KEYWORD_KEY) == 0 ? _("Enter keywords separated by commas or newlines.")
	                                                            : list ? _("Enter one value per line.") : label);
	if (custom)
		{
		GtkWidget *remove = gtk_button_new_with_label(_("Remove field"));
		gtk_widget_set_valign(remove, GTK_ALIGN_START);
		gtk_widget_set_tooltip_text(remove, _("Remove this field from the template without changing file metadata."));
		gtk_box_append(GTK_BOX(field->row), remove);
		g_signal_connect(remove, "clicked", G_CALLBACK(template_remove_field), field.get());
		}
	data->fields.push_back(std::move(field));
}

void template_reset_fields(TemplateDialog *data)
{
	for (const auto &field : data->fields) gtk_box_remove(GTK_BOX(data->fields_box), field->row);
	data->fields.clear();
	for (const auto &field : default_fields) template_add_row(data, _(field.label), field.key, field.list, false);
}

void template_add_field(GtkButton *, gpointer user_data)
{
	auto *data = static_cast<TemplateDialog *>(user_data);
	g_autofree gchar *label = g_strdup(gtk_editable_get_text(GTK_EDITABLE(data->field_label)));
	g_autofree gchar *key = g_strdup(gtk_editable_get_text(GTK_EDITABLE(data->field_key)));
	g_strstrip(label);
	g_strstrip(key);
	if (!*label)
		{
		gtk_label_set_text(GTK_LABEL(data->status), _("Enter a label for the field."));
		return;
		}
	if (!template_field_key_valid(key))
		{
		gtk_label_set_text(GTK_LABEL(data->status), _("Enter a valid XMP metadata key, such as Xmp.photoshop.City."));
		return;
		}
	if (template_has_field(data, key))
		{
		gtk_label_set_text(GTK_LABEL(data->status), _("This metadata key is already in the template."));
		return;
		}
	template_add_row(data, label, key, gtk_check_button_get_active(GTK_CHECK_BUTTON(data->field_list)), true);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(data->fields.back()->mode), static_cast<guint>(FieldAction::REPLACE));
	gtk_label_set_text(GTK_LABEL(data->status), _("Field added. Save the template to keep this change."));
}

void template_suggest_field(GObject *object, GParamSpec *, gpointer user_data)
{
	auto *data = static_cast<TemplateDialog *>(user_data);
	const guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(object));
	if (selected == 0 || selected > suggested_fields.size()) return;
	const auto &field = suggested_fields[selected - 1];
	gtk_editable_set_text(GTK_EDITABLE(data->field_label), _(field.label));
	gtk_editable_set_text(GTK_EDITABLE(data->field_key), field.key);
	gtk_check_button_set_active(GTK_CHECK_BUTTON(data->field_list), field.list);
}

gchar *template_directory()
{
	return g_build_filename(get_rc_dir(), "metadata-templates", nullptr);
}

// Read the stored name rather than deriving a path, so renamed files and
// templates saved with the previous hashed filenames remain usable.
gchar *template_find_path(const gchar *name)
{
	g_autofree gchar *directory = template_directory();
	g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
	if (!dir) return nullptr;

	const gchar *filename;
	while ((filename = g_dir_read_name(dir)))
		{
		if (!g_str_has_suffix(filename, ".ini")) continue;
		g_autofree gchar *path = g_build_filename(directory, filename, nullptr);
		g_autoptr(GKeyFile) config = g_key_file_new();
		if (!g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, nullptr)) continue;
		g_autofree gchar *stored_name = g_key_file_get_string(config, "template", "name", nullptr);
		if (g_strcmp0(stored_name, name) == 0) return g_steal_pointer(&path);
		}
	return nullptr;
}

gchar *template_filename_key(const gchar *filename)
{
	if (!g_utf8_validate(filename, -1, nullptr)) return g_strdup(filename);
	g_autofree gchar *normalized = g_utf8_normalize(filename, -1, G_NORMALIZE_ALL_COMPOSE);
	return g_utf8_casefold(normalized, -1);
}

gchar *template_new_path(const gchar *name)
{
	g_autofree gchar *directory = template_directory();
	g_autofree gchar *normalized = g_utf8_normalize(name, -1, G_NORMALIZE_DEFAULT_COMPOSE);
	std::string base;
	for (const gchar *work = normalized; *work; work = g_utf8_next_char(work))
		{
		const gunichar character = g_utf8_get_char(work);
		const gchar *next = g_utf8_next_char(work);
		if (base.size() + (next - work) > 120) break;
		if (g_unichar_iscntrl(character) || (character < 128 && strchr("/\\:*?\"<>|", character))) base += '_';
		else base.append(work, next - work);
		}
	if (base.empty()) base = "template";
	if (base.front() == '.') base.front() = '_';
	for (auto i = base.size(); i > 0 && (base[i - 1] == '.' || base[i - 1] == ' '); --i) base[i - 1] = '_';

	std::set<std::string> filenames;
	g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
	const gchar *filename;
	while (dir && (filename = g_dir_read_name(dir)))
		{
		g_autofree gchar *key = template_filename_key(filename);
		filenames.emplace(key);
		}
	for (guint suffix = 1; ; ++suffix)
		{
		g_autofree gchar *candidate = suffix == 1 ? g_strconcat(base.c_str(), ".ini", nullptr)
		                                       : g_strdup_printf("%s (%u).ini", base.c_str(), suffix);
		g_autofree gchar *key = template_filename_key(candidate);
		if (filenames.count(key) == 0) return g_build_filename(directory, candidate, nullptr);
		}
}

gboolean template_save_new(GKeyFile *config, const gchar *name, GError **error)
{
	gsize length;
	g_autofree gchar *contents = g_key_file_to_data(config, &length, nullptr);
	while (TRUE)
		{
		g_autofree gchar *path = template_new_path(name);
		g_autoptr(GFile) file = g_file_new_for_path(path);
		g_autoptr(GFileOutputStream) stream = g_file_create(file, G_FILE_CREATE_PRIVATE, nullptr, error);
		if (!stream)
			{
			if (g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_EXISTS))
				{
				g_clear_error(error);
				continue;
				}
			return FALSE;
			}
		gboolean saved = g_output_stream_write_all(G_OUTPUT_STREAM(stream), contents, length, nullptr, nullptr, error);
		if (saved) saved = g_output_stream_close(G_OUTPUT_STREAM(stream), nullptr, error);
		else g_output_stream_close(G_OUTPUT_STREAM(stream), nullptr, nullptr);
		if (!saved) g_unlink(path);
		return saved;
		}
}

gchar *field_text(GtkTextBuffer *buffer)
{
	GtkTextIter start;
	GtkTextIter end;
	gtk_text_buffer_get_bounds(buffer, &start, &end);
	return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

const gchar *template_selected(TemplateDialog *data)
{
	if (gtk_drop_down_get_selected(GTK_DROP_DOWN(data->templates)) == 0) return nullptr;
	auto *item = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(data->templates)));
	return item ? gtk_string_object_get_string(item) : nullptr;
}

void template_refresh(TemplateDialog *data, const gchar *selected)
{
	g_object_freeze_notify(G_OBJECT(data->templates));
	g_autoptr(GtkStringList) model = gtk_string_list_new(nullptr);
	gtk_string_list_append(model, _("Select a template"));
	gtk_drop_down_set_model(GTK_DROP_DOWN(data->templates), G_LIST_MODEL(model));
	gtk_drop_down_set_selected(GTK_DROP_DOWN(data->templates), 0);
	g_autofree gchar *directory = template_directory();
	g_autoptr(GDir) dir = g_dir_open(directory, 0, nullptr);
	if (!dir)
		{
		g_object_thaw_notify(G_OBJECT(data->templates));
		return;
		}

	GList *names = nullptr;
	const gchar *filename;
	while ((filename = g_dir_read_name(dir)))
		{
		if (!g_str_has_suffix(filename, ".ini")) continue;
		g_autofree gchar *path = g_build_filename(directory, filename, nullptr);
		g_autoptr(GKeyFile) config = g_key_file_new();
		if (!g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, nullptr)) continue;
		gchar *name = g_key_file_get_string(config, "template", "name", nullptr);
		if (name && *name) names = g_list_prepend(names, name);
		else g_free(name);
		}
	names = g_list_sort(names, [](gconstpointer a, gconstpointer b)
		{
		return g_utf8_collate(static_cast<const gchar *>(a), static_cast<const gchar *>(b));
		});
	for (GList *work = names; work; work = work->next)
		{
		auto *name = static_cast<gchar *>(work->data);
		gtk_string_list_append(model, name);
		}
	guint index = 1;
	guint selected_index = 0;
	for (GList *work = names; work; work = work->next, ++index)
		{
		if (g_strcmp0(static_cast<const gchar *>(work->data), selected) == 0) selected_index = index;
		}
	gtk_drop_down_set_selected(GTK_DROP_DOWN(data->templates), selected_index);
	g_list_free_full(names, g_free);
	g_object_thaw_notify(G_OBJECT(data->templates));
}

FieldAction template_load_action(GKeyFile *config, const gchar *key)
{
	g_autofree gchar *saved_action = g_key_file_get_string(config, key, "action", nullptr);
	if (saved_action)
		{
		for (guint i = 0; i < action_names.size(); ++i)
			{
			if (strcmp(saved_action, action_names[i]) == 0) return static_cast<FieldAction>(i);
			}
		return FieldAction::UNCHANGED;
		}

	// Older templates used separate enabled, clear, and write-mode settings.
	if (!g_key_file_get_boolean(config, key, "enabled", nullptr)) return FieldAction::UNCHANGED;
	if (g_key_file_get_boolean(config, key, "clear", nullptr)) return FieldAction::CLEAR;
	g_autofree gchar *mode = g_key_file_get_string(config, key, "mode", nullptr);
	bool append = strcmp(key, KEYWORD_KEY) == 0;
	if (g_strcmp0(mode, "append") == 0) append = true;
	else if (g_strcmp0(mode, "replace") == 0) append = false;
	else if (!mode && strcmp(key, KEYWORD_KEY) == 0 && g_key_file_has_key(config, "template", "append_keywords", nullptr))
		{
		append = g_key_file_get_boolean(config, "template", "append_keywords", nullptr);
		}
	return append ? FieldAction::APPEND : FieldAction::REPLACE;
}

void template_load(GObject *, GParamSpec *, gpointer user_data)
{
	auto *data = static_cast<TemplateDialog *>(user_data);
	const gchar *name = template_selected(data);
	if (!name) return;
	g_autofree gchar *path = template_find_path(name);
	if (!path)
		{
		gtk_label_set_text(GTK_LABEL(data->status), _("The template no longer exists."));
		return;
		}
	g_autoptr(GKeyFile) config = g_key_file_new();
	g_autoptr(GError) error = nullptr;
	if (!g_key_file_load_from_file(config, path, G_KEY_FILE_NONE, &error))
		{
		gtk_label_set_text(GTK_LABEL(data->status), error->message);
		return;
		}

	gtk_editable_set_text(GTK_EDITABLE(data->name), name);
	template_reset_fields(data);
	g_auto(GStrv) extra_fields = g_key_file_get_string_list(config, "template", "custom_fields", nullptr, nullptr);
	bool skipped_fields = false;
	for (gsize i = 0; extra_fields && extra_fields[i]; ++i)
		{
		const gchar *key = extra_fields[i];
		if (!template_field_key_valid(key) || template_has_field(data, key))
			{
			skipped_fields = true;
			continue;
			}
		g_autofree gchar *label = g_key_file_get_string(config, key, "label", nullptr);
		const bool list = g_key_file_get_boolean(config, key, "list", nullptr);
		template_add_row(data, label && *label ? label : key, key, list, true);
		}
	for (const auto &field : data->fields)
		{
		const gchar *key = field->key.c_str();
		g_autofree gchar *value = g_key_file_get_string(config, key, "value", nullptr);
		gtk_text_buffer_set_text(field->buffer, value ? value : "", -1);
		gtk_drop_down_set_selected(GTK_DROP_DOWN(field->mode), static_cast<guint>(template_load_action(config, key)));
		}
	gtk_label_set_text(GTK_LABEL(data->status), skipped_fields ? _("Template loaded. Invalid or duplicate fields were skipped.")
	                                                        : _("Template loaded. Edits affect this application unless you save the template."));
}

void template_save(GtkButton *, gpointer user_data)
{
	auto *data = static_cast<TemplateDialog *>(user_data);
	g_autofree gchar *name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(data->name)));
	g_strstrip(name);
	if (!*name)
		{
		gtk_label_set_text(GTK_LABEL(data->status), _("Enter a template name first."));
		return;
		}

	g_autoptr(GKeyFile) config = g_key_file_new();
	g_key_file_set_string(config, "template", "name", name);
	std::vector<const gchar *> extra_fields;
	for (const auto &field : data->fields)
		{
		const gchar *key = field->key.c_str();
		g_autofree gchar *value = field_text(field->buffer);
		g_key_file_set_string(config, key, "value", value);
		const guint action = gtk_drop_down_get_selected(GTK_DROP_DOWN(field->mode));
		g_key_file_set_string(config, key, "action", action_names[action]);
		if (field->custom)
			{
			extra_fields.push_back(key);
			g_key_file_set_string(config, key, "label", field->label.c_str());
			g_key_file_set_boolean(config, key, "list", field->list);
			}
		}
	if (!extra_fields.empty()) g_key_file_set_string_list(config, "template", "custom_fields", extra_fields.data(), extra_fields.size());
	g_autofree gchar *directory = template_directory();
	g_autofree gchar *path = template_find_path(name);
	g_autoptr(GError) error = nullptr;
	if (g_mkdir_with_parents(directory, 0700) != 0)
		{
		gtk_label_set_text(GTK_LABEL(data->status), _("Cannot create the metadata templates directory."));
		return;
		}
	const gboolean saved = path ? g_key_file_save_to_file(config, path, &error) : template_save_new(config, name, &error);
	if (!saved)
		{
		gtk_label_set_text(GTK_LABEL(data->status), error->message);
		return;
		}
	template_refresh(data, name);
	gtk_label_set_text(GTK_LABEL(data->status), _("Template saved."));
}

void template_new(GtkButton *, gpointer user_data)
{
	auto *data = static_cast<TemplateDialog *>(user_data);
	gtk_drop_down_set_selected(GTK_DROP_DOWN(data->templates), 0);
	gtk_editable_set_text(GTK_EDITABLE(data->name), "");
	template_reset_fields(data);
	gtk_label_set_text(GTK_LABEL(data->status), _("Enter values and a name to create a template."));
}

void template_delete(GtkButton *, gpointer user_data)
{
	auto *data = static_cast<TemplateDialog *>(user_data);
	const gchar *name = template_selected(data);
	if (!name) return;
	g_autofree gchar *path = template_find_path(name);
	if (!path || g_unlink(path) != 0)
		{
		gtk_label_set_text(GTK_LABEL(data->status), _("Cannot delete the template."));
		return;
		}
	template_refresh(data, nullptr);
	gtk_label_set_text(GTK_LABEL(data->status), _("Template deleted. The current field values are still available."));
}

void template_close(GenericDialog *dialog, gpointer user_data)
{
	auto *data = static_cast<TemplateDialog *>(user_data);
	generic_dialog_close(dialog);
	file_data_list_free(data->files);
	delete data;
}

GList *template_list_values(const gchar *text)
{
	GList *values = nullptr;
	std::set<std::string> seen;
	g_auto(GStrv) lines = g_strsplit(text, "\n", -1);
	for (gsize i = 0; lines[i]; ++i)
		{
		g_strstrip(lines[i]);
		if (*lines[i] && seen.emplace(lines[i]).second) values = g_list_append(values, g_strdup(lines[i]));
		}
	return values;
}

void template_apply(GenericDialog *dialog, gpointer user_data)
{
	auto *data = static_cast<TemplateDialog *>(user_data);
	gboolean changed = FALSE;
	for (const auto &field : data->fields)
		{
		const auto action = static_cast<FieldAction>(gtk_drop_down_get_selected(GTK_DROP_DOWN(field->mode)));
		if (action != FieldAction::REPLACE && action != FieldAction::APPEND && action != FieldAction::CLEAR) continue;
		const bool clear = action == FieldAction::CLEAR;
		g_autofree gchar *value = field_text(field->buffer);
		if (!clear && !*value) continue;
		const gchar *key = field->key.c_str();
		const bool keywords = field->key == KEYWORD_KEY;
		const bool append = action == FieldAction::APPEND;
		GList *values = !clear && field->list ? (keywords ? string_to_keywords_list(value) : template_list_values(value)) : nullptr;
		if (!clear && field->list && !values) continue;
		for (GList *work = data->files; work; work = work->next)
			{
			auto *fd = static_cast<FileData *>(work->data);
			if (clear) metadata_write_list(fd, key, nullptr);
			else if (field->list)
				{
				if (append) metadata_append_list(fd, key, values);
				else metadata_write_list(fd, key, values);
				}
			else if (append)
				{
				g_autofree gchar *existing = metadata_read_string(fd, key, METADATA_PLAIN);
				g_autofree gchar *combined = existing && *existing ? g_strconcat(existing, g_str_has_suffix(existing, "\n") ? "" : "\n", value, nullptr)
				                                                : g_strdup(value);
				metadata_write_string(fd, key, combined);
				}
			else metadata_write_string(fd, key, value);
			}
		g_list_free_full(values, g_free);
		changed = TRUE;
		}
	if (!changed)
		{
		gtk_label_set_text(GTK_LABEL(data->status), _("Choose Replace or Append and enter a value, or choose Clear."));
		return;
		}
	template_close(dialog, data);
}

} // namespace

void metadata_template_dialog(GtkWidget *parent, GList *files)
{
	if (!files)
		{
		warning_dialog(_("Metadata Templates"), _("Select files in the files pane first."), GQ_ICON_DIALOG_INFO, parent);
		return;
		}

	auto *data = new TemplateDialog{};
	data->files = files;
	data->dialog = generic_dialog_new(_("Metadata Templates"), "metadata_templates", parent, FALSE, template_close, data);
	gtk_box_set_spacing(GTK_BOX(gtk_widget_get_parent(data->dialog->hbox)), 6);
	gtk_window_set_default_size(GTK_WINDOW(data->dialog->dialog), 840, 640);
	g_autofree gchar *message = g_strdup_printf(ngettext("Apply metadata to %u selected file.", "Apply metadata to %u selected files.", g_list_length(files)), g_list_length(files));
	generic_dialog_add_message(data->dialog, nullptr, message,
	                          _("Choose an action for each field. Replace and Append ignore blank values. Clear removes existing values. Text append adds a new line; applying again repeats the text. Changes use the usual metadata save settings."), FALSE);

	GtkWidget *template_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append(GTK_BOX(data->dialog->vbox), template_box);
	gtk_box_append(GTK_BOX(template_box), gtk_label_new(_("Template:")));
	data->templates = gtk_drop_down_new(nullptr, nullptr);
	gtk_widget_set_hexpand(data->templates, TRUE);
	gtk_box_append(GTK_BOX(template_box), data->templates);
	GtkWidget *new_button = gtk_button_new_with_label(_("New template"));
	gtk_box_append(GTK_BOX(template_box), new_button);
	g_signal_connect(new_button, "clicked", G_CALLBACK(template_new), data);
	GtkWidget *delete_button = gtk_button_new_with_label(_("Delete template"));
	gtk_box_append(GTK_BOX(template_box), delete_button);
	g_signal_connect(delete_button, "clicked", G_CALLBACK(template_delete), data);

	GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_box_append(GTK_BOX(data->dialog->vbox), name_box);
	gtk_box_append(GTK_BOX(name_box), gtk_label_new(_("Save as:")));
	data->name = gtk_entry_new();
	gtk_widget_set_hexpand(data->name, TRUE);
	gtk_box_append(GTK_BOX(name_box), data->name);
	GtkWidget *save = gtk_button_new_with_label(_("Save template"));
	gtk_widget_set_tooltip_text(save, _("Save these values under this name, replacing any template with the same name. Use a different name to create a copy."));
	gtk_box_append(GTK_BOX(name_box), save);
	g_signal_connect(save, "clicked", G_CALLBACK(template_save), data);

	GtkWidget *scroll = gtk_scrolled_window_new();
	gtk_widget_set_vexpand(scroll, TRUE);
	gtk_box_append(GTK_BOX(data->dialog->vbox), scroll);
	data->fields_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), data->fields_box);
	template_reset_fields(data);

	gtk_box_append(GTK_BOX(data->dialog->vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
	GtkWidget *add_expander = gtk_expander_new(_("Add field"));
	gtk_box_append(GTK_BOX(data->dialog->vbox), add_expander);
	GtkWidget *add_grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(add_grid), 6);
	gtk_grid_set_column_spacing(GTK_GRID(add_grid), 6);
	gtk_expander_set_child(GTK_EXPANDER(add_expander), add_grid);
	g_autoptr(GtkStringList) suggestions = gtk_string_list_new(nullptr);
	gtk_string_list_append(suggestions, _("Custom field"));
	for (const auto &field : suggested_fields) gtk_string_list_append(suggestions, _(field.label));
	GtkWidget *suggestion = gtk_drop_down_new(G_LIST_MODEL(g_object_ref(suggestions)), nullptr);
	gtk_widget_set_name(suggestion, "metadata-template-field-suggestion");
	gtk_grid_attach(GTK_GRID(add_grid), gtk_label_new(_("Choose field:")), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(add_grid), suggestion, 1, 0, 1, 1);
	data->field_label = gtk_entry_new();
	gtk_widget_set_name(data->field_label, "metadata-template-field-label");
	gtk_widget_set_hexpand(data->field_label, TRUE);
	gtk_grid_attach(GTK_GRID(add_grid), gtk_label_new(_("Label:")), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(add_grid), data->field_label, 1, 1, 1, 1);
	data->field_key = gtk_entry_new();
	gtk_widget_set_name(data->field_key, "metadata-template-field-key");
	gtk_entry_set_placeholder_text(GTK_ENTRY(data->field_key), "Xmp.photoshop.City");
	gtk_widget_set_tooltip_text(data->field_key, _("Choose a suggested field or enter an XMP key from the Exif window."));
	gtk_grid_attach(GTK_GRID(add_grid), gtk_label_new(_("Metadata key:")), 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(add_grid), data->field_key, 1, 2, 1, 1);
	data->field_list = gtk_check_button_new_with_label(_("List of values (one per line)"));
	gtk_grid_attach(GTK_GRID(add_grid), data->field_list, 1, 3, 1, 1);
	GtkWidget *add = gtk_button_new_with_label(_("Add field"));
	gtk_grid_attach(GTK_GRID(add_grid), add, 2, 1, 1, 1);
	g_signal_connect(add, "clicked", G_CALLBACK(template_add_field), data);
	g_signal_connect(suggestion, "notify::selected", G_CALLBACK(template_suggest_field), data);
	data->status = gtk_label_new("");
	gtk_label_set_wrap(GTK_LABEL(data->status), TRUE);
	gtk_widget_set_visible(data->status, FALSE);
	g_signal_connect(data->status, "notify::label", G_CALLBACK(template_status_changed), nullptr);
	gtk_box_append(GTK_BOX(data->dialog->vbox), data->status);

	template_refresh(data, nullptr);
	g_signal_connect(data->templates, "notify::selected-item", G_CALLBACK(template_load), data);
	generic_dialog_add_button(data->dialog, GQ_ICON_APPLY, _("Apply to selection"), template_apply, FALSE);
	gtk_window_present(GTK_WINDOW(data->dialog->dialog));
}
