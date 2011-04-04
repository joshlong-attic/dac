/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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
 */

#ifndef _NAUTILUS_BURN_DRIVE_PRIVATE_H
#define _NAUTILUS_BURN_DRIVE_PRIVATE_H

#include "nautilus-burn-drive.h"

G_BEGIN_DECLS

struct NautilusBurnDrivePrivate {
        int                          *write_speeds;

        char                         *drive_udi;
        char                         *media_udi;

        int                           type;
        char                         *device;
        char                         *display_name;
        int                           max_speed_write;
        int                           max_speed_read;

        gint64                        media_size;
        gint64                        media_capacity;
        NautilusBurnMediaType         media_type;
        char                         *media_label;
        gboolean                      is_rewritable;
        gboolean                      is_blank;
        gboolean                      has_data;
        gboolean                      has_audio;

        gboolean                      media_is_mounted;
        gboolean                      media_is_appendable;

        gboolean                      is_connected;
};

NautilusBurnDrive *   _nautilus_burn_drive_new                      (void);
void                  _nautilus_burn_drive_media_added              (NautilusBurnDrive *drive);
void                  _nautilus_burn_drive_media_removed            (NautilusBurnDrive *drive);
void                  _nautilus_burn_drive_disconnected             (NautilusBurnDrive *drive);

G_END_DECLS

#endif
