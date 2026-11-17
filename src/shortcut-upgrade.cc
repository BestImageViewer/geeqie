// SPDX-License-Identifier: GPL-2.0-or-later

#include "shortcut-upgrade.h"

#include <string>

#include "accelerators.h"
#include "intl.h"

namespace
{

struct ShortcutUpgrade
{
	GtkWidget *window;
	GtkWidget *legacy;
	GtkWidget *hig;
	GtkWidget *continue_button;
	GtkWidget *error;
	std::string path;
	ShortcutUpgradeCallback callback;
	gpointer data;
};

void finish(ShortcutUpgrade *upgrade, bool accepted)
{
	const auto callback = upgrade->callback;
	const auto data = upgrade->data;
	gtk_window_destroy(GTK_WINDOW(upgrade->window));
	if (callback) callback(accepted, data);
}

void choice_changed_cb(GtkCheckButton *, gpointer data)
{
	auto *upgrade = static_cast<ShortcutUpgrade *>(data);
	gtk_widget_set_sensitive(upgrade->continue_button,
	                         gtk_check_button_get_active(GTK_CHECK_BUTTON(upgrade->legacy)) ||
	                         gtk_check_button_get_active(GTK_CHECK_BUTTON(upgrade->hig)));
}

void continue_cb(GtkButton *, gpointer data)
{
	auto *upgrade = static_cast<ShortcutUpgrade *>(data);
	const bool legacy = gtk_check_button_get_active(GTK_CHECK_BUTTON(upgrade->legacy));
	if (!legacy && !gtk_check_button_get_active(GTK_CHECK_BUTTON(upgrade->hig))) return;
	g_autoptr(GError) error = nullptr;
	bool saved;
	if (legacy)
		{
		saved = restore_v2_shortcuts(upgrade->path.c_str(), &error);
		}
	else
		{
		// An empty override file selects the current built-in HIG defaults.
		g_autoptr(GFile) file = g_file_new_for_path(upgrade->path.c_str());
		saved = g_file_replace_contents(file, "", 0, nullptr, TRUE, G_FILE_CREATE_NONE, nullptr, nullptr, &error);
		}
	if (!saved)
		{
		gtk_label_set_text(GTK_LABEL(upgrade->error), error->message);
		gtk_widget_set_visible(upgrade->error, TRUE);
		return;
		}
	finish(upgrade, true);
}

void cancel_cb(GtkButton *, gpointer data)
{
	finish(static_cast<ShortcutUpgrade *>(data), false);
}

gboolean close_cb(GtkWindow *, gpointer data)
{
	finish(static_cast<ShortcutUpgrade *>(data), false);
	return true;
}

gboolean key_pressed_cb(GtkEventControllerKey *, guint keyval, guint, GdkModifierType, gpointer data)
{
	if (keyval != GDK_KEY_Escape) return false;
	finish(static_cast<ShortcutUpgrade *>(data), false);
	return true;
}

} // namespace

bool shortcut_upgrade_needed(const gchar *path)
{
	if (g_file_test(path, G_FILE_TEST_EXISTS)) return false;
	g_autofree gchar *directory = g_path_get_dirname(path);
	g_autofree gchar *old_path = g_build_filename(directory, "accels", nullptr);
	return g_file_test(old_path, G_FILE_TEST_IS_REGULAR);
}

GtkWidget *shortcut_upgrade_dialog_new(GtkWindow *parent, const gchar *path, const gchar *description,
                                      ShortcutUpgradeCallback callback, gpointer data)
{
	auto *window = gtk_window_new();
	gtk_window_set_title(GTK_WINDOW(window), _("Changes to menus and shortcuts"));
	gtk_window_set_transient_for(GTK_WINDOW(window), parent);
	gtk_window_set_application(GTK_WINDOW(window), gtk_window_get_application(parent));
	gtk_window_set_modal(GTK_WINDOW(window), TRUE);
	gtk_window_set_default_size(GTK_WINDOW(window), 640, -1);

	auto *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_widget_set_margin_start(box, 18);
	gtk_widget_set_margin_end(box, 18);
	gtk_widget_set_margin_top(box, 18);
	gtk_widget_set_margin_bottom(box, 18);
	gtk_window_set_child(GTK_WINDOW(window), box);
	auto *scrolled = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 200);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scrolled), 420);
	gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
	gtk_widget_set_vexpand(scrolled, TRUE);
	gtk_box_append(GTK_BOX(box), scrolled);
	auto *information = gtk_label_new(description);
	gtk_label_set_wrap(GTK_LABEL(information), TRUE);
	gtk_label_set_xalign(GTK_LABEL(information), 0.0);
	gtk_label_set_selectable(GTK_LABEL(information), TRUE);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), information);

	auto *question = gtk_label_new(_("Which keyboard shortcuts would you like to use?"));
	gtk_label_set_wrap(GTK_LABEL(question), TRUE);
	gtk_label_set_xalign(GTK_LABEL(question), 0.0);
	gtk_box_append(GTK_BOX(box), question);
	auto *legacy = gtk_check_button_new_with_label(_("Maintain my v2.x shortcuts"));
	auto *hig = gtk_check_button_new_with_label(_("Use GNOME HIG shortcuts"));
	gtk_check_button_set_group(GTK_CHECK_BUTTON(hig), GTK_CHECK_BUTTON(legacy));
	gtk_box_append(GTK_BOX(box), legacy);
	gtk_box_append(GTK_BOX(box), hig);

	auto *error_label = gtk_label_new(nullptr);
	gtk_label_set_wrap(GTK_LABEL(error_label), TRUE);
	gtk_label_set_xalign(GTK_LABEL(error_label), 0.0);
	gtk_widget_add_css_class(error_label, "error");
	gtk_widget_set_visible(error_label, FALSE);
	gtk_box_append(GTK_BOX(box), error_label);
	auto *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(buttons, GTK_ALIGN_END);
	gtk_box_append(GTK_BOX(box), buttons);
	auto *cancel_button = gtk_button_new_with_label(_("Cancel"));
	auto *continue_button = gtk_button_new_with_label(_("Continue"));
	gtk_widget_set_sensitive(continue_button, FALSE);
	gtk_box_append(GTK_BOX(buttons), cancel_button);
	gtk_box_append(GTK_BOX(buttons), continue_button);

	auto *upgrade = new ShortcutUpgrade{window, legacy, hig, continue_button, error_label, path, callback, data};
	g_object_set_data_full(G_OBJECT(window), "shortcut-upgrade", upgrade, +[](gpointer value)
		{
		delete static_cast<ShortcutUpgrade *>(value);
		});
	g_signal_connect(legacy, "toggled", G_CALLBACK(choice_changed_cb), upgrade);
	g_signal_connect(hig, "toggled", G_CALLBACK(choice_changed_cb), upgrade);
	g_signal_connect(continue_button, "clicked", G_CALLBACK(continue_cb), upgrade);
	g_signal_connect(cancel_button, "clicked", G_CALLBACK(cancel_cb), upgrade);
	g_signal_connect(window, "close-request", G_CALLBACK(close_cb), upgrade);
	auto *keys = gtk_event_controller_key_new();
	g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed_cb), upgrade);
	gtk_widget_add_controller(window, keys);
	return window;
}
