/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nautilus-burn-drive.c: easy to use cd burner software
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

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __linux__
#include <linux/cdrom.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#endif /* __linux__ */

#ifdef __FreeBSD__
#include <sys/cdio.h>
#include <sys/cdrio.h>
#include <camlib.h>
#endif /* __FreeBSD__ */

#ifdef HAVE_SYS_CDIO_H
#include <sys/cdio.h>
#endif

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>

#include <libhal.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "nautilus-burn-drive.h"
#include "nautilus-burn-drive-private.h"

#ifndef INVALID_HANDLE
#define INVALID_HANDLE (GINT_TO_POINTER(-1))
#endif

#define CDS_NO_INFO             0       /* if not implemented */
#define CDS_AUDIO               100
#define CDS_DATA_1              101
#define CDS_DATA_2              102
#define CDS_XA_2_1              103
#define CDS_XA_2_2              104
#define CDS_MIXED               105

#define NAUTILUS_BURN_DRIVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NAUTILUS_BURN_TYPE_DRIVE, NautilusBurnDrivePrivate))

/* Signals */
enum {
        MEDIA_ADDED,
        MEDIA_REMOVED,
        DISCONNECTED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_MONITOR_ENABLED,
};

static int nautilus_burn_drive_table_signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(NautilusBurnDrive, nautilus_burn_drive, G_TYPE_OBJECT);

/* Media capacities, someone please check them */
static struct _media_capacity {
        NautilusBurnMediaType  type;
        guint64                capacity;
} media_capacity[] = {
        { NAUTILUS_BURN_MEDIA_TYPE_CD,                  838860800LL  },
        { NAUTILUS_BURN_MEDIA_TYPE_DVD,                 4700000000LL },

        /* DVD RAM 1.0 holds 2.58 GB,
         * DVD RAM 2.0 holds 4.7 GB, let's assume the bigger one */
        { NAUTILUS_BURN_MEDIA_TYPE_DVD_RAM,             4700000000LL },
        { NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL,       9183000000LL },
};

#ifdef __FreeBSD__

#define get_ioctl_handle_fd(x) (((struct cam_device *)x)->fd)

static gpointer
open_ioctl_handle (const char *device)
{
        struct cam_device *cam;

        cam = cam_open_device (device, O_RDWR);

        return (cam ? (gpointer)cam : INVALID_HANDLE);
}

static void
close_ioctl_handle (gpointer handle)
{
        cam_close_device ((struct cam_device *)handle);
}

#else

#define get_ioctl_handle_fd(x) (GPOINTER_TO_INT(x))

static gpointer
open_ioctl_handle (const char *device)
{
        int fd;

        if ((fd = g_open (device, O_RDWR | O_EXCL | O_NONBLOCK, 0)) < 0
            && (fd = g_open (device, O_RDONLY | O_EXCL | O_NONBLOCK, 0)) < 0
            && (fd = g_open (device, O_RDONLY | O_NONBLOCK, 0)) < 0) {

                return INVALID_HANDLE;
        }

        return GINT_TO_POINTER (fd);
}

static void
close_ioctl_handle (gpointer handle)
{
        close (GPOINTER_TO_INT (handle));
}

#endif

/**
 * nautilus_burn_drive_get_max_speed_write:
 * @drive: #NautilusBurnDrive
 *
 * Get the maximum write speed that the drive is capable of
 *
 * Returns: The speed of the drive, in device units.
 *
 * Since: 2.14
 **/
int
nautilus_burn_drive_get_max_speed_write (NautilusBurnDrive *drive)
{
        int speed;

        g_return_val_if_fail (drive != NULL, -1);

        speed = drive->priv->max_speed_write;

        return speed;
}

/**
 * nautilus_burn_drive_get_max_speed_read:
 * @drive: #NautilusBurnDrive
 *
 * Get the maximum read speed that the drive is capable of
 *
 * Returns: The speed of the drive, in device units.
 *
 * Since: 2.14
 **/
int
nautilus_burn_drive_get_max_speed_read (NautilusBurnDrive *drive)
{
        int speed;

        g_return_val_if_fail (drive != NULL, -1);

        speed = drive->priv->max_speed_read;

        return speed;
}

/**
 * nautilus_burn_drive_get_max_write_speeds:
 * @drive: #NautilusBurnDrive
 *
 * Get the list of supported write speeds
 *
 * Returns: The write speeds of the drive, in device units, as a
 * zero-terminated array of integers.  The array is sorted in
 * descending order and has no duplicate entries.
 *
 * Since: 2.14
 **/
const int *
nautilus_burn_drive_get_write_speeds (NautilusBurnDrive *drive)
{
        const int *speeds;

        g_return_val_if_fail (drive != NULL, NULL);

        speeds = drive->priv->write_speeds;

        return speeds;
}

/**
 * nautilus_burn_drive_get_name_for_display:
 * @drive: #NautilusBurnDrive
 *
 * Get the name of the drive for use in a user interface
 *
 * Returns: name of the drive.  Must be freed with g_free().
 *
 * Since: 2.14
 **/
char *
nautilus_burn_drive_get_name_for_display (NautilusBurnDrive *drive)
{
        char *name;

        g_return_val_if_fail (drive != NULL, NULL);

        name = g_strdup (drive->priv->display_name);

        return name;
}

/**
 * nautilus_burn_drive_get_device:
 * @drive: #NautilusBurnDrive
 *
 * Get the name of the device associated with the drive.
 *
 * Returns: device name.  Must be not be freed.
 *
 * Since: 2.16
 **/
const char *
nautilus_burn_drive_get_device (NautilusBurnDrive *drive)
{
        g_return_val_if_fail (drive != NULL, NULL);

        return drive->priv->device;
}

/**
 * nautilus_burn_drive_get_drive_type:
 * @drive: #NautilusBurnDrive
 *
 * Get the type of the drive.
 *
 * Returns: type of drive
 *
 * Since: 2.16
 **/
int
nautilus_burn_drive_get_drive_type (NautilusBurnDrive *drive)
{
        g_return_val_if_fail (drive != NULL, 0);

        return drive->priv->type;
}

/**
 * nautilus_burn_drive_can_rewrite:
 * @drive: #NautilusBurnDrive
 *
 * Report the whether the drive is capable of re-recording
 *
 * Returns: %TRUE if the drive can rewrite, otherwise return %FALSE.
 *
 * Since: 2.14
 **/
gboolean
nautilus_burn_drive_can_rewrite (NautilusBurnDrive *drive)
{
        int      type;
        gboolean can_rewrite;

        g_return_val_if_fail (drive != NULL, FALSE);

        type = drive->priv->type;

        can_rewrite = (type & NAUTILUS_BURN_DRIVE_TYPE_CDRW_RECORDER
                       || type & NAUTILUS_BURN_DRIVE_TYPE_DVD_RW_RECORDER
                       || type & NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_RW_RECORDER);

        return can_rewrite;
}

/**
 * nautilus_burn_drive_can_write:
 * @drive: #NautilusBurnDrive
 *
 * Report the whether the drive is capable of recording
 *
 * Returns: %TRUE if the drive is a recorder, otherwise return %FALSE.
 *
 * Since: 2.14
 **/
gboolean
nautilus_burn_drive_can_write (NautilusBurnDrive *drive)
{
        int      type;
        gboolean can_write;
        gboolean can_rewrite;

        g_return_val_if_fail (drive != NULL, FALSE);

        type = drive->priv->type;

        /* Handle rewritables first */
        can_rewrite = nautilus_burn_drive_can_rewrite (drive);
        if (can_rewrite)
                return TRUE;

        /* now only handle non-rewritables */
        can_write = (type & NAUTILUS_BURN_DRIVE_TYPE_FILE
                     || type & NAUTILUS_BURN_DRIVE_TYPE_CD_RECORDER
                     || type & NAUTILUS_BURN_DRIVE_TYPE_DVD_RAM_RECORDER
                     || type & NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_RECORDER
                     || type & NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_DL_RECORDER);

        return can_write;
}

static gboolean
drive_door_is_open (int fd)
{
        if (fd < 0) {
                return FALSE;
        }

#ifdef __linux__
        {
                int status;

                status = ioctl (fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
                if (status < 0) {
                        return FALSE;
                }

                return status == CDS_TRAY_OPEN;
        }
#else
        return FALSE;
#endif
}

/**
 * nautilus_burn_drive_door_is_open:
 * @drive: #NautilusBurnDrive
 *
 * Report the whether the drive door or tray is open.
 *
 * Returns: %TRUE if the drive door is open, otherwise return %FALSE.
 *
 * Since: 2.12
 **/
gboolean
nautilus_burn_drive_door_is_open (NautilusBurnDrive *drive)
{
        gpointer ioctl_handle;
        int      fd;
        gboolean ret;

        g_return_val_if_fail (drive != NULL, FALSE);

        ioctl_handle = open_ioctl_handle (drive->priv->device);
        if (ioctl_handle == INVALID_HANDLE)
                return FALSE;

        fd = get_ioctl_handle_fd (ioctl_handle);

        ret = drive_door_is_open (fd);

        close_ioctl_handle (ioctl_handle);

        return ret;
}

/**
 * nautilus_burn_drive_get_media_type:
 * @drive: #NautilusBurnDrive
 *
 * Determine the type of the media in the drive @drive.
 *
 * Return value: The #NautilusBurnMediaType of the media in the drive or the
 * following special values:
 *
 *    %NAUTILUS_BURN_MEDIA_TYPE_ERROR   if the type can not be determined
 *    %NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN if the type can not be determined
 *    %NAUTILUS_BURN_MEDIA_TYPE_BUSY    if the device is busy
 **/
NautilusBurnMediaType
nautilus_burn_drive_get_media_type (NautilusBurnDrive *drive)
{
        g_return_val_if_fail (drive != NULL, NAUTILUS_BURN_MEDIA_TYPE_ERROR);

        return nautilus_burn_drive_get_media_type_full (drive, NULL, NULL, NULL, NULL);
}

static NautilusBurnMediaType
nautilus_burn_drive_hal_get_media_type_full (NautilusBurnDrive *drive,
                                             gboolean          *is_rewritable,
                                             gboolean          *is_blank,
                                             gboolean          *has_data,
                                             gboolean          *has_audio)
{

        if (is_rewritable)
                *is_rewritable = FALSE;
        if (is_blank)
                *is_blank = FALSE;
        if (has_data)
                *has_data = FALSE;
        if (has_audio)
                *has_audio = FALSE;

        if (drive->priv->drive_udi == NULL) {
                goto fallback;
        }

        if (drive->priv->media_udi == NULL) {
                goto fallback;
        }

        if (is_rewritable)
                *is_rewritable = drive->priv->is_rewritable;
        if (is_blank)
                *is_blank = drive->priv->is_blank;
        if (has_data)
                *has_data = drive->priv->has_data;
        if (has_audio)
                *has_audio = drive->priv->has_audio;

        return drive->priv->media_type;

 fallback:
        return NAUTILUS_BURN_MEDIA_TYPE_ERROR;
}

/**
 * nautilus_burn_drive_get_media_type_full:
 * @drive: #NautilusBurnDrive
 * @is_rewritable: set to TRUE if media is rewritable
 * @is_blank: set to TRUE if media is blank
 * @has_data: set to TRUE if media has data
 * @has_audio: set to TRUE if media has audio
 *
 * Determine the type of the media in the drive @drive
 *
 * Return value: See nautilus_burn_drive_get_media_type() for details.
 **/
NautilusBurnMediaType
nautilus_burn_drive_get_media_type_full (NautilusBurnDrive *drive,
                                         gboolean          *is_rewritable,
                                         gboolean          *is_blank,
                                         gboolean          *has_data,
                                         gboolean          *has_audio)
{
        g_return_val_if_fail (drive != NULL, NAUTILUS_BURN_MEDIA_TYPE_ERROR);

        return nautilus_burn_drive_hal_get_media_type_full (drive,
                                                            is_rewritable,
                                                            is_blank,
                                                            has_data,
                                                            has_audio);
}

/**
 * nautilus_burn_drive_get_media_size:
 * @drive: #NautilusBurnDrive
 *
 * Determine the size of the media (i.e. amount of data that the disc
 * contains) in the specified drive.
 *
 * Return value: The size of the media in bytes or the
 * following special values:
 *
 *    %NAUTILUS_BURN_MEDIA_SIZE_UNKNOWN if the type can not be determined
 *    %0                                if the disc is blank
 **/
gint64
nautilus_burn_drive_get_media_size (NautilusBurnDrive *drive)
{
        return drive->priv->media_size;
}

/**
 * nautilus_burn_drive_get_media_capacity:
 * @drive: #NautilusBurnDrive
 *
 * Determine the capacity of the media (i.e. amount of data that the disc
 * can hold) in the specified drive.
 *
 * Return value: The capacity of the media in bytes or the
 * following special values:
 *
 *    %NAUTILUS_BURN_MEDIA_SIZE_UNKNOWN if the type can not be determined
 *    %NAUTILUS_BURN_MEDIA_SIZE_NA      if the device type is not recognized
 *    %NAUTILUS_BURN_MEDIA_SIZE_BUSY    if the device is busy
 **/
gint64
nautilus_burn_drive_get_media_capacity (NautilusBurnDrive *drive)
{
        g_return_val_if_fail (drive != NULL, NAUTILUS_BURN_MEDIA_SIZE_UNKNOWN);

        return drive->priv->media_capacity;
}

/**
 * nautilus_burn_drive_get_media_label:
 * @drive: #NautilusBurnDrive
 *
 * Determine the label of the media in the specified drive.
 *
 * Return value: The label of the media.
 *
 * Since: 2.14
 **/
char *
nautilus_burn_drive_get_media_label (NautilusBurnDrive *drive)
{
        g_return_val_if_fail (drive != NULL, NULL);

        return g_strdup (drive->priv->media_label);
}

typedef struct {
        gboolean    timeout;
        gboolean    unmount_ok;
        guint       timeout_tag;
        GMainLoop  *loop;
        GPtrArray  *argv;
} UnmountData;

static void
free_unmount_data (UnmountData *unmount_data)
{
        g_ptr_array_foreach (unmount_data->argv, (GFunc)g_free, NULL);
        g_ptr_array_free (unmount_data->argv, TRUE);

        g_free (unmount_data);
}

static gboolean
unmount_done (gpointer data)
{
        UnmountData *unmount_data;
        unmount_data = data;

        if (unmount_data->timeout_tag != 0) {
                g_source_remove (unmount_data->timeout_tag);
        }

        if (unmount_data->loop != NULL &&
            g_main_loop_is_running (unmount_data->loop)) {
                g_main_loop_quit (unmount_data->loop);
        }

        if (unmount_data->timeout) {
                /* We timed out, so unmount_data wasn't freed
                   at mainloop exit. */
                free_unmount_data (unmount_data);
        }

        return FALSE;
}

static gboolean
unmount_timeout (gpointer data)
{
        UnmountData *unmount_data;
        unmount_data = data;

        /* We're sure, the callback hasn't been run, so just say
           we were interrupted and return from the mainloop */

        unmount_data->unmount_ok = FALSE;
        unmount_data->timeout_tag = 0;
        unmount_data->timeout = TRUE;

        if (g_main_loop_is_running (unmount_data->loop)) {
                g_main_loop_quit (unmount_data->loop);
        }

        return FALSE;
}

#ifndef USE_GNOME_MOUNT
static const char *umount_known_locations [] = {
        "/sbin/umount",
        "/bin/umount",
        "/usr/sbin/umount",
        "/usr/bin/umount",
        NULL
};
#endif

/* Returns the full command */
static GPtrArray *
create_command (const char *device)
{
        GPtrArray *argv;
        char      *str;

        argv = g_ptr_array_new ();

#ifdef USE_GNOME_MOUNT
        str = g_strdup (BINDIR "/gnome-mount");
        g_ptr_array_add (argv, str);
        str = g_strdup_printf ("--device=%s", device);
        g_ptr_array_add (argv, str);
        str = g_strdup ("--unmount");
        g_ptr_array_add (argv, str);
        str = g_strdup ("--block");
        g_ptr_array_add (argv, str);
        str = g_strdup ("--no-ui");
        g_ptr_array_add (argv, str);
#else
        {
                int   i;

                str = NULL;
                for (i = 0; umount_known_locations [i]; i++){
                        if (g_file_test (umount_known_locations [i], G_FILE_TEST_EXISTS)) {
                                str = g_strdup (umount_known_locations [i]);
                                break;
                        }
                }

                /* no path then try just command */
                if (str == NULL) {
                        str = g_strdup ("umount");
                }

                g_ptr_array_add (argv, str);
                str = g_strdup_printf ("%s", device);
                g_ptr_array_add (argv, str);
        }
#endif

        g_ptr_array_add (argv, NULL);

        return argv;
}

static void *
unmount_thread_start (void *arg)
{
        GError      *error;
        UnmountData *data;
        gint         exit_status;

        data = arg;

        data->unmount_ok = TRUE;

        error = NULL;
        if (g_spawn_sync (NULL,
                          (char **)data->argv->pdata,
                          NULL,
                          0,
                          NULL, NULL,
                          NULL,
                          NULL,
                          &exit_status,
                          &error)) {
                if (exit_status == 0) {
                        data->unmount_ok = TRUE;
                } else {
                        data->unmount_ok = FALSE;
                }

                /* Delay a bit to make sure unmount finishes */
                sleep (1);
        } else {
                /* spawn failure */
                if (error) {
                        g_warning ("Unable to unmount: %s", error->message);
                        g_error_free (error);
                }
                data->unmount_ok = FALSE;
        }

        g_idle_add (unmount_done, data);

        g_thread_exit (NULL);

        return NULL;
}

/**
 * nautilus_burn_drive_unmount:
 * @drive: #NautilusBurnDrive
 *
 * Unmount the media in a #NautilusBurnDrive.
 *
 * Return value: %TRUE if the media was sucessfully unmounted, %FALSE otherwise.
 *
 * Since: 2.10
 **/
gboolean
nautilus_burn_drive_unmount (NautilusBurnDrive *drive)
{
        UnmountData *data;
        gboolean     unmount_ok;

        g_return_val_if_fail (drive != NULL, FALSE);

        if (drive->priv->device == NULL)
                return FALSE;

        unmount_ok = FALSE;

        data = g_new0 (UnmountData, 1);
        data->loop = g_main_loop_new (NULL, FALSE);

        data->timeout_tag = g_timeout_add (5 * 1000,
                                           unmount_timeout,
                                           data);
        data->argv = create_command (drive->priv->device);

        g_thread_create (unmount_thread_start, data, FALSE, NULL);

        GDK_THREADS_LEAVE ();
        g_main_loop_run (data->loop);
        GDK_THREADS_ENTER ();

        g_main_loop_unref (data->loop);
        data->loop = NULL;

        unmount_ok = data->unmount_ok;

        if (!data->timeout) {
                /* Don't free data if mount operation still running. */
                free_unmount_data (data);
        }

        return unmount_ok;
}

/**
 * nautilus_burn_drive_is_mounted:
 * @drive: #NautilusBurnDrive
 *
 * Determine if media in the specified drive is mounted.
 *
 * Return value: %TRUE if the media is mounted, %FALSE otherwise.
 *
 * Since: 2.14
 **/
gboolean
nautilus_burn_drive_is_mounted (NautilusBurnDrive *drive)
{
        g_return_val_if_fail (drive != NULL, FALSE);

        return drive->priv->media_is_mounted;
}

/**
 * nautilus_burn_drive_eject:
 * @drive: #NautilusBurnDrive
 *
 * Eject media from a #NautilusBurnDrive.
 *
 * Return value: %TRUE if the media was sucessfully ejected, %FALSE otherwise.
 *
 * Since: 2.12
 **/
gboolean
nautilus_burn_drive_eject (NautilusBurnDrive *drive)
{
        char    *cmd;
        gboolean res;

        g_return_val_if_fail (drive != NULL, FALSE);

        if (drive->priv->device == NULL) {
                return FALSE;
        }

#ifdef USE_GNOME_MOUNT
        cmd = g_strdup_printf ("gnome-mount --block --eject --no-ui --device=%s", drive->priv->device);
#else
        cmd = g_strdup_printf ("eject %s", drive->priv->device);
#endif

        res = g_spawn_command_line_sync (cmd, NULL, NULL, NULL, NULL);
        g_free (cmd);

        /* delay a bit to make sure eject finishes */
        sleep (1);

        return res;
}

/**
 * nautilus_burn_drive_unref:
 * @drive: #NautilusBurnDrive
 *
 * Decrement the refcount of @drive.
 *
 * Since: 2.14
 **/
void
nautilus_burn_drive_unref (NautilusBurnDrive *drive)
{
        if (drive == NULL) {
                return;
        }

        g_object_unref (drive);
}

/**
 * nautilus_burn_drive_ref:
 * @drive: #NautilusBurnDrive
 *
 * Increment the refcount of @drive.
 *
 * Return value: #NautilusBurnDrive
 *
 * Since: 2.14
 **/
NautilusBurnDrive *
nautilus_burn_drive_ref (NautilusBurnDrive *drive)
{
        if (drive == NULL) {
                return NULL;
        }

        g_object_ref (drive);

        return drive;
}

static LibHalContext *
get_hal_context (void)
{
        static LibHalContext *ctx = NULL;
        DBusError             error;
        DBusConnection       *dbus_conn;

        if (ctx == NULL) {
                ctx = libhal_ctx_new ();
                if (ctx == NULL) {
                        g_warning ("Could not create a HAL context");
                } else {
                        dbus_error_init (&error);
                        dbus_conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);

                        if (dbus_error_is_set (&error)) {
                                g_warning ("Could not connect to system bus: %s", error.message);
                                dbus_error_free (&error);
                                return NULL;
                        }

                        libhal_ctx_set_dbus_connection (ctx, dbus_conn);

                        if (! libhal_ctx_init (ctx, &error)) {
                                g_warning ("Could not initalize the HAL context: %s",
                                           error.message);

                                if (dbus_error_is_set (&error))
                                        dbus_error_free (&error);

                                libhal_ctx_free (ctx);
                                ctx = NULL;
                        }
                }
        }

        return ctx;
}

/**
 * nautilus_burn_drive_lock:
 * @drive: Pointer to a #NautilusBurnDrive
 * @reason:
 * @reason_for_failure:
 *
 * Lock a #NautilusBurnDrive
 *
 * Return value: %TRUE if the drive was sucessfully locked, %FALSE otherwise.
 *
 * Since: 2.8
 **/
gboolean
nautilus_burn_drive_lock (NautilusBurnDrive *drive,
                          const char        *reason,
                          char             **reason_for_failure)
{
        gboolean res;

        if (reason_for_failure != NULL)
                *reason_for_failure = NULL;

        res = TRUE;
        if (drive->priv->drive_udi != NULL) {
                LibHalContext *ctx;
                char          *dbus_reason;
                DBusError      error;

                dbus_error_init (&error);
                ctx = get_hal_context ();

                if (ctx != NULL) {
                        res = libhal_device_lock (ctx,
                                                  drive->priv->drive_udi,
                                                  reason,
                                                  &dbus_reason,
                                                  &error);

                        if (dbus_error_is_set (&error))
                                dbus_error_free (&error);

                        if (dbus_reason != NULL &&
                            reason_for_failure != NULL)
                                *reason_for_failure = g_strdup (dbus_reason);
                        if (dbus_reason != NULL)
                                dbus_free (dbus_reason);
                }
        }

        return res;
}

/**
 * nautilus_burn_drive_unlock:
 * @drive: Pointer to a #NautilusBurnDrive
 *
 * Unlock a #NautilusBurnDrive
 *
 * Return value: %TRUE if the drive was sucessfully unlocked, %FALSE otherwise.
 *
 * Since: 2.8
 **/
gboolean
nautilus_burn_drive_unlock (NautilusBurnDrive *drive)
{
        gboolean res;

        res = TRUE;

        if (drive->priv->drive_udi != NULL) {
                LibHalContext *ctx;
                DBusError      error;

                dbus_error_init (&error);
                ctx = get_hal_context ();

                if (ctx != NULL) {
                        res = libhal_device_unlock (ctx,
                                                    drive->priv->drive_udi,
                                                    &error);

                        if (dbus_error_is_set (&error))
                                dbus_error_free (&error);
                }
        }

        return res;
}

void
_nautilus_burn_drive_media_added (NautilusBurnDrive *drive)
{
        g_signal_emit (drive, nautilus_burn_drive_table_signals [MEDIA_ADDED], 0);
}

void
_nautilus_burn_drive_media_removed (NautilusBurnDrive *drive)
{
        g_signal_emit (drive, nautilus_burn_drive_table_signals [MEDIA_REMOVED], 0);
}

void
_nautilus_burn_drive_disconnected (NautilusBurnDrive *drive)
{
        g_signal_emit (drive, nautilus_burn_drive_table_signals [DISCONNECTED], 0);
}

static void
nautilus_burn_drive_finalize (GObject *object)
{
        NautilusBurnDrive *drive = NAUTILUS_BURN_DRIVE (object);

        g_return_if_fail (object != NULL);

        g_free (drive->priv->drive_udi);
        g_free (drive->priv->media_udi);

        g_free (drive->priv->media_label);

        g_free (drive->priv->write_speeds);
        g_free (drive->priv->display_name);
        g_free (drive->priv->device);

        if (G_OBJECT_CLASS (nautilus_burn_drive_parent_class)->finalize != NULL) {
                (* G_OBJECT_CLASS (nautilus_burn_drive_parent_class)->finalize) (object);
        }
}

static void
nautilus_burn_drive_init (NautilusBurnDrive *drive)
{
        drive->priv = NAUTILUS_BURN_DRIVE_GET_PRIVATE (drive);

        drive->priv->type            = 0;
        drive->priv->display_name    = NULL;
        drive->priv->max_speed_write = 0;
        drive->priv->max_speed_read  = 0;
        drive->priv->write_speeds    = NULL;
        drive->priv->device          = NULL;
}

/**
 * _nautilus_burn_drive_new:
 *
 * Create a new #NautilusBurnDrive.
 *
 * Return value: The new drive.
 *
 * Since: 2.8
 **/
NautilusBurnDrive *
_nautilus_burn_drive_new (void)
{
        return g_object_new (NAUTILUS_BURN_TYPE_DRIVE, NULL);
}

/**
 * nautilus_burn_drive_equal:
 * @a: First #NautilusBurnDrive struct to compare
 * @b: Second #NautilusBurnDrive struct to compare
 *
 * Compare the two cd drives, return %TRUE if they match exactly
 * the same drive.
 *
 * Returns: %TRUE if the two #NautilusBurnDrives are equal, otherwise return %FALSE.
 *
 * Since: 2.8
 **/
gboolean
nautilus_burn_drive_equal (NautilusBurnDrive *a,
                           NautilusBurnDrive *b)
{
        if (!a || !b)
                return FALSE;

        if ((a->priv->type & NAUTILUS_BURN_DRIVE_TYPE_FILE)
            && (b->priv->type & NAUTILUS_BURN_DRIVE_TYPE_FILE))
                return TRUE;

        if (!a->priv->device || !b->priv->device)
                return FALSE;

        return strcmp (a->priv->device, b->priv->device) == 0;
}

static void
add_desc (GString    *string,
          const char *addition)
{
        if (strcmp (string->str, "") == 0) {
                g_string_append_printf (string, "%s", addition);
        } else {
                g_string_append_printf (string, ", %s", addition);
        }
}

#define NCBD_ADD_TYPE_DESC(_t_, _desc_) if (type & _t_) add_desc (string, _desc_)

/**
 * nautilus_burn_drive_get_supported_media_string:
 * @drive: A #NautilusBurnDrive
 * @writable_only: Set to %TRUE if only writable media should be displayed
 *
 * Get a string description of the supported media types.  The
 * returned string should be freed when no longer needed.
 *
 * Returns: a string description of the supported media types
 *
 * Since: 2.14
 **/
char *
nautilus_burn_drive_get_supported_media_string (NautilusBurnDrive *drive,
                                                gboolean           writable_only)
{
        GString *string;
        int      type;

        type = drive->priv->type;

        string = g_string_new (NULL);

        NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_FILE, "File");

        NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_CD_RECORDER, "CD-R");
        NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_CDRW_RECORDER, "CD-RW");
        NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_DVD_RAM_RECORDER, "DVD-RAM");
        NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_DVD_RW_RECORDER, "DVD-R, DVD-RW");
        NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_RECORDER, "DVD+R");
        NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_DL_RECORDER, "DVD+R DL");
        NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_RW_RECORDER, "DVD+RW");

        if (! writable_only) {
                NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_CD_DRIVE, "CD");
                NCBD_ADD_TYPE_DESC (NAUTILUS_BURN_DRIVE_TYPE_DVD_DRIVE, "DVD");
        }

        return g_string_free (string, FALSE);
}

static guint64
nautilus_burn_drive_media_type_get_capacity (NautilusBurnMediaType type)
{
        int             i;
        int             size;
        gboolean        found    = FALSE;

        size = sizeof (media_capacity);

        for (i = 0; i < size && ! found; i++) {

                if (media_capacity[i].type == type) {
                        found = TRUE;

                        break;
                }
        }

        return found ? media_capacity[i].capacity : 0;
}

/* Adds decription _desc_ to @string if _is_big_enough is TRUE
 * else it returns @string */
#define NCBD_ADD_TYPE_DESC_IF_FITS(_t_, _is_big_enough_, _desc_)                                                \
                                                        do                                                      \
                                                        {                                                       \
                                                        if (type & (_t_)) {                                     \
                                                            if ((_is_big_enough_)) add_desc (string, (_desc_)); \
                                                            else return g_string_free (string, FALSE);          \
                                                        }                                                       \
                                                        }                                                       \
                                                        while (0)                                               \

/**
 * nautilus_burn_drive_get_supported_media_string_for_size:
 * @drive: A #NautilusBurnDrive
 * @size: Data size
 *
 * Get a string description of the suiting media types for the given
 * data size. The returned string should be freed when no longer needed.
 *
 * Returns: a string description of the supported media types
 *
 * Since: 2.16
 **/
char *
nautilus_burn_drive_get_supported_media_string_for_size (NautilusBurnDrive *drive,
                                                         guint64            size)
{
        GString *string;
        int      type;

/* Reserve some space for the filesystem and stuff. */
#define MARGIN  (1024 * 1024)

        size += MARGIN;

        type = nautilus_burn_drive_get_drive_type (drive);

        string = g_string_new (NULL);

        /* Check bigger media types first, in decreasing order:
         * NCBD_ADD_TYPE_IF_FITS () make us return if the media
         * is not big enough. */

        NCBD_ADD_TYPE_DESC_IF_FITS (NAUTILUS_BURN_DRIVE_TYPE_FILE, TRUE, "File");

        /* DVDs DL */
        NCBD_ADD_TYPE_DESC_IF_FITS (NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_DL_RECORDER,
                                    size < nautilus_burn_drive_media_type_get_capacity (NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL),
                                    "DVD+R DL");

        /* DVDs */
        NCBD_ADD_TYPE_DESC_IF_FITS (NAUTILUS_BURN_DRIVE_TYPE_DVD_RAM_RECORDER,
                                    size < nautilus_burn_drive_media_type_get_capacity (NAUTILUS_BURN_MEDIA_TYPE_DVD),
                                    "DVD-RAM");
        NCBD_ADD_TYPE_DESC_IF_FITS (NAUTILUS_BURN_DRIVE_TYPE_DVD_RW_RECORDER,
                                    size < nautilus_burn_drive_media_type_get_capacity (NAUTILUS_BURN_MEDIA_TYPE_DVD),
                                    "DVD-R, DVD-RW");
        NCBD_ADD_TYPE_DESC_IF_FITS (NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_RECORDER,
                                    size < nautilus_burn_drive_media_type_get_capacity (NAUTILUS_BURN_MEDIA_TYPE_DVD),
                                    "DVD+R");
        NCBD_ADD_TYPE_DESC_IF_FITS (NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_RW_RECORDER,
                                    size < nautilus_burn_drive_media_type_get_capacity (NAUTILUS_BURN_MEDIA_TYPE_DVD),
                                    "DVD+RW");

        /* CDs */
        NCBD_ADD_TYPE_DESC_IF_FITS (NAUTILUS_BURN_DRIVE_TYPE_CD_RECORDER,
                                    size < nautilus_burn_drive_media_type_get_capacity (NAUTILUS_BURN_MEDIA_TYPE_CD),
                                    "CD-R");
        NCBD_ADD_TYPE_DESC_IF_FITS (NAUTILUS_BURN_DRIVE_TYPE_CDRW_RECORDER,
                                    size < nautilus_burn_drive_media_type_get_capacity (NAUTILUS_BURN_MEDIA_TYPE_CD),
                                    "CD-RW");


        return g_string_free (string, FALSE);
}

/**
 * nautilus_burn_drive_media_type_get_string:
 * @type: A #NautilusBurnMediaType
 *
 * Get a string description of the specified media type.
 *
 * Returns: a string description for the media type.
 *
 * Since: 2.12
 **/
const char *
nautilus_burn_drive_media_type_get_string (NautilusBurnMediaType type)
{
        switch (type) {
        case NAUTILUS_BURN_MEDIA_TYPE_BUSY:
                return _("Could not determine media type because CD drive is busy");
        case NAUTILUS_BURN_MEDIA_TYPE_ERROR:
                return _("Couldn't open media");
        case NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN:
                return _("Unknown Media");
        case NAUTILUS_BURN_MEDIA_TYPE_CD:
                return _("Commercial CD or Audio CD");
        case NAUTILUS_BURN_MEDIA_TYPE_CDR:
                return _("CD-R");
        case NAUTILUS_BURN_MEDIA_TYPE_CDRW:
                return _("CD-RW");
        case NAUTILUS_BURN_MEDIA_TYPE_DVD:
                return _("DVD");
        case NAUTILUS_BURN_MEDIA_TYPE_DVDR:
                return _("DVD-R, or DVD-RAM");
        case NAUTILUS_BURN_MEDIA_TYPE_DVDRW:
                return _("DVD-RW");
        case NAUTILUS_BURN_MEDIA_TYPE_DVD_RAM:
                return _("DVD-RAM");
        case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R:
                return _("DVD+R");
        case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW:
                return _("DVD+RW");
        case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL:
                return _("DVD+R DL");
        default:
                break;
        }

        return _("Broken media type");
}

/**
 * nautilus_burn_drive_media_type_is_writable
 * @type: A #NautilusBurnMediaType
 * @is_blank: if the media type is blank or not
 *
 * Determine if a media type is writable
 *
 * Returns: %TRUE if the media type can be written to, otherwise return %FALSE.
 *
 * Since: 2.14
 **/
gboolean
nautilus_burn_drive_media_type_is_writable (NautilusBurnMediaType type,
                                            gboolean              is_blank)
{
        gboolean can_write;

        can_write = FALSE;

        switch (type) {
        case NAUTILUS_BURN_MEDIA_TYPE_BUSY:
        case NAUTILUS_BURN_MEDIA_TYPE_ERROR:
        case NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN:
        case NAUTILUS_BURN_MEDIA_TYPE_CD:
        case NAUTILUS_BURN_MEDIA_TYPE_DVD:
        case NAUTILUS_BURN_MEDIA_TYPE_DVD_RAM:
                can_write = FALSE;
                break;
        case NAUTILUS_BURN_MEDIA_TYPE_CDR:
        case NAUTILUS_BURN_MEDIA_TYPE_DVDR:
        case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R:
        case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL:
                if (is_blank) {
                        can_write = TRUE;
                } else {
                        can_write = FALSE;
                }
                break;
        case NAUTILUS_BURN_MEDIA_TYPE_CDRW:
        case NAUTILUS_BURN_MEDIA_TYPE_DVDRW:
        case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW:
                can_write = TRUE;
                break;
        default:
                g_warning ("Unknown media type: %d", type);
                can_write = FALSE;
                break;
        }

        return can_write;
}

/**
 * nautilus_burn_drive_media_is_appendable
 * @drive: #NautilusBurnDrive
 *
 * Determine if the media in the specified drive is appendable
 *
 * Returns: %TRUE if there is a media and it can be appended, otherwise returns %FALSE.
 *
 * Since: 2.16
 **/
gboolean
nautilus_burn_drive_media_is_appendable (NautilusBurnDrive *drive)
{
        g_return_val_if_fail (NAUTILUS_BURN_IS_DRIVE (drive), FALSE);

        return drive->priv->media_is_appendable;
}

static void
nautilus_burn_drive_set_property (GObject            *object,
                                  guint               prop_id,
                                  const GValue       *value,
                                  GParamSpec         *pspec)
{
        NautilusBurnDrive *self;

        self = NAUTILUS_BURN_DRIVE (object);

        switch (prop_id) {
        case PROP_MONITOR_ENABLED:
                g_warning ("the enable-monitor property is deprecated and will be removed in the next version");
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
nautilus_burn_drive_get_property (GObject            *object,
                                  guint               prop_id,
                                  GValue             *value,
                                  GParamSpec         *pspec)
{
        NautilusBurnDrive *self;

        self = NAUTILUS_BURN_DRIVE (object);

        switch (prop_id) {
        case PROP_MONITOR_ENABLED:
                g_warning ("the enable-monitor property is deprecated and will be removed in the next version");
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
nautilus_burn_drive_class_init (NautilusBurnDriveClass *klass)
{
        GObjectClass *object_class;

        object_class = (GObjectClass *) klass;

        object_class->finalize = nautilus_burn_drive_finalize;
        object_class->get_property = nautilus_burn_drive_get_property;
        object_class->set_property = nautilus_burn_drive_set_property;

        g_type_class_add_private (klass, sizeof (NautilusBurnDrivePrivate));

        /* Signals */
        nautilus_burn_drive_table_signals [MEDIA_ADDED] =
                g_signal_new ("media-added",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnDriveClass,
                                               media_added),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        nautilus_burn_drive_table_signals [MEDIA_REMOVED] =
                g_signal_new ("media-removed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnDriveClass,
                                               media_removed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        nautilus_burn_drive_table_signals [DISCONNECTED] =
                g_signal_new ("disconnected",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnDriveClass,
                                               disconnected),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

        /* Properties */

        /* this property is deprecated and will be removed in the next version */
        g_object_class_install_property (object_class,
                                         PROP_MONITOR_ENABLED,
                                         g_param_spec_boolean ("enable-monitor",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READWRITE));
}

static gboolean
can_drive_eject (int fd)
{
        if (fd < 0) {
                return FALSE;
        }

#ifdef __linux__
        {
                int status;

                status = ioctl (fd, CDROM_GET_CAPABILITY, 0);
                if (status < 0) {
                        return FALSE;
                }

                return status & CDC_OPEN_TRAY;
        }
#else
        return FALSE;
#endif
}

/**
 * nautilus_burn_drive_can_eject:
 * @drive: #NautilusBurnDrive
 *
 * Report the whether the drive support ejections or not.
 *
 * Returns: %TRUE if the drive support ejections, otherwise return %FALSE.
 **/
gboolean
nautilus_burn_drive_can_eject (NautilusBurnDrive *drive)
{
        gpointer ioctl_handle;
        int      fd;
        gboolean ret;

        g_return_val_if_fail (drive != NULL, FALSE);

        ioctl_handle = open_ioctl_handle (drive->priv->device);
        if (ioctl_handle == INVALID_HANDLE) {
                return FALSE;
        }

        fd = get_ioctl_handle_fd (ioctl_handle);

        ret = can_drive_eject (fd);

        close_ioctl_handle (ioctl_handle);

        return ret;
}
