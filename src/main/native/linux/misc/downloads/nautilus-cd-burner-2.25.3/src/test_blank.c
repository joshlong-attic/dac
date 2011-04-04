/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * test_blank.c
 *
 * Copyright (C) 2004 James Bowes <bowes@cs.dal.ca>
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
 * Authors: James Bowes <bowes@cs.dal.ca>
 *
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>

#include "nautilus-burn.h"

static void
action_changed_cb (NautilusBurnRecorder       *recorder,
                   NautilusBurnRecorderActions action,
                   NautilusBurnRecorderMedia   media)
{
        const char *text;

        text = NULL;

        switch (action) {

        case NAUTILUS_BURN_RECORDER_ACTION_PREPARING_WRITE:
                if (media == NAUTILUS_BURN_RECORDER_MEDIA_CD) {
                        text = "Preparing to write CD";
                } else {
                        text = "Preparing to write DVD";
                }
                break;
        case NAUTILUS_BURN_RECORDER_ACTION_WRITING:
                if (media == NAUTILUS_BURN_RECORDER_MEDIA_CD) {
                        text = "Writing CD";
                } else {
                        text = "Writing DVD";
                }
                break;
        case NAUTILUS_BURN_RECORDER_ACTION_FIXATING:
                if (media == NAUTILUS_BURN_RECORDER_MEDIA_CD) {
                        text = "Fixating CD";
                } else {
                        text = "Fixating DVD";
                }
                break;
        case NAUTILUS_BURN_RECORDER_ACTION_BLANKING:
                if (media == NAUTILUS_BURN_RECORDER_MEDIA_CD) {
                        text = "Erasing CD";
                } else {
                        text = "Erasing DVD";
                }
                break;
        default:
                g_warning ("Unhandled action in action_changed_cb");
        }

        g_message (text);
}

static void
progress_changed_cb (NautilusBurnRecorder *recorder,
                     gdouble               fraction,
                     long                  secs,
                     gpointer              data)
{
        g_message ("Progress: %f%% %ld sec remaining", fraction * 100.0, secs);
}

static void
blank_disc (const char                   *device,
            NautilusBurnRecorderBlankType type,
            gboolean                      debug)
{
        NautilusBurnRecorder          *recorder;
        NautilusBurnDrive             *drive;
        NautilusBurnRecorderBlankFlags flags = 0;
        GError                        *error = NULL;

        drive = nautilus_burn_drive_monitor_get_drive_for_device (nautilus_burn_get_drive_monitor (),
                                                                  device);

        if (! drive) {
                g_printf ("Device %s is not a CD/DVD drive, or was not found\n",
                          device);
                return;
        }

        recorder = nautilus_burn_recorder_new ();

        g_signal_connect (G_OBJECT (recorder),
                          "progress-changed",
                          G_CALLBACK (progress_changed_cb),
                          NULL);
        g_signal_connect (G_OBJECT (recorder),
                          "action-changed",
                          G_CALLBACK (action_changed_cb),
                          NULL);

        if (debug)
                flags |= NAUTILUS_BURN_RECORDER_BLANK_DEBUG;

        nautilus_burn_recorder_blank_disc (recorder,
                                           drive,
                                           type,
                                           flags,
                                           &error);
}

int
main (int argc, char **argv)
{
        char                         *device;
        NautilusBurnRecorderBlankType type;
        gboolean                      debug;

        g_type_init ();
        nautilus_burn_init ();

        type = NAUTILUS_BURN_RECORDER_BLANK_FAST;
        debug = FALSE;

        if (argc >= 2) {
                device = argv[1];

                if (argc >= 3) {
                        type = (g_str_equal (argv[2], "fast"))
                                ? NAUTILUS_BURN_RECORDER_BLANK_FAST
                                : NAUTILUS_BURN_RECORDER_BLANK_FULL;
                }

                if (argc >= 4) {
                        debug = g_str_equal (argv[3], "debug");
                }

                blank_disc (device, type, debug);
        } else {
                g_print ("usage: test_blank /dev/XXX [fast|full] [debug]\n");
        }

        nautilus_burn_shutdown ();

        return 0;
}
