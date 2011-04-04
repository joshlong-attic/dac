/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * make-iso.h: code to generate iso files
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
 *          William Jon McCann <mccann@jhu.edu>
 */

#ifndef NAUTILUS_BURN_ISO_H
#define NAUTILUS_BURN_ISO_H

#include <glib.h>
#include <glib-object.h>
#include <nautilus-burn-drive.h>

G_BEGIN_DECLS

#define NAUTILUS_BURN_ISO_ERROR nautilus_burn_iso_error_quark ()

GQuark nautilus_burn_iso_error_quark (void);

typedef enum {
        NAUTILUS_BURN_ISO_ERROR_INTERNAL,
        NAUTILUS_BURN_ISO_ERROR_GENERAL
} NautilusBurnIsoError;

typedef enum {
        NAUTILUS_BURN_IMAGE_TYPE_UNKNOWN,
        NAUTILUS_BURN_IMAGE_TYPE_ISO9660,
        NAUTILUS_BURN_IMAGE_TYPE_BINCUE
} NautilusBurnImageType;

typedef enum {
        NAUTILUS_BURN_IMAGE_CREATE_NONE         = 0,
        NAUTILUS_BURN_IMAGE_CREATE_DEBUG        = 1 << 0,
        NAUTILUS_BURN_IMAGE_CREATE_JOLIET       = 1 << 2,

        /* From mkisofs' man page:
         * UDF support is currently in alpha status and for
         * this reason, it is not possible to create UDF only images.
         *
         * UDF data structures are  currently  coupled  to
         * the Joliet structures, so there are many pitfalls with the current
         * implementation. There is no UID/GID support, there is no POSIX
         * permission support, there is no support for symlinks. */
        NAUTILUS_BURN_IMAGE_CREATE_UDF          = 1 << 3
} NautilusBurnImageCreateFlags;

typedef enum {
        NAUTILUS_BURN_ISO_RESULT_ERROR     = -1,
        NAUTILUS_BURN_ISO_RESULT_CANCEL    = -2,
        NAUTILUS_BURN_ISO_RESULT_FINISHED  = -3,
        NAUTILUS_BURN_ISO_RESULT_RETRY     = -4
} NautilusBurnIsoResult;

#define NAUTILUS_BURN_TYPE_ISO            (nautilus_burn_iso_get_type ())
#define NAUTILUS_BURN_ISO(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  NAUTILUS_BURN_TYPE_ISO, NautilusBurnIso))
#define NAUTILUS_BURN_ISO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),   NAUTILUS_BURN_TYPE_ISO, NautilusBurnIsoClass))
#define NAUTILUS_BURN_IS_ISO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  NAUTILUS_BURN_TYPE_ISO))
#define NAUTILUS_BURN_IS_ISO_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), NAUTILUS_BURN_TYPE_ISO))

typedef struct NautilusBurnIso                NautilusBurnIso;
typedef struct NautilusBurnIsoClass           NautilusBurnIsoClass;
typedef struct NautilusBurnIsoPrivate         NautilusBurnIsoPrivate;

struct NautilusBurnIso {
        GObject                 parent;
        NautilusBurnIsoPrivate *priv;
};

struct NautilusBurnIsoClass {
        GObjectClass parent_class;

        void     (* progress_changed)   (NautilusBurnIso       *iso,
                                         gdouble                fraction,
                                         long                   seconds);
        void     (* animation_changed)  (NautilusBurnIso       *iso,
                                         gboolean               spinning);
};

typedef struct _NautilusBurnIsoGraft NautilusBurnIsoGraft;
struct _NautilusBurnIsoGraft {
        GPtrArray *array;
        GList     *remove_files;
};

GType             nautilus_burn_iso_get_type        (void);

NautilusBurnIsoGraft * nautilus_burn_iso_graft_new      (const char           *uri);
void                   nautilus_burn_iso_graft_free     (NautilusBurnIsoGraft *graft,
                                                         gboolean              remove);
gboolean               nautilus_burn_iso_graft_get_info (NautilusBurnIsoGraft *graft,
                                                         guint64              *size,
                                                         gboolean             *use_joliet,
                                                         gboolean             *use_udf,
                                                         GError              **error);

NautilusBurnIso * nautilus_burn_iso_new             (void);

int               nautilus_burn_iso_make_from_graft (NautilusBurnIso             *iso,
                                                     NautilusBurnIsoGraft        *graft,
                                                     const char                  *filename,
                                                     const char                  *label,
                                                     NautilusBurnImageCreateFlags flags,
                                                     NautilusBurnImageType       *type,
                                                     GError                     **error);
int               nautilus_burn_iso_make            (NautilusBurnIso             *iso,
                                                     const char                  *filename,
                                                     const char                  *label,
                                                     NautilusBurnImageCreateFlags flags,
                                                     NautilusBurnImageType       *type,
                                                     GError                     **error);
int               nautilus_burn_iso_make_from_drive (NautilusBurnIso             *iso,
                                                     const char                  *filename,
                                                     NautilusBurnDrive           *drive,
                                                     NautilusBurnImageCreateFlags flags,
                                                     NautilusBurnImageType       *type,
                                                     char                       **toc_filename,
                                                     GError                     **error);
gboolean          nautilus_burn_iso_cancel          (NautilusBurnIso             *iso);
gboolean          nautilus_burn_iso_verify          (NautilusBurnIso             *iso,
                                                     const char                  *filename,
                                                     char                       **name,
                                                     GError                     **error);

G_END_DECLS

#endif
