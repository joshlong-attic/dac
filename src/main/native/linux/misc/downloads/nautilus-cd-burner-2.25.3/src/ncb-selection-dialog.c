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
#include <gtk/gtk.h>

#include <glade/glade.h>
#include <gconf/gconf-client.h>

#include "nautilus-burn-drive.h"
#include "nautilus-burn-drive-selection.h"
#include "make-iso.h"

#include "ncb-selection.h"
#include "ncb-selection-dialog.h"

static void     ncb_selection_dialog_class_init (NcbSelectionDialogClass *klass);
static void     ncb_selection_dialog_init       (NcbSelectionDialog      *fade);
static void     ncb_selection_dialog_finalize   (GObject                 *object);

#define NCB_SELECTION_DIALOG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NCB_TYPE_SELECTION_DIALOG, NcbSelectionDialogPrivate))

#define MAX_ISO_NAME_LEN  32

struct NcbSelectionDialogPrivate
{
        GladeXML                *xml;
        GtkWidget               *write_button;
        NcbSelection            *selection;
        guint                    update_selection_idle_id;
};

G_DEFINE_TYPE (NcbSelectionDialog, ncb_selection_dialog, GTK_TYPE_DIALOG)

static void
ncb_selection_dialog_set_property (GObject            *object,
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
ncb_selection_dialog_get_property (GObject            *object,
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

static void
ncb_selection_dialog_set_size (NcbSelectionDialog *dialog,
                               guint64             size)
{
        GtkWidget *label;
        char      *str;

        g_return_if_fail (dialog != NULL);

        label = glade_xml_get_widget (dialog->priv->xml, "size_label");

        str = size_to_string_text (size);
        gtk_label_set_markup (GTK_LABEL (label), str);
        g_free (str);
}

static void
ncb_selection_dialog_set_label (NcbSelectionDialog *dialog,
                                const char         *label)
{
        NcbSelectionSource type;
        GtkWidget         *entry;
        gboolean           sensitive;

        entry = glade_xml_get_widget (dialog->priv->xml, "cdname_entry");

        if (label != NULL) {
                gtk_entry_set_text (GTK_ENTRY (entry), label);
        }

        ncb_selection_get_source (dialog->priv->selection, &type, NULL);
        sensitive = (type == NCB_SELECTION_SOURCE_BURN_FOLDER);

        gtk_editable_set_editable (GTK_EDITABLE (entry), sensitive);
        gtk_widget_set_sensitive (entry, sensitive);
}

static void
update_source (NcbSelectionDialog *dialog)
{
        char              *name;
        gboolean           is_copy;
        NcbSelectionSource type;

        is_copy = FALSE;
        ncb_selection_get_source (dialog->priv->selection, &type, &name);

        if (type == NCB_SELECTION_SOURCE_DEVICE) {
                is_copy = TRUE;
        }

        if (is_copy) {
                GtkWidget *widget;

                gtk_window_set_title (GTK_WINDOW (dialog), _("Copy Disc"));
                widget = glade_xml_get_widget (dialog->priv->xml, "target_label");
                gtk_label_set_text_with_mnemonic (GTK_LABEL (widget), _("Copy disc _to:"));
                /* Translators: This is "Copy" as in the action to copy the disc onto another one */
                gtk_button_set_label (GTK_BUTTON (dialog->priv->write_button), _("_Copy"));
        } else {
                gtk_window_set_title (GTK_WINDOW (dialog), _("Write to Disc"));
        }
}

static void
update_source_size (NcbSelectionDialog *dialog)
{
        guint64            size;

        gtk_widget_set_sensitive (GTK_WIDGET (dialog), TRUE);
        gdk_window_set_cursor (GTK_WIDGET (dialog)->window, NULL);

        ncb_selection_get_size (dialog->priv->selection, &size);
        ncb_selection_dialog_set_size (dialog, size);
}

static void
update_label (NcbSelectionDialog *dialog)
{
        char              *label;

        ncb_selection_get_label (dialog->priv->selection, &label);
        ncb_selection_dialog_set_label (dialog, label);
}

static gboolean
update_dialog_selection (NcbSelectionDialog *dialog)
{
        update_source (dialog);
        update_source_size (dialog);
        update_label (dialog);

        dialog->priv->update_selection_idle_id = 0;

        return FALSE;
}

void
ncb_selection_dialog_set_selection (NcbSelectionDialog      *dialog,
                                    NcbSelection            *selection)
{
        g_return_if_fail (dialog != NULL);

        if (dialog->priv->selection != NULL) {
                g_object_unref (dialog->priv->selection);
        }

        if (selection != NULL) {
                dialog->priv->selection = g_object_ref (selection);
        }

        /* FIXME: reconnect notify signals */

        if (dialog->priv->update_selection_idle_id > 0) {
                g_source_remove (dialog->priv->update_selection_idle_id);
        }
        dialog->priv->update_selection_idle_id = g_idle_add ((GSourceFunc)update_dialog_selection, dialog);
}

NcbSelection *
ncb_selection_dialog_get_selection (NcbSelectionDialog *dialog)
{
        g_return_val_if_fail (dialog != NULL, NULL);

        return g_object_ref (dialog->priv->selection);
}

static void
label_entry_insert_text (GtkEditable        *editable,
                         const char         *new_text,
                         int                 new_text_length,
                         int                *position,
                         NcbSelectionDialog *dialog)
{
        char *current_text;

        if (new_text_length < 0) {
                new_text_length = strlen (new_text);
        }

        current_text = gtk_editable_get_chars (editable, 0, -1);
        if (strlen (current_text) +  new_text_length >= MAX_ISO_NAME_LEN) {
                gdk_display_beep (gtk_widget_get_display (GTK_WIDGET (editable)));
                g_signal_stop_emission_by_name (editable, "insert_text");
        }

        g_free (current_text);
}

static void
label_entry_changed (GtkEditable        *editable,
                     NcbSelectionDialog *dialog)
{
        char *text;

        /* sync with selection */
        text = gtk_editable_get_chars (editable, 0, -1);
        ncb_selection_set_label (dialog->priv->selection, text);
        g_free (text);
}

static void
label_entry_activated (GtkEntry           *editable,
		       NcbSelectionDialog *dialog)
{
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

static void
ncb_selection_dialog_set_drive (NcbSelectionDialog *dialog,
                                NautilusBurnDrive  *drive)
{
        GtkWidget *selection;
        selection = glade_xml_get_widget (dialog->priv->xml, "target_optionmenu");

        nautilus_burn_drive_selection_set_active (NAUTILUS_BURN_DRIVE_SELECTION (selection), drive);
}

static void
ncb_selection_dialog_get_drive (NcbSelectionDialog *dialog,
                                NautilusBurnDrive **drive)
{
        GtkWidget *selection;

        if (drive != NULL) {
                *drive = NULL;
        }

        selection = glade_xml_get_widget (dialog->priv->xml, "target_optionmenu");

        if (! selection) {
                return;
        }

        /* the drive is reffed */
        if (drive != NULL) {
                *drive = nautilus_burn_drive_selection_get_active (NAUTILUS_BURN_DRIVE_SELECTION (selection));
        }
}

static void
ncb_selection_dialog_class_init (NcbSelectionDialogClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize     = ncb_selection_dialog_finalize;
        object_class->get_property = ncb_selection_dialog_get_property;
        object_class->set_property = ncb_selection_dialog_set_property;

        g_type_class_add_private (klass, sizeof (NcbSelectionDialogPrivate));
}

static void
update_speeds_combobox (NautilusBurnDrive *drive,
                        GtkWidget         *combobox)
{
        const int               *write_speeds;
        int                      i;
        GtkTreeModel            *model;
        GtkTreeIter              iter;
        NautilusBurnMediaType    type;
        gboolean                 is_rewritable;
        gboolean                 is_blank;
        int                      default_speed;
        int                      default_speed_index = 0;
        GConfClient             *gc;

        gc = gconf_client_get_default ();
        default_speed = gconf_client_get_int (gc, "/apps/nautilus-cd-burner/default_speed", NULL);
        g_object_unref (G_OBJECT (gc));

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combobox));

        gtk_list_store_clear (GTK_LIST_STORE (model));

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, _("Maximum possible"),
                            1, 0,
                            -1);

        write_speeds = nautilus_burn_drive_get_write_speeds (drive);
        type = nautilus_burn_drive_get_media_type_full (drive,
                                                        &is_rewritable,
                                                        &is_blank,
                                                        NULL, NULL);

        for (i = 0; write_speeds[i] > 0; i++) {
                char *name;

                name = NULL;

                if (write_speeds[i] == default_speed) {
                        default_speed_index = i + 1;
                }

                if (is_rewritable || is_blank) {
                        switch (type) {
                        case NAUTILUS_BURN_MEDIA_TYPE_CDR:
                        case NAUTILUS_BURN_MEDIA_TYPE_CDRW:
                                name = g_strdup_printf ("%.1f\303\227",
                                                        NAUTILUS_BURN_DRIVE_CD_SPEED (write_speeds[i]));
                                break;
                        case NAUTILUS_BURN_MEDIA_TYPE_DVDR:
                        case NAUTILUS_BURN_MEDIA_TYPE_DVDRW:
                        case NAUTILUS_BURN_MEDIA_TYPE_DVD_RAM:
                        case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R:
                        case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW:
                        case NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL:
                                name = g_strdup_printf ("%.1f\303\227",
                                                        NAUTILUS_BURN_DRIVE_DVD_SPEED (write_speeds[i]));
                                break;
                        default:
                                break;
                        }

                } else {
                        name = g_strdup_printf ("CD: %.1f\303\227  DVD: %.1f\303\227",
                                                NAUTILUS_BURN_DRIVE_CD_SPEED (write_speeds[i]),
                                                NAUTILUS_BURN_DRIVE_DVD_SPEED (write_speeds[i]));
                }

                /* If we can't get the media type see if the drive is capable DVD */
                if (name == NULL) {
                        gboolean is_dvd;
                        int      type;

                        type = nautilus_burn_drive_get_drive_type (drive);

                        is_dvd = (type & NAUTILUS_BURN_DRIVE_TYPE_DVD_RW_RECORDER)
                                || (type & NAUTILUS_BURN_DRIVE_TYPE_DVD_RAM_RECORDER);
                        if (is_dvd) {
                                name = g_strdup_printf ("CD %.1f\303\227, DVD %.1f\303\227",
                                                        NAUTILUS_BURN_DRIVE_CD_SPEED (write_speeds[i]),
                                                        NAUTILUS_BURN_DRIVE_DVD_SPEED (write_speeds[i]));
                        } else {
                                name = g_strdup_printf ("%.1f\303\227",
                                                        NAUTILUS_BURN_DRIVE_CD_SPEED (write_speeds[i]));
                        }
                }

                gtk_list_store_append (GTK_LIST_STORE (model), &iter);
                gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                                    0, name,
                                    1, write_speeds [i],
                                    -1);
                g_free (name);
        }

        /* Disable speed if no items in list */
        gtk_widget_set_sensitive (combobox, i > 0);

        gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), default_speed_index);
}

static void
dialog_media_added_cb (NautilusBurnDrive *drive,
                       gpointer           data)
{
        update_speeds_combobox (drive, GTK_WIDGET (data));
}

static void
refresh_dialog (NcbSelectionDialog *dialog)
{
        GtkWidget               *combobox;
        NautilusBurnDrive       *drive;
        GtkTreeModel            *model;

        /* Find active recorder: */
        ncb_selection_dialog_get_drive (dialog, &drive);

        /* add speed items: */
        combobox = glade_xml_get_widget (dialog->priv->xml, "speed_combobox");
        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combobox));

        g_signal_connect (drive, "media-added", G_CALLBACK (dialog_media_added_cb), combobox);

        if (drive) {
                int type;

                type = nautilus_burn_drive_get_drive_type (drive);

                if (type == NAUTILUS_BURN_DRIVE_TYPE_FILE) {
                        gtk_list_store_clear (GTK_LIST_STORE (model));
                        gtk_widget_set_sensitive (combobox, FALSE);
                } else {
                        update_speeds_combobox (drive, combobox);
                }

                nautilus_burn_drive_unref (drive);
        }
}

static void
setup_speed_combobox (GtkWidget *combobox)
{
        GtkCellRenderer *cell;
        GtkListStore    *store;

        store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);

        gtk_combo_box_set_model (GTK_COMBO_BOX (combobox),
                                 GTK_TREE_MODEL (store));
        g_object_unref (store);

        cell = gtk_cell_renderer_text_new ();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), cell, TRUE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), cell,
                                        "text", 0,
                                        NULL);
}

static void
sync_drive_selection (NcbSelectionDialog *dialog)
{
        NautilusBurnDrive *drive;
        GtkWidget         *combobox;

        combobox = glade_xml_get_widget (dialog->priv->xml, "target_optionmenu");

        /* sync with selection */
        drive = nautilus_burn_drive_selection_get_active (NAUTILUS_BURN_DRIVE_SELECTION (combobox));
        ncb_selection_set_drive (dialog->priv->selection, drive);
        g_object_unref (drive);
}

static void
target_changed (NautilusBurnDriveSelection *drive_selection,
                const char                 *device_path,
                NcbSelectionDialog         *dialog)
{
        refresh_dialog (dialog);
        sync_drive_selection (dialog);
}

static void
ncb_selection_dialog_get_speed (NcbSelectionDialog *dialog,
                                int                *speed)
{
        GtkTreeModel *model;
        GtkTreeIter   iter;
        GtkWidget    *combobox;

        if (speed != NULL) {
                *speed = 0;
        }

        combobox = glade_xml_get_widget (dialog->priv->xml, "speed_combobox");
        if (! gtk_combo_box_get_active_iter (GTK_COMBO_BOX (combobox), &iter)) {
                return;
        }

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (combobox));
        gtk_tree_model_get (model, &iter, 1, speed, -1);
}

static void
speed_changed (GtkComboBox        *widget,
               NcbSelectionDialog *dialog)
{
        int speed;

        ncb_selection_dialog_get_speed (dialog, &speed);
        ncb_selection_set_speed (dialog->priv->selection, speed);
}

static void
init_dialog (NcbSelectionDialog *dialog)
{
        GtkWidget *label;
        GtkWidget *drive_combobox;
        GtkWidget *speed_combobox;
        GtkWidget *dummy_check;

        /* Fill in targets */
        drive_combobox = glade_xml_get_widget (dialog->priv->xml, "target_optionmenu");

        gtk_widget_show (drive_combobox);
        g_object_set (drive_combobox, "file-image", TRUE, NULL);
        g_object_set (drive_combobox, "show-recorders-only", TRUE, NULL);
        g_signal_connect (drive_combobox, "device-changed", G_CALLBACK (target_changed), dialog);

        speed_combobox = glade_xml_get_widget (dialog->priv->xml, "speed_combobox");
        g_signal_connect (speed_combobox, "changed", G_CALLBACK (speed_changed), dialog);
        setup_speed_combobox (speed_combobox);

        ncb_selection_dialog_set_label (dialog, "");

        label = glade_xml_get_widget (dialog->priv->xml, "size_label");
        gtk_label_set_markup (GTK_LABEL (label), _("Calculating..."));

        dummy_check = glade_xml_get_widget (dialog->priv->xml, "dummy_check");
        gtk_widget_hide (dummy_check);

        refresh_dialog (dialog);
        sync_drive_selection (dialog);
}

static void
selection_source_type_changed (NcbSelection       *selection,
                               GParamSpec         *pspec,
                               NcbSelectionDialog *dialog)
{
        update_source (dialog);
}

static void
selection_source_size_changed (NcbSelection       *selection,
                               GParamSpec         *pspec,
                               NcbSelectionDialog *dialog)
{
        update_source_size (dialog);
}

static void
selection_label_changed (NcbSelection       *selection,
                         GParamSpec         *pspec,
                         NcbSelectionDialog *dialog)
{
        update_label (dialog);
}

static void
selection_drive_changed (NcbSelection       *selection,
                         GParamSpec         *pspec,
                         NcbSelectionDialog *dialog)
{
        NautilusBurnDrive *drive;

        drive = ncb_selection_peek_drive (selection);
        ncb_selection_dialog_set_drive (dialog, drive);
}

static void
ncb_selection_dialog_init (NcbSelectionDialog *dialog)
{
        GtkWidget *vbox;
        GtkWidget *entry;

        dialog->priv = NCB_SELECTION_DIALOG_GET_PRIVATE (dialog);

        dialog->priv->selection = ncb_selection_new ();
        g_signal_connect (dialog->priv->selection, "notify::source-type",
                          G_CALLBACK (selection_source_type_changed),
                          dialog);
        g_signal_connect (dialog->priv->selection, "notify::source-size",
                          G_CALLBACK (selection_source_size_changed),
                          dialog);
        g_signal_connect (dialog->priv->selection, "notify::drive",
                          G_CALLBACK (selection_drive_changed),
                          dialog);
        g_signal_connect (dialog->priv->selection, "notify::label",
                          G_CALLBACK (selection_label_changed),
                          dialog);

        dialog->priv->xml = glade_xml_new (DATADIR "/nautilus-cd-burner.glade", "selection_dialog_vbox", NULL);

        gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

        vbox = glade_xml_get_widget (dialog->priv->xml, "selection_dialog_vbox");
        gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
                            vbox,
                            TRUE, TRUE, 0);
        gtk_widget_show_all (vbox);

        gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_HELP, GTK_RESPONSE_HELP);
        gtk_dialog_add_button (GTK_DIALOG (dialog), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
        dialog->priv->write_button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Write"), GTK_RESPONSE_OK);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);

        init_dialog (dialog);

        gtk_window_set_icon_name (GTK_WINDOW (dialog), "nautilus-cd-burner");

        /* make insensitive until fill_dialog finishes */
        gtk_widget_set_sensitive (GTK_WIDGET (dialog), FALSE);

        entry = glade_xml_get_widget (dialog->priv->xml, "cdname_entry");
        g_signal_connect (entry, "insert_text", G_CALLBACK (label_entry_insert_text), dialog);
        g_signal_connect (entry, "changed", G_CALLBACK (label_entry_changed), dialog);
        g_signal_connect (entry, "activate", G_CALLBACK (label_entry_activated), dialog);
}

static void
ncb_selection_dialog_finalize (GObject *object)
{
        NcbSelectionDialog *dialog;

        g_return_if_fail (object != NULL);
        g_return_if_fail (NCB_IS_SELECTION_DIALOG (object));

        dialog = NCB_SELECTION_DIALOG (object);

        g_return_if_fail (dialog->priv != NULL);

        G_OBJECT_CLASS (ncb_selection_dialog_parent_class)->finalize (object);
}

GtkWidget *
ncb_selection_dialog_new (void)
{
        GObject *object;

        object = g_object_new (NCB_TYPE_SELECTION_DIALOG, NULL);

        return GTK_WIDGET (object);
}
