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
 * @brief Mouse and arrow key shortcuts
 * @param
 * @returns
 *
 *
 */
static gchar *resource_text_load_extra()
{
	g_autoptr(GError) error = nullptr;
	g_autoptr(GBytes) bytes = g_resources_lookup_data(GQ_RESOURCE_PATH_UI "/keyboard-shortcuts-ui.txt", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);

	if (!bytes)
		{
		log_printf("Failed to load resource keyboard-shortcuts.ui: %s", error ? error->message : "unknown error");

		return nullptr;
		}

	gsize length = 0;
	const gchar *data = static_cast<const gchar *>(g_bytes_get_data(bytes, &length));

	return g_strndup(data, length);
}

struct ActionTableDef
{
	const ActionDef *actions;
	const gchar *section_name;
	const gchar *section_title;
};

/**
 * @brief
 * @param xml
 * @param kf
 * @param table
 * @param max_per_group
 *
 *
 */
void shortcuts_xml_append_section(GString *xml, GKeyFile *kf, const ActionTableDef *table, gint max_per_group)
{
	g_autofree gchar *section_name = g_markup_escape_text(table->section_name, -1);

	g_autofree gchar *section_title = g_markup_escape_text((table->section_title), -1);

	g_string_append_printf(xml,
	                       "    <child>\n"
	                       "      <object class='GtkShortcutsSection'>\n"
	                       "        <property name=\"visible\">1</property>\n"
	                       "        <property name=\"section-name\">%s</property>\n"
	                       "        <property name=\"title\" translatable=\"yes\">%s</property>\n"
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
		    : g_strdup_printf(("Keys (%d)"), group_number);

		g_autofree gchar *escaped = g_markup_escape_text(title, -1);

		g_string_append_printf(xml,
		                       "        <child>\n"
		                       "          <object class='GtkShortcutsGroup'>\n"
		                       "            <property name=\"visible\">1</property>\n"
		                       "            <property name=\"title\" translatable=\"yes\">%s</property>\n",
		                       escaped);

		group_open = TRUE;
		count_in_group = 0;
		group_number++;
		};

	auto close_group = [&]()
		{
		g_string_append(xml,
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

			for (gsize a = 0; a < n_accels; a++)
				{
				if (!accels[a] || !*accels[a])
					{
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
					title = g_strdup_printf("%s %s", description, targets[t]);
					}
				else
					{
					title = g_strdup(description);
					}

				g_autofree gchar *title_escaped = g_markup_escape_text(title, -1);
				g_autofree gchar *accel_escaped = g_markup_escape_text(accels[a], -1);

				g_string_append_printf(xml,
				                       "            <child>\n"
				                       "              <object class='GtkShortcutsShortcut'>\n"
				                       "                <property name=\"visible\">1</property>\n"
				                       "                <property name=\"title\" translatable=\"yes\">%s</property>\n"
				                       "                <property name=\"accelerator\">%s</property>\n"
				                       "              </object>\n"
				                       "            </child>\n",
				                       title_escaped,
				                       accel_escaped);

				count_in_group++;
				}
			}
		}

	if (group_open)
		{
		close_group();
		}

	g_string_append(xml,
	                "      </object>\n"
	                "    </child>\n");
}

/**
 * @brief
 * @param kf
 * @param tables
 * @param n_tables
 * @param max_per_group
 * @returns
 *
 *
 */
static gchar *shortcuts_xml_from_keyfile_and_actions(GKeyFile *kf, const ActionTableDef *tables, gsize n_tables, gint max_per_group)
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

	for (gsize i = 0; i < n_tables; i++)
		{
		if (!tables[i].actions)
			{
			continue;
			}

		shortcuts_xml_append_section(xml, kf, &tables[i], max_per_group);
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
 * @brief
 * @param kf
 * @param tables
 * @param n_tables
 * @returns
 *
 *
 */
GtkShortcutsWindow *shortcuts_window_new_from_keyfile_and_actions(GKeyFile *kf, const ActionTableDef *tables, gsize n_tables)
{
	if (!kf)
		{
		return nullptr;
		}

	g_autofree gchar *xml = shortcuts_xml_from_keyfile_and_actions(kf, tables, n_tables, 10);

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
	{ nullptr, "app", _("All Windows") },
	{ nullptr, "collection", _("Collection Window") },
	{ nullptr, "dupe-main", _("Duplicates Main Window") },
	{ nullptr, "dupe-second", _("Duplicates Second Window") },
	{ nullptr, "image", _("Image View Window") },
	{ nullptr, "main", _("Main Window") },
	{ nullptr, "pan", _("Pan View Window") },
	{ nullptr, "search", _("Search Window") },
};

} // namespace

void shortcuts_window_new_from_keyfile()
{

	GKeyFile *accel_keyfile = get_keyfile_merged();

	shortcut_tables[0].actions = get_app_actions();
	shortcut_tables[1].actions = get_collection_actions();
	shortcut_tables[2].actions = get_dupe_main_actions();
	shortcut_tables[3].actions = get_dupe_second_actions();
	shortcut_tables[4].actions = get_image_actions();
	shortcut_tables[5].actions = get_main_actions();
	shortcut_tables[6].actions = get_pan_view_actions();
	shortcut_tables[7].actions = get_search_actions();

	GtkShortcutsWindow *win = shortcuts_window_new_from_keyfile_and_actions(accel_keyfile, shortcut_tables, G_N_ELEMENTS(shortcut_tables));

	if (win)
		{
		gtk_window_present(GTK_WINDOW(win));
		}
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
