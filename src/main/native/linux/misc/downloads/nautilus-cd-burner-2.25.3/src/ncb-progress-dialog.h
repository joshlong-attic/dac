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

#ifndef NCB_PROGRESS_DIALOG_H
#define NCB_PROGRESS_DIALOG_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NCB_TYPE_PROGRESS_DIALOG            (ncb_progress_dialog_get_type ())
#define NCB_PROGRESS_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  NCB_TYPE_PROGRESS_DIALOG, NcbProgressDialog))
#define NCB_PROGRESS_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  NCB_TYPE_PROGRESS_DIALOG , NcbProgressDialogClass))
#define NCB_IS_PROGRESS_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  NCB_TYPE_PROGRESS_DIALOG))
#define NCB_IS_PROGRESS_DIALOG_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), NCB_TYPE_PROGRESS_DIALOG))

typedef struct NcbProgressDialog                   NcbProgressDialog;
typedef struct NcbProgressDialogClass              NcbProgressDialogClass;
typedef struct NcbProgressDialogPrivate            NcbProgressDialogPrivate;

struct NcbProgressDialog {
        GtkDialog                  parent;

        NcbProgressDialogPrivate  *priv;
};

struct NcbProgressDialogClass {
        GtkDialogClass parent_class;
};

GType                 ncb_progress_dialog_get_type   (void);

GtkWidget *           ncb_progress_dialog_new        (void);

void                  ncb_progress_dialog_set_active           (NcbProgressDialog *dialog,
                                                                gboolean           active);
void                  ncb_progress_dialog_set_confirm_cancel   (NcbProgressDialog *dialog,
                                                                gboolean           confirm);
void                  ncb_progress_dialog_set_heading          (NcbProgressDialog *dialog,
                                                                const char        *string);
void                  ncb_progress_dialog_set_description      (NcbProgressDialog *dialog,
                                                                const char        *string);
void                  ncb_progress_dialog_set_operation_string (NcbProgressDialog *dialog,
                                                                const char        *operation_string);
void                  ncb_progress_dialog_set_fraction         (NcbProgressDialog *dialog,
                                                                gdouble            fraction);
void                  ncb_progress_dialog_set_time_remaining   (NcbProgressDialog *dialog,
                                                                long               secs);
void                  ncb_progress_dialog_set_make_another_button_visible (NcbProgressDialog *dialog,
                                                                           gboolean           visible);
void                  ncb_progress_dialog_clear                (NcbProgressDialog *dialog);
void                  ncb_progress_dialog_done                 (NcbProgressDialog *dialog);

G_END_DECLS

#endif
