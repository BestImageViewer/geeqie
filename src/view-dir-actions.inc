// SPDX-License-Identifier: GPL-2.0-or-later


static const gchar *const dir_mode_targets[] =
{
	"list",
	"tree",
	nullptr
};

static const gchar *const dir_sort_targets[] =
{
	"name",
	"number",
	"date",
	nullptr
};

static const ActionDef view_dir_actions[] =
{
		{
		"win.dir-win-up",
		nullptr,
		N_("Up to parent"),
		vd_pop_menu_up_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-slideshow",
		GQ_ICON_COPY,
		N_("_Slideshow"),
		vd_pop_menu_slide_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-slideshow-recursive",
		GQ_ICON_COPY,
		N_("Slideshow recursive"),
		vd_pop_menu_slide_rec_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-find-duplicates",
		GQ_ICON_COPY,
		N_("Find _duplicates"),
		vd_pop_menu_dupe_cb<FALSE>,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-find-duplicates-recursive",
		GQ_ICON_COPY,
		N_("Find _duplicates recursive"),
		vd_pop_menu_dupe_cb<TRUE>,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-new-folder",
		GQ_ICON_COPY,
		N_("New folder"),
		vd_pop_menu_new_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-rename",
		GQ_ICON_DELETE,
		N_("Rename"),
		vd_pop_menu_rename_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-copy-path",
		GQ_ICON_DELETE,
		N_("Copy to clipboard"),
		vd_pop_menu_copy_path_cb<TRUE>,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-copy-path-unquoted",
		nullptr,
		N_("Copy to clipboard (unquoted)"),
		vd_pop_menu_copy_path_cb<FALSE>,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-cut-to-clipboard",
		nullptr,
		N_("Cut to clipboard"),
		vd_pop_menu_cut_path_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-delete",
		nullptr,
		N_("Delete"),
		vd_pop_menu_delete_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-view-type",
		nullptr,
		N_("View as "),
		vd_pop_submenu_dir_view_as_cb,
		G_VARIANT_TYPE_STRING,
		ActionStateType::STRING,
		FALSE,
		"tree",
		0,
		dir_mode_targets,
		},

		{
		"win.dir-win-sort",
		nullptr,
		N_("Sort by"),
		vd_pop_menu_sort_cb,
		G_VARIANT_TYPE_STRING,
		ActionStateType::STRING,
		FALSE,
		"name",
		0,
		dir_sort_targets,
		},

		{
		"win.dir-win-sort-ascending",
		nullptr,
		N_("Sort by"),
		vd_pop_menu_sort_ascend_cb,
		nullptr,
		ActionStateType::BOOLEAN,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-sort-case",
		nullptr,
		N_("Sort by"),
		vd_pop_menu_sort_case_cb,
		nullptr,
		ActionStateType::BOOLEAN,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-show-hidden-files",
		nullptr,
		N_("Show hidden files"),
		vd_toggle_show_hidden_files_cb,
		nullptr,
		ActionStateType::BOOLEAN,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.dir-win-refresh",
		nullptr,
		N_("Refresh"),
		vd_pop_menu_refresh_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		}
};
