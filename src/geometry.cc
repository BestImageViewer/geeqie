/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "geometry.h"

G_DEFINE_BOXED_TYPE(GqSize, gq_size, gq_size_copy, g_free)

GqSize *gq_size_copy(GqSize *size)
{
	auto *size_copy = g_new0(GqSize, 1);
	*size_copy = *size;
	return size_copy;
}

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
