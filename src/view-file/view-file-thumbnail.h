/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef VIEW_FILE_THUMBNAIL_H
#define VIEW_FILE_THUMBNAIL_H

#include <glib.h>

class FileData;
struct ViewFile;

void vf_thumbnail_init(ViewFile *vf);
void vf_thumbnail_destroy(ViewFile *vf);
void vf_thumbnail_rebuild(ViewFile *vf);
void vf_thumbnail_model_changed(ViewFile *vf);
void vf_thumbnail_schedule(ViewFile *vf);
void vf_thumbnail_loaded(ViewFile *vf, FileData *fd);
void vf_thumbnail_trim();
FileData *vf_thumbnail_next(ViewFile *vf);
double vf_thumbnail_progress(ViewFile *vf);
gsize vf_thumbnail_resident_bytes();
gint vf_thumbnail_position(ViewFile *vf, FileData *fd);

#endif
