/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nautilus-cd-burner.c: easy to use cd burner software
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

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <libgnome/gnome-help.h>
#include <libgnomeui/gnome-ui-init.h>
#include <gconf/gconf-client.h>

#include "nautilus-burn.h"

#include "nautilus-cd-burner.h"
#include "ncb-selection-dialog.h"
#include "ncb-operation.h"

/* Profiling stuff adapted from gtkfilechooserdefault */
/* To use run:
 *  strace -ttt -f -o logfile.txt nautilus-cd-burner
 */

#undef PROFILE_NCB
#ifdef PROFILE_NCB

#define PROFILE_INDENT 4
static int profile_indent;

static void
profile_add_indent (int indent)
{
        profile_indent += indent;
        if (profile_indent < 0) {
                g_error ("You screwed up your indentation");
        }
}

static void
_ncb_profile_log (const char *func,
                  int         indent,
                  const char *msg1,
                  const char *msg2)
{
        char *str;

        if (indent < 0) {
                profile_add_indent (indent);
        }

        if (profile_indent == 0) {
                str = g_strdup_printf ("MARK: %s: %s %s %s", G_STRLOC, func, msg1 ? msg1 : "", msg2 ? msg2 : "");
        } else {
                str = g_strdup_printf ("MARK: %s: %*c %s %s %s", G_STRLOC, profile_indent - 1, ' ', func, msg1 ? msg1 : "", msg2 ? msg2 : "");
        }

        access (str, F_OK);

        g_free (str);

        if (indent > 0) {
                profile_add_indent (indent);
        }
}

#define profile_start(x, y) _ncb_profile_log (G_STRFUNC, PROFILE_INDENT, x, y)
#define profile_end(x, y)   _ncb_profile_log (G_STRFUNC, -PROFILE_INDENT, x, y)
#define profile_msg(x, y)   _ncb_profile_log (NULL, 0, x, y)
#else
#define profile_start(x, y)
#define profile_end(x, y)
#define profile_msg(x, y)
#endif

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
help_activate (GtkWindow *parent)
{
        GError *err = NULL;

        if (gnome_help_display_desktop (NULL, "user-guide", "user-guide.xml", "gosnautilus-475", &err) == FALSE) {
                char *msg;

                msg = g_strdup_printf (_("There was a problem displaying the help contents: %s."), err->message);
                ncb_hig_show_error_dialog (_("Cannot display help"),
                                           msg, parent);
                g_error_free (err);
                g_free (msg);
        }
}

static void
gconf_save_device (GConfClient *client,
                   const char  *device)
{
        const char *hostname;

        if (device == NULL) {
                return;
        }

        hostname = g_get_host_name ();

        if (hostname != NULL) {
                gboolean    res;
                char       *key;

                key = g_strdup_printf ("/apps/nautilus-cd-burner/%s/last_device", hostname);
                res = gconf_client_set_string  (client, key, device, NULL);
        }
}

static char *
gconf_load_device (GConfClient *client)
{
        const char *hostname;
        char       *device;

        device = NULL;

        hostname = g_get_host_name ();
        if (hostname != NULL) {
                char *key;

                key = g_strdup_printf ("/apps/nautilus-cd-burner/%s/last_device", hostname);
                device = gconf_client_get_string  (client, key, NULL);

                g_free (key);
        }

        return device;
}

static void
op_finished (NcbOperation *operation,
             gpointer      data)
{
        g_object_unref (operation);
        gtk_main_quit ();
}

static void
selection_dialog_response (GtkWidget *dialog,
                           int        response_id,
                           gpointer   data)
{

        if (response_id == GTK_RESPONSE_HELP) {
                help_activate (GTK_WINDOW (dialog));
                return;
        }

        if (response_id != GTK_RESPONSE_OK) {
                gtk_widget_destroy (dialog);
                gtk_main_quit ();
                return;
        }

        if (response_id == GTK_RESPONSE_OK) {
                NcbOperation            *operation;
                GConfClient             *gconf_client;
                NcbSelection            *selection;
                int                      speed;
                NautilusBurnDrive       *drive;

                selection = ncb_selection_dialog_get_selection (NCB_SELECTION_DIALOG (dialog));

                ncb_selection_get_speed (selection, &speed);
                drive = ncb_selection_peek_drive (selection);

                gtk_widget_hide (dialog);

                /* save selections */
                gconf_client = gconf_client_get_default ();
                gconf_client_set_int (gconf_client, "/apps/nautilus-cd-burner/default_speed", speed, NULL);
                gconf_save_device (gconf_client, nautilus_burn_drive_get_device (drive));
                g_object_unref (gconf_client);

                operation = ncb_operation_new ();
                ncb_operation_set_selection (operation, selection);
                g_object_unref (selection);

                ncb_operation_do_async (operation, op_finished, NULL);
        }
}

static char *
expand_path_input (const char *input)
{
        GFile *file;
        char *path;

        file = g_file_new_for_commandline_arg (input);
        path = g_file_get_path (file);
        g_object_unref (file);

        if (path == NULL) {
                g_warning ("Only local files are supported at this time");
        }

        return path;
}

int
main (int argc, char *argv[])
{
        GConfClient              *gc;
        GtkWidget                *dialog;
        NcbSelection             *selection;
        NcbSelectionDialogSource  source_type;
        char                     *source_name;
        char                     *last_device;
        static char              *source_device;
        static char              *source_iso;
        static char              *source_cue;
        GOptionContext           *context;
        GnomeProgram             *program;
        static const GOptionEntry options []  = {
                { "source-device", 0, 0, G_OPTION_ARG_FILENAME, &source_device,
                  N_("Use CD/DVD device as source instead of burn:///"), N_("DEVICE") },
                { "source-iso", 0, 0, G_OPTION_ARG_FILENAME, &source_iso,
                  N_("Use ISO image as source instead of burn:///"), N_("FILENAME") },
                { "source-cue", 0, 0, G_OPTION_ARG_FILENAME, &source_cue,
                  N_("Use CUE/TOC file as source instead of burn:///"), N_("FILENAME") },
                { NULL}
        };

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        context = g_option_context_new (NULL);

        g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);

        program = gnome_program_init ("nautilus-cd-burner", VERSION,
                                      LIBGNOMEUI_MODULE, argc, argv,
                                      GNOME_PARAM_GOPTION_CONTEXT, context,
                                      GNOME_PARAM_APP_DATADIR, SHAREDIR,
                                      GNOME_PARAM_NONE);

        nautilus_burn_init ();

        dialog = ncb_selection_dialog_new ();
        g_signal_connect (dialog, "response", G_CALLBACK (selection_dialog_response), NULL);
        gtk_widget_show (dialog);

        if (source_iso != NULL) {
                char *path;

                path = expand_path_input (source_iso);
                if (path == NULL) {
                        goto out;
                }

                source_type = NCB_SELECTION_DIALOG_SOURCE_ISO;
                source_name = path;
        } else if (source_cue != NULL) {
                char *path;

                path = expand_path_input (source_cue);
                if (path == NULL) {
                        goto out;
                }

                source_type = NCB_SELECTION_DIALOG_SOURCE_CUE;
                source_name = path;
        } else if (source_device != NULL) {
                source_type = NCB_SELECTION_DIALOG_SOURCE_DEVICE;
                source_name = g_strdup (source_device);
        } else {
                source_type = NCB_SELECTION_DIALOG_SOURCE_BURN_FOLDER;
                source_name = NULL;
        }

        selection = ncb_selection_dialog_get_selection (NCB_SELECTION_DIALOG (dialog));
        ncb_selection_set_source (selection, source_type, source_name);

        gc = gconf_client_get_default ();
        last_device = gconf_load_device (gc);
        if (last_device != NULL) {
                NautilusBurnDrive *drive;

                drive = nautilus_burn_drive_monitor_get_drive_for_device (nautilus_burn_get_drive_monitor (),
                                                                          last_device);
                ncb_selection_set_drive (selection, drive);
                if (drive != NULL) {
                        g_object_unref (drive);
                } else {
                        g_warning ("Drive not found for saved device: %s", last_device);
                }
                g_free (last_device);
        }

        g_object_unref (selection);

        g_object_unref (gc);

        g_free (source_name);

        gtk_main ();

 out:
        g_object_unref (program);
        nautilus_burn_shutdown ();

        return 0;
}
