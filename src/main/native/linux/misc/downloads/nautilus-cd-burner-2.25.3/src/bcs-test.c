/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2004 William Jon McCann <mccann@jhu.edu>
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
 *          Bastien Nocera <hadess@hadess.net>
 */

#include <gtk/gtk.h>

#include "nautilus-burn.h"

static GtkWidget *bcs_menu = NULL;

static void
on_file_image_checkbutton_toggled (GtkToggleButton *widget,
                                   gpointer         data)
{
        gboolean value = gtk_toggle_button_get_active (widget);
        g_message ("Setting file image value to %s", value ? "TRUE":"FALSE");
        g_object_set (bcs_menu, "file-image", value, NULL);
}

static void
on_show_recorders_only_checkbutton_toggled (GtkToggleButton *widget,
                                            gpointer         data)
{
        gboolean value = gtk_toggle_button_get_active (widget);
        g_message ("Setting show recorders value to %s", value ? "TRUE":"FALSE");
        g_object_set (bcs_menu, "show-recorders-only", value, NULL);
}

static void
drive_changed (NautilusBurnDriveSelection *selection,
               NautilusBurnDrive          *drive)
{
        char *name;

        name = nautilus_burn_drive_get_name_for_display (drive);
        g_message ("The drive selection changed to device: %s name: %s",
                   nautilus_burn_drive_get_device (drive),
                   name);

        g_free (name);
}

int
main (int argc, char **argv)
{
        GtkWidget *dialog;
        GtkWidget *widget;
        GtkWidget *vbox;

        gtk_init (&argc, &argv);

        nautilus_burn_init ();

        dialog = gtk_window_new (GTK_WINDOW_TOPLEVEL);

        g_signal_connect (dialog, "delete-event",
                          G_CALLBACK (gtk_main_quit), NULL);

        vbox = gtk_vbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (dialog), vbox);

        bcs_menu = nautilus_burn_drive_selection_new ();

        g_signal_connect (bcs_menu, "drive-changed",
                          G_CALLBACK (drive_changed), NULL);

        gtk_box_pack_start_defaults (GTK_BOX (vbox), bcs_menu);

        widget = gtk_check_button_new_with_label ("Show only recorders");
        g_signal_connect (widget, "toggled",
                        G_CALLBACK (on_show_recorders_only_checkbutton_toggled), NULL);
        gtk_box_pack_start_defaults (GTK_BOX (vbox), widget);
        gtk_widget_set_sensitive (widget, g_object_class_find_property (G_OBJECT_GET_CLASS (bcs_menu), "show-recorders-only") != NULL);

        widget = gtk_check_button_new_with_label ("Show file image");
        g_signal_connect (widget, "toggled",
                          G_CALLBACK (on_file_image_checkbutton_toggled), NULL);
        gtk_box_pack_start_defaults (GTK_BOX (vbox), widget);

        gtk_widget_show_all (dialog);
        gtk_main ();

        nautilus_burn_shutdown ();

        return 0;
}
