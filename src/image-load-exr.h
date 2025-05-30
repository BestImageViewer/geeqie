/*
 * Copyright (C) 2024 - The Geeqie Team
 *
 * Author: Colin Clark
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

#ifndef IMAGE_LOAD_EXR_H
#define IMAGE_LOAD_EXR_H

#include <memory>

struct ImageLoaderBackend;

std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_exr();

#endif /* IMAGE_LOAD_EXR_H */
/* vim: set shiftwidth=8 softtabstop=0 cindent cinoptions={1s: */
