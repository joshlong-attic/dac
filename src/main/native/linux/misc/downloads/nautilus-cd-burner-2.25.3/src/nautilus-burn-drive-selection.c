/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nautilus-burn-drive-selection.c
 *
 * Copyright (C) 2002-2004 Bastien Nocera <hadess@hadess.net>
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
 *          William Jon McCann <mccann@jhu.edu>
 *
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "nautilus-burn-drive-selection.h"
#include "nautilus-burn-drive-monitor.h"

/* Signals */
enum {
        DEVICE_CHANGED,
        DRIVE_CHANGED,
        LAST_SIGNAL
};

/* Arguments */
enum {
        PROP_0,
        PROP_DEVICE,
        PROP_DRIVE,
        PROP_FILE_IMAGE,
        PROP_RECORDERS_ONLY,
};

enum {
        DISPLAY_NAME_COLUMN,
        DRIVE_COLUMN,
        N_COLUMNS
};

#define NAUTILUS_BURN_DRIVE_SELECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NAUTILUS_BURN_TYPE_DRIVE_SELECTION, NautilusBurnDriveSelectionPrivate))

struct NautilusBurnDriveSelectionPrivate {
        NautilusBurnDriveMonitor *monitor;
        gboolean                  have_file_image;
        gboolean                  show_recorders_only;

        NautilusBurnDrive        *selected_drive;
};

static void nautilus_burn_drive_selection_init         (NautilusBurnDriveSelection *selection);

static void nautilus_burn_drive_selection_set_property (GObject      *object,
                                                        guint         property_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void nautilus_burn_drive_selection_get_property (GObject      *object,
                                                        guint         property_id,
                                                        GValue       *value,
                                                        GParamSpec   *pspec);

static void nautilus_burn_drive_selection_finalize     (GObject      *object);

static int nautilus_burn_drive_selection_table_signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (NautilusBurnDriveSelection, nautilus_burn_drive_selection, GTK_TYPE_COMBO_BOX);

static void
nautilus_burn_drive_selection_class_init (NautilusBurnDriveSelectionClass *klass)
{
        GObjectClass *object_class;
        GtkWidgetClass *widget_class;

        object_class = (GObjectClass *) klass;
        widget_class = (GtkWidgetClass *) klass;

        /* GObject */
        object_class->set_property = nautilus_burn_drive_selection_set_property;
        object_class->get_property = nautilus_burn_drive_selection_get_property;
        object_class->finalize = nautilus_burn_drive_selection_finalize;

        g_type_class_add_private (klass, sizeof (NautilusBurnDriveSelectionPrivate));

        /* Properties */
        g_object_class_install_property (object_class,
                                         PROP_DRIVE,
                                         g_param_spec_object ("drive",
                                                              _("Drive"),
                                                              NULL,
                                                              NAUTILUS_BURN_TYPE_DRIVE,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class, PROP_DEVICE,
                                         g_param_spec_string ("device", NULL, NULL,
                                                              NULL, G_PARAM_READWRITE));
        g_object_class_install_property (object_class, PROP_FILE_IMAGE,
                                         g_param_spec_boolean ("file-image", NULL, NULL,
                                                               FALSE, G_PARAM_READWRITE));
        g_object_class_install_property (object_class, PROP_RECORDERS_ONLY,
                                         g_param_spec_boolean ("show-recorders-only", NULL, NULL,
                                                               FALSE, G_PARAM_READWRITE));

        /* Signals */
        nautilus_burn_drive_selection_table_signals [DEVICE_CHANGED] =
                g_signal_new ("device-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnDriveSelectionClass,
                                               device_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE, 1, G_TYPE_STRING);
        nautilus_burn_drive_selection_table_signals [DRIVE_CHANGED] =
                g_signal_new ("drive-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnDriveSelectionClass,
                                               drive_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__OBJECT,
                              G_TYPE_NONE, 1,
                              NAUTILUS_BURN_TYPE_DRIVE);
}

static void
nautilus_burn_drive_selection_set_drive_internal (NautilusBurnDriveSelection *selection,
                                                  NautilusBurnDrive          *drive)
{
        selection->priv->selected_drive = nautilus_burn_drive_ref (drive);

        g_signal_emit (G_OBJECT (selection),
                       nautilus_burn_drive_selection_table_signals [DRIVE_CHANGED],
                       0, drive);
        g_signal_emit (G_OBJECT (selection),
                       nautilus_burn_drive_selection_table_signals [DEVICE_CHANGED],
                       0, nautilus_burn_drive_get_device (drive));

        g_object_notify (G_OBJECT (selection), "device");
        g_object_notify (G_OBJECT (selection), "drive");
}

static void
combo_changed (GtkComboBox                *combo,
               NautilusBurnDriveSelection *selection)
{
        NautilusBurnDrive *drive;
        GtkTreeModel      *model;
        GtkTreeIter        iter;

        if (! gtk_combo_box_get_active_iter (GTK_COMBO_BOX (selection), &iter)) {
                return;
        }

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (selection));
        gtk_tree_model_get (model, &iter, DRIVE_COLUMN, &drive, -1);

        if (drive == NULL) {
                return;
        }

        nautilus_burn_drive_selection_set_drive_internal (selection, drive);
}

static void
selection_update_sensitivity (NautilusBurnDriveSelection *selection)
{
        GtkTreeModel *model;
        int           num_drives;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (selection));
        num_drives = gtk_tree_model_iter_n_children (model, NULL);

        gtk_widget_set_sensitive (GTK_WIDGET (selection), (num_drives > 0));
}

static gboolean
get_iter_for_drive (NautilusBurnDriveSelection *selection,
                    NautilusBurnDrive          *drive,
                    GtkTreeIter                *iter)
{
        GtkTreeModel      *model;
        gboolean           found;

        found = FALSE;

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (selection));
        if (! gtk_tree_model_get_iter_first (model, iter)) {
                goto out;
        }

        do {
                NautilusBurnDrive *drive2;

                gtk_tree_model_get (model, iter, DRIVE_COLUMN, &drive2, -1);

                if (nautilus_burn_drive_equal (drive, drive2)) {
                        found = TRUE;
                        break;
                }

        } while (gtk_tree_model_iter_next (model, iter));
 out:
        return found;
}

static void
selection_append_drive (NautilusBurnDriveSelection *selection,
                        NautilusBurnDrive          *drive)
{
        char         *display_name;
        GtkTreeIter   iter;
        GtkTreeModel *model;

        display_name = nautilus_burn_drive_get_name_for_display (drive);

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (selection));
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            DISPLAY_NAME_COLUMN, display_name ? display_name : _("Unnamed CD/DVD Drive"),
                            DRIVE_COLUMN, drive,
                            -1);

        g_free (display_name);
}

static void
selection_remove_drive (NautilusBurnDriveSelection *selection,
                        NautilusBurnDrive          *drive)
{
        gboolean      found;
        GtkTreeIter   iter;
        GtkTreeModel *model;

        found = get_iter_for_drive (selection, drive, &iter);
        if (! found) {
                return;
        }

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (selection));
        gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

        if (selection->priv->selected_drive != NULL
            && nautilus_burn_drive_equal (drive, selection->priv->selected_drive)) {
                if (gtk_tree_model_get_iter_first (model, &iter)) {
                        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (selection), &iter);
                }
        }
}

static void
populate_model (NautilusBurnDriveSelection *selection,
                GtkListStore               *store)
{
        GList                    *drives;
        NautilusBurnDrive        *drive;
        NautilusBurnDriveMonitor *monitor;

        monitor = nautilus_burn_get_drive_monitor ();
        if (selection->priv->show_recorders_only) {
                drives = nautilus_burn_drive_monitor_get_recorder_drives (monitor);
        } else {
                drives = nautilus_burn_drive_monitor_get_drives (monitor);
        }

        while (drives != NULL) {
                drive = drives->data;

                selection_append_drive (selection, drive);

                if (drive != NULL) {
                        nautilus_burn_drive_unref (drive);
                }
                drives = g_list_delete_link (drives, drives);
        }

        if (selection->priv->have_file_image) {
                drive = nautilus_burn_drive_monitor_get_drive_for_image (selection->priv->monitor);
                selection_append_drive (selection, drive);
                nautilus_burn_drive_unref (drive);
        }

        gtk_combo_box_set_active (GTK_COMBO_BOX (selection), 0);
}

static void
drive_connected_cb (NautilusBurnDriveMonitor   *monitor,
                    NautilusBurnDrive          *drive,
                    NautilusBurnDriveSelection *selection)
{
        selection_append_drive (selection, drive);

        selection_update_sensitivity (selection);
}

static void
drive_disconnected_cb (NautilusBurnDriveMonitor   *monitor,
                       NautilusBurnDrive          *drive,
                       NautilusBurnDriveSelection *selection)
{
        selection_remove_drive (selection, drive);

        selection_update_sensitivity (selection);
}

static void
nautilus_burn_drive_selection_init (NautilusBurnDriveSelection *selection)
{
        GtkCellRenderer *cell;
        GtkListStore    *store;

        selection->priv = NAUTILUS_BURN_DRIVE_SELECTION_GET_PRIVATE (selection);

        selection->priv->monitor = nautilus_burn_get_drive_monitor ();

        g_signal_connect (selection->priv->monitor, "drive-connected", G_CALLBACK (drive_connected_cb), selection);
        g_signal_connect (selection->priv->monitor, "drive-disconnected", G_CALLBACK (drive_disconnected_cb), selection);

        store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, NAUTILUS_BURN_TYPE_DRIVE);
        gtk_combo_box_set_model (GTK_COMBO_BOX (selection),
                                 GTK_TREE_MODEL (store));

        cell = gtk_cell_renderer_text_new ();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (selection), cell, TRUE);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (selection), cell,
                                        "text", DISPLAY_NAME_COLUMN,
                                        NULL);

        populate_model (selection, store);

        selection_update_sensitivity (selection);

        g_signal_connect (G_OBJECT (selection), "changed",
                          G_CALLBACK (combo_changed), selection);

}

static void
nautilus_burn_drive_selection_finalize (GObject *object)
{
        NautilusBurnDriveSelection *selection = (NautilusBurnDriveSelection *) object;

        g_return_if_fail (selection != NULL);
        g_return_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (selection));

        g_signal_handlers_disconnect_by_func (selection->priv->monitor, G_CALLBACK (drive_connected_cb), selection);
        g_signal_handlers_disconnect_by_func (selection->priv->monitor, G_CALLBACK (drive_disconnected_cb), selection);

        if (selection->priv->selected_drive != NULL) {
                nautilus_burn_drive_unref (selection->priv->selected_drive);
        }

        if (G_OBJECT_CLASS (nautilus_burn_drive_selection_parent_class)->finalize != NULL) {
                (* G_OBJECT_CLASS (nautilus_burn_drive_selection_parent_class)->finalize) (object);
        }
}

/**
 * nautilus_burn_drive_selection_new:
 *
 * Create a new drive selector.
 *
 * Return value: Newly allocated #NautilusBurnDriveSelection widget
 **/
GtkWidget *
nautilus_burn_drive_selection_new (void)
{
        GtkWidget *widget;

        widget = GTK_WIDGET
                (g_object_new (NAUTILUS_BURN_TYPE_DRIVE_SELECTION, NULL));

        return widget;
}

static void
repopulate_model (NautilusBurnDriveSelection *selection)
{
        GtkTreeModel *model;

        /* block the combo changed signal handler until we're done */
        g_signal_handlers_block_by_func (G_OBJECT (selection),
                                         combo_changed, selection);

        model = gtk_combo_box_get_model (GTK_COMBO_BOX (selection));
        gtk_list_store_clear (GTK_LIST_STORE (model));
        populate_model (selection, GTK_LIST_STORE (model));

        g_signal_handlers_unblock_by_func (G_OBJECT (selection),
                                           combo_changed, selection);

        /* Force a signal out */
        combo_changed (GTK_COMBO_BOX (selection), (gpointer) selection);
}

static void
nautilus_burn_drive_selection_set_have_file_image (NautilusBurnDriveSelection *selection,
                                                   gboolean                    have_file_image)
{
        g_return_if_fail (selection != NULL);
        g_return_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (selection));

        if (selection->priv->have_file_image == have_file_image) {
                return;
        }

        selection->priv->have_file_image = have_file_image;

        repopulate_model (selection);
        selection_update_sensitivity (selection);
}

static void
nautilus_burn_drive_selection_set_recorders_only (NautilusBurnDriveSelection *selection,
                                                  gboolean                    recorders_only)
{
        g_return_if_fail (selection != NULL);
        g_return_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (selection));

        if (selection->priv->show_recorders_only == recorders_only) {
                return;
        }

        selection->priv->show_recorders_only = recorders_only;

        repopulate_model (selection);
        selection_update_sensitivity (selection);
}

static void
nautilus_burn_drive_selection_set_property (GObject      *object,
                                            guint         property_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
        NautilusBurnDriveSelection *selection;

        g_return_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (object));

        selection = NAUTILUS_BURN_DRIVE_SELECTION (object);

        switch (property_id) {
        case PROP_DRIVE:
                nautilus_burn_drive_selection_set_active (selection, g_value_get_object (value));
                break;
        case PROP_DEVICE:
                nautilus_burn_drive_selection_set_device (selection, g_value_get_string (value));
                break;
        case PROP_FILE_IMAGE:
                nautilus_burn_drive_selection_set_have_file_image (selection,
                                                                   g_value_get_boolean (value));
                break;
        case PROP_RECORDERS_ONLY:
                nautilus_burn_drive_selection_set_recorders_only (selection,
                                                                  g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

static void
nautilus_burn_drive_selection_get_property (GObject    *object,
                                            guint       property_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
        NautilusBurnDriveSelection *selection;

        g_return_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (object));

        selection = NAUTILUS_BURN_DRIVE_SELECTION (object);

        switch (property_id) {
        case PROP_DRIVE:
                g_value_set_object (value, selection->priv->selected_drive);
                break;
        case PROP_DEVICE:
                g_value_set_string (value, nautilus_burn_drive_selection_get_device (selection));
                break;
        case PROP_FILE_IMAGE:
                g_value_set_boolean (value, selection->priv->have_file_image);
                break;
        case PROP_RECORDERS_ONLY:
                g_value_set_boolean (value, selection->priv->show_recorders_only);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
}

/**
 * nautilus_burn_drive_selection_set_active:
 * @selection: #NautilusBurnDriveSelection
 * @drive: #NautilusBurnDrive
 *
 * Set the current selected drive to that which corresponds to the
 * specified drive.
 *
 * Since: 2.14
 *
 **/
void
nautilus_burn_drive_selection_set_active (NautilusBurnDriveSelection *selection,
                                          NautilusBurnDrive          *drive)
{
        GtkTreeIter        iter;
        gboolean           found;

        g_return_if_fail (selection != NULL);
        g_return_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (selection));

        found = get_iter_for_drive (selection, drive, &iter);
        if (! found) {
                return;
        }

        gtk_combo_box_set_active_iter (GTK_COMBO_BOX (selection), &iter);
}

/**
 * nautilus_burn_drive_selection_get_active:
 * @selection: #NautilusBurnDriveSelection
 *
 * Get the currently selected drive
 *
 * Return value: currently selected #NautilusBurnDrive.  The drive must be
 * unreffed using nautilus_burn_drive_unref after use.
 *
 * Since: 2.14
 *
 **/
NautilusBurnDrive *
nautilus_burn_drive_selection_get_active (NautilusBurnDriveSelection *selection)
{
        g_return_val_if_fail (selection != NULL, NULL);
        g_return_val_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (selection), NULL);

        if (selection->priv->selected_drive != NULL) {
                nautilus_burn_drive_ref (selection->priv->selected_drive);
        }

        return selection->priv->selected_drive;
}

/* Deprecated functions */

/**
 * nautilus_burn_drive_selection_get_default_device:
 * @selection: #NautilusBurnDriveSelection
 *
 * Get the block device name that corresponds to the default drive.
 *
 * Deprecated:
 *
 **/
const char *
nautilus_burn_drive_selection_get_default_device (NautilusBurnDriveSelection *selection)
{
        GList      *drives;
        const char *device;

        g_return_val_if_fail (selection != NULL, "/dev/cdrom");
        g_return_val_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (selection), "/dev/cdrom");

        drives = nautilus_burn_drive_monitor_get_drives (selection->priv->monitor);
        if (drives == NULL) {
                device = "/dev/cdrom";
        } else {
                device = nautilus_burn_drive_get_device (drives->data);
                g_list_foreach (drives, (GFunc)nautilus_burn_drive_unref, NULL);
                g_list_free (drives);
        }

        return device;
}

/**
 * nautilus_burn_drive_selection_set_device:
 * @selection: #NautilusBurnDriveSelection
 * @device: block device name
 *
 * Set the current selected drive to that which corresponds to the
 * specified block device name.
 *
 * Deprecated: Use nautilus_burn_drive_selection_set_active() instead
 *
 **/
void
nautilus_burn_drive_selection_set_device (NautilusBurnDriveSelection *selection,
                                          const char                 *device)
{
        NautilusBurnDrive *drive;

        drive = nautilus_burn_drive_monitor_get_drive_for_device (selection->priv->monitor, device);
        nautilus_burn_drive_selection_set_active (selection, drive);
        nautilus_burn_drive_unref (drive);
}

/**
 * nautilus_burn_drive_selection_get_device:
 * @selection: #NautilusBurnDriveSelection
 *
 * Get the block device name that corresponds to the currently
 * selected drive
 *
 * Return value: String block device name that corresponds to the
 * currently selected drive
 *
 * Deprecated: Use nautilus_burn_drive_selection_get_active() instead
 *
 **/
const char *
nautilus_burn_drive_selection_get_device (NautilusBurnDriveSelection *selection)
{
        g_return_val_if_fail (selection != NULL, NULL);
        g_return_val_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (selection), NULL);

        return selection->priv->selected_drive != NULL ? nautilus_burn_drive_get_device (selection->priv->selected_drive) : NULL;
}

/**
 * nautilus_burn_drive_selection_get_drive:
 * @selection: #NautilusBurnDriveSelection
 *
 * Get the currently selected drive
 *
 * Return value: currently selected #NautilusBurnDrive
 *
 * Deprecated: Use nautilus_burn_drive_selection_get_active() instead
 *
 **/
const NautilusBurnDrive *
nautilus_burn_drive_selection_get_drive (NautilusBurnDriveSelection *selection)
{
        g_return_val_if_fail (selection != NULL, NULL);
        g_return_val_if_fail (NAUTILUS_BURN_IS_DRIVE_SELECTION (selection), NULL);

        return selection->priv->selected_drive;
}
