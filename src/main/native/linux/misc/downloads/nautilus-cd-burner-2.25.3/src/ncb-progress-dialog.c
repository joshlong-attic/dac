/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
#include <gtk/gtk.h>

#include <glade/glade.h>

#include "ncb-progress-dialog.h"
#include "nautilus-file-operations-progress-icons.h"

static void     ncb_progress_dialog_class_init (NcbProgressDialogClass *klass);
static void     ncb_progress_dialog_init       (NcbProgressDialog      *fade);
static void     ncb_progress_dialog_finalize   (GObject                *object);

#define NCB_PROGRESS_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NCB_TYPE_PROGRESS_DIALOG, NcbProgressDialogPrivate))

#define BURN_URI "burn:///"
#define MAX_ISO_NAME_LEN  32

struct NcbProgressDialogPrivate
{
        GladeXML                *xml;
        /* For the image spinning */
        GList                   *image_list;

        GtkWidget               *cancel_button;
        GtkWidget               *close_button;
        GtkWidget               *make_another_button;

        gboolean                 confirm_cancel;
        gboolean                 make_another_button_visible;

        guint                    spin_id;
};

G_DEFINE_TYPE (NcbProgressDialog, ncb_progress_dialog, GTK_TYPE_DIALOG)

static int                      progress_jar_position = 0;
static GdkPixbuf               *empty_jar_pixbuf;
static GdkPixbuf               *full_jar_pixbuf;

static void
ncb_progress_dialog_set_property (GObject            *object,
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
ncb_progress_dialog_get_property (GObject            *object,
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

static void
ncb_progress_dialog_update_icon (NcbProgressDialog *dialog,
                                 double             fraction)
{
        GdkPixbuf *pixbuf;
        int        position;

        position = gdk_pixbuf_get_height (empty_jar_pixbuf) * (1 - fraction);

        if (position == progress_jar_position) {
                return;
        }

        progress_jar_position = position;

        pixbuf = gdk_pixbuf_copy (empty_jar_pixbuf);
        gdk_pixbuf_copy_area (full_jar_pixbuf,
                              0, position,
                              gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf) - position,
                              pixbuf,
                              0, position);

        gtk_window_set_icon (GTK_WINDOW (dialog), pixbuf);
        g_object_unref (pixbuf);
}

void
ncb_progress_dialog_set_fraction (NcbProgressDialog *dialog,
                                  double             fraction)
{
        GtkWidget *progress_bar;

        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (dialog));

        progress_bar = glade_xml_get_widget (dialog->priv->xml, "cd_progress");

        gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar), fraction);
        ncb_progress_dialog_update_icon (dialog, fraction);
}

/* Adapted from totem_time_to_string_text */
static char *
time_to_string_text (long time)
{
        char *secs, *mins, *hours, *string;
        int sec, min, hour;

        sec = time % 60;
        time = time - sec;
        min = (time % (60 * 60)) / 60;
        time = time - (min * 60);
        hour = time / (60 * 60);

        hours = g_strdup_printf (ngettext ("%d hour", "%d hours", hour), hour);

        mins = g_strdup_printf (ngettext ("%d minute",
                                          "%d minutes", min), min);

        secs = g_strdup_printf (ngettext ("%d second",
                                          "%d seconds", sec), sec);

        if (hour > 0) {
                /* hour:minutes:seconds */
                string = g_strdup_printf (_("%s %s %s"), hours, mins, secs);
        } else if (min > 0) {
                /* minutes:seconds */
                string = g_strdup_printf (_("%s %s"), mins, secs);
        } else if (sec > 0) {
                /* seconds */
                string = g_strdup_printf (_("%s"), secs);
        } else {
                /* 0 seconds */
                string = g_strdup (_("0 seconds"));
        }

        g_free (hours);
        g_free (mins);
        g_free (secs);

        return string;
}

void
ncb_progress_dialog_set_time_remaining (NcbProgressDialog *dialog,
                                        long               secs)
{
        GtkWidget *progress_bar;
        char      *text;

        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (dialog));

        progress_bar = glade_xml_get_widget (dialog->priv->xml, "cd_progress");

        if (secs >= 0) {
                char *remaining;
                remaining = time_to_string_text (secs);
                text = g_strdup_printf (_("About %s left"), remaining);
                g_free (remaining);
        } else {
                text = g_strdup (" ");
        }

        gtk_progress_bar_set_text (GTK_PROGRESS_BAR (progress_bar), text);

        g_free (text);
}

void
ncb_progress_dialog_set_description (NcbProgressDialog *dialog,
                                     const char        *text)
{
        GtkWidget *label;

        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (dialog));

        label = glade_xml_get_widget (dialog->priv->xml, "progress_description_label");
        gtk_label_set_text (GTK_LABEL (label), text);
}

void
ncb_progress_dialog_set_heading (NcbProgressDialog *dialog,
                                 const char        *text)
{
        GtkWidget *heading;
        char      *markup;

        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (dialog));

        heading = glade_xml_get_widget (dialog->priv->xml, "info_title_label");

        markup = g_strdup_printf ("<big><b>%s</b></big>", text);
        gtk_label_set_markup (GTK_LABEL (heading), markup);
        g_free (markup);
}

void
ncb_progress_dialog_set_operation_string (NcbProgressDialog *dialog,
                                          const char        *text)
{
        GtkWidget *progress_label;
        char      *string;

        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (dialog));

        progress_label = glade_xml_get_widget (dialog->priv->xml, "cd_progress_label");
        string = g_strdup_printf ("<i>%s</i>", text);
        gtk_label_set_markup (GTK_LABEL (progress_label), string);
        g_free (string);
}

static gboolean
ncb_progress_dialog_set_image (NcbProgressDialog *dialog)
{
        GtkWidget *image;
        GdkPixbuf *pixbuf;

        if (dialog->priv->image_list == NULL) {
                return FALSE;
        }

        image = glade_xml_get_widget (dialog->priv->xml, "cd_image");
        pixbuf = dialog->priv->image_list->data;

        if (pixbuf == NULL || image == NULL) {
                return FALSE;
        }

        gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);

        if (dialog->priv->image_list->next != NULL) {
                dialog->priv->image_list = dialog->priv->image_list->next;
        } else {
                dialog->priv->image_list = g_list_first (dialog->priv->image_list);
        }

        return TRUE;
}

void
ncb_progress_dialog_set_active (NcbProgressDialog *dialog,
                                gboolean           active)
{
        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (dialog));

        if (active) {
                if (dialog->priv->spin_id == 0) {
                        dialog->priv->spin_id = g_timeout_add (100, (GSourceFunc) ncb_progress_dialog_set_image, dialog);
                }
        } else {
                if (dialog->priv->spin_id != 0) {
                        g_source_remove (dialog->priv->spin_id);
                        dialog->priv->spin_id = 0;
                }
        }
}

static void
ncb_progress_dialog_image_setup (NcbProgressDialog *dialog)
{
        GdkPixbuf *pixbuf;
        char      *filename;
        int        i;

        if (dialog->priv->image_list != NULL) {
                return;
        }

        i = 1;

        /* Setup the pixbuf list */
        filename = g_strdup_printf (DATADIR "/cdspin%d.png", i);
        while (g_file_test (filename, G_FILE_TEST_IS_REGULAR)) {
                pixbuf = gdk_pixbuf_new_from_file (filename, NULL);

                if (pixbuf != NULL) {
                        dialog->priv->image_list = g_list_prepend (dialog->priv->image_list, (gpointer) pixbuf);
                }

                i++;
                g_free (filename);
                filename = g_strdup_printf (DATADIR "/cdspin%d.png", i);
        }

        g_free (filename);
        if (dialog->priv->image_list != NULL) {
                dialog->priv->image_list = g_list_reverse (dialog->priv->image_list);
        }

        /* Set the first image */
        ncb_progress_dialog_set_image (dialog);
}

static void
ncb_progress_dialog_image_cleanup (NcbProgressDialog *dialog)
{
        GdkPixbuf *pixbuf;

        dialog->priv->image_list = g_list_first (dialog->priv->image_list);

        while (dialog->priv->image_list != NULL) {
                pixbuf = (GdkPixbuf *) dialog->priv->image_list->data;
                if (pixbuf != NULL) {
                        gdk_pixbuf_unref (pixbuf);
                }
                dialog->priv->image_list = g_list_remove (dialog->priv->image_list, dialog->priv->image_list->data);
        }
}

static void
dialog_sync_buttons (NcbProgressDialog *dialog,
                     gboolean           finished)
{
        if (finished) {
                gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL, FALSE);
                gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE, TRUE);
                gtk_widget_show (dialog->priv->close_button);
                gtk_widget_hide (dialog->priv->cancel_button);

                if (dialog->priv->make_another_button_visible) {
                        gtk_widget_show (dialog->priv->make_another_button);
                } else {
                        gtk_widget_hide (dialog->priv->make_another_button);
                }

                gtk_dialog_set_default_response (GTK_DIALOG (dialog),
                                                 GTK_RESPONSE_CLOSE);

        } else {
                gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL, TRUE);
                gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE, FALSE);
                gtk_widget_hide (dialog->priv->close_button);
                gtk_widget_show (dialog->priv->cancel_button);

                gtk_widget_hide (dialog->priv->make_another_button);
        }
}

void
ncb_progress_dialog_set_make_another_button_visible (NcbProgressDialog *dialog,
                                                     gboolean           visible)
{
        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (dialog));

        dialog->priv->make_another_button_visible = visible;

        dialog_sync_buttons (dialog, FALSE);
}

void
ncb_progress_dialog_clear (NcbProgressDialog *dialog)
{
        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (dialog));

        dialog_sync_buttons (dialog, FALSE);

        ncb_progress_dialog_set_fraction (dialog, 0.0);
        ncb_progress_dialog_set_time_remaining (dialog, -1);
        ncb_progress_dialog_set_operation_string (dialog, "");
}

void
ncb_progress_dialog_done (NcbProgressDialog *dialog)
{
        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (dialog));

        ncb_progress_dialog_set_fraction (dialog, 1.0);

        dialog_sync_buttons (dialog, TRUE);

        ncb_progress_dialog_set_operation_string (dialog, "");
}

static void
ncb_progress_dialog_class_init (NcbProgressDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize     = ncb_progress_dialog_finalize;
        object_class->get_property = ncb_progress_dialog_get_property;
        object_class->set_property = ncb_progress_dialog_set_property;

        g_type_class_add_private (klass, sizeof (NcbProgressDialogPrivate));

        /* Load the jar pixbufs */
        empty_jar_pixbuf = gdk_pixbuf_new_from_inline (-1, progress_jar_empty_icon, FALSE, NULL);
        full_jar_pixbuf = gdk_pixbuf_new_from_inline (-1, progress_jar_full_icon, FALSE, NULL);
}

static void
ncb_progress_dialog_init (NcbProgressDialog *dialog)
{
        GtkWidget *vbox;

        dialog->priv = NCB_PROGRESS_DIALOG_GET_PRIVATE (dialog);

        dialog->priv->xml = glade_xml_new (DATADIR "/nautilus-cd-burner.glade", "progress_dialog_vbox", NULL);

        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

        vbox = glade_xml_get_widget (dialog->priv->xml, "progress_dialog_vbox");
        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
vbox,
                            TRUE, TRUE, 0);
        gtk_widget_show_all (vbox);

        dialog->priv->make_another_button_visible = TRUE;
        dialog->priv->make_another_button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Make Another Copy"), GTK_RESPONSE_ACCEPT);
        dialog->priv->cancel_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
        dialog->priv->close_button = gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);

        dialog_sync_buttons (dialog, FALSE);

        /* Set window icon */
        gtk_window_set_icon (GTK_WINDOW (dialog), empty_jar_pixbuf);

        /* Set progress jar position */
        progress_jar_position = gdk_pixbuf_get_height (empty_jar_pixbuf);

        gtk_dialog_set_default_response (GTK_DIALOG (dialog),
                                         GTK_RESPONSE_CLOSE);

        ncb_progress_dialog_image_setup (dialog);
}

static void
ncb_progress_dialog_finalize (GObject *object)
{
        NcbProgressDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (NCB_IS_PROGRESS_DIALOG (object));

        dialog = NCB_PROGRESS_DIALOG (object);

        g_return_if_fail (dialog->priv != NULL);

        if (dialog->priv->spin_id != 0) {
                g_source_remove (dialog->priv->spin_id);
                dialog->priv->spin_id = 0;
        }

        ncb_progress_dialog_image_cleanup (dialog);

        G_OBJECT_CLASS (ncb_progress_dialog_parent_class)->finalize (object);
}

GtkWidget *
ncb_progress_dialog_new (void)
{
        GObject *object;

        object = g_object_new (NCB_TYPE_PROGRESS_DIALOG, NULL);

        return GTK_WIDGET (object);
}
