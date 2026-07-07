// SPDX-License-Identifier: GPL-2.0-or-later

#include "actions.h"

#include <algorithm>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "accelerators.h"
#include "convert-configuration.h"
#include "layout-util.h"
#include "main-defines.h"
#include "ui-fileops.h"
#include "ui-menu.h"
#include "ui-misc.h"

const char *get_description_for_action_name(const char *action_name)
{
	const ActionDef *action_sets[] =
		{
		get_app_actions(),
		get_collection_actions(),
		get_dupe_main_actions(),
		get_dupe_second_actions(),
		get_image_actions(),
		get_main_actions(),
		get_pan_view_actions(),
		get_search_actions(),
		get_view_file_actions()
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

static inline bool is_app_action(const char *name)
{
	return g_str_has_prefix(name, "app.");
}

namespace
{

constexpr const char *ACCEL_FOCUS_HANDLER_ATTACHED = "geeqie-accel-focus-handler-attached";

GHashTable *registered_accels = nullptr;
bool registered_accels_suppressed = false;

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

	const char *empty_accels[] = {nullptr};
	gtk_application_set_accels_for_action(app, detailed_action,
	                                      registered_accels_suppressed ? empty_accels : const_cast<const char * const *>(accels));
}

void registered_accels_apply(GtkApplication *app, bool suppress)
{
	if (!app || !registered_accels || registered_accels_suppressed == suppress)
		{
		return;
		}

	registered_accels_suppressed = suppress;
	const char *empty_accels[] = {nullptr};

	GHashTableIter iter;
	gpointer key;
	gpointer value;

	g_hash_table_iter_init(&iter, registered_accels);
	while (g_hash_table_iter_next(&iter, &key, &value))
		{
		const auto *detailed_action = static_cast<const char *>(key);
		auto accels = static_cast<GStrv>(value);

		gtk_application_set_accels_for_action(app, detailed_action,
		                                      suppress ? empty_accels : const_cast<const char * const *>(accels));
		}
}

void window_focus_widget_notify_cb(GtkWindow *window, GParamSpec *, gpointer data)
{
	auto app = GTK_APPLICATION(data);

	registered_accels_apply(app, focus_is_editable(window));
}

void attach_accel_focus_handler(GtkApplication *app, GtkWidget *window)
{
	if (!app || !GTK_IS_WINDOW(window) ||
	    g_object_get_data(G_OBJECT(window), ACCEL_FOCUS_HANDLER_ATTACHED))
		{
		return;
		}

	g_signal_connect(window, "notify::focus-widget", G_CALLBACK(window_focus_widget_notify_cb), app);
	g_object_set_data(G_OBJECT(window), ACCEL_FOCUS_HANDLER_ATTACHED, GINT_TO_POINTER(TRUE));
}

} // namespace

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
	char *action_name;
	attach_accel_focus_handler(app, window);

	for (const ActionDef *d = defs; d->action_name; ++d)
		{
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

		GSimpleAction *action = nullptr;

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
}

const char *get_icon_for_action_name(const char *action_name)
{
	const ActionDef *action_sets[] =
		{
		get_app_actions(),
		get_collection_actions(),
		get_dupe_main_actions(),
		get_dupe_second_actions(),
		get_image_actions(),
		get_main_actions(),
		get_pan_view_actions(),
		get_search_actions(),
		get_view_file_actions()
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
