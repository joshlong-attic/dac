/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*-
 *
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
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

#ifndef __NAUTILUS_BURN_VERSION_H__
#define __NAUTILUS_BURN_VERSION_H__

/* compile time version
 */
#define NAUTILUS_BURN_VERSION_MAJOR	(2)
#define NAUTILUS_BURN_VERSION_MINOR	(25)
#define NAUTILUS_BURN_VERSION_MICRO	(3)

/* check whether a version equal to or greater than
 * major.minor.micro is present.
 */
#define	NAUTILUS_BURN_CHECK_VERSION(major,minor,micro)	\
    (NAUTILUS_BURN_VERSION_MAJOR > (major) || \
     (NAUTILUS_BURN_VERSION_MAJOR == (major) && NAUTILUS_BURN_VERSION_MINOR > (minor)) || \
     (NAUTILUS_BURN_VERSION_MAJOR == (major) && NAUTILUS_BURN_VERSION_MINOR == (minor) && \
      NAUTILUS_BURN_VERSION_MICRO >= (micro)))


#endif /* __NAUTILUS_BURN_VERSION_H__ */

