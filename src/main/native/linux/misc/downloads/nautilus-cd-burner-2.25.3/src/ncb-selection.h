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

#ifndef NCB_SELECTION_H
#define NCB_SELECTION_H

#include <glib-object.h>
#include <glib.h>

#include "nautilus-burn-drive.h"

G_BEGIN_DECLS

typedef enum {
        NCB_SELECTION_SOURCE_BURN_FOLDER,
        NCB_SELECTION_SOURCE_DEVICE,
        NCB_SELECTION_SOURCE_CUE,
        NCB_SELECTION_SOURCE_ISO
} NcbSelectionSource;

typedef enum {
        NCB_SELECTION_VERIFY_UNREADABLE_FILES,
        NCB_SELECTION_VERIFY_HAS_IMAGES,
        NCB_SELECTION_VERIFY_ONLY_IMAGES,
} NcbSelectionVerifyFlags;

#define NCB_TYPE_SELECTION            (ncb_selection_get_type ())
#define NCB_SELECTION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  NCB_TYPE_SELECTION, NcbSelection))
#define NCB_SELECTION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  NCB_TYPE_SELECTION , NcbSelectionClass))
#define NCB_IS_SELECTION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  NCB_TYPE_SELECTION))
#define NCB_IS_SELECTION_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), NCB_TYPE_SELECTION))

typedef struct NcbSelection                   NcbSelection;
typedef struct NcbSelectionClass              NcbSelectionClass;
typedef struct NcbSelectionPrivate            NcbSelectionPrivate;

struct NcbSelection {
        GObject                    parent;

        NcbSelectionPrivate *priv;
};

struct NcbSelectionClass {
        GObjectClass parent_class;
};

typedef enum {
        NCB_SELECTION_ERROR_GENERAL,
        NCB_SELECTION_ERROR_SOURCE_INVALID,
} NcbSelectionError;

#define NCB_SELECTION_ERROR ncb_selection_error_quark ()

GType                 ncb_selection_source_get_type (void) G_GNUC_CONST;
#define NCB_SELECTION_TYPE_SOURCE (ncb_selection_source_get_type())

GQuark                ncb_selection_error_quark (void);

GType                 ncb_selection_get_type   (void);

NcbSelection *        ncb_selection_new        (void);

void                  ncb_selection_set_source (NcbSelection       *selection,
                                                NcbSelectionSource  type,
                                                const char         *name);
void                  ncb_selection_set_drive  (NcbSelection       *selection,
                                                NautilusBurnDrive  *drive);
void                  ncb_selection_set_label   (NcbSelection       *selection,
                                                 const char         *label);
void                  ncb_selection_set_size    (NcbSelection       *selection,
                                                 guint64             size);
void                  ncb_selection_set_speed   (NcbSelection       *selection,
                                                 int                 speed);

void                  ncb_selection_get_source (NcbSelection       *selection,
                                                NcbSelectionSource *type,
                                                char              **name);
void                  ncb_selection_get_drive   (NcbSelection       *selection,
                                                 NautilusBurnDrive **drive);
NautilusBurnDrive *   ncb_selection_peek_drive  (NcbSelection       *selection);

void                  ncb_selection_get_label   (NcbSelection       *selection,
                                                 char              **text);
const char *          ncb_selection_peek_label  (NcbSelection       *selection);

void                  ncb_selection_get_speed   (NcbSelection       *selection,
                                                 int                *speed);

void                  ncb_selection_get_size    (NcbSelection       *selection,
                                                 guint64            *size);

G_END_DECLS

#endif
