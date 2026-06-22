// SPDX-License-Identifier: GPL-2.0-or-later

static const ActionDef toolbar_main_actions[] =
{
		{
		"win.preferences-win-main-toolbarlist-add",
		nullptr,
		N_("Toolbar add"),
		toolbarlist_add_button_main_cb,
		G_VARIANT_TYPE_STRING,
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

static const ActionDef toolbar_status_actions[] =
{
		{
		"win.preferences-win-status-toolbarlist-add",
		nullptr,
		N_("Toolbar add"),
		toolbarlist_add_button_status_cb,
		G_VARIANT_TYPE_STRING,
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
