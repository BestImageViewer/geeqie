/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IMAGE_LOAD_ORA_H
#define IMAGE_LOAD_ORA_H

#include <memory>

#include <gio/gio.h>

struct ImageLoaderBackend;

/** @brief Display an OpenRaster archive's merged PNG without extracting files. */
std::unique_ptr<ImageLoaderBackend> get_image_loader_backend_ora(GCancellable *cancellable = nullptr);

#endif
