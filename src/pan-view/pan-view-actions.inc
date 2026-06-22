// SPDX-License-Identifier: GPL-2.0-or-later

static const gchar *const show_image_targets[] =
{
	"none",
	"full-size",
	"1-2",
	"1-3",
	"1-4",
	"1-10",
	nullptr
};
static const gchar *const zoom_targets[] =
{
	"in",
	"out",
	"1-1",
	"1-2",
	"1-3",
	"1-4",
	"2-1",
	"3-1",
	"4-1",
	nullptr
};

static  const ActionDef pan_actions[] =
{
		{
		"win.pan-win-close-window",
		nullptr,
		N_("Close window"),
		pan_close_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-collections",
		nullptr,
		N_("Collection"),
		pan_pop_menu_collections_cb,
		G_VARIANT_TYPE_INT32,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-copy",
		nullptr,
		N_("Copy…"),
		pan_copy_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		},

		{
		"win.pan-win-copy-path",
		nullptr,
		N_("Copy path"),
		pan_copy_path_cb<TRUE>,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-copy-path-unquoted",
		nullptr,
		N_("Copy path unquoted"),
		pan_copy_path_cb<FALSE>,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-delete",
		nullptr,
		N_("Move to trash"),
		pan_delete_cb<TRUE>,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-delete-permanent",
		nullptr,
		N_("Delete"),
		pan_delete_cb<FALSE>,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-display-find-search-bar",
		nullptr,
		N_("Display find search bar"),
		pan_search_bar_visible_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		},

		{
		"win.pan-win-escape",
		nullptr,
		N_("Exit fullscreen / Hide seach bar"),
		pan_escape_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		},

		{
		"win.pan-win-fullscreen",
		nullptr,
		N_("Fullscreen"),
		pan_fullscreen_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-fullscreen-toggle",
		nullptr,
		N_("Fullscreen toggle"),
		pan_fullscreen_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-go-to-original",
		nullptr,
		N_("Go to original"),
		pan_go_to_original_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		},

		{
		"win.pan-win-help",
		nullptr,
		N_("Help"),
		pan_window_help_action_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-menu",
		nullptr,
		N_("Menu"),
		pan_popup_menu_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-move",
		nullptr,
		N_("Move…"),
		pan_move_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		},

		{
		"win.pan-win-play",
		nullptr,
		N_("Play…"),
		pan_play_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		},

		{
		"win.pan-win-plugin-run",
		nullptr,
		N_("Run Plugin"),
		pan_edit_cb,
		G_VARIANT_TYPE_STRING,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-plugins",
		nullptr,
		N_("Plugins"),
		pan_edit_cb,
		G_VARIANT_TYPE_STRING,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-rename",
		nullptr,
		N_("Rename…"),
		pan_rename_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		},

		{
		"win.pan-win-search-start",
		nullptr,
		N_("Start search"),
		pan_search_start_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		},

		{
		"win.pan-win-show-exif-information",
		nullptr,
		N_("Show exif information"),
		pan_info_toggle_exif_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-show-image",
		nullptr,
		N_("Show image"),
		pan_info_toggle_image_cb,
		G_VARIANT_TYPE_STRING,
		ActionStateType::STRING,
		FALSE,
		"none",
		0,
		show_image_targets,
		},

		{
		"win.pan-win-sort-by-exif-date",
		nullptr,
		N_("Sort by exif date"),
		pan_exif_date_toggle_cb,
		nullptr,
		ActionStateType::BOOLEAN,
		FALSE,
		nullptr,
		0,
		nullptr,
		},

		{
		"win.pan-win-view-in-new-window",
		nullptr,
		N_("View in new window"),
		pan_new_window_cb,
		nullptr,
		ActionStateType::NONE,
		FALSE,
		nullptr,
		0,
		nullptr
		},

		{
		"win.pan-win-zoom",
		nullptr,
		N_("Image zoom"),
		pan_zoom_cb,
		G_VARIANT_TYPE_STRING,
		ActionStateType::NONE,
		FALSE,
		"1-1",
		0,
		zoom_targets,
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
