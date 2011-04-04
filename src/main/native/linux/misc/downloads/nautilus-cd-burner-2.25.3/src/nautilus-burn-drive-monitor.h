/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#ifndef __NAUTILUS_BURN_DRIVE_MONITOR_H
#define __NAUTILUS_BURN_DRIVE_MONITOR_H

#include <glib-object.h>
#include "nautilus-burn-drive.h"

G_BEGIN_DECLS

#define NAUTILUS_BURN_TYPE_DRIVE_MONITOR         (nautilus_burn_drive_monitor_get_type ())
#define NAUTILUS_BURN_DRIVE_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), NAUTILUS_BURN_TYPE_DRIVE_MONITOR, NautilusBurnDriveMonitor))
#define NAUTILUS_BURN_DRIVE_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), NAUTILUS_BURN_TYPE_DRIVE_MONITOR, NautilusBurnDriveMonitorClass))
#define NAUTILUS_BURN_IS_DRIVE_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), NAUTILUS_BURN_TYPE_DRIVE_MONITOR))
#define NAUTILUS_BURN_IS_DRIVE_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), NAUTILUS_BURN_TYPE_DRIVE_MONITOR))
#define NAUTILUS_BURN_DRIVE_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), NAUTILUS_BURN_TYPE_DRIVE_MONITOR, NautilusBurnDriveMonitorClass))

typedef struct NautilusBurnDriveMonitorPrivate NautilusBurnDriveMonitorPrivate;

typedef struct
{
        GObject                          parent;
        NautilusBurnDriveMonitorPrivate *priv;
} NautilusBurnDriveMonitor;

typedef struct
{
        GObjectClass                     parent_class;

        void (* media_added)            (NautilusBurnDriveMonitor *monitor,
                                         NautilusBurnDrive        *drive);
        void (* media_removed)          (NautilusBurnDriveMonitor *monitor,
                                         NautilusBurnDrive        *drive);
        void (* drive_connected)        (NautilusBurnDriveMonitor *monitor,
                                         NautilusBurnDrive        *drive);
        void (* drive_disconnected)     (NautilusBurnDriveMonitor *monitor,
                                         NautilusBurnDrive        *drive);
} NautilusBurnDriveMonitorClass;

GType                         nautilus_burn_drive_monitor_get_type             (void);

NautilusBurnDriveMonitor *    nautilus_burn_get_drive_monitor                  (void);

GList *                       nautilus_burn_drive_monitor_get_drives           (NautilusBurnDriveMonitor *monitor);
GList *                       nautilus_burn_drive_monitor_get_recorder_drives  (NautilusBurnDriveMonitor *monitor);

NautilusBurnDrive *           nautilus_burn_drive_monitor_get_drive_for_device (NautilusBurnDriveMonitor *monitor,
                                                                                const char               *path);
NautilusBurnDrive *           nautilus_burn_drive_monitor_get_drive_for_image  (NautilusBurnDriveMonitor *monitor);

G_END_DECLS

#endif /* __NAUTILUS_BURN_DRIVE_MONITOR_H */
