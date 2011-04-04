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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <libhal.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "nautilus-burn-drive.h"
#include "nautilus-burn-drive-private.h"
#include "nautilus-burn-drive-monitor.h"
#include "nautilus-burn-drive-monitor-private.h"

#define USE_PRIVATE_DBUS_CONNECTION 1

static void     nautilus_burn_drive_monitor_class_init (NautilusBurnDriveMonitorClass *klass);
static void     nautilus_burn_drive_monitor_init       (NautilusBurnDriveMonitor      *monitor);
static void     nautilus_burn_drive_monitor_finalize   (GObject                       *object);

#define NAUTILUS_BURN_DRIVE_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NAUTILUS_BURN_TYPE_DRIVE_MONITOR, NautilusBurnDriveMonitorPrivate))

struct NautilusBurnDriveMonitorPrivate
{
        LibHalContext                *ctx;
        GList                        *drives;
        NautilusBurnDrive            *image_drive;
};

enum {
        MEDIA_ADDED,
        MEDIA_REMOVED,
        DRIVE_CONNECTED,
        DRIVE_DISCONNECTED,
        LAST_SIGNAL
};

enum {
        PROP_0
};

static guint signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (NautilusBurnDriveMonitor, nautilus_burn_drive_monitor, G_TYPE_OBJECT)

static gpointer monitor_object = NULL;
static gboolean monitor_was_shutdown = FALSE;

static char *
monitor_get_drive_first_child_udi (NautilusBurnDriveMonitor *monitor,
                                   NautilusBurnDrive        *drive)
{
        char                **device_names;
        int                   num_devices;
        DBusError             error;
        char                 *udi;

        udi = NULL;

        if (drive->priv->drive_udi == NULL) {
                return NULL;
        }

        if (monitor->priv->ctx == NULL) {
                return NULL;
        }

        num_devices = -1;
        dbus_error_init (&error);
        device_names = libhal_manager_find_device_string_match (monitor->priv->ctx,
                                                                "info.parent",
                                                                drive->priv->drive_udi,
                                                                &num_devices,
                                                                &error);

        if (dbus_error_is_set (&error)) {
                g_warning ("%s\n", error.message);
                dbus_error_free (&error);

                goto done;
        }

        if (num_devices <= 0) {
                /*g_warning ("No HAL devices found for UDI '%s'", drive->priv->udi);*/

                goto done;
        }

        udi = g_strdup (device_names [0]);

 done:
        libhal_free_string_array (device_names);

        return udi;
}

static NautilusBurnMediaType
hal_type_to_media_type (const char *hal_type)
{
        NautilusBurnMediaType type;

        if (hal_type == NULL || strcmp (hal_type, "unknown") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN;
        } else if (strcmp (hal_type, "cd_rom") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_CD;
        } else if (strcmp (hal_type, "cd_r") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_CDR;
        } else if (strcmp (hal_type, "cd_rw") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_CDRW;
        } else if (strcmp (hal_type, "dvd_rom") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_DVD;
        } else if (strcmp (hal_type, "dvd_r") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_DVDR;
        } else if (strcmp (hal_type, "dvd_ram") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_DVD_RAM;
        } else if (strcmp (hal_type, "dvd_rw") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_DVDRW;
        } else if (strcmp (hal_type, "dvd_plus_rw") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW;
        } else if (strcmp (hal_type, "dvd_plus_r") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R;
        } else if (strcmp (hal_type, "dvd_plus_r_dl") == 0) {
                type = NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL;
        } else {
                type = NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN;
        }

        return type;
}

static void
monitor_set_drive_media (NautilusBurnDriveMonitor *monitor,
                         NautilusBurnDrive        *drive)
{
        char     *hal_type;
        DBusError error;
        guint64   size;

        drive->priv->media_type = NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN;
        drive->priv->media_size = NAUTILUS_BURN_MEDIA_SIZE_UNKNOWN;
        drive->priv->media_capacity = NAUTILUS_BURN_MEDIA_SIZE_UNKNOWN;
        drive->priv->media_is_mounted = FALSE;
        drive->priv->media_is_appendable = FALSE;
        drive->priv->media_label = NULL;

        drive->priv->is_rewritable = FALSE;
        drive->priv->is_blank = FALSE;
        drive->priv->has_data = FALSE;
        drive->priv->has_audio = FALSE;

        if (drive->priv->drive_udi == NULL) {
                return;
        }

        g_free (drive->priv->media_udi);
        drive->priv->media_udi = monitor_get_drive_first_child_udi (monitor, drive);
        if (drive->priv->media_udi == NULL) {
                return;
        }

        drive->priv->is_rewritable = libhal_device_get_property_bool (monitor->priv->ctx,
                                                                      drive->priv->media_udi,
                                                                      "volume.disc.is_rewritable",
                                                                      NULL);
        drive->priv->is_blank = libhal_device_get_property_bool (monitor->priv->ctx,
                                                                 drive->priv->media_udi,
                                                                 "volume.disc.is_blank",
                                                                 NULL);
        drive->priv->has_data = libhal_device_get_property_bool (monitor->priv->ctx,
                                                                 drive->priv->media_udi,
                                                                 "volume.disc.has_data",
                                                                 NULL);
        drive->priv->has_audio = libhal_device_get_property_bool (monitor->priv->ctx,
                                                                  drive->priv->media_udi,
                                                                  "volume.disc.has_audio",
                                                                  NULL);
        hal_type = libhal_device_get_property_string (monitor->priv->ctx,
                                                      drive->priv->media_udi,
                                                      "volume.disc.type",
                                                      NULL);
        drive->priv->media_type = hal_type_to_media_type (hal_type);

        if (hal_type != NULL) {
                libhal_free_string (hal_type);
        }

        dbus_error_init (&error);
        size = libhal_device_get_property_uint64 (monitor->priv->ctx,
                                                  drive->priv->media_udi,
                                                  "volume.disc.capacity",
                                                  &error);
        if (dbus_error_is_set (&error)) {
                g_warning ("%s\n", error.message);
                dbus_error_free (&error);
        } else {
                drive->priv->media_capacity = (gint64) size;
        }

        if (drive->priv->is_blank) {
                drive->priv->media_size = 0;
        } else {
                dbus_error_init (&error);
                size = libhal_device_get_property_uint64 (monitor->priv->ctx,
                                                          drive->priv->media_udi,
                                                          "volume.size",
                                                          &error);

                if (dbus_error_is_set (&error)) {
                        g_warning ("%s\n", error.message);
                        dbus_error_free (&error);
                } else {
                        drive->priv->media_size = (gint64) size;
                }
        }

        dbus_error_init (&error);
        g_free (drive->priv->media_label);
        drive->priv->media_label = libhal_device_get_property_string (monitor->priv->ctx,
                                                                      drive->priv->media_udi,
                                                                      "volume.label",
                                                                      &error);
        if (dbus_error_is_set (&error)) {
                g_warning ("%s\n", error.message);
                dbus_error_free (&error);
        }

        dbus_error_init (&error);
        drive->priv->media_is_mounted = libhal_device_get_property_bool (monitor->priv->ctx,
                                                                         drive->priv->media_udi,
                                                                         "volume.is_mounted",
                                                                         &error);
        if (dbus_error_is_set (&error)) {
                g_warning ("%s\n", error.message);
                dbus_error_free (&error);
        }

        dbus_error_init (&error);
        drive->priv->media_is_appendable  =  libhal_device_get_property_bool (monitor->priv->ctx,
                                                                         drive->priv->media_udi,
                                                                         "volume.disc.is_appendable",
                                                                         &error);
        if (dbus_error_is_set (&error)) {
                g_warning ("%s\n", error.message);
                dbus_error_free (&error);
        }
}

static NautilusBurnDrive *
find_drive_by_udi (NautilusBurnDriveMonitor *monitor,
                   const char               *udi)
{
        NautilusBurnDrive *drive;
        NautilusBurnDrive *ret;
        GList             *l;

        drive = NULL;

        ret = NULL;
        for (l = monitor->priv->drives; l != NULL; l = l->next) {
                drive = l->data;

                if (drive->priv != NULL && drive->priv->drive_udi != NULL &&
                    strcmp (drive->priv->drive_udi, udi) == 0) {
                        ret = drive;
                        break;
                }
        }

        return ret;
}

static NautilusBurnDrive *
find_drive_by_media_udi (NautilusBurnDriveMonitor *monitor,
                         const char               *udi)
{
        NautilusBurnDrive *drive;
        NautilusBurnDrive *ret;
        GList             *l;

        drive = NULL;

        ret = NULL;
        for (l = monitor->priv->drives; l != NULL; l = l->next) {
                drive = l->data;

                if (drive->priv != NULL && drive->priv->media_udi != NULL &&
                    strcmp (drive->priv->media_udi, udi) == 0) {
                        ret = drive;
                        break;
                }
        }

        return ret;
}

static void
hal_device_property_modified (LibHalContext *ctx,
                              const char    *udi,
                              const char    *key,
                              dbus_bool_t    is_removed,
                              dbus_bool_t    is_added)
{
        DBusError                 error;
        NautilusBurnDriveMonitor *monitor;

        monitor = libhal_ctx_get_user_data (ctx);

        if (!is_removed && g_ascii_strcasecmp (key, "volume.is_mounted") == 0) {
                NautilusBurnDrive *drive;

                drive = find_drive_by_media_udi (monitor, udi);
                if (drive != NULL) {
                        gboolean is_mounted;

                        dbus_error_init (&error);
                        is_mounted = libhal_device_get_property_bool (ctx, udi, "volume.is_mounted", &error);
                        if (dbus_error_is_set (&error)) {
                                g_warning ("Error retrieving volume.is_mounted on '%s': Error: '%s' Message: '%s'",
                                           udi, error.name, error.message);
                                dbus_error_free (&error);
                                return;
                        }

                        /*g_message ("Drive %s: %s", drive->priv->device, is_mounted ? "mounted" : "unmounted");*/
                        drive->priv->media_is_mounted = is_mounted;
                }
        }
}

/**
 * nautilus_burn_drive_monitor_get_drives:
 * @monitor: #NautilusBurnDriveMonitor
 *
 * Get the list of available #NautilusBurnDrive
 *
 * Return value: list of drives
 *
 * Since: 2.16
 **/
GList *
nautilus_burn_drive_monitor_get_drives (NautilusBurnDriveMonitor *monitor)
{
        GList *ret;

        ret = g_list_copy (monitor->priv->drives);
        g_list_foreach (ret, (GFunc)nautilus_burn_drive_ref, NULL);

        return ret;
}

/**
 * nautilus_burn_drive_monitor_get_recorder_drives:
 * @monitor: #NautilusBurnDriveMonitor
 *
 * Get the list of available #NautilusBurnDrive that are capable of recording
 *
 * Return value: list of drives that can record
 *
 * Since: 2.16
 **/
GList *
nautilus_burn_drive_monitor_get_recorder_drives (NautilusBurnDriveMonitor *monitor)
{
        GList *ret;
        GList *l;

        ret = NULL;

        for (l = monitor->priv->drives; l != NULL; l = l->next) {
                NautilusBurnDrive *drive;

                drive = l->data;

                if (nautilus_burn_drive_can_write (drive)) {
                        ret = g_list_prepend (ret, drive);
                }
        }

        ret = g_list_reverse (ret);
        g_list_foreach (ret, (GFunc)nautilus_burn_drive_ref, NULL);

        return ret;
}

/* fill in the write speeds as max_speed downto 1 if we
 * are using a detection method that doesn't report all
 * of the drive speeds
 */
static void
fill_write_speeds (NautilusBurnDrive *drive)
{
        int max_speed;
        int i;
        int cdr_speed = 150;
        int n_speeds;

        max_speed = drive->priv->max_speed_write;
        n_speeds = max_speed / cdr_speed + 1;

        drive->priv->write_speeds = g_new0 (int, n_speeds);

        /* there is no point adding every kps to the list
           so we'll just show every 150 kbps */
        for (i = 0; i < n_speeds; i++) {
                /* in descending order */
                drive->priv->write_speeds[n_speeds - 1 - i] = cdr_speed * i;
        }
}

/**
 * nautilus_burn_drive_monitor_get_drive_for_image:
 * @monitor: #NautilusBurnDriveMonitor
 *
 * Create a new %NAUTILUS_BURN_DRIVE_TYPE_FILE #NautilusBurnDrive.
 *
 * Return value: A new drive.
 *
 * Since: 2.16
 **/
NautilusBurnDrive *
nautilus_burn_drive_monitor_get_drive_for_image (NautilusBurnDriveMonitor *monitor)
{
        if (monitor->priv->image_drive == NULL) {
                monitor->priv->image_drive = _nautilus_burn_drive_new ();
                monitor->priv->image_drive->priv->display_name = g_strdup (_("File image"));
                monitor->priv->image_drive->priv->max_speed_read = 0;
                monitor->priv->image_drive->priv->max_speed_write = 0;
                fill_write_speeds (monitor->priv->image_drive);
                monitor->priv->image_drive->priv->type = NAUTILUS_BURN_DRIVE_TYPE_FILE;
        }

        nautilus_burn_drive_ref (monitor->priv->image_drive);

        return monitor->priv->image_drive;
}

/* copied from gtk/gtkfilesystemunix.c on 31 May 2006 */
static void
canonicalize_filename (gchar *filename)
{
  gchar *p, *q;
  gboolean last_was_slash = FALSE;

  p = filename;
  q = filename;

  while (*p)
    {
      if (*p == G_DIR_SEPARATOR)
        {
          if (!last_was_slash)
            *q++ = G_DIR_SEPARATOR;

          last_was_slash = TRUE;
        }
      else
        {
          if (last_was_slash && *p == '.')
            {
              if (*(p + 1) == G_DIR_SEPARATOR ||
                  *(p + 1) == '\0')
                {
                  if (*(p + 1) == '\0')
                    break;

                  p += 1;
                }
              else if (*(p + 1) == '.' &&
                       (*(p + 2) == G_DIR_SEPARATOR ||
                        *(p + 2) == '\0'))
                {
                  if (q > filename + 1)
                    {
                      q--;
                      while (q > filename + 1 &&
                             *(q - 1) != G_DIR_SEPARATOR)
                        q--;
                    }

                  if (*(p + 2) == '\0')
                    break;

                  p += 2;
                }
              else
                {
                  *q++ = *p;
                  last_was_slash = FALSE;
                }
            }
          else
            {
              *q++ = *p;
              last_was_slash = FALSE;
            }
        }

      p++;
    }

  if (q > filename + 1 && *(q - 1) == G_DIR_SEPARATOR)
    q--;

  *q = '\0';
}

/* copied from gtk/updateiconcache.c on 31 May 2006 */
static gchar *
follow_links (const gchar *path)
{
  gchar *target;
  gchar *d, *s;
  gchar *path2 = NULL;

  path2 = g_strdup (path);
  while (g_file_test (path2, G_FILE_TEST_IS_SYMLINK))
    {
      target = g_file_read_link (path2, NULL);

      if (target)
        {
          if (g_path_is_absolute (target))
            path2 = target;
          else
            {
              d = g_path_get_dirname (path2);
              s = g_build_filename (d, target, NULL);
              g_free (d);
              g_free (target);
              g_free (path2);
              path2 = s;
            }
        }
      else
        break;
    }

  return path2;
}

static char *
resolve_symlink (const char *file)
{
        char *target;

        target = follow_links (file);
        if (target != NULL) {
                canonicalize_filename (target);
        }

        return target;
}

/**
 * nautilus_burn_drive_monitor_get_drive_for_device:
 * @monitor: #NautilusBurnDriveMonitor
 * @device: device path
 *
 * Retrieve the #NautilusBurnDrive that corresponds to device.
 *
 * Return value: A new drive.
 *
 * Since: 2.16
 **/
NautilusBurnDrive *
nautilus_burn_drive_monitor_get_drive_for_device (NautilusBurnDriveMonitor *monitor,
                                                  const char               *device)
{
        GList             *l;
        NautilusBurnDrive *drive;
        NautilusBurnDrive *ret;
        char              *target;

        ret = NULL;

        g_return_val_if_fail (monitor != NULL, NULL);
        g_return_val_if_fail (device != NULL, NULL);

        target = resolve_symlink (device);
        if (target == NULL) {
                goto out;
        }

        for (l = monitor->priv->drives; l != NULL; l = l->next) {
                drive = l->data;

                /*
                 * On Solaris, the device name in the list of drives
                 * is not expanded to the symlink so compare against
                 * either the symlink value or the real device name.
                 */
                if (drive->priv->device != NULL &&
                   (strcmp (drive->priv->device, target) == 0 ||
                    strcmp (drive->priv->device, device) == 0)) {
                        ret = nautilus_burn_drive_ref (drive);
                        break;
                }
        }

        g_free (target);

 out:
        return ret;
}

static int *
hal_parse_write_speeds (char **strlist)
{
        char *end;
        int  *write_speeds;
        int   fields = 1;
        int   i;

        if (strlist == NULL) {
                return NULL;
        }

        fields = libhal_string_array_length (strlist);

        if (fields == 0) {
                return NULL;
        }

        write_speeds = g_new0 (int, fields + 1);

        for (i = 0; i < fields; i++) {
                write_speeds[i] = strtol (strlist[i], &end, 10);

                if (write_speeds[i] < 0
                    || write_speeds[i] > 65535
                    || *end != '\0') {

                        g_free (write_speeds);
                        return NULL;
                }
        }

        return write_speeds;
}

#define LIBHAL_PROP_EXTRACT_BEGIN if (FALSE)
#define LIBHAL_PROP_EXTRACT_END ;
#define LIBHAL_PROP_EXTRACT_INT(_property_, _where_) else if (strcmp (key, _property_) == 0 && type == LIBHAL_PROPERTY_TYPE_INT32) _where_ = libhal_psi_get_int (&it)
#define LIBHAL_PROP_EXTRACT_STRING(_property_, _where_) else if (strcmp (key, _property_) == 0 && type == LIBHAL_PROPERTY_TYPE_STRING) _where_ = (libhal_psi_get_string (&it) != NULL && strlen (libhal_psi_get_string (&it)) > 0) ? strdup (libhal_psi_get_string (&it)) : NULL
#define LIBHAL_PROP_EXTRACT_BOOL(_property_, _where_) else if (strcmp (key, _property_) == 0 && type == LIBHAL_PROPERTY_TYPE_BOOLEAN) _where_ = libhal_psi_get_bool (&it)
#define LIBHAL_PROP_EXTRACT_BOOL_BITFIELD(_property_, _where_, _field_) else if (strcmp (key, _property_) == 0 && type == LIBHAL_PROPERTY_TYPE_BOOLEAN) _where_ |= libhal_psi_get_bool (&it) ? _field_ : 0
#define LIBHAL_PROP_EXTRACT_STRLIST(_property_, _where_) else if (strcmp (key, _property_) == 0 && type == LIBHAL_PROPERTY_TYPE_STRLIST) _where_ = libhal_psi_get_strlist (&it)

static NautilusBurnDrive *
hal_drive_from_udi (LibHalContext *ctx,
                    const char    *udi)
{
        LibHalPropertySet        *pset;
        LibHalPropertySetIterator it;
        DBusError                 error;
        NautilusBurnDrive        *drive;
        char                    **write_speeds = NULL;
        char                     *raw_device = NULL;

        LIBHAL_CHECK_LIBHALCONTEXT (ctx, FALSE);

        dbus_error_init (&error);

        if ((pset = libhal_device_get_all_properties (ctx, udi, &error)) == NULL) {
                if (dbus_error_is_set (&error)) {
                        g_warning ("Could not get all properties: %s", error.message);
                        dbus_error_free (&error);
                }

                return NULL;
        }

        drive = _nautilus_burn_drive_new ();
        drive->priv->type = NAUTILUS_BURN_DRIVE_TYPE_CD_DRIVE;

        for (libhal_psi_init (&it, pset); libhal_psi_has_more (&it); libhal_psi_next (&it)) {
                int   type;
                char *key;

                type = libhal_psi_get_type (&it);
                key = libhal_psi_get_key (&it);

                LIBHAL_PROP_EXTRACT_BEGIN;

                LIBHAL_PROP_EXTRACT_STRING  ("block.device",               drive->priv->device);
                LIBHAL_PROP_EXTRACT_STRING  ("block.solaris.raw_device",   raw_device);
                LIBHAL_PROP_EXTRACT_STRING  ("storage.model",              drive->priv->display_name);

                LIBHAL_PROP_EXTRACT_INT ("storage.cdrom.read_speed",   drive->priv->max_speed_read);
                LIBHAL_PROP_EXTRACT_INT ("storage.cdrom.write_speed",  drive->priv->max_speed_write);
                LIBHAL_PROP_EXTRACT_STRLIST ("storage.cdrom.write_speeds", write_speeds);

                LIBHAL_PROP_EXTRACT_BOOL_BITFIELD ("storage.cdrom.cdr",        drive->priv->type, NAUTILUS_BURN_DRIVE_TYPE_CD_RECORDER);
                LIBHAL_PROP_EXTRACT_BOOL_BITFIELD ("storage.cdrom.cdrw",       drive->priv->type, NAUTILUS_BURN_DRIVE_TYPE_CDRW_RECORDER);
                LIBHAL_PROP_EXTRACT_BOOL_BITFIELD ("storage.cdrom.dvd",        drive->priv->type, NAUTILUS_BURN_DRIVE_TYPE_DVD_DRIVE);
                LIBHAL_PROP_EXTRACT_BOOL_BITFIELD ("storage.cdrom.dvdplusr",   drive->priv->type, NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_RECORDER);
                LIBHAL_PROP_EXTRACT_BOOL_BITFIELD ("storage.cdrom.dvdplusrw",  drive->priv->type, NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_RW_RECORDER);
                LIBHAL_PROP_EXTRACT_BOOL_BITFIELD ("storage.cdrom.dvdplusrdl", drive->priv->type, NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_DL_RECORDER);

                LIBHAL_PROP_EXTRACT_BOOL_BITFIELD ("storage.cdrom.dvdr",       drive->priv->type, NAUTILUS_BURN_DRIVE_TYPE_DVD_RW_RECORDER);
                LIBHAL_PROP_EXTRACT_BOOL_BITFIELD ("storage.cdrom.dvdrw",      drive->priv->type, NAUTILUS_BURN_DRIVE_TYPE_DVD_RW_RECORDER);
                LIBHAL_PROP_EXTRACT_BOOL_BITFIELD ("storage.cdrom.dvdram",     drive->priv->type, NAUTILUS_BURN_DRIVE_TYPE_DVD_RAM_RECORDER);

                LIBHAL_PROP_EXTRACT_END;
        }

        if (raw_device != NULL) {
                g_free (drive->priv->device);
                drive->priv->device = raw_device;
        }

        drive->priv->drive_udi = g_strdup (udi);
        drive->priv->write_speeds = hal_parse_write_speeds (write_speeds);
        /* we do not own write_speeds so do not free it. */
        if (drive->priv->write_speeds == NULL) {
                fill_write_speeds (drive);
        }

        if (drive->priv->display_name == NULL) {
                drive->priv->display_name = g_strdup_printf ("Unnamed Drive (%s)", drive->priv->device);
        }

        libhal_free_property_set (pset);

        return drive;
}

static void
monitor_drive_connected (NautilusBurnDriveMonitor *monitor,
                         NautilusBurnDrive        *drive)
{
        nautilus_burn_drive_ref (drive);
        monitor->priv->drives = g_list_prepend (monitor->priv->drives, drive);
        drive->priv->is_connected = 1;

        g_signal_emit (monitor, signals [DRIVE_CONNECTED], 0, drive);

}

static void
monitor_drive_disconnected (NautilusBurnDriveMonitor *monitor,
                            NautilusBurnDrive        *drive)
{
        _nautilus_burn_drive_disconnected (drive);
        monitor->priv->drives = g_list_remove (monitor->priv->drives, drive);
        drive->priv->is_connected = 0;

        g_signal_emit (monitor, signals [DRIVE_DISCONNECTED], 0, drive);
        nautilus_burn_drive_unref (drive);
}

static void
monitor_drive_media_added (NautilusBurnDriveMonitor *monitor,
                           NautilusBurnDrive        *drive)
{
        /* update media for drive */
        monitor_set_drive_media (monitor, drive);

        g_signal_emit (monitor, signals [MEDIA_ADDED], 0, drive);

        _nautilus_burn_drive_media_added (drive);
}

static void
monitor_drive_media_removed (NautilusBurnDriveMonitor *monitor,
                             NautilusBurnDrive        *drive)
{
        /* update media for drive */
        monitor_set_drive_media (monitor, drive);

        g_signal_emit (monitor, signals [MEDIA_REMOVED], 0, drive);

        _nautilus_burn_drive_media_removed (drive);
}

static void
hal_device_added (LibHalContext *ctx,
                  const char    *udi)
{
        NautilusBurnDriveMonitor *monitor;

        monitor = libhal_ctx_get_user_data (ctx);

        g_return_if_fail (monitor != NULL);
        g_return_if_fail (udi != NULL);

        if (libhal_device_query_capability (ctx, udi, "storage.cdrom", NULL)) {
                /* new drive */
                NautilusBurnDrive *drive;

                drive = hal_drive_from_udi (monitor->priv->ctx, udi);

                monitor_set_drive_media (monitor, drive);
                monitor_drive_connected (monitor, drive);
        }

        if (libhal_device_query_capability (ctx, udi, "volume", NULL)) {
                /* new media */
                NautilusBurnDrive *drive;
                char              *parent_udi;

                parent_udi = libhal_device_get_property_string (monitor->priv->ctx,
                                                                udi,
                                                                "info.parent",
                                                                NULL);
                drive = find_drive_by_udi (monitor, parent_udi);
                if (drive != NULL) {
                        monitor_drive_media_added (monitor, drive);
                }
                g_free (parent_udi);
        }
}

static void
hal_device_removed (LibHalContext *ctx,
                    const char    *udi)
{
        NautilusBurnDriveMonitor *monitor;
        NautilusBurnDrive        *drive;

        monitor = libhal_ctx_get_user_data (ctx);

        g_return_if_fail (monitor != NULL);
        g_return_if_fail (udi != NULL);

        drive = find_drive_by_udi (monitor, udi);
        if (drive != NULL) {
                monitor_drive_disconnected (monitor, drive);
        }

        drive = find_drive_by_media_udi (monitor, udi);
        if (drive != NULL) {
                monitor_drive_media_removed (monitor, drive);
        }

        /* check if parent is a drive */
}

static dbus_bool_t
hal_mainloop_integration (LibHalContext *ctx,
                          DBusError     *error)
{
        DBusConnection *dbus_connection;

#if USE_PRIVATE_DBUS_CONNECTION
        dbus_connection = dbus_bus_get_private (DBUS_BUS_SYSTEM, error);
#else
        dbus_connection = dbus_bus_get (DBUS_BUS_SYSTEM, error);
#endif

        if (dbus_error_is_set (error)) {
                return FALSE;
        }

        dbus_connection_set_exit_on_disconnect (dbus_connection, FALSE);
        dbus_connection_setup_with_g_main (dbus_connection, NULL);

        libhal_ctx_set_dbus_connection (ctx, dbus_connection);

        return TRUE;
}

static void
hal_update_all (NautilusBurnDriveMonitor *monitor)
{
        GList         *drives = NULL;
        int            i;
        int            num_devices;
        char         **device_names;

        device_names = libhal_find_device_by_capability (monitor->priv->ctx,
                                                         "storage.cdrom",
                                                         &num_devices,
                                                         NULL);

        if (device_names == NULL) {
                return;
        }

        for (i = 0; i < num_devices; i++) {
                NautilusBurnDrive *drive;

                drive = hal_drive_from_udi (monitor->priv->ctx, device_names [i]);

                monitor_set_drive_media (monitor, drive);

                drives = g_list_prepend (drives, drive);
        }

        libhal_free_string_array (device_names);

        drives = g_list_reverse (drives);

        monitor->priv->drives = drives;
}

static void
set_hal_monitor_enabled (NautilusBurnDriveMonitor *monitor,
                         gboolean                  enabled)
{
        DBusError error;

        if (enabled) {
                libhal_ctx_set_user_data (monitor->priv->ctx, monitor);
                libhal_ctx_set_device_added (monitor->priv->ctx, hal_device_added);
                libhal_ctx_set_device_removed (monitor->priv->ctx, hal_device_removed);
                libhal_ctx_set_device_property_modified (monitor->priv->ctx, hal_device_property_modified);

                dbus_error_init (&error);
                libhal_device_property_watch_all (monitor->priv->ctx, &error);
                if (dbus_error_is_set (&error)) {
                        g_warning ("Error watching all device properties: %s", error.message);
                        dbus_error_free (&error);
                }

                hal_update_all (monitor);
        } else {
                libhal_ctx_set_user_data (monitor->priv->ctx, NULL);
                libhal_ctx_set_device_added (monitor->priv->ctx, NULL);
                libhal_ctx_set_device_removed (monitor->priv->ctx, NULL);
        }

        /* FIXME: check for devices already in the drive and emit signals for them? */
}

static void
monitor_hal_shutdown (NautilusBurnDriveMonitor *monitor)
{
        DBusError       error;
#if USE_PRIVATE_DBUS_CONNECTION
        DBusConnection *dbus_connection;

        dbus_connection = libhal_ctx_get_dbus_connection (monitor->priv->ctx);
#endif

        set_hal_monitor_enabled (monitor, FALSE);

        dbus_error_init (&error);
	if (! libhal_ctx_shutdown (monitor->priv->ctx, &error)) {
		if (dbus_error_is_set (&error)) {
			g_warning ("hal_shutdown failed: %s\n", error.message);
			dbus_error_free (&error);
		} else {
			g_warning ("failed to shutdown HAL!");
		}
		return;
	}

#if USE_PRIVATE_DBUS_CONNECTION
        dbus_connection_close (dbus_connection);
#endif

        if (! libhal_ctx_free (monitor->priv->ctx)) {
                g_warning ("hal_shutdown failed - unable to free hal context\n");
        }
}

static void
monitor_hal_init (NautilusBurnDriveMonitor *monitor)
{
        LibHalContext *ctx;
        DBusError      error;

        if (! (ctx = libhal_ctx_new ())) {
                g_warning ("failed to initialize HAL!");
                return;
        }

        dbus_error_init (&error);
        if (! hal_mainloop_integration (ctx, &error)) {
                g_warning ("hal_initialize failed: %s", error.message);
                dbus_error_free (&error);
                return;
        }

	if (! libhal_ctx_init (ctx, &error)) {
		if (dbus_error_is_set (&error)) {
			g_warning ("hal_shutdown failed: %s", error.message);
			dbus_error_free (&error);
		} else {
			g_warning ("failed to shuddown HAL!");
		}

		libhal_ctx_free (ctx);
		return;
	}

        monitor->priv->ctx = ctx;

        set_hal_monitor_enabled (monitor, TRUE);
}

static void
nautilus_burn_drive_monitor_set_property (GObject            *object,
                                          guint               prop_id,
                                          const GValue       *value,
                                          GParamSpec         *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
nautilus_burn_drive_monitor_get_property (GObject        *object,
                                          guint           prop_id,
                                          GValue         *value,
                                          GParamSpec     *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
nautilus_burn_drive_monitor_class_init (NautilusBurnDriveMonitorClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize     = nautilus_burn_drive_monitor_finalize;
        object_class->get_property = nautilus_burn_drive_monitor_get_property;
        object_class->set_property = nautilus_burn_drive_monitor_set_property;

        signals [MEDIA_ADDED] =
                g_signal_new ("media-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (NautilusBurnDriveMonitorClass, media_added),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              NAUTILUS_BURN_TYPE_DRIVE);

        signals [MEDIA_REMOVED] =
                g_signal_new ("media-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (NautilusBurnDriveMonitorClass, media_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              NAUTILUS_BURN_TYPE_DRIVE);

        signals [DRIVE_CONNECTED] =
                g_signal_new ("drive-connected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (NautilusBurnDriveMonitorClass, drive_connected),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              NAUTILUS_BURN_TYPE_DRIVE);
        signals [DRIVE_DISCONNECTED] =
                g_signal_new ("drive-disconnected",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (NautilusBurnDriveMonitorClass, drive_disconnected),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              NAUTILUS_BURN_TYPE_DRIVE);

        g_type_class_add_private (klass, sizeof (NautilusBurnDriveMonitorPrivate));
}

static void
nautilus_burn_drive_monitor_unref (NautilusBurnDriveMonitor *monitor)
{
        if (monitor == NULL) {
                return;
        }

        g_object_unref (monitor);
}

static void
nautilus_burn_drive_monitor_init (NautilusBurnDriveMonitor *monitor)
{
        monitor->priv = NAUTILUS_BURN_DRIVE_MONITOR_GET_PRIVATE (monitor);

        monitor_hal_init (monitor);
}

static void
nautilus_burn_drive_monitor_finalize (GObject *object)
{
        NautilusBurnDriveMonitor *monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (NAUTILUS_BURN_IS_DRIVE_MONITOR (object));

        monitor = NAUTILUS_BURN_DRIVE_MONITOR (object);

        g_return_if_fail (monitor->priv != NULL);

        monitor_hal_shutdown (monitor);

        if (monitor->priv->image_drive != NULL) {
                nautilus_burn_drive_unref (monitor->priv->image_drive);
        }

        G_OBJECT_CLASS (nautilus_burn_drive_monitor_parent_class)->finalize (object);
}

/**
 * nautilus_burn_get_drive_monitor:
 *
 * Get a reference to the drive monitor
 *
 * Returns a pointer to the #NautilusBurnDriveMonitor singleton.
 * #NautilusBurnDriveMonitor is a singleton, this means it is guaranteed to
 * exist and be valid until nautilus_burn_shutdown() is called. Consequently,
 * it doesn't need to be refcounted since nautilus-burn will hold a reference to
 * it until it is shut down.  It is created by nautilus_burn_init().
 *
 * Return value: #NautilusBurnDriveMonitor
 *
 * Since: 2.16
 **/
NautilusBurnDriveMonitor *
nautilus_burn_get_drive_monitor (void)
{
        if (monitor_object == NULL && !monitor_was_shutdown) {
                monitor_object = g_object_new (NAUTILUS_BURN_TYPE_DRIVE_MONITOR, NULL);
                g_object_add_weak_pointer (monitor_object,
                                           (gpointer *) &monitor_object);
        }

        return NAUTILUS_BURN_DRIVE_MONITOR (monitor_object);
}

void
_nautilus_burn_drive_monitor_shutdown (void)
{
        if (monitor_object != NULL) {
                nautilus_burn_drive_monitor_unref (monitor_object);
        }

        monitor_was_shutdown = TRUE;
}
