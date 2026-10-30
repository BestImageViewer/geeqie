// SPDX-License-Identifier: GPL-2.0-or-later

#include "actions.h"

#include <algorithm>
#include <string>

#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "convert-configuration.h"
#include "layout-util.h"
#include "main-defines.h"
#include "pixbuf-renderer.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"

const char *get_description_for_action_name(const char *action_name)
{
	const ActionDef *action_sets[] =
		{
		get_app_actions(),
		get_dupe_main_actions(),
		get_dupe_second_actions(),
		get_image_actions(),
		get_main_actions(),
		get_pan_view_actions(),
		get_search_actions(),
		get_view_file_actions(),
		get_log_actions()
		};

	g_autofree gchar *base_name = nullptr;
	g_autoptr(GVariant) target = nullptr;

	if (!g_action_parse_detailed_name(action_name, &base_name, &target, nullptr))
		{
		base_name = g_strdup(action_name);
		}

	for (const ActionDef *action_table : action_sets)
		{
		for (unsigned int i = 0; action_table[i].action_name != nullptr; i++)
			{
			if (!g_strcmp0(action_table[i].action_name, base_name))
				{
				if (!action_table[i].description)
					{
					return nullptr;
					}

				if (target)
					{
					if (g_variant_is_of_type(target, G_VARIANT_TYPE_STRING))
						{
						return g_strdup_printf("%s %s", action_table[i].description, g_variant_get_string(target, nullptr));
						}

					if (g_variant_is_of_type(target, G_VARIANT_TYPE_INT32))
						{
						return g_strdup_printf("%s %d", action_table[i].description, g_variant_get_int32(target));
						}
					}

				return action_table[i].description;
				}
			}
		}

	return nullptr;
}

namespace
{

gchar *accelerator_label_from_list(GStrv accels)
{
	if (!accels || !accels[0]) return nullptr;

	guint accelerator_key = 0;
	GdkModifierType accelerator_mods = GDK_NO_MODIFIER_MASK;
	gtk_accelerator_parse(accels[0], &accelerator_key, &accelerator_mods);
	if (accelerator_key == 0) return nullptr;

	return gtk_accelerator_get_label(accelerator_key, accelerator_mods);
}

} // namespace

gchar *action_accelerator_label(const gchar *action_name)
{
	auto *app = GTK_APPLICATION(g_application_get_default());

	if (app)
		{
		g_auto(GStrv) registered_accels = gtk_application_get_accels_for_action(app, action_name);
		g_autofree gchar *registered_label = accelerator_label_from_list(registered_accels);
		if (registered_label) return g_steal_pointer(&registered_label);
		}

	g_auto(GStrv) configured_accels = g_key_file_get_string_list(get_keyfile_merged(), action_name, "accels", nullptr, nullptr);
	return accelerator_label_from_list(configured_accels);
}

static GStrv get_targets(const char *action)
{
	g_autofree char *prefix = g_strconcat(action, "::", nullptr);

	GPtrArray *targets = g_ptr_array_new_with_free_func(g_free);

	size_t length = 0;
	g_auto(GStrv) groups = g_key_file_get_groups(get_keyfile_merged(), &length);

	for (size_t i = 0; groups[i] != nullptr; i++)
		{
		if (g_str_has_prefix(groups[i], prefix))
			{
			const char *target = groups[i] + strlen(prefix);
			g_ptr_array_add(targets, g_strdup(target));
			}
		}

	g_ptr_array_add(targets, nullptr);

	return reinterpret_cast<GStrv>(g_ptr_array_free(targets, FALSE));
}

namespace
{

constexpr const char *ACCEL_FOCUS_HANDLER_ATTACHED = "geeqie-accel-focus-handler-attached";

GHashTable *registered_accels = nullptr;
bool registered_accels_suppressed = false;
guint64 accels_generation = 0;
guint64 applied_generation = 0;
GtkApplication *last_accel_app = nullptr;
std::string last_accel_context;
constexpr const char *CONTEXT_SHORTCUT_ACTIONS = "geeqie-context-shortcut-actions";

void registered_accels_ensure()
{
	if (!registered_accels)
		{
		registered_accels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, reinterpret_cast<GDestroyNotify>(g_strfreev));
		}
}

void registered_accels_set(GtkApplication *app, const char *detailed_action, GStrv accels)
{
	if (!app || !detailed_action || !accels)
		{
		return;
		}

	registered_accels_ensure();
	g_hash_table_replace(registered_accels, g_strdup(detailed_action), g_strdupv(accels));
	++accels_generation;

	const char *empty_accels[] = {nullptr};
	gtk_application_set_accels_for_action(app, detailed_action,
	                                      registered_accels_suppressed ? empty_accels : const_cast<const char * const *>(accels));
}

void registered_accels_apply(GtkApplication *app, GtkWindow *window)
{
	if (!app || !registered_accels) return;
	const bool suppress = focus_is_editable(window);
	g_autoptr(GHashTable) reserved = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);
	g_autoptr(GString) context = g_string_new(suppress ? "editable" : "view");
	auto *focus = window ? gtk_root_get_focus(GTK_ROOT(window)) : nullptr;
	const auto *defs = window ? static_cast<const ActionDef *>(g_object_get_data(G_OBJECT(window), CONTEXT_SHORTCUT_ACTIONS)) : nullptr;
	// GTK application shortcuts run before view key handlers. Reserve matching keys
	// while the view has focus, and restore the original bindings when focus leaves.
	if (!suppress && defs && focus &&
	    (IS_PIXBUF_RENDERER(focus) || (defs == get_log_actions() && GTK_IS_TEXT_VIEW(focus))))
		{
		for (const auto *def = defs; def->action_name; ++def)
			{
			if (!def->context_shortcut) continue;
			g_auto(GStrv) accels = g_key_file_get_string_list(get_keyfile_merged(), def->action_name, "accels", nullptr, nullptr);
			for (gchar **accel = accels; accel && *accel; ++accel)
				{
				guint key;
				GdkModifierType mods;
				if (!gtk_accelerator_parse(*accel, &key, &mods) || !key) continue;
				gchar *normalized = gtk_accelerator_name(key, mods);
				g_string_append_printf(context, ";%s", normalized);
				g_hash_table_add(reserved, normalized);
				}
			}
		}
	if (registered_accels_suppressed == suppress && last_accel_app == app &&
	    applied_generation == accels_generation && last_accel_context == context->str) return;
	registered_accels_suppressed = suppress;
	last_accel_app = app;
	last_accel_context = context->str;
	applied_generation = accels_generation;

	GHashTableIter iter;
	gpointer key;
	gpointer value;

	g_hash_table_iter_init(&iter, registered_accels);
	while (g_hash_table_iter_next(&iter, &key, &value))
		{
		const auto *detailed_action = static_cast<const char *>(key);
		auto accels = static_cast<GStrv>(value);

		g_autoptr(GPtrArray) active = g_ptr_array_new();
		for (gchar **accel = accels; !suppress && accel && *accel; ++accel)
			{
			guint key;
			GdkModifierType mods;
			if (!gtk_accelerator_parse(*accel, &key, &mods)) continue;
			g_autofree gchar *normalized = gtk_accelerator_name(key, mods);
			if (!g_hash_table_contains(reserved, normalized)) g_ptr_array_add(active, *accel);
			}
		g_ptr_array_add(active, nullptr);
		gtk_application_set_accels_for_action(app, detailed_action, reinterpret_cast<const char * const *>(active->pdata));
		}
}

void window_focus_widget_notify_cb(GtkWindow *window, GParamSpec *, gpointer data)
{
	if (!gtk_window_is_active(window)) return;

	auto app = GTK_APPLICATION(data);

	registered_accels_apply(app, window);
}

void window_is_active_notify_cb(GtkWindow *window, GParamSpec *, gpointer data)
{
	if (!gtk_window_is_active(window)) return;

	auto app = GTK_APPLICATION(data);
	registered_accels_apply(app, window);
}

void attach_accel_focus_handler(GtkApplication *app, GtkWidget *window)
{
	if (!app || !GTK_IS_WINDOW(window) ||
	    g_object_get_data(G_OBJECT(window), ACCEL_FOCUS_HANDLER_ATTACHED))
		{
		return;
		}

	g_signal_connect(window, "notify::focus-widget", G_CALLBACK(window_focus_widget_notify_cb), app);
	g_signal_connect(window, "notify::is-active", G_CALLBACK(window_is_active_notify_cb), app);
	g_object_set_data(G_OBJECT(window), ACCEL_FOCUS_HANDLER_ATTACHED, GINT_TO_POINTER(TRUE));
}

} // namespace

void register_accels_for_action(GtkApplication *app, const char *detailed_action, GStrv accels)
{
	registered_accels_set(app, detailed_action, accels);
	if (app) registered_accels_apply(app, gtk_application_get_active_window(app));
}

void register_context_shortcuts(GtkApplication *app, GtkWidget *window, const ActionDef *defs)
{
	if (!GTK_IS_WINDOW(window)) return;
	if (g_object_get_data(G_OBJECT(window), CONTEXT_SHORTCUT_ACTIONS) != defs)
		{
		g_object_set_data(G_OBJECT(window), CONTEXT_SHORTCUT_ACTIONS, const_cast<ActionDef *>(defs));
		++accels_generation;
		}
	attach_accel_focus_handler(app, window);
	if (app) registered_accels_apply(app, gtk_application_get_active_window(app));
}

void reload_registered_accels(GtkApplication *app, GKeyFile *accels_keyfile)
{
	if (!app || !accels_keyfile || !registered_accels) return;

	const char *empty_accels[] = {nullptr};
	GHashTableIter iter;
	gpointer key;
	gpointer value;

	g_hash_table_iter_init(&iter, registered_accels);
	while (g_hash_table_iter_next(&iter, &key, &value))
		{
		const auto *detailed_action = static_cast<const char *>(key);
		g_auto(GStrv) accels = g_key_file_get_string_list(accels_keyfile, detailed_action, "accels", nullptr, nullptr);
		if (!accels) accels = g_new0(gchar *, 1);
		gtk_application_set_accels_for_action(app, detailed_action,
		                                      registered_accels_suppressed ? empty_accels : const_cast<const char * const *>(accels));
		g_hash_table_iter_replace(&iter, g_steal_pointer(&accels));
		}
	++accels_generation;
	registered_accels_apply(app, gtk_application_get_active_window(app));
}

static GVariant *action_def_create_state(const ActionDef *d)
{
	switch (d->state_type)
		{
		case ActionStateType::BOOLEAN:
			return g_variant_new_boolean(d->boolean_state);

		case ActionStateType::STRING:
			return g_variant_new_string(d->string_state);

		case ActionStateType::INT32:
			return g_variant_new_int32(d->int32_state);

		case ActionStateType::NONE:
		default:
			return nullptr;
		}
}

void register_actions_from_table(GtkApplication *app, GtkWidget *window, const ActionDef *defs, GKeyFile *accels_keyfile, gpointer data)
{
	attach_accel_focus_handler(app, window);

	for (const ActionDef *d = defs; d->action_name; ++d)
		{
		g_autofree char *action_name = nullptr;
		const char *full_action_name = strchr(d->action_name, '.') + 1; /* strip app./win. */
		const char *paren = strchr(full_action_name, '(');

		if (!paren)
			{
			action_name =  g_strdup(full_action_name);
			}
		else
			{
			action_name =  g_strndup(full_action_name, paren - full_action_name);
			}

		g_autoptr(GSimpleAction) action = nullptr;

		if (!action)
			{
			/* Create the action */
			GVariant *state = action_def_create_state(d);

			if (state)
				{
				action = g_simple_action_new_stateful(action_name, d->parameter_type, state);
				g_signal_connect(action, "change-state", G_CALLBACK(d->callback), data);
				}
			else
				{
				action = g_simple_action_new(action_name, d->parameter_type);
				g_signal_connect(action, "activate", G_CALLBACK(d->callback), data);
				}

			if (!window)
				{
				g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
				}
			else
				{
				g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(action));
				}
			}

		if (d->context_shortcut)
			{
			register_context_shortcuts(app, window, defs);
			continue;
			}
		if (d->targets)
			{
			for (int i = 0; d->targets[i] != nullptr; i++)
				{
				g_autofree char *target = g_strdup_printf("%s('%s')", d->action_name, d->targets[i]);

				g_auto(GStrv) accels = g_key_file_get_string_list(accels_keyfile, target, "accels", nullptr, nullptr);

				if (accels)
					{
					registered_accels_set(app, target, accels);
					}
				}
			}
		else
			{
			if (d->parameter_type && g_variant_type_equal(d->parameter_type, G_VARIANT_TYPE_STRING))
				{
				g_auto(GStrv) targets = get_targets(d->action_name);

				for (size_t i = 0; targets && targets[i] != nullptr; i++)
					{
					g_autofree char *detailed_action =
					    g_strdup_printf("%s::%s", d->action_name, targets[i]);

					size_t n_accels = 0;
					g_auto(GStrv) accels = g_key_file_get_string_list(get_keyfile_merged(), detailed_action, "accels", &n_accels, nullptr);

					if (accels && n_accels > 0)
						{
						registered_accels_set(app, detailed_action, accels);
						}
					}
				}
			else
				{
				char **accels = g_key_file_get_string_list(accels_keyfile, d->action_name, "accels", nullptr, nullptr);
				if (accels)
					{
					registered_accels_set(app, d->action_name, accels);
					g_strfreev(accels);
					}
				}
			}
		}
	if (app) registered_accels_apply(app, gtk_application_get_active_window(app));
}

bool action_uses_context_shortcut(const gchar *name)
{
	const ActionDef *tables[] = {get_main_actions(), get_image_actions(), get_pan_view_actions(), get_log_actions()};
	for (const auto *table : tables)
		for (const auto *def = table; def->action_name; ++def)
			if (g_strcmp0(name, def->action_name) == 0) return def->context_shortcut;
	return false;
}

bool activate_context_shortcut(GtkWidget *window, const ActionDef *defs, guint keyval, GdkModifierType state)
{
	GKeyFile *key_file = get_keyfile_merged();
	if (!window || !key_file) return false;
	const auto modifiers = static_cast<GdkModifierType>(state & gtk_accelerator_get_default_mod_mask());
	for (const ActionDef *def = defs; def->action_name; ++def)
		{
		if (!def->context_shortcut) continue;
		g_auto(GStrv) accels = g_key_file_get_string_list(key_file, def->action_name, "accels", nullptr, nullptr);
		for (gchar **accel = accels; accel && *accel; ++accel)
			{
			guint key = 0;
			GdkModifierType mods = GDK_NO_MODIFIER_MASK;
			if (!gtk_accelerator_parse(*accel, &key, &mods) || !key ||
			    gdk_keyval_to_lower(keyval) != key || modifiers != mods) continue;
			const char *name = strchr(def->action_name, '.') + 1;
			auto *action = g_action_map_lookup_action(G_ACTION_MAP(window), name);
			if (!action || !g_action_get_enabled(action)) continue;
			g_action_activate(action, nullptr);
			return true;
			}
		}
	return false;
}

const char *get_icon_for_action_name(const char *action_name)
{
	const ActionDef *action_sets[] =
		{
		get_app_actions(),
		get_dupe_main_actions(),
		get_dupe_second_actions(),
		get_image_actions(),
		get_main_actions(),
		get_pan_view_actions(),
		get_search_actions(),
		get_view_file_actions(),
		get_log_actions()
		};

	for (const ActionDef *action_table : action_sets)
		{
		for (unsigned int i = 0; action_table[i].action_name != nullptr; i++)
			{
			if (action_table[i].action_name)
				{
				if (!g_strcmp0(action_table[i].action_name, action_name))
					{
					return g_strdup(action_table[i].icon_name);
					}
				}
			}
		}

	return nullptr;
}

struct ActionLine
{
	const char *action_name;
	const char *description;
};

GStrv action_defs_to_aligned_lines(const ActionDef *app_actions)
{
	std::vector<ActionLine> rows;
	size_t max_entry_len = 0;

	for (const ActionDef *action = app_actions; action->action_name != nullptr; action++)
		{
		if (!action->description)
			{
			continue;
			}

		const char *action_name = action->action_name;
		const char *pos = g_strrstr(action_name, "-win-");

		if (pos)
			{
			action_name = pos + strlen("-win-");
			}
		else
			{
			pos = g_strrstr(action_name, "app.");
			if (pos)
				{
				action_name = pos + strlen("app.");
				}
			}

		rows.push_back({ action_name, action->description });

		size_t len = strlen(action_name);
		max_entry_len = std::max(len, max_entry_len);
		}

	std::sort(rows.begin(), rows.end(), [](const ActionLine &a, const ActionLine &b)
		{
		return g_strcmp0(a.action_name, b.action_name) < 0;
		});

	GPtrArray *array = g_ptr_array_new_with_free_func(g_free);

	for (const auto &row : rows)
		{
		g_ptr_array_add(array, g_strdup_printf("%-*s %s", static_cast<int>(max_entry_len), row.action_name, row.description));
		}

	g_ptr_array_add(array, nullptr);

	return reinterpret_cast<GStrv>(g_ptr_array_free(array, FALSE));
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
