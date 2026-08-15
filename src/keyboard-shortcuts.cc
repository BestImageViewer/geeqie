/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "keyboard-shortcuts.h"

#include <glib/gi18n.h>

#include "accelerators.h"
#include "actions.h"
#include "main-defines.h"

namespace
{

/**
 * @brief Load the XML fragment for mouse and arrow key shortcuts
 * @return Newly allocated XML fragment, or @c nullptr on failure
 */
gchar *resource_text_load_extra()
{
	g_autoptr(GError) error = nullptr;
	g_autoptr(GBytes) bytes = g_resources_lookup_data(GQ_RESOURCE_PATH_UI "/keyboard-shortcuts-extra.ui.inc", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);

	if (!bytes)
		{
		log_printf("Failed to load resource keyboard-shortcuts-extra.ui.inc: %s", error ? error->message : "unknown error");

		return nullptr;
		}

	gsize length = 0;
	const auto *data = static_cast<const gchar *>(g_bytes_get_data(bytes, &length));

	return g_strndup(data, length);
}

struct ActionTableDef
{
	const ActionDef *actions;
	const gchar *section_name;
	const gchar *section_title;
};

/**
 * @brief Append a non-empty shortcuts section generated from an action table
 * @param xml Destination XML
 * @param kf Accelerator key file
 * @param table Action table and section details
 * @param max_per_group Maximum shortcuts in each group
 * @param used_accels Normalized accelerators already assigned to higher-priority actions
 */
void shortcuts_xml_append_section(GString *xml, GKeyFile *kf, const ActionTableDef *table, gint max_per_group, GHashTable *used_accels)
{
	g_autofree gchar *section_name = g_markup_escape_text(table->section_name, -1);
	g_autofree gchar *section_title = g_markup_escape_text(_(table->section_title), -1);
	GString *section_xml = g_string_new(nullptr);

	g_string_append_printf(section_xml,
	                       "    <child>\n"
	                       "      <object class='GtkShortcutsSection'>\n"
	                       "        <property name=\"visible\">1</property>\n"
	                       "        <property name=\"section-name\">%s</property>\n"
	                       "        <property name=\"title\">%s</property>\n"
	                       "        <property name=\"max-height\">10</property>\n",
	                       section_name,
	                       section_title);

	gboolean group_open = FALSE;
	gint count_in_group = 0;
	gint group_number = 1;

	auto open_group = [&]()
		{
		g_autofree gchar *title =
		    (group_number == 1)
		    ? g_strdup(_("Keys"))
		    : g_strdup_printf(_("Keys (%d)"), group_number);

		g_autofree gchar *escaped = g_markup_escape_text(title, -1);

		g_string_append_printf(section_xml,
		                       "        <child>\n"
		                       "          <object class='GtkShortcutsGroup'>\n"
		                       "            <property name=\"visible\">1</property>\n"
		                       "            <property name=\"title\">%s</property>\n",
		                       escaped);

		group_open = TRUE;
		count_in_group = 0;
		group_number++;
		};

	auto close_group = [&]()
		{
		g_string_append(section_xml,
		                "          </object>\n"
		                "        </child>\n");

		group_open = FALSE;
		};

	for (guint i = 0; table->actions[i].action_name != nullptr; i++)
		{
		const gchar *action_name = table->actions[i].action_name;
		const gchar *description = table->actions[i].description;
		const gchar * const *targets = table->actions[i].targets;

		if (!description)
			{
			continue;
			}
		for (guint t = 0; ; t++)
			{
			g_autofree gchar *detailed_action = nullptr;

			if (targets)
				{
				if (!targets[t])
					{
					break;
					}

				detailed_action = g_strdup_printf("%s('%s')", action_name, targets[t]);
				}
			else
				{
				if (t > 0)
					{
					break;
					}

				detailed_action = g_strdup(action_name);
				}

			gsize n_accels = 0;
			g_auto(GStrv) accels = g_key_file_get_string_list(kf, detailed_action, "accels", &n_accels, nullptr);

			if (!accels || n_accels == 0)
				{
				continue;
				}

			GString *joined_accels = g_string_new(nullptr);
			for (gsize a = 0; a < n_accels; a++)
				{
				if (!accels[a] || !*accels[a])
					{
					continue;
					}

				guint key = 0;
				auto modifiers = static_cast<GdkModifierType>(0);
				gtk_accelerator_parse(accels[a], &key, &modifiers);
				if (key == 0)
					{
					continue;
					}

				g_autofree gchar *normalized = gtk_accelerator_name(key, modifiers);
				if (!normalized || g_hash_table_contains(used_accels, normalized))
					{
					continue;
					}

				g_hash_table_add(used_accels, g_strdup(normalized));

				if (joined_accels->len > 0)
					{
					g_string_append_c(joined_accels, ' ');
					}
				g_string_append(joined_accels, normalized);
				}

			if (joined_accels->len == 0)
				{
				g_string_free(joined_accels, TRUE);
				continue;
				}

			if (!group_open || count_in_group >= max_per_group)
				{
				if (group_open)
					{
					close_group();
					}

				open_group();
				}

			g_autofree gchar *title = nullptr;

			if (targets)
				{
				title = g_strdup_printf("%s %s", _(description), targets[t]);
				}
			else
				{
				title = g_strdup(_(description));
				}

			g_autofree gchar *title_escaped = g_markup_escape_text(title, -1);
			g_autofree gchar *accel_escaped = g_markup_escape_text(joined_accels->str, -1);

			g_string_append_printf(section_xml,
			                       "            <child>\n"
			                       "              <object class='GtkShortcutsShortcut'>\n"
			                       "                <property name=\"visible\">1</property>\n"
			                       "                <property name=\"title\">%s</property>\n"
			                       "                <property name=\"accelerator\">%s</property>\n"
			                       "              </object>\n"
			                       "            </child>\n",
			                       title_escaped,
			                       accel_escaped);

			g_string_free(joined_accels, TRUE);
			count_in_group++;
			}
		}

	if (!group_open)
		{
		g_string_free(section_xml, TRUE);
		return;
		}

	close_group();

	g_string_append(section_xml,
	                "      </object>\n"
	                "    </child>\n");
	g_string_append_len(xml, section_xml->str, section_xml->len);
	g_string_free(section_xml, TRUE);
}

/**
 * @brief Generate GtkBuilder XML from accelerator and action tables
 * @param kf Accelerator key file
 * @param tables Action tables and section details
 * @param n_tables Number of action tables
 * @param max_per_group Maximum shortcuts in each group
 * @return Newly allocated GtkBuilder XML
 */
gchar *shortcuts_xml_from_keyfile_and_actions(GKeyFile *kf, const ActionTableDef *tables, gsize n_tables, gint max_per_group)
{
	if (!kf || !tables || n_tables == 0)
		{
		return nullptr;
		}

	GString *xml = g_string_new(
	                   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	                   "<interface>\n"
	                   "  <object class='GtkShortcutsWindow' id=\"shortcuts-builder\">\n"
	                   "    <property name=\"modal\">0</property>\n");
	g_autoptr(GHashTable) used_accels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);

	for (gsize i = 0; i < n_tables; i++)
		{
		if (!tables[i].actions)
			{
			continue;
			}

		shortcuts_xml_append_section(xml, kf, &tables[i], max_per_group, used_accels);
		}

	g_autofree gchar *extra_xml = resource_text_load_extra();
	if (extra_xml && *extra_xml)
		{
		g_string_append(xml, extra_xml);
		if (xml->len > 0 && xml->str[xml->len - 1] != '\n')
			{
			g_string_append_c(xml, '\n');
			}
		}

	g_string_append(xml,
	                "  </object>\n"
	                "</interface>\n");

	return g_string_free(xml, FALSE);
}

/**
 * @brief Create a keyboard shortcuts window from generated XML
 * @param xml GtkBuilder XML
 * @return A new referenced shortcuts window, or @c nullptr on failure
 */
GtkShortcutsWindow *shortcuts_window_new_from_xml(const gchar *xml)
{
	if (!xml)
		{
		return nullptr;
		}

	GtkBuilder *builder = gtk_builder_new_from_string(xml, -1);

	if (!builder)
		{
		log_printf("Failed to create GtkBuilder from shortcuts XML");
		return nullptr;
		}

	GObject *obj = gtk_builder_get_object(builder, "shortcuts-builder");

	if (!obj || !GTK_IS_SHORTCUTS_WINDOW(obj))
		{
		log_printf("GtkShortcutsWindow object not found in generated XML");
		g_object_unref(builder);
		return nullptr;
		}

	GtkShortcutsWindow *window = GTK_SHORTCUTS_WINDOW(obj);

	g_object_ref(window);
	g_object_unref(builder);

	return window;
}

ActionTableDef shortcut_tables[] =
{
	{ nullptr, "app", N_("All Windows") },
	{ nullptr, "main", N_("Main Window") },
	{ nullptr, "advanced-exif", N_("Advanced EXIF Window") },
	{ nullptr, "collection", N_("Collection Window") },
	{ nullptr, "dupe-main", N_("Duplicates Main Window") },
	{ nullptr, "dupe-second", N_("Duplicates Second Window") },
	{ nullptr, "image", N_("Image View Window") },
	{ nullptr, "pan", N_("Pan View Window") },
	{ nullptr, "search", N_("Search Window") },
	{ nullptr, "view-file", N_("View File Window") },
};

void shortcut_tables_set_actions()
{
	shortcut_tables[0].actions = get_app_actions();
	shortcut_tables[1].actions = get_main_actions();
	shortcut_tables[2].actions = get_advanced_exif_actions();
	shortcut_tables[3].actions = get_collection_actions();
	shortcut_tables[4].actions = get_dupe_main_actions();
	shortcut_tables[5].actions = get_dupe_second_actions();
	shortcut_tables[6].actions = get_image_actions();
	shortcut_tables[7].actions = get_pan_view_actions();
	shortcut_tables[8].actions = get_search_actions();
	shortcut_tables[9].actions = get_view_file_actions();
}

} // namespace

gchar *shortcuts_xml_from_keyfile(GKeyFile *key_file)
{
	shortcut_tables_set_actions();

	return shortcuts_xml_from_keyfile_and_actions(key_file, shortcut_tables, G_N_ELEMENTS(shortcut_tables), 10);
}

void shortcuts_window_new_from_keyfile()
{
	GKeyFile *accel_keyfile = get_keyfile_merged();

	g_autofree gchar *xml = shortcuts_xml_from_keyfile(accel_keyfile);
	GtkShortcutsWindow *win = shortcuts_window_new_from_xml(xml);

	if (win)
		{
		gtk_window_present(GTK_WINDOW(win));
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
