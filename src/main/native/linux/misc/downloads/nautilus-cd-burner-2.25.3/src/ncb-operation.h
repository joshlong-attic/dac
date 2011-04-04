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

#ifndef NCB_OPERATION_H
#define NCB_OPERATION_H

#include <glib-object.h>
#include <glib.h>

#include "nautilus-burn-drive.h"
#include "ncb-selection.h"

G_BEGIN_DECLS

#define NCB_TYPE_OPERATION            (ncb_operation_get_type ())
#define NCB_OPERATION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  NCB_TYPE_OPERATION, NcbOperation))
#define NCB_OPERATION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  NCB_TYPE_OPERATION , NcbOperationClass))
#define NCB_IS_OPERATION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  NCB_TYPE_OPERATION))
#define NCB_IS_OPERATION_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), NCB_TYPE_OPERATION))

typedef struct NcbOperation                   NcbOperation;
typedef struct NcbOperationClass              NcbOperationClass;
typedef struct NcbOperationPrivate            NcbOperationPrivate;

struct NcbOperation {
        GObject              parent;

        NcbOperationPrivate *priv;
};

struct NcbOperationClass {
        GObjectClass parent_class;
};

typedef void  (* NcbOperationDoneFunc) (NcbOperation *operation,
                                        gpointer      data);

GType                 ncb_operation_get_type   (void);

NcbOperation *        ncb_operation_new           (void);
void                  ncb_operation_set_selection (NcbOperation        *operation,
                                                   NcbSelection        *selection);
void                  ncb_operation_do_async      (NcbOperation        *operation,
                                                   NcbOperationDoneFunc done_cb,
                                                   gpointer             data);
void                  ncb_operation_cancel        (NcbOperation        *operation);

G_END_DECLS

#endif
