/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nautilus-burn-drive-selection.h
 *
 * Copyright (C) 2002-2004 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2005-2006 William Jon McCann <mccann@jhu.edu>
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
 * Authors: Bastien Nocera <hadess@hadess.net>
 *          William Jon McCann <mccann@jhu.edu>
 */

#ifndef NAUTILUS_BURN_DRIVE_SELECTION_H
#define NAUTILUS_BURN_DRIVE_SELECTION_H

#include <gtk/gtk.h>
#include <nautilus-burn-drive.h>

G_BEGIN_DECLS

#define NAUTILUS_BURN_TYPE_DRIVE_SELECTION              (nautilus_burn_drive_selection_get_type ())
#define NAUTILUS_BURN_DRIVE_SELECTION(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), NAUTILUS_BURN_TYPE_DRIVE_SELECTION, NautilusBurnDriveSelection))
#define NAUTILUS_BURN_DRIVE_SELECTION_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), NAUTILUS_BURN_TYPE_DRIVE_SELECTION, NautilusBurnDriveSelectionClass))
#define NAUTILUS_BURN_IS_DRIVE_SELECTION(obj)           (G_TYPE_CHECK_INSTANCE_TYPE (obj, NAUTILUS_BURN_TYPE_DRIVE_SELECTION))
#define NAUTILUS_BURN_IS_DRIVE_SELECTION_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), NAUTILUS_BURN_TYPE_DRIVE_SELECTION))

typedef struct NautilusBurnDriveSelectionPrivate NautilusBurnDriveSelectionPrivate;

typedef struct {
        GtkComboBox                        widget;
        NautilusBurnDriveSelectionPrivate *priv;
} NautilusBurnDriveSelection;

typedef struct {
        GtkComboBoxClass parent_class;

        void (* device_changed) (GtkWidget         *selection,
                                 const char        *device);
        void (* drive_changed)  (GtkWidget         *selection,
                                 NautilusBurnDrive *drive);
} NautilusBurnDriveSelectionClass;

GtkType                  nautilus_burn_drive_selection_get_type           (void);
GtkWidget               *nautilus_burn_drive_selection_new                (void);

void                     nautilus_burn_drive_selection_set_active         (NautilusBurnDriveSelection *selection,
                                                                           NautilusBurnDrive          *drive);
NautilusBurnDrive       *nautilus_burn_drive_selection_get_active         (NautilusBurnDriveSelection *selection);

/* Deprecated */

const NautilusBurnDrive *nautilus_burn_drive_selection_get_drive          (NautilusBurnDriveSelection *selection);
void                     nautilus_burn_drive_selection_set_device         (NautilusBurnDriveSelection *selection,
                                                                           const char                 *device);
const char              *nautilus_burn_drive_selection_get_device         (NautilusBurnDriveSelection *selection);
const char              *nautilus_burn_drive_selection_get_default_device (NautilusBurnDriveSelection *selection);

G_END_DECLS

#endif /* NAUTILUS_BURN_DRIVE_SELECTION_H */
