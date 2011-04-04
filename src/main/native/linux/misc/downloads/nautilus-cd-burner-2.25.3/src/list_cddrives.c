/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2003-2004 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2005-2006 William Jon McCann <mccann@jhu.edu>
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
 * Authors: Bastien Nocera <hadess@hadess.net>
 *
 */

#include <glib.h>
#include <string.h>

#include "nautilus-burn.h"

static void
list_drive_info (NautilusBurnDrive *drive)
{
        char       *display_name;
        char       *type_str;
        gboolean    door_is_open;
        gboolean    is_mounted;
        int         max_speed_read;
        int         max_speed_write;
        int         i;
        const int  *write_speeds;

        type_str = nautilus_burn_drive_get_supported_media_string (drive, FALSE);

        door_is_open = nautilus_burn_drive_door_is_open (drive);
        is_mounted = nautilus_burn_drive_is_mounted (drive);
        display_name = nautilus_burn_drive_get_name_for_display (drive);
        max_speed_read = nautilus_burn_drive_get_max_speed_read (drive);
        max_speed_write = nautilus_burn_drive_get_max_speed_write (drive);

        g_print ("Drive:\n");
        g_print ("  name:\t\t\t%s\n", display_name);
        g_print ("  device:\t\t%s\n", nautilus_burn_drive_get_device (drive));
        g_print ("  door:\t\t\t%s\n", door_is_open ? "open" : "closed");
        g_print ("  type:\t\t\t%s\n", type_str);
        g_print ("  is mounted:\t\t%s\n", is_mounted ? "TRUE" : "FALSE");
        g_print ("  max read speed:\t%d KiB/s (CD %.1fx, DVD %.1fx)\n",
                  max_speed_read,
                  NAUTILUS_BURN_DRIVE_CD_SPEED (max_speed_read),
                  NAUTILUS_BURN_DRIVE_DVD_SPEED (max_speed_read));
        g_print ("  max write speed:\t%d KiB/s (CD %.1fx, DVD %.1fx)\n",
                  max_speed_write,
                  NAUTILUS_BURN_DRIVE_CD_SPEED (max_speed_write),
                  NAUTILUS_BURN_DRIVE_DVD_SPEED (max_speed_write));

        write_speeds = nautilus_burn_drive_get_write_speeds (drive);

        g_print ("  write speeds:\t\t");
        for (i = 0; write_speeds[i] > 0; i++) {
                g_print ("%d KiB/s (CD %.1fx, DVD %.1fx)\n\t\t\t",
                          write_speeds[i],
                          NAUTILUS_BURN_DRIVE_CD_SPEED (write_speeds[i]),
                          NAUTILUS_BURN_DRIVE_DVD_SPEED (write_speeds[i]));
        }
        g_print ("\n");

        g_free (type_str);
        g_free (display_name);
}

static void
display_size (gint64 bytes,
              int    media_type)
{
        if (bytes >= 0) {
                g_print ("%0.2f MiB", (float) bytes / 1024 / 1024);

                if (media_type == NAUTILUS_BURN_MEDIA_TYPE_CD
                    || media_type == NAUTILUS_BURN_MEDIA_TYPE_CDR
                    || media_type == NAUTILUS_BURN_MEDIA_TYPE_CDRW) {
                        g_print (" approx. or %d mins %d secs\n",
                                 NAUTILUS_BURN_DRIVE_SIZE_TO_TIME (bytes) / 60,
                                 NAUTILUS_BURN_DRIVE_SIZE_TO_TIME (bytes) % 60);
                } else {
                        g_print ("\n");
                }
        } else {
                g_print ("Could not be determined\n");
        }
}

static void
list_media_info (NautilusBurnDrive *drive)
{
        const char *media;
        int         media_type;
        gboolean    is_appendable;
        gboolean    is_rewritable;
        gboolean    is_blank;
        gboolean    is_writable;
        gboolean    has_data;
        gboolean    has_audio;
        gint64      capacity;
        gint64      size;
        char       *label;

        media_type = nautilus_burn_drive_get_media_type_full (drive,
                                                              &is_rewritable,
                                                              &is_blank,
                                                              &has_data,
                                                              &has_audio);
        media = nautilus_burn_drive_media_type_get_string (media_type);
        is_writable = nautilus_burn_drive_media_type_is_writable (media_type, is_blank);
        label = nautilus_burn_drive_get_media_label (drive);
        is_appendable = nautilus_burn_drive_media_is_appendable (drive);

        g_print ("Media:\n");
        g_print ("  label:\t\t'%s'\n", label ? label : "");
        g_print ("  type:\t\t\t%s%s%s%s%s\n",
                 media,
                 is_rewritable ? " (rewritable)" : "",
                 is_blank ? " (blank)" : "",
                 has_data ? " (has-data)" : "",
                 has_audio ? " (has-audio)" : "");
        g_print ("  is writable:\t\t%s\n", is_writable ? "TRUE" : "FALSE");
        g_print ("  is appendable:\t%s\n", is_appendable ? "TRUE" : "FALSE");

        capacity = nautilus_burn_drive_get_media_capacity (drive);
        g_print ("  capacity:\t\t");
        display_size (capacity, media_type);

        size = nautilus_burn_drive_get_media_size (drive);
        g_print ("  size:\t\t\t");
        display_size (size, media_type);
}

static void
list_drives (void)
{
        GList                    *drives, *l;
        NautilusBurnDrive        *drive;
        NautilusBurnDriveMonitor *monitor;

        monitor = nautilus_burn_get_drive_monitor ();
        drives = nautilus_burn_drive_monitor_get_drives (monitor);

        for (l = drives; l != NULL; l = l->next) {

                drive = l->data;

                nautilus_burn_drive_unmount (drive);

                /* DRIVE */
                list_drive_info (drive);

                /* MEDIA */
                list_media_info (drive);

                nautilus_burn_drive_unref (drive);
                g_print ("---\n");
        }

        g_list_free (drives);
}

int main (int argc, char **argv)
{

        if (! g_thread_supported ()) {
                g_thread_init (NULL);
        }

        g_type_init ();

        nautilus_burn_init ();
        list_drives ();
        nautilus_burn_shutdown ();

        return 0;
}
