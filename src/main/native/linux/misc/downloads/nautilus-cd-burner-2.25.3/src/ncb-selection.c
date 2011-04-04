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
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <glade/glade.h>
#include <gconf/gconf-client.h>

#include "nautilus-burn-drive.h"
#include "nautilus-burn-drive-monitor.h"
#include "ncb-rename-dialog.h"
#include "make-iso.h"

#include "ncb-selection.h"

static void     ncb_selection_class_init (NcbSelectionClass *klass);
static void     ncb_selection_init       (NcbSelection      *fade);
static void     ncb_selection_finalize   (GObject                 *object);

#define NCB_SELECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NCB_TYPE_SELECTION, NcbSelectionPrivate))

#define BURN_URI "burn:///"
#define MAX_ISO_NAME_LEN  32

enum {
        PROP_0,
        PROP_SOURCE_TYPE,
        PROP_SOURCE_NAME,
        PROP_SOURCE_SIZE,
        PROP_LABEL,
        PROP_SPEED,
        PROP_DRIVE
};

struct NcbSelectionPrivate
{
        NcbSelectionSource       source_type;
        char                    *source_name;
        guint64                  size;
        char                    *label;

        int                      speed;
        NautilusBurnDrive       *drive;

        guint                    update_source_idle_id;
};

G_DEFINE_TYPE (NcbSelection, ncb_selection, G_TYPE_OBJECT)

GQuark
ncb_selection_error_quark (void)
{
        static GQuark quark = 0;
        if (! quark)
                quark = g_quark_from_static_string ("ncb_selection_error");

        return quark;
}

GType
ncb_selection_source_get_type (void)
{
        static GType etype = 0;
        if (etype == 0) {
                static const GEnumValue values[] = {
                        { NCB_SELECTION_SOURCE_BURN_FOLDER, "NCB_SELECTION_SOURCE_BURN_FOLDER", "Burn folder" },
                        { NCB_SELECTION_SOURCE_DEVICE, "NCB_SELECTION_SOURCE_DEVICE", "device name" },
                        { NCB_SELECTION_SOURCE_ISO, "NCB_SELECTION_SOURCE_ISO", "ISO image file" },
                        { NCB_SELECTION_SOURCE_CUE, "NCB_SELECTION_SOURCE_CUE", "CUE file" },
                        { 0, NULL, NULL }
                };
                etype = g_enum_register_static (g_intern_static_string ("NcbSelectionSource"), values);
        }

        return etype;
}
static void
ncb_selection_set_property (GObject            *object,
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
ncb_selection_get_property (GObject            *object,
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

static char *
get_backing_file (GFile *file)
{
        char      *mapping;
        GFileInfo *info;

        mapping = NULL;
        info = g_file_query_info (file,
                                  "burn::backing-file",
                                  0, NULL, NULL);
        if (info) {
                mapping = g_strdup (g_file_info_get_attribute_byte_string (info,
                                                                           "burn::backing-file"));
                g_object_unref (info);
        }

        return mapping;
}

typedef struct
{
        GList           *unreadable_paths;
        GList           *image_paths;
        gboolean         only_images;
        goffset          size;
} EstimateSizeCallbackData;

/* FIXME: This should probably be an inline */
static gboolean
file_info_is_image (GFileInfo *info)
{
        gboolean    is_image;
        const char *content_type;

        g_return_val_if_fail (info != NULL, FALSE);

        if (! g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE)) {
                return FALSE;
        }

        content_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);

        if (content_type == NULL) {
                return FALSE;
        }

        is_image = (strcmp (content_type, "application/x-cd-image") == 0);

        return is_image;
}

/* FIXME: This should probably be an inline */
static gboolean
file_info_is_allowed (GFileInfo *info)
{
        GFileType type;

        g_return_val_if_fail (info != NULL, FALSE);

        type = g_file_info_get_file_type (info);

        /* only allow regular,directory,symlink files */
        if (type != G_FILE_TYPE_REGULAR
            && type != G_FILE_TYPE_DIRECTORY
            && type != G_FILE_TYPE_SYMBOLIC_LINK) {
                return FALSE;
        } else if (type == G_FILE_TYPE_REGULAR
                   && !g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ)) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
estimate_size_visitor (GFile                    *file,
                       const char               *rel_path,
                       GFileInfo                *info,
                       EstimateSizeCallbackData *data,
                       gboolean                 *recurse)
{

        if (file_info_is_image (info)) {
                data->image_paths = g_list_append (data->image_paths, get_backing_file (file));
        } else {
                data->only_images = FALSE;
        }

        if (file_info_is_allowed (info)) {
                if (g_file_info_get_file_type (info) != G_FILE_TYPE_DIRECTORY) {
                        data->size += g_file_info_get_size (info);
                }
        } else {
                data->unreadable_paths = g_list_append (data->unreadable_paths, g_strdup (rel_path));
        }

        *recurse = TRUE;

        return TRUE;
}

static gboolean
estimate_size_internal (GFile                    *file,
                        const char               *prefix,
                        EstimateSizeCallbackData *data)
{
        GFileEnumerator          *enumerator;
        gboolean                  stop;

        enumerator = g_file_enumerate_children (file,
                                                G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                                G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                                G_FILE_ATTRIBUTE_STANDARD_SIZE ","
                                                G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
                                                G_FILE_ATTRIBUTE_ACCESS_CAN_READ,
                                                0,
                                                NULL, NULL);
        if (enumerator == NULL) {
                return FALSE;
        }

        stop = FALSE;
        while (! stop) {
                GFile     *child;
                GFileInfo *info;
                gboolean   recurse;
                char      *rel_path;

                info = g_file_enumerator_next_file (enumerator, NULL, NULL);
                if (info == NULL) {
                        break;
                }

                child = g_file_get_child (file, g_file_info_get_name (info));

                if (prefix == NULL) {
                        rel_path = g_strdup (g_file_info_get_name (info));
                } else {
                        rel_path = g_strconcat (prefix, g_file_info_get_name (info), NULL);
                }
                stop = ! estimate_size_visitor (child, rel_path, info, data, &recurse);

                if (! stop
                    && recurse
                    && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
                        char *new_prefix;
                        if (prefix == NULL) {
                                new_prefix = g_strconcat (g_file_info_get_name (info),
                                                          "/",
                                                          NULL);
                        } else {
                                new_prefix = g_strconcat (prefix,
                                                          g_file_info_get_name (info),
                                                          "/",
                                                          NULL);
                        }

                        estimate_size_internal (child, new_prefix, data);

                        g_free (new_prefix);

                }
                g_object_unref (child);
                g_free (rel_path);
                g_object_unref (info);
        }
        g_object_unref (enumerator);

        return TRUE;
}

static goffset
estimate_size (const char *uri,
               GList     **unreadable_paths,
               GList     **image_paths,
               gboolean   *only_images)
{
        GFile                    *file;
        EstimateSizeCallbackData *data;
        goffset                   size;
        gboolean                  res;

        data = g_new0 (EstimateSizeCallbackData, 1);
        data->only_images = TRUE;

        file = g_file_new_for_uri (uri);

        res = estimate_size_internal (file, NULL, data);

        if (data->size == 0) {
                /* FIXME: if files exist then set to one */
        }

        if (data->unreadable_paths) {
                if (unreadable_paths) {
                        GList *l;
                        *unreadable_paths = NULL;
                        for (l = data->unreadable_paths; l; l = l->next) {
                                *unreadable_paths = g_list_append (*unreadable_paths, g_strdup ((char *)l->data));
                        }
                }

                g_list_foreach (data->unreadable_paths, (GFunc)g_free, NULL);
                g_list_free (data->unreadable_paths);
        }

        if (data->image_paths) {
                if (image_paths) {
                        GList *l;
                        *image_paths = NULL;
                        for (l = data->image_paths; l; l = l->next) {
                                *image_paths = g_list_append (*image_paths, g_strdup ((char *)l->data));
                        }
                }

                g_list_foreach (data->image_paths, (GFunc)g_free, NULL);
                g_list_free (data->image_paths);
        }

        size = data->size;
        *only_images = data->only_images;

        g_free (data);

        return size;
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

static gboolean
verify_source_location_contents (const char *text_uri,
                                 guint64    *size_out,
                                 GList     **images,
                                 GError    **error)
{
        GList           *unreadable_paths       = NULL;
        GList           *image_paths            = NULL;
        NcbRenameDialog *rename_dialog;
        gboolean         only_images;
        gint64           size;
        int              res;

        if (images != NULL) {
                *images = NULL;
        }

        size = estimate_size (text_uri, &unreadable_paths, &image_paths, &only_images);

        rename_dialog = ncb_rename_dialog_new ();
        g_signal_connect (G_OBJECT (rename_dialog), "response",
                          (GCallback)ncb_rename_dialog_response_cb, NULL);

        /* Display the rename dialog if there are invalid filenames in
         * BURN_URI. */
        if (ncb_rename_dialog_set_invalid_filenames (rename_dialog, BURN_URI)) {

                /* The dialog renames the files directly in BURN_URI
                 * for us. */
                res = gtk_dialog_run (GTK_DIALOG (rename_dialog));
                if (res == GTK_RESPONSE_CANCEL) {
                        gtk_widget_destroy (GTK_WIDGET (rename_dialog));

                        return FALSE;
                }
        }
        gtk_widget_destroy (GTK_WIDGET (rename_dialog));

        if (only_images == TRUE && size != 0) {
                GtkWidget *dialog;
                char      *msg;

                if (g_list_length (image_paths) == 1) {
                        msg = g_strdup (_("It appears that the disc, when created, will contain a single disc image file.  "
                                          "Do you want to create a disc from the contents of the image or with the image file inside?"));
                        dialog = ncb_hig_dialog (GTK_MESSAGE_QUESTION,
                                                 _("Create disc containing a single disc image file?"),
                                                 msg, NULL);
                        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                                _("Create From Image"), GTK_RESPONSE_NO,
                                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                _("Create With File"), GTK_RESPONSE_YES,
                                                NULL);
                } else {
                        msg = g_strdup (_("It appears that the disc, when created, will contain only disc image files.  "
                                          "Do you want to continue and write them to the disc as files?"));
                        dialog = ncb_hig_dialog (GTK_MESSAGE_QUESTION,
                                                 _("Create disc containing only disc image files?"),
                                                 msg, NULL);
                        gtk_dialog_add_buttons (GTK_DIALOG (dialog),
                                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                _("Create With Files"), GTK_RESPONSE_YES,
                                                NULL);
                }
                g_free (msg);

                gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
                res = gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);

                if (res == GTK_RESPONSE_NO) {
                        if (images != NULL) {
                                *images = image_paths;
                        } else {
                                g_list_foreach (image_paths, (GFunc)g_free, NULL);
                                g_list_free (image_paths);
                        }
                        return FALSE;
                } else if (res == GTK_RESPONSE_CANCEL) {
                        g_list_foreach (image_paths, (GFunc)g_free, NULL);
                        g_list_free (image_paths);
                        return FALSE;
                }
        }

        if (unreadable_paths != NULL) {
                GList *l;

                for (l = unreadable_paths; l; l = l->next) {
                        GtkWidget *dialog;
                        char      *msg;

                        msg = g_strdup_printf (_("The file '%s' is unreadable.  Do you wish to skip this file and continue?"),
                                               (char *)l->data);
                        dialog = ncb_hig_dialog (GTK_MESSAGE_WARNING,
                                                 _("Skip unreadable file?"),
                                                 msg,
                                                 NULL);
                        g_free (msg);
                        gtk_dialog_add_buttons (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                _("Skip"), GTK_RESPONSE_ACCEPT, _("Skip All"), GTK_RESPONSE_YES, NULL);
                        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
                        res = gtk_dialog_run (GTK_DIALOG (dialog));
                        gtk_widget_destroy (dialog);
                        if (res == GTK_RESPONSE_ACCEPT) {
                        } else if (res == GTK_RESPONSE_YES) {
                                break;
                        } else {
                                /* cancel */
                                g_list_foreach (unreadable_paths, (GFunc)g_free, NULL);
                                g_list_free (unreadable_paths);
                                return FALSE;
                        }
                }

                g_list_foreach (unreadable_paths, (GFunc)g_free, NULL);
                g_list_free (unreadable_paths);
        }

        if (size <= 0) {
                g_set_error (error,
                             NCB_SELECTION_ERROR,
                             NCB_SELECTION_ERROR_SOURCE_INVALID,
                             _("No files were selected."));

                return FALSE;
        }

        if (size_out) {
                *size_out = size;
        }

        return TRUE;
}

static gboolean
verify_source_image (const char           *text_uri,
                     guint64              *size_out,
                     char                **disc_name,
                     GError              **error)
{
        GError          *error_local;
        struct stat      stat_buf;
        int              res;
        NautilusBurnIso *iso;

        error_local = NULL;

        iso = nautilus_burn_iso_new ();
        res = nautilus_burn_iso_verify (iso, text_uri, disc_name, &error_local);
        g_object_unref (iso);
        if (! res) {
                char *msg;

                msg = g_strdup_printf (_("The file '%s' is not a valid disc image."),
                                       text_uri);
                g_set_error (error,
                             NCB_SELECTION_ERROR,
                             NCB_SELECTION_ERROR_SOURCE_INVALID,
                             msg);
                g_free (msg);
                g_error_free (error_local);

                return FALSE;
        }

        /* TODO: is this close enough for the CD size? */
        res = g_stat (text_uri, &stat_buf);

        if (size_out) {
                *size_out = (guint64)stat_buf.st_size;
        }

        return TRUE;
}

static gboolean
verify_burn_folder (NcbSelection *selection,
                    char        **name,
                    guint64      *size,
                    GError      **error)
{
        GList *image_paths = NULL;

        if (size != NULL) {
                *size = 0;
        }

        if (name != NULL) {
                *name = NULL;
        }

        if (! verify_source_location_contents (BURN_URI, size, &image_paths, error)) {
                if (image_paths != NULL) {
                        /* change the source type */
                        ncb_selection_set_source (selection, NCB_SELECTION_SOURCE_ISO, (char *)image_paths->data);
                        g_list_foreach (image_paths, (GFunc)g_free, NULL);
                        g_list_free (image_paths);
                        return TRUE;
                } else {
                        return FALSE;
                }
        }

        return TRUE;
}

static gboolean
verify_device (NcbSelection *selection,
               char        **name,
               guint64      *size,
               GError      **error)
{
        NautilusBurnDrive *drive;

        if (size != NULL) {
                *size = 0;
        }

        if (name != NULL) {
                *name = NULL;
        }

        drive = nautilus_burn_drive_monitor_get_drive_for_device (nautilus_burn_get_drive_monitor (),
                                                                  selection->priv->source_name);

        if (! drive) {
                char *msg;

                msg = g_strdup_printf (_("The specified device '%s' is not a valid CD/DVD drive."),
                                       selection->priv->source_name);

                g_set_error (error,
                             NCB_SELECTION_ERROR,
                             NCB_SELECTION_ERROR_GENERAL,
                             msg);
                g_free (msg);

                return FALSE;
        }

        nautilus_burn_drive_unmount (drive);
        if (name != NULL) {
                *name = nautilus_burn_drive_get_media_label (drive);
        }
        if (size != NULL) {
                *size = nautilus_burn_drive_get_media_size (drive);
        }

        if ((gint64) *size < 0) {
                g_set_error (error,
                             NCB_SELECTION_ERROR,
                             NCB_SELECTION_ERROR_GENERAL,
                             _("There doesn't seem to be any media in the selected drive."));
                return FALSE;

        } else if (*size == 0) {
                g_set_error (error,
                             NCB_SELECTION_ERROR,
                             NCB_SELECTION_ERROR_GENERAL,
                             _("The media is blank."));
                return FALSE;
        }

        return TRUE;
}

static gboolean
verify_cue (NcbSelection *selection,
            char        **name,
            guint64      *size,
            GError      **error)
{
        gboolean    ret;
        struct stat stat_buf;
        int         res;

        if (size != NULL) {
                *size = 0;
        }

        if (name != NULL) {
                *name = NULL;
        }

        res = g_stat (selection->priv->source_name, &stat_buf);

        ret = TRUE;
        if (res != 0) {
                ret = FALSE;
                g_set_error (error,
                             NCB_SELECTION_ERROR,
                             NCB_SELECTION_ERROR_GENERAL,
                             _("File not found: %s"),
                             selection->priv->source_name);
        }

        if (stat_buf.st_size <= 0) {
                ret = FALSE;
                g_set_error (error,
                             NCB_SELECTION_ERROR,
                             NCB_SELECTION_ERROR_GENERAL,
                             _("File does not appear to be a valid CUE sheet: %s"),
                             selection->priv->source_name);
        }

        return ret;
}

static gboolean
verify_iso (NcbSelection *selection,
            char        **name,
            guint64      *size,
            GError      **error)
{
        if (size != NULL) {
                *size = 0;
        }

        if (name != NULL) {
                *name = NULL;
        }

        if (! verify_source_image (selection->priv->source_name, size, name, error)) {
                return FALSE;
        }

        return TRUE;
}

static char *
create_default_label (void)
{
        GDate     *now;
        char       buf [129];
        char      *str;
        char      *last;

        now = g_date_new ();

        g_date_set_time_t (now, time (NULL));

        /*
          translators: see strftime man page for meaning of %b, %d and %Y
          the maximum length for this field is 32 bytes
        */
        g_date_strftime (buf, sizeof (buf), _("Personal Data, %b %d, %Y"), now);

        g_date_free (now);

        /* Cut off at 32 bytes */
        last = str = buf;
        while (*str != 0 &&
               (str - buf) < MAX_ISO_NAME_LEN) {
                last = str;
                str = g_utf8_next_char (str);
        }
        if (*str != 0) {
                *last = 0;
        }

        return g_strdup (buf);
}

static gboolean
update_source (NcbSelection *selection)
{
        char      *name = NULL;
        guint64    size;
        gboolean   res;
        gboolean   is_copy;
        GError    *error;

        is_copy = FALSE;

        error = NULL;

        switch (selection->priv->source_type) {
        case NCB_SELECTION_SOURCE_BURN_FOLDER:
                res = verify_burn_folder (selection, &name, &size, &error);
                break;
        case NCB_SELECTION_SOURCE_DEVICE:
                res = verify_device (selection, &name, &size, &error);
                is_copy = TRUE;
                break;
        case NCB_SELECTION_SOURCE_CUE:
                res = verify_cue (selection, &name, &size, &error);
                break;
        case NCB_SELECTION_SOURCE_ISO:
                res = verify_iso (selection, &name, &size, &error);
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        if (! res) {
                goto fail;
        }

        ncb_selection_set_size (selection, size);
        if (name == NULL) {
                name = create_default_label ();
        }
        ncb_selection_set_label (selection, name);

        g_free (name);

        selection->priv->update_source_idle_id = 0;

        return FALSE;

 fail:
        g_free (name);

        selection->priv->update_source_idle_id = 0;

        if (error != NULL) {
                ncb_hig_show_error_dialog (_("Unable to create CD/DVD"), error->message, NULL);
                g_error_free (error);
        }

        gtk_main_quit ();

        return FALSE;
}

void
ncb_selection_set_source (NcbSelection      *selection,
                          NcbSelectionSource type,
                          const char        *name)
{
        g_return_if_fail (selection != NULL);

        g_free (selection->priv->source_name);
        selection->priv->source_name = g_strdup (name);
        selection->priv->source_type = type;
        g_object_notify (G_OBJECT (selection), "source-name");
        g_object_notify (G_OBJECT (selection), "source-type");

        if (selection->priv->update_source_idle_id > 0) {
                g_source_remove (selection->priv->update_source_idle_id);
        }
        selection->priv->update_source_idle_id = g_idle_add ((GSourceFunc)update_source, selection);
}

void
ncb_selection_set_drive (NcbSelection      *selection,
                         NautilusBurnDrive *drive)
{
        g_return_if_fail (selection != NULL);

        if (selection->priv->drive != NULL) {
                g_object_unref (selection->priv->drive);
        }

        if (drive != NULL) {
                selection->priv->drive = g_object_ref (drive);
        }

        g_object_notify (G_OBJECT (selection), "drive");
}

void
ncb_selection_set_speed (NcbSelection *selection,
                         int           speed)
{
        g_return_if_fail (selection != NULL);

        selection->priv->speed = speed;
        g_object_notify (G_OBJECT (selection), "speed");
}

void
ncb_selection_set_size (NcbSelection *selection,
                        guint64       size)
{
        g_return_if_fail (selection != NULL);

        selection->priv->size = size;
        g_object_notify (G_OBJECT (selection), "source-size");
}

void
ncb_selection_get_size (NcbSelection *selection,
                        guint64      *size)
{
        g_return_if_fail (selection != NULL);

        if (size != NULL) {
                *size = selection->priv->size;
        }
}

void
ncb_selection_set_label (NcbSelection *selection,
                         const char   *label)
{

        if (selection->priv->label) {
                g_free (selection->priv->label);
        }

        selection->priv->label = g_strdup (label);
        g_object_notify (G_OBJECT (selection), "label");
}

void
ncb_selection_get_source (NcbSelection       *selection,
                          NcbSelectionSource *type,
                          char              **name)
{
        g_return_if_fail (selection != NULL);

        if (type != NULL) {
                *type = selection->priv->source_type;
        }

        if (name != NULL) {
                *name = g_strdup (selection->priv->source_name);
        }
}

void
ncb_selection_get_drive (NcbSelection       *selection,
                         NautilusBurnDrive **drive)
{
        g_return_if_fail (selection != NULL);

        if (drive != NULL) {
                *drive = g_object_ref (selection->priv->drive);
        }
}

NautilusBurnDrive *
ncb_selection_peek_drive (NcbSelection *selection)
{
        g_return_val_if_fail (selection != NULL, NULL);
        return selection->priv->drive;
}

void
ncb_selection_get_speed (NcbSelection *selection,
                         int          *speed)
{
        g_return_if_fail (selection != NULL);

        if (speed != NULL) {
                *speed = selection->priv->speed;
        }
}

void
ncb_selection_get_label (NcbSelection *selection,
                         char        **label)
{
        g_return_if_fail (selection != NULL);

        if (label != NULL) {
                *label = g_strdup (selection->priv->label);
        }
}

const char *
ncb_selection_peek_label (NcbSelection *selection)
{
        g_return_val_if_fail (selection != NULL, NULL);
        return selection->priv->label;
}

static void
ncb_selection_class_init (NcbSelectionClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize     = ncb_selection_finalize;
        object_class->get_property = ncb_selection_get_property;
        object_class->set_property = ncb_selection_set_property;

        g_object_class_install_property (object_class,
                                         PROP_SOURCE_NAME,
                                         g_param_spec_string ("source-name",
                                                              _("Source name"),
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SOURCE_TYPE,
                                         g_param_spec_enum ("source-type",
                                                            _("Source type"),
                                                            NULL,
                                                            NCB_SELECTION_TYPE_SOURCE,
                                                            NCB_SELECTION_SOURCE_BURN_FOLDER,
                                                            G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SOURCE_SIZE,
                                         g_param_spec_uint64 ("source-size",
                                                              _("Source size"),
                                                              NULL,
                                                              0,
                                                              G_MAXUINT64,
                                                              0,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_LABEL,
                                         g_param_spec_string ("label",
                                                              _("Disc label"),
                                                              NULL,
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_SPEED,
                                         g_param_spec_int ("speed",
                                                           _("Write speed"),
                                                           NULL,
                                                           -1,
                                                           G_MAXINT,
                                                           0,
                                                           G_PARAM_READWRITE));

        g_object_class_install_property (object_class,
                                         PROP_DRIVE,
                                         g_param_spec_object ("drive",
                                                              _("Drive"),
                                                              NULL,
                                                              NAUTILUS_BURN_TYPE_DRIVE,
                                                              G_PARAM_READWRITE));

        g_type_class_add_private (klass, sizeof (NcbSelectionPrivate));
}

static void
ncb_selection_init (NcbSelection *selection)
{
        selection->priv = NCB_SELECTION_GET_PRIVATE (selection);
}

static void
ncb_selection_finalize (GObject *object)
{
        NcbSelection *selection;

        g_return_if_fail (object != NULL);
        g_return_if_fail (NCB_IS_SELECTION (object));

        selection = NCB_SELECTION (object);

        g_return_if_fail (selection->priv != NULL);

        if (selection->priv->update_source_idle_id > 0) {
                g_source_remove (selection->priv->update_source_idle_id);
        }

        g_free (selection->priv->label);
        g_free (selection->priv->source_name);

        G_OBJECT_CLASS (ncb_selection_parent_class)->finalize (object);
}

NcbSelection *
ncb_selection_new (void)
{
        GObject *object;

        object = g_object_new (NCB_TYPE_SELECTION, NULL);

        return NCB_SELECTION (object);
}
