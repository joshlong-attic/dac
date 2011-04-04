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

#ifndef NAUTILUS_BURN_PROCESS_H
#define NAUTILUS_BURN_PROCESS_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _NautilusBurnProcess      NautilusBurnProcess;
typedef struct _NautilusBurnProcessFuncs NautilusBurnProcessFuncs;

typedef gboolean (* NautilusBurnProcessLineFunc) (NautilusBurnProcess *process,
                                                 const char          *line,
                                                 gpointer             data);

struct _NautilusBurnProcessFuncs {
        gboolean (* out_line) (NautilusBurnProcess *process,
                               const char          *line,
                               gpointer             data);
        gboolean (* err_line) (NautilusBurnProcess *process,
                               const char          *line,
                               gpointer             data);
};

struct _NautilusBurnProcess {
        GMainLoop                *loop;
        GPid                      pid;

        int                       result;
        char                     *last_error;

        GString                  *line;
        GString                  *line_stderr;
        NautilusBurnProcessFuncs *funcs;

        int                       pipe_stdin;

        time_t                    start_time;
        gint64                    start_num;
        GList                    *rates;

        gboolean                  changed_text;
        gboolean                  expect_process_to_die;

        gboolean                  dangerous;
        gboolean                  debug;

        guint                     hup_refcount;
};

#define NAUTILUS_BURN_PROCESS_ERROR nautilus_burn_process_error_quark ()

GQuark nautilus_burn_process_error_quark (void);

typedef enum {
        NAUTILUS_BURN_PROCESS_ERROR_INTERNAL,
        NAUTILUS_BURN_PROCESS_ERROR_GENERAL
} NautilusBurnProcessError;

NautilusBurnProcess * nautilus_burn_process_new       (void);
void                  nautilus_burn_process_free      (NautilusBurnProcess        *process);
gboolean              nautilus_burn_process_cancel    (NautilusBurnProcess        *process,
                                                       gboolean                    skip_if_dangerous);
int                   nautilus_burn_process_run       (NautilusBurnProcess        *process,
                                                       GPtrArray                  *argv,
                                                       NautilusBurnProcessLineFunc out_line_func,
                                                       NautilusBurnProcessLineFunc err_line_func,
                                                       gpointer                    data,
                                                       GError                    **error);

gboolean              nautilus_burn_process_set_error (NautilusBurnProcess        *process,
                                                       const char                 *message,
                                                       int                         code);
gboolean              nautilus_burn_process_get_error (NautilusBurnProcess        *process,
                                                       char                      **message,
                                                       int                        *code);

G_END_DECLS

#endif
