/*
 * Copyright (C) 2006 John Ellis
 * Copyright (C) 2008 - 2016 The Geeqie Team
 *
 * Author: John Ellis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/** @file renderer-tiles.h
 * @ingroup rendering
 */

#ifndef RENDERER_TILES_H
#define RENDERER_TILES_H

#include <cairo.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

struct PixbufRenderer;
struct RendererFuncs;

/** @addtogroup rendering
 * @{
 */

/** Create a backend owned by @p pr; release it through RendererFuncs::free.
 * The backend borrows the widget, which must outlive it. Use on the main thread.
 */
RendererFuncs *renderer_tiles_new(PixbufRenderer *pr);
/** @brief Whether all queued image tiles have finished rendering. */
gboolean renderer_tiles_is_ready(RendererFuncs *renderer);
/** Create an immutable, opaque copy of borrowed RGB24 surface pixels.
 * The returned texture is an owned reference for g_object_unref(), or nullptr
 * on invalid input. The caller may change/destroy the surface after this call.
 */
GdkTexture *renderer_tiles_surface_to_texture(cairo_surface_t *surface);
/** Append the current composition to a borrowed snapshot on the main thread.
 * The snapshot may retain immutable textures/render nodes beyond this call;
 * mutable tile storage must not be shared with those retained textures.
 */
void renderer_tiles_snapshot(RendererFuncs *renderer, GtkSnapshot *snapshot, double display_scale);

/** @} */

#endif
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
