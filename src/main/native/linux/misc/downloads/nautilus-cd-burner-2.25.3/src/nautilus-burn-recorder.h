/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nautilus-burn-recorder.h
 *
 * Copyright (C) 2002-2004 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
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

#ifndef NAUTILUS_BURN_RECORDER_H
#define NAUTILUS_BURN_RECORDER_H

#include <glib-object.h>
#include <nautilus-burn-drive.h>

G_BEGIN_DECLS

#define NAUTILUS_BURN_RECORDER_ERROR nautilus_burn_recorder_error_quark ()

GQuark nautilus_burn_recorder_error_quark (void);

typedef enum {
        NAUTILUS_BURN_RECORDER_ERROR_INTERNAL,
        NAUTILUS_BURN_RECORDER_ERROR_GENERAL
} NautilusBurnRecorderError;

typedef void (*CancelFunc) (gpointer       data);

typedef enum {
        NAUTILUS_BURN_RECORDER_RESULT_ERROR     = -1,
        NAUTILUS_BURN_RECORDER_RESULT_CANCEL    = -2,
        NAUTILUS_BURN_RECORDER_RESULT_FINISHED  = -3,
        NAUTILUS_BURN_RECORDER_RESULT_RETRY     = -4
} NautilusBurnRecorderResult;

typedef enum {
        NAUTILUS_BURN_RECORDER_RESPONSE_NONE   =  0,
        NAUTILUS_BURN_RECORDER_RESPONSE_CANCEL = -1,
        NAUTILUS_BURN_RECORDER_RESPONSE_ERASE  = -2,
        NAUTILUS_BURN_RECORDER_RESPONSE_RETRY  = -3
} NautilusBurnRecorderResponse;

typedef enum {
        NAUTILUS_BURN_RECORDER_TRACK_TYPE_UNKNOWN,
        NAUTILUS_BURN_RECORDER_TRACK_TYPE_AUDIO,
        NAUTILUS_BURN_RECORDER_TRACK_TYPE_DATA,
        NAUTILUS_BURN_RECORDER_TRACK_TYPE_CUE,
        NAUTILUS_BURN_RECORDER_TRACK_TYPE_GRAFT_LIST
} NautilusBurnRecorderTrackType;

typedef struct NautilusBurnRecorderTrack NautilusBurnRecorderTrack;

struct NautilusBurnRecorderTrack {
        NautilusBurnRecorderTrackType type;
        union {
                struct {
                        char *filename;
                        char *cdtext;
                } audio;
                struct {
                        char *filename;
                } data;
                struct {
                        char *filename;
                } cue;
                struct {
                        char **entries;
                        char *label;
                } graft_list;
        } contents;
};

typedef enum {
        NAUTILUS_BURN_RECORDER_WRITE_NONE               = 0,
        NAUTILUS_BURN_RECORDER_WRITE_EJECT              = 1 << 0,
        NAUTILUS_BURN_RECORDER_WRITE_BLANK              = 1 << 1,
        NAUTILUS_BURN_RECORDER_WRITE_DUMMY_WRITE        = 1 << 2,
        NAUTILUS_BURN_RECORDER_WRITE_DISC_AT_ONCE       = 1 << 3,
        NAUTILUS_BURN_RECORDER_WRITE_DEBUG              = 1 << 4,
        NAUTILUS_BURN_RECORDER_WRITE_OVERBURN           = 1 << 5,
        NAUTILUS_BURN_RECORDER_WRITE_BURNPROOF          = 1 << 6,
        NAUTILUS_BURN_RECORDER_WRITE_JOLIET             = 1 << 7,
        NAUTILUS_BURN_RECORDER_WRITE_UDF                = 1 << 8
} NautilusBurnRecorderWriteFlags;

typedef enum {
        NAUTILUS_BURN_RECORDER_BLANK_NONE               = 0,
        NAUTILUS_BURN_RECORDER_BLANK_DUMMY_WRITE        = 1 << 1,
        NAUTILUS_BURN_RECORDER_BLANK_DEBUG              = 1 << 2,
} NautilusBurnRecorderBlankFlags;

typedef enum {
        NAUTILUS_BURN_RECORDER_BLANK_FAST,
        NAUTILUS_BURN_RECORDER_BLANK_FULL
} NautilusBurnRecorderBlankType;

#define NAUTILUS_BURN_TYPE_RECORDER            (nautilus_burn_recorder_get_type ())
#define NAUTILUS_BURN_RECORDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),  NAUTILUS_BURN_TYPE_RECORDER, NautilusBurnRecorder))
#define NAUTILUS_BURN_RECORDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),   NAUTILUS_BURN_TYPE_RECORDER, NautilusBurnRecorderClass))
#define NAUTILUS_BURN_IS_RECORDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),  NAUTILUS_BURN_TYPE_RECORDER))
#define NAUTILUS_BURN_IS_RECORDER_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS ((klass), NAUTILUS_BURN_TYPE_RECORDER))

typedef struct NautilusBurnRecorder                NautilusBurnRecorder;
typedef struct NautilusBurnRecorderClass           NautilusBurnRecorderClass;
typedef struct NautilusBurnRecorderPrivate         NautilusBurnRecorderPrivate;

typedef enum {
        NAUTILUS_BURN_RECORDER_ACTION_PREPARING_WRITE,
        NAUTILUS_BURN_RECORDER_ACTION_WRITING,
        NAUTILUS_BURN_RECORDER_ACTION_FIXATING,
        NAUTILUS_BURN_RECORDER_ACTION_BLANKING
} NautilusBurnRecorderActions;

typedef enum {
        NAUTILUS_BURN_RECORDER_MEDIA_CD,
        NAUTILUS_BURN_RECORDER_MEDIA_DVD,
} NautilusBurnRecorderMedia;

struct NautilusBurnRecorder {
        GObject                      parent;
        NautilusBurnRecorderPrivate *priv;
};

struct NautilusBurnRecorderClass {
        GObjectClass parent_class;

        void     (* progress_changed)   (NautilusBurnRecorder       *recorder,
                                         gdouble                     fraction,
                                         long                        seconds);
        void     (* action_changed)     (NautilusBurnRecorder       *recorder,
                                         NautilusBurnRecorderActions action,
                                         NautilusBurnRecorderMedia   media);
        void     (* animation_changed)  (NautilusBurnRecorder       *recorder,
                                         gboolean                    spinning);
        gboolean (* insert_cd_request)  (NautilusBurnRecorder       *recorder,
                                         gboolean                    is_reload,
                                         gboolean                    can_rewrite,
                                         gboolean                    busy);
        int      (* warn_data_loss)     (NautilusBurnRecorder       *recorder);
};

GType                  nautilus_burn_recorder_get_type                  (void);

NautilusBurnRecorder * nautilus_burn_recorder_new                       (void);

int                    nautilus_burn_recorder_write_tracks              (NautilusBurnRecorder          *recorder,
                                                                         NautilusBurnDrive             *drive,
                                                                         GList                         *tracks,
                                                                         int                            speed,
                                                                         NautilusBurnRecorderWriteFlags flags,
                                                                         GError                       **error);

int                    nautilus_burn_recorder_blank_disc                (NautilusBurnRecorder          *recorder,
                                                                         NautilusBurnDrive             *drive,
                                                                         NautilusBurnRecorderBlankType  type,
                                                                         NautilusBurnRecorderBlankFlags flags,
                                                                         GError                       **error);

gboolean               nautilus_burn_recorder_cancel                    (NautilusBurnRecorder          *recorder,
                                                                         gboolean                       skip_if_dangerous);

void                   nautilus_burn_recorder_track_free                (NautilusBurnRecorderTrack     *track);

G_END_DECLS

#endif /* NAUTILUS_BURN_RECORDER_H */
