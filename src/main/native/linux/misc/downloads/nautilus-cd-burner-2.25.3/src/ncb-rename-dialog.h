/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2006 Fabio Bonelli <fabiobonelli@libero.it>
 *
 *  ncb-rename-dialog: Provide a dialog to rename files when their
 *  name is invalid (eg. not encoded properly, too long, etc.).
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
 * Authors: Fabio Bonelli <fabiobonelli@libero.it> */

#ifndef NCB_RENAME_DIALOG_H
#define NCB_RENAME_DIALOG_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NCB_TYPE_RENAME_DIALOG            (ncb_rename_dialog_get_type ())
#define NCB_RENAME_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NCB_TYPE_RENAME_DIALOG,   \
                                           NcbRenameDialog))
#define NCB_RENAME_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NCB_TYPE_RENAME_DIALOG,    \
                                           NcbRenameDialogClass))
#define NCB_IS_RENAME_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  NCB_TYPE_RENAME_DIALOG))
#define NCB_IS_RENAME_DIALOG_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), NCB_TYPE_RENAME_DIALOG))

typedef struct NcbRenameDialog                   NcbRenameDialog;
typedef struct NcbRenameDialogClass              NcbRenameDialogClass;
typedef struct NcbRenameDialogPrivate            NcbRenameDialogPrivate;

struct NcbRenameDialog {
        GtkDialog                parent;

        NcbRenameDialogPrivate  *priv;
};

struct NcbRenameDialogClass {
        GtkDialogClass parent_class;
};

GType                     ncb_rename_dialog_get_type                    (void);

NcbRenameDialog         * ncb_rename_dialog_new                         (void);
gboolean                  ncb_rename_dialog_set_invalid_filenames       (NcbRenameDialog        *dialog,
                                                                         const char             *uri);
void                      ncb_rename_dialog_response_cb                 (NcbRenameDialog        *dialog,
                                                                         int                     id,
                                                                         gpointer                user_data);

G_END_DECLS

#endif /* ! NCB_RENAME_DIALOG_H */
