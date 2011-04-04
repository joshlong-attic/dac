/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <gconf/gconf-client.h>

#include <dbus/dbus-glib.h>

#include "nautilus-burn-drive.h"
#include "nautilus-burn-drive-monitor.h"
#include "nautilus-burn-recorder.h"
#include "make-iso.h"

#include "ncb-progress-dialog.h"
#include "ncb-operation.h"

static void     ncb_operation_class_init (NcbOperationClass *klass);
static void     ncb_operation_init       (NcbOperation      *fade);
static void     ncb_operation_finalize   (GObject           *object);

static int      write_disc               (NcbOperation      *operation);

#define NCB_OPERATION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NCB_TYPE_OPERATION, NcbOperationPrivate))

#define BURN_URI "burn:///"

#define SM_DBUS_NAME      "org.gnome.SessionManager"
#define SM_DBUS_PATH      "/org/gnome/SessionManager"
#define SM_DBUS_INTERFACE "org.gnome.SessionManager"

#define GPM_DBUS_NAME      "org.freedesktop.PowerManagement"
#define GPM_DBUS_PATH      "/org/freedesktop/PowerManagement/Inhibit"
#define GPM_DBUS_INTERFACE "org.freedesktop.PowerManagement.Inhibit"

struct NcbOperationPrivate
{
        NcbOperationDoneFunc           done_cb;
        gpointer                       callback_data;
        guint                          do_idle_id;
        guint                          callback_idle_id;

        guint                          gpm_inhibit_cookie;
        guint                          sm_inhibit_cookie;

        NcbSelection                  *selection;

        GtkWidget                     *progress_dialog;
        int                            cancel;

        NautilusBurnRecorder          *recorder;
        NautilusBurnRecorderWriteFlags record_flags;
        GList                         *tracks;

        NautilusBurnIso               *iso;

        gboolean                       overburn;
        gboolean                       debug;
        gboolean                       dummy;
        GList                         *files_for_cleanup;
};

enum {
        CANCEL_NONE,
        CANCEL_MAKE_ISO,
        CANCEL_CD_RECORD,
};

enum {
        NCB_SOURCE_BURN = 0,
        NCB_SOURCE_IMAGE = 1,
        NCB_SOURCE_DRIVE = 2,
};

enum {
        NCB_OUTPUT_DEFAULT = 0,
        NCB_OUTPUT_FILE = 1,
        NCB_OUTPUT_CD = 2,
        NCB_OUTPUT_DVD = 3,
};

G_DEFINE_TYPE (NcbOperation, ncb_operation, G_TYPE_OBJECT)

static void
ncb_operation_set_property (GObject            *object,
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
ncb_operation_get_property (GObject            *object,
                            guint               prop_id,
                            GValue             *value,
                            GParamSpec         *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GtkWidget *
ncb_hig_dialog (GtkMessageType type,
                char          *title,
                char          *reason,
                GtkWindow     *parent)
{
        GtkWidget *error_dialog;

        if (reason == NULL) {
                g_warning ("ncb_hig_dialog called with reason == NULL");
        }

        error_dialog =
                gtk_message_dialog_new (parent,
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        type,
                                        GTK_BUTTONS_NONE,
                                        title);
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (error_dialog), "%s", reason);
        gtk_window_set_title (GTK_WINDOW (error_dialog), "");
        gtk_window_set_icon_name (GTK_WINDOW (error_dialog), "nautilus-cd-burner");

        gtk_container_set_border_width (GTK_CONTAINER (error_dialog), 5);

        return error_dialog;
}

static void
ncb_hig_show_error_dialog (char      *title,
                           char      *reason,
                           GtkWindow *parent)
{
        GtkWidget *dialog;

        dialog = ncb_hig_dialog (GTK_MESSAGE_ERROR, title, reason, parent);
        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_OK, GTK_RESPONSE_OK);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
}

static void
ncb_operation_temp_files_add (NcbOperation *operation,
                              const char   *filename)
{
        if (filename != NULL) {
                operation->priv->files_for_cleanup = g_list_append (operation->priv->files_for_cleanup, g_strdup (filename));
        }
}

static void
remove_file (const char *filename)
{
        int res;

        if (filename == NULL) {
                return;
        }

        res = g_remove (filename);
        if (res != 0) {
                g_warning ("Could not remove file %s: %s",
                           filename,
                           g_strerror (errno));
        }
}

static void
ncb_operation_temp_files_cleanup (NcbOperation *operation)
{
        operation->priv->files_for_cleanup = g_list_first (operation->priv->files_for_cleanup);

        while (operation->priv->files_for_cleanup != NULL) {
                char *filename = operation->priv->files_for_cleanup->data;

                remove_file (filename);
                g_free (filename);

                operation->priv->files_for_cleanup = g_list_remove (operation->priv->files_for_cleanup,
                                                                    operation->priv->files_for_cleanup->data);
        }
}

static void
iso_animation_changed_cb (NautilusBurnIso *iso,
                          gboolean         active,
                          NcbOperation    *operation)
{
        ncb_progress_dialog_set_active (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), active);
}

static void
animation_changed_cb (NautilusBurnRecorder *recorder,
                      gboolean              active,
                      NcbOperation         *operation)
{
        ncb_progress_dialog_set_active (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), active);
}

static void
action_changed_cb (NautilusBurnRecorder       *recorder,
                   NautilusBurnRecorderActions action,
                   NautilusBurnRecorderMedia   media,
                   NcbOperation               *operation)
{
        const char *text;

        text = NULL;

        switch (action) {

        case NAUTILUS_BURN_RECORDER_ACTION_PREPARING_WRITE:
                if (media == NAUTILUS_BURN_RECORDER_MEDIA_CD) {
                        text = _("Preparing to write CD");
                } else {
                        text = _("Preparing to write DVD");
                }
                break;
        case NAUTILUS_BURN_RECORDER_ACTION_WRITING:
                if (media == NAUTILUS_BURN_RECORDER_MEDIA_CD) {
                        text = _("Writing CD");
                } else {
                        text = _("Writing DVD");
                }
                break;
        case NAUTILUS_BURN_RECORDER_ACTION_FIXATING:
                if (media == NAUTILUS_BURN_RECORDER_MEDIA_CD) {
                        text = _("Finishing write");
                } else {
                        text = _("Finishing write");
                }
                break;
        case NAUTILUS_BURN_RECORDER_ACTION_BLANKING:
                if (media == NAUTILUS_BURN_RECORDER_MEDIA_CD) {
                        text = _("Erasing CD");
                } else {
                        text = _("Erasing DVD");
                }
                break;
        default:
                g_warning ("Unhandled action in action_changed_cb");
        }

        ncb_progress_dialog_set_time_remaining (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), -1);
        ncb_progress_dialog_set_operation_string (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), text);
}

static void
iso_progress_changed_cb (NautilusBurnIso *iso,
                         gdouble          fraction,
                         long             secs,
                         NcbOperation    *operation)
{
        ncb_progress_dialog_set_fraction (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), fraction);
        ncb_progress_dialog_set_time_remaining (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), secs);
}

static void
progress_changed_cb (NautilusBurnRecorder *recorder,
                     gdouble               fraction,
                     long                  secs,
                     NcbOperation         *operation)
{
        ncb_progress_dialog_set_fraction (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), fraction);
        ncb_progress_dialog_set_time_remaining (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), secs);
}

static char *
size_to_string_text (guint64 bytes)
{
        guint64     t_bytes;
        const char *unit;
        char       *result;

        if (bytes > 1125899906842624ull) {
                t_bytes = (bytes * 10) / 1125899906842624ull;
                unit = "PiB";
        } else if (bytes > 1099511627776ull) {
                t_bytes = (bytes * 10) / 1099511627776ull;
                unit = "TiB";
        } else if (bytes > 1073741824ull) {
                t_bytes = (bytes * 10) / 1073741824ull;
                unit = "GiB";
        } else if (bytes > 1048576) {
                t_bytes = (bytes * 10) / 1048576;
                unit = "MiB";
        } else if (bytes > 1024) {
                t_bytes = (bytes * 10) / 1024;
                unit = "KiB";
        } else {
                t_bytes = bytes;
                unit = "B";
        }

        result = g_strdup_printf ("%" G_GUINT64_FORMAT ".%" G_GUINT64_FORMAT " %s", t_bytes / 10, t_bytes % 10, unit);

        return result;
}

static int
ask_for_media (NcbOperation      *operation,
               NautilusBurnDrive *drive,
               gboolean           is_reload,
               gboolean           busy_cd,
               gint64             required_size)
{
        GtkWidget  *dialog;
        GtkWidget  *button;
        gboolean    can_rewrite;
        gboolean    show_eject;
        char       *msg;
        char       *type_string;
        const char *title;
        int         res;
        char       *size_string;

        size_string = size_to_string_text ((guint64)required_size);

        type_string = nautilus_burn_drive_get_supported_media_string_for_size (drive, (guint64)required_size);
        if (type_string == NULL) {
                type_string = g_strdup (_("None"));
        }

        can_rewrite = nautilus_burn_drive_can_rewrite (drive);
        show_eject = FALSE;

        if (busy_cd) {
                msg = g_strdup (_("Please make sure another application is not using the drive."));
                title = N_("Drive is busy");
        } else if (!is_reload && can_rewrite) {
                msg = g_strdup_printf (_("Please put a disc, with at least %s free, into the drive.  The following disc types are supported:\n%s"),
                                       size_string,
                                       type_string);
                title = N_("Insert a rewritable or blank disc");
        } else if (!is_reload && !can_rewrite) {
                msg = g_strdup_printf (_("Please put a disc, with at least %s free, into the drive.  The following disc types are supported:\n%s"),
                                       size_string,
                                       type_string);
                title = N_("Insert a blank disc");
        } else if (can_rewrite) {
                show_eject = TRUE;
                msg = g_strdup_printf (_("Please replace the disc in the drive with a supported disc with at least %s free.  The following disc types are supported:\n%s"),
                                       size_string,
                                       type_string);
                title = N_("Reload a rewritable or blank disc");
        } else {
                show_eject = TRUE;
                msg = g_strdup_printf (_("Please replace the disc in the drive with a supported disc with at least %s free.  The following disc types are supported:\n%s"),
                                       size_string,
                                       type_string);
                title = N_("Reload a blank disc");
        }

        g_free (size_string);
        g_free (type_string);

        dialog = ncb_hig_dialog (GTK_MESSAGE_INFO,
                                 _(title), _(msg), GTK_WINDOW (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog)));

        if (show_eject) {
                button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                                _("_Eject"),
                                                GTK_RESPONSE_NO);
        }

        gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_OK, GTK_RESPONSE_OK);

        gtk_dialog_set_default_response (GTK_DIALOG (dialog),
                                         GTK_RESPONSE_OK);

 retry:
        res = gtk_dialog_run (GTK_DIALOG (dialog));

        if (res == GTK_RESPONSE_NO) {
                nautilus_burn_drive_eject (drive);
                goto retry;
        }

        gtk_widget_destroy (dialog);

        g_free (msg);

        return res;
}

static gboolean
insert_media_request_cb (NautilusBurnRecorder *recorder,
                         gboolean              is_reload,
                         gboolean              can_rewrite,
                         gboolean              busy_cd,
                         NcbOperation         *operation)
{
        int                res;
        NautilusBurnDrive *drive;
        guint64            size;

        drive = ncb_selection_peek_drive (operation->priv->selection);
        ncb_selection_get_size (operation->priv->selection, &size);

        res = ask_for_media (operation,
                             drive,
                             is_reload,
                             busy_cd,
                             size);

        if (res == GTK_RESPONSE_CANCEL) {
                return FALSE;
        }

        return TRUE;
}

static int
ask_rewrite_disc (NcbOperation *operation)
{
        GtkWidget            *dialog;
        GtkWidget            *button;
        GtkWidget            *image;
        int                   res;
        NautilusBurnMediaType type;
        char                 *msg;
        NautilusBurnDrive    *drive;

        drive = ncb_selection_peek_drive (operation->priv->selection);

        type = nautilus_burn_drive_get_media_type (drive);

        msg = g_strdup_printf (_("This %s appears to have information already recorded on it."),
                               nautilus_burn_drive_media_type_get_string (type));

        dialog = ncb_hig_dialog (GTK_MESSAGE_WARNING,
                                 _("Erase information on this disc?"),
                                 msg,
                                 GTK_WINDOW (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog)));
        g_free (msg);

        image = gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON);
        gtk_widget_show (image);
        button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                        _("_Try Another"),
                                        NAUTILUS_BURN_RECORDER_RESPONSE_RETRY);
        g_object_set (button, "image", image, NULL);

        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_CANCEL,
                               NAUTILUS_BURN_RECORDER_RESPONSE_CANCEL);

        image = gtk_image_new_from_stock (GTK_STOCK_CLEAR, GTK_ICON_SIZE_BUTTON);
        gtk_widget_show (image);
        button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                        _("_Erase Disc"),
                                        NAUTILUS_BURN_RECORDER_RESPONSE_ERASE);
        g_object_set (button, "image", image, NULL);

        gtk_dialog_set_default_response (GTK_DIALOG (dialog),
                                         NAUTILUS_BURN_RECORDER_RESPONSE_CANCEL);

        res = gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);

        if (res == NAUTILUS_BURN_RECORDER_RESPONSE_RETRY) {
                nautilus_burn_drive_eject (drive);
        }

        return res;
}

static int
warn_data_loss_cb (NautilusBurnRecorder *recorder,
                   NcbOperation         *operation)
{
        return ask_rewrite_disc (operation);
}

static void
show_error_message (NcbOperation *operation,
                    GError       *error)
{
        GtkWidget *dialog;
        char      *msg;

        if (error) {
                msg = g_strdup_printf (_("There was an error writing to the disc:\n%s"),
                                       error->message);
        } else {
                msg = g_strdup (_("There was an error writing to the disc"));
        }

        dialog = ncb_hig_dialog (GTK_MESSAGE_ERROR, _("Error writing to disc"),
                                 msg, GTK_WINDOW (operation->priv->progress_dialog));
        g_free (msg);
        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_OK, GTK_RESPONSE_OK);

        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
}

static void
reset_progress_dialog (NcbOperation *operation)
{
        ncb_progress_dialog_clear (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog));
}

static void
do_make_another (NcbOperation *operation)
{
        int res;
        NautilusBurnDrive *drive;

        reset_progress_dialog (operation);

        drive = ncb_selection_peek_drive (operation->priv->selection);
        if (drive != NULL) {
                nautilus_burn_drive_eject (drive);
        }

        res = write_disc (operation);

        if (res == NAUTILUS_BURN_RECORDER_RESULT_CANCEL) {
                /* this doesn't return */
                gtk_dialog_response (GTK_DIALOG (operation->priv->progress_dialog), GTK_RESPONSE_CANCEL);
        }
}

static void
finish_progress_dialog (NcbOperation *operation)
{
        NautilusBurnDrive *drive;

        drive = ncb_selection_peek_drive (operation->priv->selection);

        ncb_progress_dialog_done (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog));
}

static gboolean
overwrite_existing_file (GtkWindow  *filesel,
                         const char *filename)
{
        GtkWidget *dialog;
        int        ret;
        char      *msg;

        msg = g_strdup_printf (_("A file named \"%s\" already exists.  Do you want to overwrite it?"),
                               filename);
        dialog = ncb_hig_dialog (GTK_MESSAGE_QUESTION, _("Overwrite existing file?"),
                                 msg, filesel);

        /* Add Cancel button */
        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

        /* Add Overwrite button */
        gtk_dialog_add_button (GTK_DIALOG (dialog),
                               _("_Overwrite"), GTK_RESPONSE_YES);

        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
        gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

        ret = gtk_dialog_run (GTK_DIALOG (dialog));

        gtk_widget_destroy (dialog);

        return (ret == GTK_RESPONSE_YES);
}

static char *
select_iso_filename (const char *default_name)
{
        GtkWidget *chooser;
        char      *filename;
        int        response;

        chooser = gtk_file_chooser_dialog_new (_("Choose a filename for the disc image"),
					       NULL,
					       GTK_FILE_CHOOSER_ACTION_SAVE,
					       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					       GTK_STOCK_SAVE, GTK_RESPONSE_OK,
					       NULL);
        gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (chooser), TRUE);
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (chooser),
                                           default_name);
 reselect:
        filename = NULL;
        response = gtk_dialog_run (GTK_DIALOG (chooser));

        if (response == GTK_RESPONSE_OK) {
                filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (chooser));
                if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
                        if (! overwrite_existing_file (GTK_WINDOW (chooser), filename)) {
                                g_free (filename);
                                goto reselect;
                        }
                }

        }
        gtk_widget_destroy (chooser);

        return filename;
}

static gboolean
request_media (NcbOperation      *operation,
               NautilusBurnDrive *drive,
               gint64             required_capacity,
               gboolean           request_overburn)
{
        gint64                media_capacity;
        gboolean              reload;
        NautilusBurnMediaType type;
        gboolean              is_rewritable;
        gboolean              can_write;
        gboolean              is_blank;
        gboolean              has_data;
        gboolean              has_audio;

        /* If the output is a file no need to prompt */
        if (nautilus_burn_drive_get_drive_type (drive) == NAUTILUS_BURN_DRIVE_TYPE_FILE) {
                return TRUE;
        }

        reload = FALSE;

 again:

        /* if drive is mounted then unmount before checking anything */
        if (nautilus_burn_drive_is_mounted (drive)) {
                gboolean res;
                res = nautilus_burn_drive_unmount (drive);
                if (! res) {
                        g_warning ("Couldn't unmount volume in drive: %s", nautilus_burn_drive_get_device (drive));
                }
        }

        /* if overburn requested then no need to check capacity/type */
        if (request_overburn) {
                return TRUE;
        }

        media_capacity = nautilus_burn_drive_get_media_capacity (drive);
        type = nautilus_burn_drive_get_media_type_full (drive, &is_rewritable, &is_blank, &has_data, &has_audio);
        can_write = nautilus_burn_drive_media_type_is_writable (type, is_blank);

        if ((! can_write) || (required_capacity > media_capacity)) {
                int res;

                reload = (media_capacity > 0);
                if (type == NAUTILUS_BURN_MEDIA_TYPE_ERROR) {
                        reload = FALSE;
                }

                res = ask_for_media (operation,
                                     drive,
                                     reload,
                                     FALSE,
                                     required_capacity);

                if (res == GTK_RESPONSE_CANCEL) {
                        return FALSE;
                }

                goto again;
        }

        return TRUE;
}

/* Adapted from sound-juicer */
static char *
sanitize_filename (const char *path,
                   gboolean    strip_chars)
{
        char *ret;

        /* Skip leading periods, otherwise files disappear... */
        while (*path == '.') {
                path++;
        }

        ret = g_strdup (path);
        /* Replace path seperators with a hyphen */
        g_strdelimit (ret, "/", '-');

        if (strip_chars) {
                /* Replace separators with a hyphen */
                g_strdelimit (ret, "\\:|", '-');
                /* Replace all other weird characters to whitespace */
                g_strdelimit (ret, "*?&!\'\"$()`>{}", ' ');
                /* Replace all whitespace with underscores */
                /* TODO: I'd like this to compress whitespace aswell */
                g_strdelimit (ret, "\t ", '_');
        }

        return ret;
}

static char *
get_output_filename (const char *disc_name)
{
        char *filename         = NULL;
        char *default_filename = NULL;
        char *temp;

        if (disc_name != NULL && strcmp (disc_name, "") != 0) {
                default_filename = g_strdup_printf ("%s.iso", disc_name);
        }

        if (default_filename == NULL) {
                /* Translators: this is the filename of the image */
                default_filename = g_strdup (_("image.iso"));
        } else {
                temp = sanitize_filename (default_filename, TRUE);
                g_free (default_filename);
                default_filename = temp;
        }

        /* Run the file selector and get the iso file name selected */
        filename = select_iso_filename (default_filename);
        g_free (default_filename);

        if (filename == NULL) {
                return NULL;
        }

        /* Check if you have permission to create or
         * overwrite the file.
         */
        if (g_file_test (filename, G_FILE_TEST_EXISTS)) {
                if (g_access (filename, W_OK) == -1) {
                        char *msg;
                        msg = g_strdup_printf (_("You do not have permissions to overwrite that file (%s)."), filename);
                        ncb_hig_show_error_dialog (_("File image creation failed"), msg, NULL);

                        g_free (msg);
                        g_free (filename);

                        return NULL;
                }
        } else {
                char *dir_name;
                dir_name = g_path_get_dirname (filename);

                if (access (dir_name, W_OK) == -1) {
                        char *msg;

                        msg = g_strdup_printf (_("You do not have permissions to create that file (%s)."), filename);
                        ncb_hig_show_error_dialog (_("File image creation failed"), msg, NULL);

                        g_free (filename);
                        g_free (dir_name);
                        g_free (msg);

                        return NULL;
                }

                g_free (dir_name);
        }

        return filename;
}

static int
write_disc (NcbOperation *operation)
{
        GError *error = NULL;
        int     res   = NAUTILUS_BURN_RECORDER_RESULT_ERROR;
        NautilusBurnDrive *drive;
        int                speed;

        operation->priv->recorder = nautilus_burn_recorder_new ();
        operation->priv->cancel = CANCEL_CD_RECORD;

        g_signal_connect (G_OBJECT (operation->priv->recorder),
                          "progress-changed",
                          G_CALLBACK (progress_changed_cb),
                          operation);
        g_signal_connect (G_OBJECT (operation->priv->recorder),
                          "action-changed",
                          G_CALLBACK (action_changed_cb),
                          operation);
        g_signal_connect (G_OBJECT (operation->priv->recorder),
                          "animation-changed",
                          G_CALLBACK (animation_changed_cb),
                          operation);
        g_signal_connect (G_OBJECT (operation->priv->recorder),
                          "warn-data-loss",
                          G_CALLBACK (warn_data_loss_cb),
                          operation);
        g_signal_connect (G_OBJECT (operation->priv->recorder),
                          "insert-media-request",
                          G_CALLBACK (insert_media_request_cb),
                          operation);

        drive = ncb_selection_peek_drive (operation->priv->selection);
        ncb_selection_get_speed (operation->priv->selection, &speed);

        res = nautilus_burn_recorder_write_tracks (operation->priv->recorder,
                                                   drive,
                                                   operation->priv->tracks,
                                                   speed,
                                                   operation->priv->record_flags,
                                                   &error);

        ncb_progress_dialog_set_fraction (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), 1.0);
        ncb_progress_dialog_set_time_remaining (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), -1);

        operation->priv->cancel = CANCEL_NONE;

        if (res == NAUTILUS_BURN_RECORDER_RESULT_FINISHED) {
                finish_progress_dialog (operation);
                ncb_progress_dialog_set_operation_string (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), _("Complete"));
        } else if (res != NAUTILUS_BURN_RECORDER_RESULT_CANCEL) {
                finish_progress_dialog (operation);
                ncb_progress_dialog_set_operation_string (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), _("An error occurred while writing"));

                show_error_message (operation, error);
                if (error != NULL) {
                        g_error_free (error);
                }
        }

        g_object_unref (operation->priv->recorder);
        operation->priv->recorder = NULL;

        return res;
}

static int
get_input_type_index (NcbSelectionSource source_type)
{
        int index;

        /* map NcbSelectionSource to array index
           input-type: files/default, image, drive
        */

        switch (source_type) {
        case NCB_SELECTION_SOURCE_BURN_FOLDER:
                index = 0;
                break;
        case NCB_SELECTION_SOURCE_ISO:
        case NCB_SELECTION_SOURCE_CUE:
                index = 1;
                break;
        case NCB_SELECTION_SOURCE_DEVICE:
                index = 2;
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        return index;
}

static void
update_progress_dialog_disc_type (NcbProgressDialog        *dialog,
                                  NautilusBurnDrive        *drive,
                                  NcbSelectionSource        source_type,
                                  NautilusBurnMediaType     type)
{
        const char *label_text;
        const char *title_text;
        const char *heading_text;
        int        input_type_index;
        int        output_type;

        /* Arrays structured as [input-type][output-type]
           input-type: files/default, image, drive
           output-type: disc/default, file, CD, DVD
         */
        static const char *titles [][4] = {
                { N_("Writing Files to Disc"),
                  N_("Writing Files to a Disc Image"),
                  N_("Writing Files to CD"),
                  N_("Writing Files to DVD")
                },

                { N_("Writing Image to Disc"),
                  N_("Copying Disc Image"),
                  N_("Writing Image to CD"),
                  N_("Writing Image to DVD")
                },

                { N_("Copying Disc"),
                  N_("Copying Disc to a Disc Image"),
                  N_("Copying Disc to CD"),
                  N_("Copying Disc to DVD")
                }
        };
        static const char *headings [][4] = {
                { N_("Writing files to disc"),
                  N_("Writing files to a disc image"),
                  N_("Writing files to CD"),
                  N_("Writing files to DVD")
                },

                { N_("Writing image to disc"),
                  N_("Copying disc image"),
                  N_("Writing image to CD"),
                  N_("Writing image to DVD")
                },

                { N_("Copying disc"),
                  N_("Copying disc to a disc image"),
                  N_("Copying disc to CD"),
                  N_("Copying disc to DVD")
                }
        };
        static const char *descriptions [][4] = {
                { N_("The selected files are being written to a CD or DVD.  This operation may take a long time, depending on data size and write speed."),
                  N_("The selected files are being written to a disc image file."),
                  N_("The selected files are being written to a CD.  This operation may take a long time, depending on data size and write speed."),
                  N_("The selected files are being written to a DVD.  This operation may take a long time, depending on data size and write speed.")
                },

                { N_("The selected disc image is being written to a CD or DVD.  This operation may take a long time, depending on data size and write speed."),
                  N_("The selected disc image is being copied to a disc image file."),
                  N_("The selected disc image is being written to a CD.  This operation may take a long time, depending on data size and write speed."),
                  N_("The selected disc image is being written to a DVD.  This operation may take a long time, depending on data size and write speed.")
                },

                { N_("The selected disc is being copied to a CD or DVD.  This operation may take a long time, depending on data size and drive speed."),
                  N_("The selected disc is being copied to a disc image file.  This operation may take a long time, depending on data size and drive speed."),
                  N_("The selected disc is being copied to a CD.  This operation may take a long time, depending on data size and drive speed."),
                  N_("The selected disc is being copied to a DVD.  This operation may take a long time, depending on data size and drive speed.")
                }
        };

        input_type_index = get_input_type_index (source_type);

        if (nautilus_burn_drive_get_drive_type (drive) == NAUTILUS_BURN_DRIVE_TYPE_FILE) {
                output_type = NCB_OUTPUT_FILE;
        } else {
                switch (type) {
                case NAUTILUS_BURN_MEDIA_TYPE_CD:
                case NAUTILUS_BURN_MEDIA_TYPE_CDR:
                case NAUTILUS_BURN_MEDIA_TYPE_CDRW:
                        output_type = NCB_OUTPUT_CD;
                        break;
                case NAUTILUS_BURN_MEDIA_TYPE_DVD:
                case NAUTILUS_BURN_MEDIA_TYPE_DVDR:
                case NAUTILUS_BURN_MEDIA_TYPE_DVDRW:
                case NAUTILUS_BURN_MEDIA_TYPE_DVD_RAM:
                case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R:
                case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW:
                case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL:
                        output_type = NCB_OUTPUT_DVD;
                        break;
                case NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN:
                default:
                        output_type = NCB_OUTPUT_DEFAULT;
                        break;
                }
        }

        label_text = descriptions [input_type_index][output_type];
        title_text = titles [input_type_index][output_type];
        heading_text = headings [input_type_index][output_type];

        gtk_window_set_title (GTK_WINDOW (dialog), _(title_text));

        ncb_progress_dialog_set_heading (dialog, _(heading_text));
        ncb_progress_dialog_set_description (dialog, _(label_text));
}

static gboolean
maybe_request_media (NcbOperation             *operation,
                     NautilusBurnDrive        *drive,
                     NcbSelectionSource        source_type,
                     guint64                   source_size,
                     gboolean                  overburn,
                     NautilusBurnMediaType    *media_type)
{
        NautilusBurnMediaType _media_type = NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN;

        if (nautilus_burn_drive_get_drive_type (drive) != NAUTILUS_BURN_DRIVE_TYPE_FILE) {
                /* if we are reading from a drive then don't request media yet */
                if (source_type != NCB_SELECTION_SOURCE_DEVICE) {
                        gboolean              is_locked;
                        gboolean              got_media;

                        is_locked = nautilus_burn_drive_lock (drive, _("Burning CD"), NULL);
                        got_media = request_media (operation, drive, source_size, overburn);

                        if (got_media) {
                                _media_type = nautilus_burn_drive_get_media_type (drive);
                        }

                        if (is_locked) {
                                nautilus_burn_drive_unlock (drive);
                        }

                        if (! got_media) {
                                return FALSE;
                        }
                }

                /* update the description now that we may know the media type */
                update_progress_dialog_disc_type (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog),
                                                  drive,
                                                  source_type,
                                                  _media_type);
        }

        if (media_type != NULL) {
                *media_type = _media_type;
        }

        return TRUE;
}

static char *
build_temp_iso_filename (const char *path)
{
        char *filename;

        filename = NULL;

        if (path != NULL && path[0] != '\0') {
                int fd;

                filename = g_build_filename (path, "image.iso.XXXXXX", NULL);
                fd = g_mkstemp (filename);
                if (fd < 0) {
                        g_warning ("Could not create temporary filename in path: %s", path);
                        g_free (filename);
                        return NULL;
                } else {
                        close (fd);
                }
        }

        return filename;
}

static char *
get_temp_path_gconf (void)
{
        char        *path;
        GConfClient *gc;

        gc = gconf_client_get_default ();
        path = gconf_client_get_string (gc, "/apps/nautilus-cd-burner/temp_iso_dir", NULL);
        g_object_unref (gc);

        return path;
}

static char **
get_temp_paths (void)
{
        char      *path;
        GPtrArray *arr;

        arr = g_ptr_array_new ();

        path = get_temp_path_gconf ();
        if (path != NULL && path[0] != '\0') {
                g_ptr_array_add (arr, g_strdup (path));
        }
        g_free (path);

        g_ptr_array_add (arr, g_strdup (g_get_tmp_dir ()));
        g_ptr_array_add (arr, g_strdup (g_get_home_dir ()));
        g_ptr_array_add (arr, NULL);

        return (char **)g_ptr_array_free (arr, FALSE);
}

static NautilusBurnRecorderTrack *
create_iso_track (const char *filename)
{
        NautilusBurnRecorderTrack *track;

        track = g_new0 (NautilusBurnRecorderTrack, 1);

        track->type = NAUTILUS_BURN_RECORDER_TRACK_TYPE_DATA;
        track->contents.data.filename = g_strdup (filename);

        return track;
}

static NautilusBurnRecorderTrack *
create_cue_track (const char *filename)
{
       NautilusBurnRecorderTrack *track;

       track = g_new0 (NautilusBurnRecorderTrack, 1);
       track->type = NAUTILUS_BURN_RECORDER_TRACK_TYPE_CUE;
       track->contents.cue.filename = g_strdup (filename);

       return track;
}

static NautilusBurnRecorderTrack *
create_track_from_device (NcbOperation *operation,
                          const char   *source_name,
                          GError      **error)
{
        NautilusBurnRecorderTrack   *track;
        NautilusBurnImageCreateFlags image_flags = 0;
        int                          res;
        NautilusBurnImageType        image_type;
        char                        *filename;
        NautilusBurnDrive           *source_drive;
        NautilusBurnDrive           *drive;
        char                        *label;
        char                        *iso_filename;
        char                        *toc_filename;
        char                       **temp_paths;
        int                          i;

        track = NULL;
        filename = NULL;
        temp_paths = NULL;

        operation->priv->cancel = CANCEL_MAKE_ISO;

        if (operation->priv->debug) {
                image_flags |= NAUTILUS_BURN_IMAGE_CREATE_DEBUG;
        }
        if (TRUE) {
                image_flags |= NAUTILUS_BURN_IMAGE_CREATE_JOLIET;
        }

        ncb_selection_get_label (operation->priv->selection, &label);
        source_drive = nautilus_burn_drive_monitor_get_drive_for_device (nautilus_burn_get_drive_monitor (),
                                                                         source_name);

        iso_filename = NULL;
        drive = ncb_selection_peek_drive (operation->priv->selection);
        if (nautilus_burn_drive_get_drive_type (drive) == NAUTILUS_BURN_DRIVE_TYPE_FILE) {
                iso_filename = get_output_filename (label);

                if (iso_filename == NULL) {
                        goto done;
                }
        }

        operation->priv->iso = nautilus_burn_iso_new ();
        g_signal_connect (operation->priv->iso,
                          "progress-changed",
                          G_CALLBACK (iso_progress_changed_cb),
                          operation);
        g_signal_connect (operation->priv->iso,
                          "animation-changed",
                          G_CALLBACK (iso_animation_changed_cb),
                          operation);
        ncb_progress_dialog_set_operation_string (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), _("Creating disc image"));

        toc_filename = NULL;
        filename     = NULL;
        res          = NAUTILUS_BURN_ISO_RESULT_RETRY;
        temp_paths   = get_temp_paths ();
        for (i = 0; temp_paths [i] != NULL; i++) {

                if (iso_filename == NULL) {
                        filename = build_temp_iso_filename (temp_paths [i]);
                } else {
                        filename = g_strdup (iso_filename);
                }

                if (filename == NULL) {
                        continue;
                }

                res = nautilus_burn_iso_make_from_drive (operation->priv->iso,
                                                         filename,
                                                         source_drive,
                                                         image_flags,
                                                         &image_type,
                                                         &toc_filename,
                                                         error);
                if (res != NAUTILUS_BURN_ISO_RESULT_RETRY) {
                        break;
                }

                /* if this was the last try then save the error */
                if (temp_paths [i + 1] != NULL) {
                        g_clear_error (error);
                }

                /* remove temporary file */
                if (iso_filename == NULL) {
                        remove_file (filename);
                }

                g_free (filename);
                filename = NULL;
                g_free (toc_filename);
                toc_filename = NULL;
        }

        if (res != NAUTILUS_BURN_ISO_RESULT_FINISHED) {
                goto done;
        }

        switch (image_type) {
        case NAUTILUS_BURN_IMAGE_TYPE_BINCUE:
                track = create_cue_track (toc_filename);
                break;

        case NAUTILUS_BURN_IMAGE_TYPE_ISO9660:
                track = create_iso_track (filename);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

 done:

        if (operation->priv->iso != NULL) {
                g_object_unref (operation->priv->iso);
                operation->priv->iso = NULL;
        }

        g_strfreev (temp_paths);
        g_free (toc_filename);
        g_free (filename);
        g_free (iso_filename);

        g_free (label);

        g_object_unref (source_drive);

        operation->priv->cancel = CANCEL_NONE;

        return track;
}

static NautilusBurnRecorderTrack *
create_graft_track (NautilusBurnIsoGraft *graft,
                    const char           *label)
{
        NautilusBurnRecorderTrack *track;

        track = g_new0 (NautilusBurnRecorderTrack, 1);

        track->type = NAUTILUS_BURN_RECORDER_TRACK_TYPE_GRAFT_LIST;
        track->contents.graft_list.entries = g_strdupv ((char **)(graft->array)->pdata);
        track->contents.graft_list.label = g_strdup (label);

        return track;

}

/* Checks if the burn folder seems to be a wannabe
 * DVD Video */
static gboolean
burn_folder_is_dvd_video (void)
{
	GFile           *file;
	gboolean         is_dvd_video;
	char           **content_types;
	guint            i;

	is_dvd_video = FALSE;

	file = g_file_new_for_uri (BURN_URI);
	content_types = g_content_type_guess_for_tree (file);
	g_object_unref (file);

	for (i = 0; content_types[i] != NULL; i++) {
		if (g_str_equal (content_types[i], "x-content/video-dvd") != FALSE) {
			is_dvd_video = TRUE;
			break;
		}
	}
	g_strfreev (content_types);

	return is_dvd_video;
}

static NautilusBurnRecorderTrack *
create_track_from_burn_folder (NcbOperation *operation,
                               const char   *source_name,
                               GError      **error)
{
        NautilusBurnRecorderTrack   *track;
        NautilusBurnImageCreateFlags image_flags = 0;
        int                          res;
        NautilusBurnImageType        image_type;
        char                        *filename;
        NautilusBurnDrive           *drive;
        char                        *label;
        char                        *iso_filename;
        gboolean                     use_udf;
        NautilusBurnIsoGraft        *graft;
        NautilusBurnMediaType        media_type;
        guint64                      source_size;
        char                       **temp_paths;
        int                          i;

        track = NULL;
        filename = NULL;
        temp_paths = NULL;

        operation->priv->cancel = CANCEL_MAKE_ISO;

        if (operation->priv->debug) {
                image_flags |= NAUTILUS_BURN_IMAGE_CREATE_DEBUG;
        }
        if (TRUE) {
                image_flags |= NAUTILUS_BURN_IMAGE_CREATE_JOLIET;
        }

        /* Create a UDF image if we are trying to burn a DVD Video,
         * otherwise we can't play it. */
        use_udf = burn_folder_is_dvd_video ();
        if (use_udf) {
                image_flags |= NAUTILUS_BURN_IMAGE_CREATE_UDF;
        }

        ncb_selection_get_label (operation->priv->selection, &label);

        iso_filename = NULL;
        drive = ncb_selection_peek_drive (operation->priv->selection);
        if (nautilus_burn_drive_get_drive_type (drive) == NAUTILUS_BURN_DRIVE_TYPE_FILE) {
                iso_filename = get_output_filename (label);

                if (iso_filename == NULL) {
                        goto done;
                }
        }

        ncb_selection_get_size (operation->priv->selection, &source_size);
        if (! maybe_request_media (operation, drive, NCB_SELECTION_SOURCE_BURN_FOLDER, source_size, operation->priv->overburn, &media_type)) {
                goto done;
        }

        operation->priv->iso = nautilus_burn_iso_new ();
        g_signal_connect (operation->priv->iso,
                          "progress-changed",
                          G_CALLBACK (iso_progress_changed_cb),
                          operation);
        g_signal_connect (operation->priv->iso,
                          "animation-changed",
                          G_CALLBACK (iso_animation_changed_cb),
                          operation);
        ncb_progress_dialog_set_operation_string (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), _("Creating disc image"));

        graft = nautilus_burn_iso_graft_new (BURN_URI);

        if (nautilus_burn_drive_get_drive_type (drive) != NAUTILUS_BURN_DRIVE_TYPE_FILE
            && NAUTILUS_BURN_DRIVE_MEDIA_TYPE_IS_DVD (media_type)) {
                GList   *l;
                gboolean use_joliet;
                guint64  size;

                for (l = graft->remove_files; l != NULL; l = l->next) {
                        ncb_operation_temp_files_add (operation, (char *)l->data);
                }

                if (! nautilus_burn_iso_graft_get_info (graft, &size, &use_joliet, &use_udf, error)) {
                        nautilus_burn_iso_graft_free (graft, TRUE);
                        goto done;
                }

                if (use_joliet) {
                        operation->priv->record_flags |= NAUTILUS_BURN_RECORDER_WRITE_JOLIET;
                }
                if (use_udf) {
                        operation->priv->record_flags |= NAUTILUS_BURN_RECORDER_WRITE_UDF;
                }

                track = create_graft_track (graft, label);

                /* Don't remove the files */
                nautilus_burn_iso_graft_free (graft, FALSE);
        } else {

                res = NAUTILUS_BURN_ISO_RESULT_ERROR;

                temp_paths = get_temp_paths ();
                for (i = 0; temp_paths [i] != NULL; i++) {

                        if (iso_filename == NULL) {
                                filename = build_temp_iso_filename (temp_paths [i]);
                        } else {
                                filename = g_strdup (iso_filename);
                        }

                        if (filename == NULL) {
                                continue;
                        }

                        res = nautilus_burn_iso_make_from_graft (operation->priv->iso,
                                                                 graft,
                                                                 filename,
                                                                 label,
                                                                 image_flags,
                                                                 &image_type,
                                                                 error);

                        if (res != NAUTILUS_BURN_ISO_RESULT_RETRY) {
                                break;
                        }

                        /* if this was the last try then save the error */
                        if (temp_paths [i + 1] != NULL) {
                                g_clear_error (error);
                        }

                        /* remove temporary file */
                        if (iso_filename == NULL) {
                                remove_file (filename);
                        }

                        g_free (filename);
                        filename = NULL;
                }

                if (res != NAUTILUS_BURN_ISO_RESULT_FINISHED) {
                        nautilus_burn_iso_graft_free (graft, TRUE);
                        goto done;
                }

                /* if we aren't writing to an ISO schedule this temp file for removal */
                if (nautilus_burn_drive_get_drive_type (drive) != NAUTILUS_BURN_DRIVE_TYPE_FILE) {
                        ncb_operation_temp_files_add (operation, filename);
                }

                track = create_iso_track (filename);
                nautilus_burn_iso_graft_free (graft, TRUE);
        }

 done:
        g_strfreev (temp_paths);

        if (operation->priv->iso != NULL) {
                g_object_unref (operation->priv->iso);
                operation->priv->iso = NULL;
        }

        g_free (filename);
        g_free (iso_filename);

        g_free (label);

        operation->priv->cancel = CANCEL_NONE;

        return track;
}
void
ncb_operation_set_selection (NcbOperation *operation,
                             NcbSelection *selection)
{
        g_return_if_fail (operation != NULL);

        if (operation->priv->selection != NULL) {
                g_object_unref (operation->priv->selection);
        }

        if (selection != NULL) {
                operation->priv->selection = g_object_ref (selection);
        }
}

static gboolean
prepare_tracks (NcbOperation *operation)
{
        GError                        *error;
        gboolean                       try_overburn = FALSE;
        NcbSelectionSource             source_type;
        char                          *source_name;
        const char                    *label;
        int                            speed;
        NautilusBurnDrive             *drive;
        NautilusBurnRecorderTrack     *track;

        ncb_selection_get_source (operation->priv->selection, &source_type, &source_name);
        drive = ncb_selection_peek_drive (operation->priv->selection);
        label = ncb_selection_peek_label (operation->priv->selection);
        ncb_selection_get_speed (operation->priv->selection, &speed);

        /* Default flags */
        operation->priv->record_flags = 0;
        if (TRUE) {
                operation->priv->record_flags |= NAUTILUS_BURN_RECORDER_WRITE_EJECT;
        }
        if (operation->priv->dummy) {
                operation->priv->record_flags |= NAUTILUS_BURN_RECORDER_WRITE_DUMMY_WRITE;
        }
        if (operation->priv->debug) {
                operation->priv->record_flags |= NAUTILUS_BURN_RECORDER_WRITE_DEBUG;
        }
        if (operation->priv->overburn || try_overburn) {
                operation->priv->record_flags |= NAUTILUS_BURN_RECORDER_WRITE_OVERBURN;
        }
        if (TRUE) {
                operation->priv->record_flags |= NAUTILUS_BURN_RECORDER_WRITE_BURNPROOF;
        }
        if (TRUE) {
                operation->priv->record_flags |= NAUTILUS_BURN_RECORDER_WRITE_DISC_AT_ONCE;
        }

        error = NULL;
        switch (source_type) {
        case NCB_SELECTION_SOURCE_ISO:
                track = create_iso_track (source_name);
                break;
        case NCB_SELECTION_SOURCE_CUE:
                track = create_cue_track (source_name);
                break;
        case NCB_SELECTION_SOURCE_DEVICE:
                track = create_track_from_device (operation, source_name, &error);
                break;
        case NCB_SELECTION_SOURCE_BURN_FOLDER:
                track = create_track_from_burn_folder (operation, source_name, &error);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        if (track == NULL) {
                /* User cancelled or we had an error. */
                if (error != NULL) {
                        ncb_hig_show_error_dialog (_("File image creation failed"), error->message, NULL);
                        g_error_free (error);
                }
                return FALSE;
        } else {
                operation->priv->tracks = g_list_prepend (operation->priv->tracks, track);
        }

        return TRUE;
}

static DBusGProxy *
get_sm_proxy (void)
{
        GError          *error;
        DBusGConnection *bus;
        DBusGProxy      *proxy;

        error = NULL;
        bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (bus == NULL) {
                g_warning ("Cannot connect to session bus: %s", error->message);
                g_error_free (error);
                return NULL;
        }

        proxy = dbus_g_proxy_new_for_name (bus,
                                           SM_DBUS_NAME,
                                           SM_DBUS_PATH,
                                           SM_DBUS_INTERFACE);
        return proxy;
}

static DBusGProxy *
get_gpm_proxy (void)
{
        GError          *error;
        DBusGConnection *bus;
        DBusGProxy      *proxy;

        error = NULL;

        bus = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
        if (error != NULL) {
                g_warning ("Cannot connect to session bus: %s", error->message);
                g_error_free (error);
                return NULL;
        }

        proxy = dbus_g_proxy_new_for_name (bus,
                                           GPM_DBUS_NAME,
                                           GPM_DBUS_PATH,
                                           GPM_DBUS_INTERFACE);

        return proxy;
}
typedef enum {
        GSM_INHIBITOR_FLAG_LOGOUT      = 1 << 0,
        GSM_INHIBITOR_FLAG_SWITCH_USER = 1 << 1,
        GSM_INHIBITOR_FLAG_SUSPEND     = 1 << 2
} GsmInhibitFlag;

static void
inhibit_sm (NcbOperation *operation,
            const char   *reason)
{
        GError     *error;
        gboolean    res;
        const char *app_id;
        guint       toplevel_xid;
        guint       flags;
        DBusGProxy *proxy;

        proxy = get_sm_proxy ();
        if (proxy == NULL) {
                return;
        }

        app_id = "nautilus-cd-burner";

        toplevel_xid = 0;
        flags = GSM_INHIBITOR_FLAG_LOGOUT
                | GSM_INHIBITOR_FLAG_SWITCH_USER
                | GSM_INHIBITOR_FLAG_SUSPEND;

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "Inhibit",
                                 &error,
                                 G_TYPE_STRING, app_id,
                                 G_TYPE_UINT, toplevel_xid,
                                 G_TYPE_STRING, reason,
                                 G_TYPE_UINT, flags,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, &operation->priv->sm_inhibit_cookie,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Failed to inhibit: %s", error->message);
                g_error_free (error);
        }

        g_object_unref (proxy);

}

static void
uninhibit_sm (NcbOperation *operation)
{
        GError     *error;
        gboolean    res;
        const char *app_id;
        guint       toplevel_xid;
        guint       flags;
        DBusGProxy *proxy;

        proxy = get_sm_proxy ();
        if (proxy == NULL) {
                return;
        }

        app_id = "nautilus-cd-burner";

        toplevel_xid = 0;
        flags = GSM_INHIBITOR_FLAG_LOGOUT
                | GSM_INHIBITOR_FLAG_SWITCH_USER
                | GSM_INHIBITOR_FLAG_SUSPEND;

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "Uninhibit",
                                 &error,
                                 G_TYPE_UINT, operation->priv->sm_inhibit_cookie,
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Failed to uninhibit: %s", error->message);
                g_error_free (error);
        }

        operation->priv->sm_inhibit_cookie = 0;
        g_object_unref (proxy);
}

static void
inhibit_gpm (NcbOperation *operation,
             const char   *reason)
{
        DBusGProxy           *proxy;
        gboolean              result;
        GError               *error;

        proxy = get_gpm_proxy ();
        if (proxy == NULL) {
                return;
        }

        error = NULL;
        result = dbus_g_proxy_call (proxy,
                                    "Inhibit",
                                    &error,
                                    G_TYPE_STRING,
                                    _("CD/DVD Creator"),
                                    G_TYPE_STRING,
                                    reason,
                                    G_TYPE_INVALID,
                                    G_TYPE_UINT,
                                    &operation->priv->gpm_inhibit_cookie,
                                    G_TYPE_INVALID);
        if (! result) {
                g_warning ("Failed to inhibit PowerManager from suspending: %s.", error->message);
                g_error_free (error);
        }

        g_object_unref (proxy);
}

static void
uninhibit_gpm (NcbOperation *operation)
{
        DBusGProxy           *proxy;
        gboolean              result;
        GError               *error;

        proxy = get_gpm_proxy ();
        if (proxy == NULL) {
                return;
        }

        error = NULL;
        result = dbus_g_proxy_call (proxy,
                                    "UnInhibit",
                                    &error,
                                    G_TYPE_UINT,
                                    operation->priv->gpm_inhibit_cookie,
                                    G_TYPE_INVALID,
                                    G_TYPE_INVALID);
        if (! result) {
                g_warning ("Failed to uninhibit PowerManager from suspending: %s.", error->message);
                g_error_free (error);
        }

        g_object_unref (proxy);
        operation->priv->gpm_inhibit_cookie = 0;
}

static void
inhibit_suspend (NcbOperation *operation)
{
        char                 *disc_name;
        char                 *reason;
        NautilusBurnDrive    *drive;
        NautilusBurnMediaType media_type;

        ncb_selection_get_label (operation->priv->selection, &disc_name);
        drive = ncb_selection_peek_drive (operation->priv->selection);

        if (nautilus_burn_drive_get_drive_type (drive) == NAUTILUS_BURN_DRIVE_TYPE_FILE) {
                reason = g_strdup_printf (_("Writing CD/DVD-Image \'%s\'."), disc_name);
        } else {
                media_type = nautilus_burn_drive_get_media_type (drive);
                if (media_type == NAUTILUS_BURN_MEDIA_TYPE_BUSY
                    || media_type == NAUTILUS_BURN_MEDIA_TYPE_ERROR) {
                    	/* Translators:
                    	 * Writing disc 'Disc name' */
                        reason = g_strdup_printf (_("Writing disc \'%s\'."), disc_name);
                } else {
                	/* Translators:
                	 * Writing DVD+RW 'Disc name' */
                        reason = g_strdup_printf (_("Writing %s \'%s\'."), nautilus_burn_drive_media_type_get_string (media_type), disc_name);
                }
        }

        inhibit_gpm (operation, reason);
        inhibit_sm (operation, reason);

        g_free (reason);
        g_free (disc_name);
}


static void
uninhibit_suspend (NcbOperation *operation)
{
        uninhibit_gpm (operation);
        uninhibit_sm (operation);
}

static int
burn_cd (NcbOperation *operation)
{
        int                res;
        NautilusBurnDrive *drive;
        NcbSelectionSource source_type;
        char              *source_name;

        inhibit_suspend (operation);

        drive = ncb_selection_peek_drive (operation->priv->selection);

        if (! prepare_tracks (operation)) {
                res = NAUTILUS_BURN_RECORDER_RESULT_CANCEL;
                g_warning ("Unable to prepare tracks for burning");
                goto out;
        }

        if (nautilus_burn_drive_get_drive_type (drive) == NAUTILUS_BURN_DRIVE_TYPE_FILE) {
                NautilusBurnRecorderTrack *track;
                char                      *str;

                /* don't show make another button */
                ncb_progress_dialog_set_make_another_button_visible (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), FALSE);

                finish_progress_dialog (operation);

                track = operation->priv->tracks->data;
                str = g_strdup_printf (_("Completed writing %s"), track->contents.data.filename);

                ncb_progress_dialog_set_operation_string (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), str);
                ncb_progress_dialog_set_time_remaining (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog), -1);
                g_free (str);

                res = NAUTILUS_BURN_RECORDER_RESULT_FINISHED;
                goto out;
        }

        /* special case: when copying a disc eject the source if same as destination */
        source_name = NULL;
        ncb_selection_get_source (operation->priv->selection, &source_type, &source_name);
        if (source_type == NCB_SELECTION_SOURCE_DEVICE) {
                NautilusBurnDrive *source_drive;

                source_drive = nautilus_burn_drive_monitor_get_drive_for_device (nautilus_burn_get_drive_monitor (),
                                                                                 source_name);
                if (source_drive != NULL
                    && nautilus_burn_drive_equal (drive, source_drive)) {
                        nautilus_burn_drive_eject (source_drive);
                        g_object_unref (source_drive);
                }
        }

        res = write_disc (operation);

 out:

        uninhibit_suspend (operation);

        return res;
}

static gboolean
do_operation_idle (NcbOperation *operation)
{
        GConfClient       *gconf_client;
        NcbSelectionSource source_type;
        int                res;

        ncb_selection_get_source (operation->priv->selection, &source_type, NULL);

        update_progress_dialog_disc_type (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog),
                                          ncb_selection_peek_drive (operation->priv->selection),
                                          source_type,
                                          NAUTILUS_BURN_MEDIA_TYPE_UNKNOWN);

        gtk_widget_show (GTK_WIDGET (operation->priv->progress_dialog));

        gconf_client = gconf_client_get_default ();
        operation->priv->debug     = gconf_client_get_bool (gconf_client, "/apps/nautilus-cd-burner/debug", NULL);
        operation->priv->overburn  = gconf_client_get_bool (gconf_client, "/apps/nautilus-cd-burner/overburn", NULL);
        g_object_unref (gconf_client);

        res = burn_cd (operation);

        operation->priv->do_idle_id = 0;

        if (res == NAUTILUS_BURN_RECORDER_RESULT_CANCEL) {
                /* this doesn't return */
                gtk_dialog_response (GTK_DIALOG (operation->priv->progress_dialog), GTK_RESPONSE_CANCEL);
        }

        return FALSE;
}

void
ncb_operation_do_async (NcbOperation        *operation,
                        NcbOperationDoneFunc done_cb,
                        gpointer             data)
{
        if (operation->priv->do_idle_id != 0) {
                return;
        }

        g_object_ref (operation);

        operation->priv->done_cb = done_cb;
        operation->priv->callback_data = data;
        operation->priv->do_idle_id = g_idle_add ((GSourceFunc)do_operation_idle, operation);
}

static void
ncb_operation_class_init (NcbOperationClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize     = ncb_operation_finalize;
        object_class->get_property = ncb_operation_get_property;
        object_class->set_property = ncb_operation_set_property;

        g_type_class_add_private (klass, sizeof (NcbOperationPrivate));
}

/* Returns TRUE if writing should be cancelled */
static gboolean
ask_cancel (NcbProgressDialog *dialog)
{
        GtkWidget *warning_dialog;
        GtkWidget *image;
        GtkWidget *button;
        int        res;

        warning_dialog = ncb_hig_dialog (GTK_MESSAGE_WARNING,
                                         _("Interrupt writing files to disc?"),
                                         _("Are you sure you want to interrupt the disc write operation? "
                                           "Some drives may require that you restart the computer to get them working again."),
                                         GTK_WINDOW (dialog));

        image = gtk_image_new_from_stock (GTK_STOCK_STOP, GTK_ICON_SIZE_BUTTON);
        gtk_widget_show (image);
        button = gtk_dialog_add_button (GTK_DIALOG (warning_dialog), _("Interrupt"), GTK_RESPONSE_YES);
        g_object_set (button, "image", image, NULL);
        gtk_dialog_add_button (GTK_DIALOG (warning_dialog), _("Continue"), GTK_RESPONSE_NO);

        gtk_dialog_set_default_response (GTK_DIALOG (warning_dialog), GTK_RESPONSE_NO);
        res = gtk_dialog_run (GTK_DIALOG (warning_dialog));
        gtk_widget_destroy (warning_dialog);

        return (res == GTK_RESPONSE_YES);
}

/* return true if we have handled the cancel */
static gboolean
handle_cancel (NcbOperation *operation)
{
        gboolean cancelled;

        if (operation->priv->cancel == CANCEL_NONE) {
                return FALSE;
        }

        if (operation->priv->cancel == CANCEL_MAKE_ISO) {
                if (operation->priv->iso != NULL) {
                        nautilus_burn_iso_cancel (operation->priv->iso);
                }
                return TRUE;
        }

        cancelled = nautilus_burn_recorder_cancel (operation->priv->recorder, TRUE);

        if (! cancelled) {
                if (ask_cancel (NCB_PROGRESS_DIALOG (operation->priv->progress_dialog)) == TRUE) {
                        /* do the dangerous cancel */
                        nautilus_burn_recorder_cancel (operation->priv->recorder, FALSE);
                } else {
                        return TRUE;
                }
        }

        return TRUE;
}

static gboolean
handle_delete_event (GtkWidget         *widget,
                     GdkEventAny       *event,
                     NcbOperation      *operation)
{
        return handle_cancel (operation);
}

static void
do_callback (NcbOperation *operation)
{
        operation->priv->done_cb (operation, operation->priv->callback_data);
        g_object_unref (operation);
}

static void
progress_dialog_response (GtkWidget    *dialog,
                          int           response_id,
                          NcbOperation *operation)
{
        if (response_id == GTK_RESPONSE_ACCEPT) {
                do_make_another (operation);
        }

        if (response_id == GTK_RESPONSE_CLOSE) {
                gtk_widget_hide (dialog);
                do_callback (operation);
        }

        if (response_id == GTK_RESPONSE_CANCEL
            || response_id == GTK_RESPONSE_DELETE_EVENT) {
                if (! handle_cancel (operation)) {
                        gtk_widget_hide (dialog);
                        do_callback (operation);
                }
        }
}

static void
ncb_operation_init (NcbOperation *operation)
{
        operation->priv = NCB_OPERATION_GET_PRIVATE (operation);

        operation->priv->progress_dialog = ncb_progress_dialog_new ();

        g_signal_connect (operation->priv->progress_dialog, "response", G_CALLBACK (progress_dialog_response), operation);
        g_signal_connect (operation->priv->progress_dialog, "delete_event", G_CALLBACK (handle_delete_event), operation);
}

static void
ncb_operation_finalize (GObject *object)
{
        NcbOperation *operation;

        g_return_if_fail (object != NULL);
        g_return_if_fail (NCB_IS_OPERATION (object));

        operation = NCB_OPERATION (object);

        g_return_if_fail (operation->priv != NULL);

        if (operation->priv->do_idle_id != 0) {
                g_source_remove (operation->priv->do_idle_id);
                operation->priv->do_idle_id = 0;
        }

        if (operation->priv->progress_dialog != NULL) {
                gtk_widget_destroy (operation->priv->progress_dialog);
        }

        if (operation->priv->selection != NULL) {
                g_object_unref (operation->priv->selection);
                operation->priv->selection = NULL;
        }

        if (operation->priv->recorder != NULL) {
                g_object_unref (operation->priv->recorder);
                operation->priv->recorder = NULL;
        }

        if (operation->priv->iso != NULL) {
                g_object_unref (operation->priv->iso);
                operation->priv->iso = NULL;
        }

        ncb_operation_temp_files_cleanup (operation);

        g_list_foreach (operation->priv->tracks, (GFunc)nautilus_burn_recorder_track_free, NULL);
        g_list_free (operation->priv->tracks);

        G_OBJECT_CLASS (ncb_operation_parent_class)->finalize (object);
}

NcbOperation *
ncb_operation_new (void)
{
        GObject *object;

        object = g_object_new (NCB_TYPE_OPERATION, NULL);

        return NCB_OPERATION (object);
}
