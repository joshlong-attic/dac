/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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
 */

#include "config.h"

#include <glib.h>

#include "nautilus-burn-init.h"
#include "nautilus-burn-drive-monitor.h"
#include "nautilus-burn-drive-monitor-private.h"

static volatile gint burn_init_ref_count = 0;

/**
 * nautilus_burn_init:
 *
 * If nautilus-burn is not already initialized, initialize it. This must be
 * called prior to performing any other nautilus-burn operations, and may
 * be called multiple times without error.
 *
 * Return value: %TRUE if nautilus-burn is successfully initialized (or was
 * already initialized).
 */
gboolean
nautilus_burn_init (void)
{
        gint old_val;

        g_return_val_if_fail (burn_init_ref_count >= 0, FALSE);

        if (! g_thread_supported ()) {
                g_thread_init (NULL);
        }

        old_val = g_atomic_int_exchange_and_add (&burn_init_ref_count, 1);

        if (old_val == 0) {
                nautilus_burn_get_drive_monitor ();
        }

        return TRUE;
}

/**
 * nautilus_burn_initialized:
 *
 * Detects if nautilus-burn has already been initialized (nautilus-burn must be
 * initialized prior to using any methods or operations).
 *
 * Return value: %TRUE if nautilus-burn has already been initialized.
 */
gboolean
nautilus_burn_initialized (void)
{
        gboolean out;

        out = (g_atomic_int_get (&burn_init_ref_count) == 0);

        return out;
}

/**
 * nautilus_burn_shutdown:
 *
 * Cease all active nautilus-burn operations.
 *
 */
void
nautilus_burn_shutdown (void)
{
        gboolean is_zero;

        g_return_if_fail (burn_init_ref_count > 0);

        is_zero = g_atomic_int_dec_and_test (&burn_init_ref_count);
        if (is_zero) {
                _nautilus_burn_drive_monitor_shutdown ();
        }
}
