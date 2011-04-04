/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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
 * Authors: William Jon McCann <mccann@jhu.edu>
 */

#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gdk/gdk.h>

#include "nautilus-burn-process.h"

GQuark
nautilus_burn_process_error_quark (void)
{
        static GQuark quark = 0;
        if (! quark)
                quark = g_quark_from_static_string ("nautilus_burn_process_error");

        return quark;
}

NautilusBurnProcess *
nautilus_burn_process_new (void)
{
        NautilusBurnProcess      *process;
        NautilusBurnProcessFuncs *funcs;

        process = g_new0 (NautilusBurnProcess, 1);
        funcs = g_new0 (NautilusBurnProcessFuncs, 1);
        process->funcs = funcs;

        return process;
}

void
nautilus_burn_process_free (NautilusBurnProcess *process)
{
        if (! process) {
                return;
        }

        g_free (process->last_error);

        if (process->line) {
                g_string_free (process->line, TRUE);
        }

        if (process->line_stderr) {
                g_string_free (process->line_stderr, TRUE);
        }

        g_list_free (process->rates);

        g_free (process->funcs);
        g_free (process);
}

gboolean
nautilus_burn_process_set_error (NautilusBurnProcess *process,
                                 const char          *message,
                                 int                  code)
{
        /* don't clobber existing errors */
        if (process->last_error != NULL) {
                return FALSE;
        }

        g_free (process->last_error);
        process->last_error = g_strdup (message);
        process->result = code;

        return TRUE;
}

gboolean
nautilus_burn_process_get_error (NautilusBurnProcess *process,
                                 char               **message,
                                 int                 *code)
{
        g_return_val_if_fail (process != NULL, FALSE);

        if (process->last_error == NULL) {
                return FALSE;
        }

        if (message != NULL) {
                *message = g_strdup (process->last_error);
        }

        if (code != NULL) {
                *code = process->result;
        }

        return TRUE;
}
gboolean
nautilus_burn_process_cancel (NautilusBurnProcess *process,
                              gboolean             skip_if_dangerous)
{
        g_return_val_if_fail (process != NULL, FALSE);

        if (! process->dangerous || ! skip_if_dangerous) {
                if (process->pid > 0) {
                        kill (process->pid, SIGINT);
                }

                if (process->loop != NULL && g_main_loop_is_running (process->loop)) {
                        /*g_main_loop_quit (process->loop);*/
                }

                return TRUE;
        }

        return FALSE;
}

static gboolean
start_async_with_watch (char        **args,
                        GPid         *ppid,
                        GIOFunc       out_watch_func,
                        GIOFunc       err_watch_func,
                        gpointer      user_data,
                        guint        *out_watch_id,
                        guint        *err_watch_id,
                        int          *input_pipe,
                        GError      **error)
{
        gboolean    ret;
        int         stdin_pipe;
        int         stdout_pipe;
        int         stderr_pipe;
        GPid        pid = 0;

        g_return_val_if_fail (args != NULL, FALSE);

        ret = g_spawn_async_with_pipes (NULL,
                                        args,
                                        NULL,
                                        G_SPAWN_SEARCH_PATH,
                                        NULL,
                                        NULL,
                                        &pid,
                                        &stdin_pipe,
                                        &stdout_pipe,
                                        &stderr_pipe,
                                        error);

        if (!ret) {
                return FALSE;
        }

        if (ppid) {
                *ppid = pid;
        }

        if (input_pipe) {
                *input_pipe = stdin_pipe;
        }

        fcntl (stdout_pipe, F_SETFL, O_NONBLOCK);
        fcntl (stderr_pipe, F_SETFL, O_NONBLOCK);

        if (out_watch_func) {
                GIOChannel *channel;
                guint       id;

                channel = g_io_channel_unix_new (stdout_pipe);
                g_io_channel_set_flags (channel,
                                        g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                        NULL);
                g_io_channel_set_encoding (channel, NULL, NULL);

                id = g_io_add_watch (channel,
                                     G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                     out_watch_func,
                                     user_data);

                g_io_channel_unref (channel);

                if (out_watch_id) {
                        *out_watch_id = id;
                }
        } else {
                if (out_watch_id) {
                        *out_watch_id = -1;
                }
        }

        if (err_watch_func) {
                GIOChannel *channel;
                guint       id;

                channel = g_io_channel_unix_new (stderr_pipe);
                g_io_channel_set_flags (channel,
                                        g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                        NULL);
                g_io_channel_set_encoding (channel, NULL, NULL);

                id = g_io_add_watch (channel,
                                     G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                     err_watch_func,
                                     user_data);

                g_io_channel_unref (channel);

                if (err_watch_id) {
                        *err_watch_id = id;
                }
        } else {
                if (err_watch_id) {
                        *err_watch_id = -1;
                }
        }

        return ret;
}

typedef struct
{
        NautilusBurnProcess *process;
        gpointer             user_data;
} ProcessCallbackData;

static gboolean
nautilus_burn_process_stdout_read (GIOChannel   *source,
                                   GIOCondition  condition,
                                   gpointer      data)
{
        ProcessCallbackData   *callback_data = data;
        NautilusBurnProcess   *process;
        gboolean               res      = TRUE;

        process = callback_data->process;

        if (condition & G_IO_IN) {
                char     *line;
                char      buf [1];
                GIOStatus status;

                status = g_io_channel_read_line (source,
                                                 &line, NULL, NULL, NULL);

                if (status == G_IO_STATUS_NORMAL) {
                        if (process->line) {
                                g_string_append (process->line, line);
                                g_free (line);
                                line = g_string_free (process->line, FALSE);
                                process->line = NULL;
                        }

                        if (process->funcs->out_line) {
                                res = process->funcs->out_line (process, line, callback_data->user_data);
                        }

                        g_free (line);
                } else if (status == G_IO_STATUS_AGAIN) {
                        /* A non-terminated line was read, read the data into the buffer. */
                        status = g_io_channel_read_chars (source, buf, 1, NULL, NULL);

                        if (status == G_IO_STATUS_NORMAL) {
                                char *line2;

                                if (process->line == NULL) {
                                        process->line = g_string_new (NULL);
                                }
                                g_string_append_c (process->line, buf [0]);

                                switch (buf [0]) {
                                case '\n':
                                case '\r':
                                case '\xe2':
                                case '\0':
                                        line2 = g_string_free (process->line, FALSE);
                                        process->line = NULL;

                                        if (process->funcs->out_line) {
                                                res = process->funcs->out_line (process, line2, callback_data->user_data);
                                        }

                                        g_free (line2);
                                        break;
                                default:
                                        break;
                                }
                        }
                } else if (status == G_IO_STATUS_EOF) {
                        if (process->debug) {
                                g_print ("process stdout: EOF\n");
                        }
                        if (process->loop != NULL && g_main_loop_is_running (process->loop)) {
                                g_main_loop_quit (process->loop);
                        }

                        res = FALSE;
                }
        } else if (condition & G_IO_HUP) {
                /* only handle the HUP when we have read all available lines of output */
                if (process->debug) {
                        g_print ("process stdout: HUP\n");
                }

                /* FIXME: use atomic */
                process->hup_refcount--;

                if (process->loop != NULL && g_main_loop_is_running (process->loop)) {
                        if (process->hup_refcount <= 0) {
                                g_main_loop_quit (process->loop);
                        }
                }

                res = FALSE;
        }

        return res;
}

static gboolean
nautilus_burn_process_stderr_read (GIOChannel   *source,
                                   GIOCondition  condition,
                                   gpointer      data)
{
        ProcessCallbackData   *callback_data = data;
        NautilusBurnProcess   *process;
        gboolean               res      = TRUE;

        process = callback_data->process;

        if (condition & G_IO_IN) {
                char     *line;
                char      buf [1];
                GIOStatus status;

                status = g_io_channel_read_line (source,
                                                 &line, NULL, NULL, NULL);

                if (status == G_IO_STATUS_NORMAL) {
                        if (process->line_stderr) {
                                g_string_append (process->line_stderr, line);
                                g_free (line);
                                line = g_string_free (process->line_stderr, FALSE);
                                process->line_stderr = NULL;
                        }

                        if (process->funcs->err_line) {
                                res = process->funcs->err_line (process, line, callback_data->user_data);
                        }

                        g_free (line);
                } else if (status == G_IO_STATUS_AGAIN) {
                        /* A non-terminated line was read, read the data into the buffer. */
                        status = g_io_channel_read_chars (source, buf, 1, NULL, NULL);

                        if (status == G_IO_STATUS_NORMAL) {
                                char *line2;

                                if (process->line_stderr == NULL) {
                                        process->line_stderr = g_string_new (NULL);
                                }
                                g_string_append_c (process->line_stderr, buf [0]);

                                switch (buf [0]) {
                                case '\n':
                                case '\r':
                                case '\xe2':
                                case '\0':
                                        line2 = g_string_free (process->line_stderr, FALSE);
                                        process->line_stderr = NULL;

                                        if (process->funcs->err_line) {
                                                res = process->funcs->err_line (process, line2, callback_data->user_data);
                                        }

                                        g_free (line2);
                                        break;
                                default:
                                        break;
                                }
                        }
                } else if (status == G_IO_STATUS_EOF) {
                        if (process->debug) {
                                g_print ("process stderr: EOF\n");
                        }
                        if (process->loop != NULL && g_main_loop_is_running (process->loop)) {
                                g_main_loop_quit (process->loop);
                        }

                        res = FALSE;
                }
        } else if (condition & G_IO_HUP) {
                /* only handle the HUP when we have read all available lines of output */
                if (process->debug) {
                        g_print ("process stderr: HUP\n");
                }

                /* FIXME: use atomic */
                process->hup_refcount--;

                if (process->loop != NULL && g_main_loop_is_running (process->loop)) {
                        if (process->hup_refcount <= 0) {
                                g_main_loop_quit (process->loop);
                        }
                }

                res = FALSE;
        }

        return res;
}

int
nautilus_burn_process_run (NautilusBurnProcess        *process,
                           GPtrArray                  *argv,
                           NautilusBurnProcessLineFunc out_line_func,
                           NautilusBurnProcessLineFunc err_line_func,
                           gpointer                    user_data,
                           GError                    **error)
{
        int                  res;
        int                  result;
        guint                stdout_tag;
        guint                stderr_tag;
        GError              *local_error = NULL;
        ProcessCallbackData *callback_data;

        callback_data = g_new0 (ProcessCallbackData, 1);
        callback_data->process = process;
        callback_data->user_data = user_data;

        process->funcs->out_line = out_line_func;
        process->funcs->err_line = err_line_func;
        process->result = 0;

        if (process->debug) {
                guint i;
                g_print ("launching command: ");
                for (i = 0; i < argv->len - 1; i++) {
                        g_print ("%s ", (char *) g_ptr_array_index (argv, i));
                }
                g_print ("\n");
        }

        local_error = NULL;

        stdout_tag = 0;
        stderr_tag = 0;
        res = start_async_with_watch ((char **)argv->pdata,
                                      &process->pid,
                                      nautilus_burn_process_stdout_read,
                                      nautilus_burn_process_stderr_read,
                                      callback_data,
                                      &stdout_tag,
                                      &stderr_tag,
                                      &process->pipe_stdin,
                                      &local_error);

        /* Since we attach to both stderr and stdout */
        process->hup_refcount = 2;

        if (! res) {
                g_warning ("command failed: %s\n", local_error->message);
                g_set_error (error,
                             NAUTILUS_BURN_PROCESS_ERROR,
                             NAUTILUS_BURN_PROCESS_ERROR_GENERAL,
                             _("Could not run the necessary command: %s"),
                             local_error->message);
                g_error_free (local_error);

                if (stdout_tag > 0) {
                        g_source_remove (stdout_tag);
                }
                if (stderr_tag > 0) {
                        g_source_remove (stderr_tag);
                }
        } else {
                process->loop = g_main_loop_new (NULL, FALSE);

                process->dangerous = FALSE;

                GDK_THREADS_LEAVE ();
                g_main_loop_run (process->loop);
                GDK_THREADS_ENTER ();

                g_main_loop_unref (process->loop);

                if (stdout_tag > 0) {
                        g_source_remove (stdout_tag);
                }
                if (stderr_tag > 0) {
                        g_source_remove (stderr_tag);
                }

                if (process->last_error) {
                        g_set_error (error,
                                     NAUTILUS_BURN_PROCESS_ERROR,
                                     NAUTILUS_BURN_PROCESS_ERROR_GENERAL,
                                     process->last_error);
                }
        }

        g_free (callback_data);

        result = process->result;

        return result;
}
