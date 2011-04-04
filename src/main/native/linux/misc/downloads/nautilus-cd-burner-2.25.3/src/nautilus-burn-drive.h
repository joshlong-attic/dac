/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nautilus-burn-drive.h: easy to use cd burner software
 *
 * Copyright (C) 2002-2004 Red Hat, Inc.
 * Copyright (C) 2005-2006 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Alexander Larsson <alexl@redhat.com>
 *          Bastien Nocera <hadess@hadess.net>
 *          William Jon McCann <mccann@jhu.edu>
 */

#ifndef NAUTILUS_BURN_DRIVE_H
#define NAUTILUS_BURN_DRIVE_H

#include <glib-object.h>
#include <glib.h>
#include <math.h>

#include "nautilus-burn-features.h"

G_BEGIN_DECLS

typedef enum {
        NAUTILUS_BURN_MEDIA_TYPE_BUSY,
        NAUTILUS_BURN_MEDIA_TYPE_ERROR,
        NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN,
        NAUTILUS_BURN_MEDIA_TYPE_CD,
        NAUTILUS_BURN_MEDIA_TYPE_CDR,
        NAUTILUS_BURN_MEDIA_TYPE_CDRW,
        NAUTILUS_BURN_MEDIA_TYPE_DVD,
        NAUTILUS_BURN_MEDIA_TYPE_DVDR,
        NAUTILUS_BURN_MEDIA_TYPE_DVDRW,
        NAUTILUS_BURN_MEDIA_TYPE_DVD_RAM,
        NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R,
        NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW,
        NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL,
} NautilusBurnMediaType;

#define NAUTILUS_BURN_MEDIA_SIZE_UNKNOWN -1
#define NAUTILUS_BURN_MEDIA_SIZE_NA      -2
#define NAUTILUS_BURN_MEDIA_SIZE_BUSY    -3

typedef enum {
        NAUTILUS_BURN_DRIVE_TYPE_FILE                   = 1 << 0,
        NAUTILUS_BURN_DRIVE_TYPE_CD_RECORDER            = 1 << 1,
        NAUTILUS_BURN_DRIVE_TYPE_CDRW_RECORDER          = 1 << 2,
        NAUTILUS_BURN_DRIVE_TYPE_DVD_RAM_RECORDER       = 1 << 3,
        /* Drives are usually DVD-R and DVD-RW */
        NAUTILUS_BURN_DRIVE_TYPE_DVD_RW_RECORDER        = 1 << 4,
        NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_RECORDER    = 1 << 5,
        NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_RW_RECORDER   = 1 << 6,
        NAUTILUS_BURN_DRIVE_TYPE_CD_DRIVE               = 1 << 7,
        NAUTILUS_BURN_DRIVE_TYPE_DVD_DRIVE              = 1 << 8,
        NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_DL_RECORDER = 1 << 9,
} NautilusBurnDriveType;

#define NAUTILUS_BURN_TYPE_DRIVE            (nautilus_burn_drive_get_type ())
#define NAUTILUS_BURN_DRIVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  NAUTILUS_BURN_TYPE_DRIVE, NautilusBurnDrive))
#define NAUTILUS_BURN_DRIVE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),   NAUTILUS_BURN_TYPE_DRIVE, NautilusBurnDriveClass))
#define NAUTILUS_BURN_IS_DRIVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  NAUTILUS_BURN_TYPE_DRIVE))
#define NAUTILUS_BURN_IS_DRIVE_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), NAUTILUS_BURN_TYPE_DRIVE))

typedef struct NautilusBurnDrive                   NautilusBurnDrive;
typedef struct NautilusBurnDriveClass              NautilusBurnDriveClass;
typedef struct NautilusBurnDrivePrivate            NautilusBurnDrivePrivate;

struct NautilusBurnDrive {
        GObject                  parent;

        NautilusBurnDrivePrivate *priv;
};

struct NautilusBurnDriveClass {
        GObjectClass parent_class;

        void     (* media_added)   (NautilusBurnDrive *drive);
        void     (* media_removed) (NautilusBurnDrive *drive);
        void     (* disconnected)  (NautilusBurnDrive *drive);
};

#define NAUTILUS_BURN_DRIVE_SIZE_TO_TIME(size) (size > 1024 * 1024 ? (int) (((size / 1024 / 1024) - 1) * 48 / 7): 0)
#define NAUTILUS_BURN_DRIVE_CD_SPEED(speed) (floorf (((speed) * 1024 / 153600.0) * 10) / 10.0)
#define NAUTILUS_BURN_DRIVE_DVD_SPEED(speed) (floorf (((speed)  * 1024 / 1385000.0) * 10) / 10.0)
#define NAUTILUS_BURN_DRIVE_MEDIA_TYPE_IS_DVD(media_type) \
                                          (((media_type == NAUTILUS_BURN_MEDIA_TYPE_DVD)                ||  \
                                            (media_type == NAUTILUS_BURN_MEDIA_TYPE_DVDR)               ||  \
                                            (media_type == NAUTILUS_BURN_MEDIA_TYPE_DVDRW)              ||  \
                                            (media_type == NAUTILUS_BURN_MEDIA_TYPE_DVD_RAM)            ||  \
                                            (media_type == NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R)         ||  \
                                            (media_type == NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW)        ||  \
                                            (media_type == NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL)) != 0)

GType                 nautilus_burn_drive_get_type                 (void);

NautilusBurnDrive *   nautilus_burn_drive_ref                      (NautilusBurnDrive       *drive);
void                  nautilus_burn_drive_unref                    (NautilusBurnDrive       *drive);

gboolean              nautilus_burn_drive_equal                    (NautilusBurnDrive       *a,
                                                                    NautilusBurnDrive       *b);

gboolean              nautilus_burn_drive_lock                     (NautilusBurnDrive       *drive,
                                                                    const char              *reason,
                                                                    char                   **reason_for_failure);
gboolean              nautilus_burn_drive_unlock                   (NautilusBurnDrive       *drive);
gboolean              nautilus_burn_drive_unmount                  (NautilusBurnDrive       *drive);
gboolean              nautilus_burn_drive_is_mounted               (NautilusBurnDrive       *drive);
gboolean              nautilus_burn_drive_eject                    (NautilusBurnDrive       *drive);

gboolean              nautilus_burn_drive_door_is_open             (NautilusBurnDrive       *drive);

int                   nautilus_burn_drive_get_drive_type           (NautilusBurnDrive       *drive);
char *                nautilus_burn_drive_get_name_for_display     (NautilusBurnDrive       *drive);
const char *          nautilus_burn_drive_get_device               (NautilusBurnDrive       *drive);

/* Capabilities */
gboolean              nautilus_burn_drive_can_write                (NautilusBurnDrive       *drive);
gboolean              nautilus_burn_drive_can_rewrite              (NautilusBurnDrive       *drive);

int                   nautilus_burn_drive_get_max_speed_write      (NautilusBurnDrive       *drive);
int                   nautilus_burn_drive_get_max_speed_read       (NautilusBurnDrive       *drive);
const int *           nautilus_burn_drive_get_write_speeds         (NautilusBurnDrive       *drive);

gboolean              nautilus_burn_drive_can_eject                (NautilusBurnDrive       *drive);

/* Media handling */

NautilusBurnMediaType nautilus_burn_drive_get_media_type           (NautilusBurnDrive       *drive);

NautilusBurnMediaType nautilus_burn_drive_get_media_type_full      (NautilusBurnDrive       *drive,
                                                                    gboolean                *is_rewritable,
                                                                    gboolean                *is_blank,
                                                                    gboolean                *has_data,
                                                                    gboolean                *has_audio);
gint64                nautilus_burn_drive_get_media_capacity       (NautilusBurnDrive       *drive);
gint64                nautilus_burn_drive_get_media_size           (NautilusBurnDrive       *drive);
char *                nautilus_burn_drive_get_media_label          (NautilusBurnDrive       *drive);
gboolean              nautilus_burn_drive_media_is_appendable      (NautilusBurnDrive       *drive);

const char *          nautilus_burn_drive_media_type_get_string         (NautilusBurnMediaType    type);
char *                nautilus_burn_drive_get_supported_media_string    (NautilusBurnDrive       *drive,
                                                                         gboolean                 writable_only);
char *                nautilus_burn_drive_get_supported_media_string_for_size (NautilusBurnDrive *drive,
                                                                               guint64            size);
gboolean              nautilus_burn_drive_media_type_is_writable        (NautilusBurnMediaType    type,
                                                                         gboolean                 is_blank);

G_END_DECLS

#endif
