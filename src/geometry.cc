/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "geometry.h"

#include "misc.h"

GqSize *gq_size_copy(GqSize *size)
{
	return new GqSize(*size);
}

G_DEFINE_BOXED_TYPE(GqSize, gq_size, gq_size_copy, delete_cb<GqSize>)

/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
