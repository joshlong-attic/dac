/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * test_blank.c
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
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
 * Authors: William Jon McCann
 *
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>

#include "nautilus-burn.h"

static GMainLoop *loop = NULL;

static void
drive_media_added_cb (NautilusBurnDrive *drive,
                      gpointer           data)
{
        g_print ("** DRIVE: media added to device %s\n", nautilus_burn_drive_get_device (drive));
}

static void
drive_media_removed_cb (NautilusBurnDrive *drive,
                        gpointer           data)
{
        g_print ("** DRIVE: media removed from device %s\n", nautilus_burn_drive_get_device (drive));
}

static void
drive_disconnected_cb (NautilusBurnDrive *drive,
                       gpointer           data)
{
        g_print ("** DRIVE: drive disconnected from device %s\n", nautilus_burn_drive_get_device (drive));
}

static void
monitor_connected_cb (NautilusBurnDriveMonitor *monitor,
                      NautilusBurnDrive        *drive)
{
        g_print ("** MONITOR: drive connected to device %s\n", nautilus_burn_drive_get_device (drive));
}

static void
monitor_disconnected_cb (NautilusBurnDriveMonitor *monitor,
                         NautilusBurnDrive        *drive)
{
        g_print ("** MONITOR: drive disconnected from device %s\n", nautilus_burn_drive_get_device (drive));
}

static void
monitor_media_added_cb (NautilusBurnDriveMonitor *monitor,
                        NautilusBurnDrive        *drive)
{
        g_print ("** MONITOR: media added to device %s\n", nautilus_burn_drive_get_device (drive));
}

static void
monitor_media_removed_cb (NautilusBurnDriveMonitor *monitor,
                          NautilusBurnDrive        *drive)
{
        g_print ("** MONITOR: media removed from device %s\n", nautilus_burn_drive_get_device (drive));
}

static gboolean
monitor_drives (void)
{
        GList                    *drives;
        GList                    *l;
        NautilusBurnDriveMonitor *monitor;

        monitor = nautilus_burn_get_drive_monitor ();
        drives = nautilus_burn_drive_monitor_get_drives (monitor);

        g_signal_connect (monitor, "media-added", G_CALLBACK (monitor_media_added_cb), NULL);
        g_signal_connect (monitor, "media-removed", G_CALLBACK (monitor_media_removed_cb), NULL);
        g_signal_connect (monitor, "drive-connected", G_CALLBACK (monitor_connected_cb), NULL);
        g_signal_connect (monitor, "drive-disconnected", G_CALLBACK (monitor_disconnected_cb), NULL);

        for (l = drives; l != NULL; l = l->next) {
                NautilusBurnDrive *drive;
                char              *display_name;

                drive = l->data;

                display_name = nautilus_burn_drive_get_name_for_display (drive);

                g_print ("Adding monitor for name: %s device: %s\n",
                         display_name,
                         nautilus_burn_drive_get_device (drive));
                g_free (display_name);

                g_signal_connect (drive, "media-added", G_CALLBACK (drive_media_added_cb), NULL);
                g_signal_connect (drive, "media-removed", G_CALLBACK (drive_media_removed_cb), NULL);
                g_signal_connect (drive, "disconnected", G_CALLBACK (drive_disconnected_cb), NULL);
        }

        return FALSE;
}

int
main (int argc, char **argv)
{
        g_type_init ();

        nautilus_burn_init ();

        g_idle_add ((GSourceFunc)monitor_drives, NULL);

        loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (loop);

        nautilus_burn_shutdown ();

        return 0;
}
