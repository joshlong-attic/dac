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

#ifndef NCB_SELECTION_DIALOG_H
#define NCB_SELECTION_DIALOG_H

#include <glib-object.h>
#include <glib.h>

#include "nautilus-burn-drive.h"
#include "ncb-selection.h"

G_BEGIN_DECLS

typedef enum {
        NCB_SELECTION_DIALOG_SOURCE_BURN_FOLDER,
        NCB_SELECTION_DIALOG_SOURCE_DEVICE,
        NCB_SELECTION_DIALOG_SOURCE_CUE,
        NCB_SELECTION_DIALOG_SOURCE_ISO
} NcbSelectionDialogSource;

#define NCB_TYPE_SELECTION_DIALOG            (ncb_selection_dialog_get_type ())
#define NCB_SELECTION_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  NCB_TYPE_SELECTION_DIALOG, NcbSelectionDialog))
#define NCB_SELECTION_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  NCB_TYPE_SELECTION_DIALOG , NcbSelectionDialogClass))
#define NCB_IS_SELECTION_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  NCB_TYPE_SELECTION_DIALOG))
#define NCB_IS_SELECTION_DIALOG_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), NCB_TYPE_SELECTION_DIALOG))

typedef struct NcbSelectionDialog                   NcbSelectionDialog;
typedef struct NcbSelectionDialogClass              NcbSelectionDialogClass;
typedef struct NcbSelectionDialogPrivate            NcbSelectionDialogPrivate;

struct NcbSelectionDialog {
        GtkDialog                  parent;

        NcbSelectionDialogPrivate *priv;
};

struct NcbSelectionDialogClass {
        GtkDialogClass parent_class;
};

GType                 ncb_selection_dialog_get_type       (void);

GtkWidget *           ncb_selection_dialog_new            (void);
void                  ncb_selection_dialog_set_selection  (NcbSelectionDialog       *dialog,
                                                           NcbSelection             *selection);
NcbSelection *        ncb_selection_dialog_get_selection  (NcbSelectionDialog       *dialog);

G_END_DECLS

#endif
