/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nautilus-cd-burner.c: easy to use cd burner software
 *
 * Copyright (C) 2002-2004 Red Hat, Inc.
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
 *          Bastien Nocera <hadess@hadess.net>
 */

#include "config.h"
#include <libgnome/gnome-i18n.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "nautilus-burn-drive.h"
#include "nautilus-burn-recorder.h"

G_BEGIN_DECLS

/* UI callbacks: */
extern void nautilus_burn_progress_set_text           (const char *text);
extern void nautilus_burn_progress_set_fraction       (double      fraction);
extern void nautilus_burn_progress_set_time           (long        secs);
extern void nautilus_burn_progress_set_image_spinning (gboolean    spinning);
extern void nautilus_burn_progress_set_cancel_func    (CancelFunc  cancel_func,
                                                       gboolean    cancel_dangerous,
                                                       gpointer    user_data);
GtkWindow * nautilus_burn_progress_get_window         (void);

G_END_DECLS
