/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * nautilus-burn-recorder.c
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

#include "config.h"

#include <locale.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#ifdef USE_HAL
#include <libhal.h>
#endif

#include "nautilus-burn-process.h"
#include "nautilus-burn-recorder.h"
#include "nautilus-burn-recorder-marshal.h"

#define CDR_SPEED 153600
#define DVD_SPEED 1385000

#define NAUTILUS_BURN_RECORDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NAUTILUS_BURN_TYPE_RECORDER, NautilusBurnRecorderPrivate))

struct NautilusBurnRecorderPrivate {
        NautilusBurnProcess *process;

        GList               *tracks;
        gint32               track_count;
        guint32              current_track_num;
        guint64              current_track_end_pos;
        guint64              tracks_total_bytes;
        gboolean             debug;
        gboolean             can_rewrite;

#ifdef USE_HAL
        LibHalContext *ctx;
#endif
};

/* Signals */
enum {
        PROGRESS_CHANGED,
        ACTION_CHANGED,
        ANIMATION_CHANGED,
        INSERT_MEDIA_REQUEST,
        WARN_DATA_LOSS,
        LAST_SIGNAL
};

static int nautilus_burn_recorder_table_signals [LAST_SIGNAL] = { 0 };

static int  nautilus_burn_recorder_write_cdrecord  (NautilusBurnRecorder          *recorder,
                                                    NautilusBurnDrive             *drive,
                                                    GList                         *tracks,
                                                    int                            speed,
                                                    NautilusBurnRecorderWriteFlags flags,
                                                    GError                       **error);
static int  nautilus_burn_recorder_write_growisofs (NautilusBurnRecorder          *recorder,
                                                    NautilusBurnDrive             *drive,
                                                    GList                         *tracks,
                                                    int                            speed,
                                                    NautilusBurnRecorderWriteFlags flags,
                                                    GError                       **error);

G_DEFINE_TYPE(NautilusBurnRecorder, nautilus_burn_recorder, G_TYPE_OBJECT);

GQuark
nautilus_burn_recorder_error_quark (void)
{
        static GQuark quark = 0;
        if (! quark)
                quark = g_quark_from_static_string ("nautilus_burn_recorder_error");

        return quark;
}

/**
 * nautilus_burn_recorder_track_free:
 * @track: #NautilusBurnRecorderTrack
 *
 * Free track data.
 *
 **/
void
nautilus_burn_recorder_track_free (NautilusBurnRecorderTrack *track)
{
        switch (track->type) {
        case NAUTILUS_BURN_RECORDER_TRACK_TYPE_CUE:
                g_free (track->contents.cue.filename);
                break;
        case NAUTILUS_BURN_RECORDER_TRACK_TYPE_DATA:
                g_free (track->contents.data.filename);
                break;
        case NAUTILUS_BURN_RECORDER_TRACK_TYPE_AUDIO:
                g_free (track->contents.audio.filename);
                g_free (track->contents.audio.cdtext);
                break;
        case NAUTILUS_BURN_RECORDER_TRACK_TYPE_GRAFT_LIST:
                g_strfreev (track->contents.graft_list.entries);
                g_free (track->contents.graft_list.label);
                break;
        default:
                g_warning ("Invalid track type %d", track->type);
                break;
        }

        g_free (track);
}

static gboolean
can_burn_dvds (NautilusBurnDrive *drive)
{
        int type;

        type = nautilus_burn_drive_get_drive_type (drive);

        if (!(type & NAUTILUS_BURN_DRIVE_TYPE_DVD_RW_RECORDER) &&
            !(type & NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_RECORDER) &&
            !(type & NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_R_DL_RECORDER) &&
            !(type & NAUTILUS_BURN_DRIVE_TYPE_DVD_PLUS_RW_RECORDER)) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
cd_write_needs_growisofs (NautilusBurnDrive    *recorder,
                          NautilusBurnMediaType type,
                          GList                *tracks)
{
        GList *l;

        /* If we cannot burn DVDs, then we don't need growisofs */
        if (can_burn_dvds (recorder) == FALSE) {
                return FALSE;
        }

        if (type == NAUTILUS_BURN_MEDIA_TYPE_DVDR
            || type == NAUTILUS_BURN_MEDIA_TYPE_DVDRW
            || type == NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R
            || type == NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_R_DL
            || type == NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW) {
                return TRUE;
        }

        /* If we have audio tracks, we're using cdrecord */
        for (l = tracks; l != NULL; l = l->next) {
                NautilusBurnRecorderTrack *track = l->data;
                if (track->type == NAUTILUS_BURN_RECORDER_TRACK_TYPE_AUDIO) {
                        return FALSE;
                }
        }

        if (((NautilusBurnRecorderTrack *)tracks->data)->type == NAUTILUS_BURN_RECORDER_TRACK_TYPE_GRAFT_LIST) {
                return TRUE;
        }

        return FALSE;
}

/**
 * nautilus_burn_recorder_cancel:
 * @recorder: #NautilusBurnRecorder
 * @skip_if_dangerous:
 *
 * Cancel active writing process.
 *
 * Return value: %TRUE if succesfully cancelled, %FALSE otherwise
 **/
gboolean
nautilus_burn_recorder_cancel (NautilusBurnRecorder *recorder,
                               gboolean              skip_if_dangerous)
{
        gboolean res;

        g_return_val_if_fail (recorder != NULL, FALSE);

        g_return_val_if_fail (recorder->priv->process != NULL, FALSE);

        res = nautilus_burn_process_cancel (recorder->priv->process,
                                            skip_if_dangerous);
        if (res) {
                recorder->priv->process->result = NAUTILUS_BURN_RECORDER_RESULT_CANCEL;
        }

        return res;
}

static void
insert_cd_retry (NautilusBurnProcess *process,
                 gboolean             cancel,
                 gboolean             is_reload,
                 gboolean             send_return)
{
        if (cancel) {
                /* Shouldn't be dangerous on reload */
                nautilus_burn_process_cancel (process, FALSE);
        } else if (is_reload) {
                if (send_return) {
                        write (process->pipe_stdin, "\n", 1);
                } else {
                        kill (process->pid, SIGUSR1);
                }
        } else {
                process->result = NAUTILUS_BURN_RECORDER_RESULT_RETRY;
                if (g_main_loop_is_running (process->loop))
                        g_main_loop_quit (process->loop);
        }
}

static long
compute_time_remaining (gint64 bytes,
                        double bytes_per_sec)
{
        long secs;

        if (bytes_per_sec <= 1) {
                return -1;
        }

        secs = bytes / bytes_per_sec;

        return secs;
}

static gboolean
growisofs_stdout_line (NautilusBurnProcess   *process,
                       const char            *line,
                       gpointer               data)
{
        NautilusBurnRecorder *recorder = data;
        int                   perc_1, perc_2;
        long long             b_written, b_total;
        float                 speed;

        if (line && process->debug) {
                g_print ("growisofs stdout: %s", line);
        }

        if (sscanf (line, "%10lld/%lld ( %2d.%1d%%) @%fx,", &b_written, &b_total, &perc_1, &perc_2, &speed) == 5) {
                double percent;
                long   secs;

                if (!process->changed_text) {
                        g_signal_emit (recorder,
                                       nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                                       NAUTILUS_BURN_RECORDER_ACTION_WRITING,
                                       NAUTILUS_BURN_RECORDER_MEDIA_DVD);
                }

                percent = (perc_1 + ((float) perc_2 / 10)) / 100;
                secs = compute_time_remaining (b_total - b_written, (double)speed * DVD_SPEED);

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                               percent, secs);
        } else if (strstr (line, "About to execute") != NULL) {
                process->dangerous = TRUE;
        }

        return TRUE;
}

static gboolean
growisofs_blank_stdout_line (NautilusBurnProcess   *process,
                             const char            *line,
                             gpointer               data)
{
        NautilusBurnRecorder *recorder = data;
        int                   perc_1, perc_2;
        long long             b_written, b_total;
        float                 speed;

        if (line && process->debug) {
                g_print ("growisofs blank stdout: %s", line);
        }

        if (sscanf (line, "%10lld/%lld ( %2d.%1d%%) @%fx,", &b_written, &b_total, &perc_1, &perc_2, &speed) == 5) {
                double percent;
                long   secs;

                if (!process->changed_text) {
                        g_signal_emit (recorder,
                                       nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                                       NAUTILUS_BURN_RECORDER_ACTION_BLANKING,
                                       NAUTILUS_BURN_RECORDER_MEDIA_DVD);
                }

                percent = (perc_1 + ((float) perc_2 / 10)) / 100;
                secs = compute_time_remaining (b_total - b_written, (double)speed * DVD_SPEED);

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                               percent, secs);
        } else if (strstr (line, "About to execute") != NULL) {
                process->dangerous = TRUE;
        }

        return TRUE;
}

static gdouble
get_average_rate (GList  **rates,
                  gdouble  rate)
{
        const unsigned int max_num = 16;
        const unsigned int scale  = 1000;
        gdouble            average;
        gint32             int_rate;
        GList             *l;

        if (g_list_length (*rates) > max_num) {
                *rates = g_list_delete_link (*rates, *rates);
        }
        int_rate = (gint32)ceil (scale * rate);

        *rates = g_list_append (*rates, GINT_TO_POINTER (int_rate));

        average = 0;
        for (l = *rates; l != NULL; l = l->next) {
                gdouble r = (gdouble)GPOINTER_TO_INT (l->data) / scale;
                average += r;
        }

        average /= g_list_length (*rates);

        return average;
}

static gboolean
cdrecord_stdout_line (NautilusBurnProcess   *process,
                      const char            *line,
                      gpointer               data)
{
        NautilusBurnRecorder *recorder = data;
        unsigned int          track, mb_written, mb_total;
        float                 speed;
        int                   tmp;

        if (line && process->debug) {
                g_print ("cdrecord stdout: %s", line);
        }

        if (sscanf (line, "Track %2u: %d of %d MB written (fifo %d%%) [buf %d%%] %fx.",
                    &track, &mb_written, &mb_total, &tmp, &tmp, &speed) == 6) {
                double       percent;
                gint64       bytes;
                gint64       this_remain;
                gint64       total;
                long         secs;

                if (recorder->priv->tracks_total_bytes > 0) {
                        total = recorder->priv->tracks_total_bytes;
                } else {
                        total = mb_total * 1048576;
                }

                if (track > recorder->priv->current_track_num) {
                        recorder->priv->current_track_num = track;
                        recorder->priv->current_track_end_pos += mb_total * 1048576;
                }

                this_remain = (mb_total - mb_written) * 1048576;
                bytes = (total - recorder->priv->current_track_end_pos) + this_remain;

                secs = -1;
                if (speed > 0) {
                        gdouble ave_rate;
                        gdouble rate;

                        rate = (double)speed * CDR_SPEED;

                        ave_rate = get_average_rate (&process->rates, rate);
                        secs = compute_time_remaining (bytes, ave_rate);
                }

                if (!process->changed_text) {
                        g_signal_emit (recorder,
                                       nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                                       NAUTILUS_BURN_RECORDER_ACTION_WRITING,
                                       NAUTILUS_BURN_RECORDER_MEDIA_CD);
                }

                if (recorder->priv->tracks_total_bytes > 0) {
                        percent = 0.98 * (1.0 - (double)bytes / (double)recorder->priv->tracks_total_bytes);
                } else {
                        percent = 0.98 * ((double)((track - 1) / (double)recorder->priv->track_count)
                                          + ((double)mb_written / mb_total) / (double)recorder->priv->track_count);
                }

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                               percent, secs);
        } else if (sscanf (line, "Track %*d: %*s %d MB ", &mb_total)) {
                /* %s can be either "data" or "audio" */
                if (mb_total > 0) {
                        recorder->priv->tracks_total_bytes += mb_total * 1048576;
                }
        } else if (g_str_has_prefix (line, "Re-load disk and hit <CR>") ||
                   g_str_has_prefix (line, "send SIGUSR1 to continue")) {
                gboolean  res;

                /* This is not supposed to happen since we checked for the cd
                   before starting, but we try to handle it anyway, since mmc
                   profiling can fail. */

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [INSERT_MEDIA_REQUEST],
                               0, TRUE, recorder->priv->can_rewrite,
                               FALSE, &res);

                process->expect_process_to_die = TRUE;

                insert_cd_retry (process, !res, TRUE, (*line == 'R'));

        } else if (g_str_has_prefix (line, "Fixating...")) {
                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                               NAUTILUS_BURN_RECORDER_ACTION_FIXATING,
                               NAUTILUS_BURN_RECORDER_MEDIA_CD);
        } else if (g_str_has_prefix (line, "Fixating time:")) {
                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                               1.0, (long)-1);
                /* only set result if not already set */
                if (process->result == 0) {
                        process->result = NAUTILUS_BURN_RECORDER_RESULT_FINISHED;
                }
        } else if (g_str_has_prefix (line, "Last chance to quit, ")) {
                process->dangerous = TRUE;
        } else if (g_str_has_prefix (line, "Blanking PMA, TOC, pregap")) {
                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                               NAUTILUS_BURN_RECORDER_ACTION_BLANKING,
                               NAUTILUS_BURN_RECORDER_MEDIA_CD);
        }

        return TRUE;
}

static gboolean
cdrecord_blank_stdout_line (NautilusBurnProcess   *process,
                            const char            *line,
                            gpointer               data)
{
        NautilusBurnRecorder *recorder = data;

        if (line && process->debug) {
                g_print ("cdrecord blank stdout: %s", line);
        }

        if (g_str_has_prefix (line, "Re-load disk and hit <CR>") ||
            g_str_has_prefix (line, "send SIGUSR1 to continue")) {
                gboolean  res;

                /* This is not supposed to happen since we checked for the cd
                   before starting, but we try to handle it anyway, since mmc
                   profiling can fail. */

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [INSERT_MEDIA_REQUEST],
                               0, TRUE, recorder->priv->can_rewrite,
                               FALSE, &res);

                process->expect_process_to_die = TRUE;

                insert_cd_retry (process, !res, TRUE, (*line == 'R'));
        } else if (g_str_has_prefix (line, "Blanking time:")) {
                if (process->result == 0) {
                        process->result = NAUTILUS_BURN_RECORDER_RESULT_FINISHED;
                }
        } else if (g_str_has_prefix (line, "Last chance to quit, ")) {
                process->dangerous = TRUE;
        } else if (g_str_has_prefix (line, "Blanking PMA, TOC, pregap")) {
                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                               NAUTILUS_BURN_RECORDER_ACTION_BLANKING,
                               NAUTILUS_BURN_RECORDER_MEDIA_CD);
        }

        return TRUE;
}

static gboolean
dvdrw_format_stderr_line (NautilusBurnProcess   *process,
                          const char            *line,
                          gpointer               data)
{
        NautilusBurnRecorder *recorder = data;
        float                 percent;

        if (line && process->debug) {
                g_print ("dvdrw format stderr: %s", line);
        }

        if ((sscanf (line, "* blanking %f%%,", &percent) == 1)
            || (sscanf (line, "* formatting %f%%,", &percent) == 1)
            || (sscanf (line, "* relocating lead-out %f%%,", &percent) == 1)) {

                process->dangerous = TRUE;

                if (percent > 1) {
                        if (!process->changed_text) {
                                g_signal_emit (recorder,
                                               nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                                               NAUTILUS_BURN_RECORDER_ACTION_BLANKING,
                                               NAUTILUS_BURN_RECORDER_MEDIA_DVD);
                        }

                        g_signal_emit (recorder,
                                       nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                                       percent, (long)-1);
                }

        } else {

        }

        return TRUE;
}

static gboolean
growisofs_stderr_line (NautilusBurnProcess   *process,
                       const char            *line,
                       gpointer               data)
{
        NautilusBurnRecorder *recorder = data;
        char                 *pos;

        if (line && process->debug) {
                g_print ("growisofs stderr: %s", line);
        }

        if (strstr (line, "unsupported MMC profile") != NULL
            || (strstr (line, "already carries isofs") != NULL
                && strstr (line, "FATAL:") != NULL)) {
                gboolean  res;

                /* This is not supposed to happen since we checked for the cd
                   type before starting, but we try to handle it anyway, since mmc
                   profiling can fail. */

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [INSERT_MEDIA_REQUEST],
                               0, TRUE, recorder->priv->can_rewrite,
                               FALSE, &res);

                process->expect_process_to_die = TRUE;

                insert_cd_retry (process, !res, FALSE, FALSE);
        } else if (strstr (line, "pre-formatting") != NULL) {
                process->dangerous = TRUE;

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                               NAUTILUS_BURN_RECORDER_ACTION_BLANKING,
                               NAUTILUS_BURN_RECORDER_MEDIA_DVD);
        } else if (strstr (line, "Current Write Speed") != NULL) {
                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                               NAUTILUS_BURN_RECORDER_ACTION_WRITING,
                               NAUTILUS_BURN_RECORDER_MEDIA_DVD);
        } else if (strstr (line, "unable to open") != NULL || strstr (line, "unable to stat") != NULL) {
                /* This fits the "open64" and "open"-like messages */
                nautilus_burn_process_set_error (process,
                                                 _("The recorder could not be accessed."),
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        } else if (strstr (line, "not enough space available") != NULL) {
                nautilus_burn_process_set_error (process,
                                                 _("Not enough space available on the disc."),
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        } else if (strstr (line, "end of user area encountered on this track") != NULL) {
                nautilus_burn_process_set_error (process,
                                                 _("The files selected did not fit on the DVD."),
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        } else if (strstr (line, "blocks are free") != NULL) {
                nautilus_burn_process_set_error (process,
                                                 _("The files selected did not fit on the DVD."),
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        } else if (strstr (line, "flushing cache") != NULL) {
                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                               NAUTILUS_BURN_RECORDER_ACTION_FIXATING,
                               NAUTILUS_BURN_RECORDER_MEDIA_DVD);
                if (process->result == 0) {
                        process->result = NAUTILUS_BURN_RECORDER_RESULT_FINISHED;
                }
        } else if (strstr (line, "unable to unmount") != NULL) {
                nautilus_burn_process_set_error (process,
                                                 _("The target DVD drive is still in use."),
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);

        } else if (strstr (line, ":-(") != NULL || strstr (line, "FATAL") != NULL) {
                if (! nautilus_burn_process_get_error (process, NULL, NULL)) {
                        nautilus_burn_process_set_error (process,
                                                         _("Unhandled error, aborting"),
                                                         NAUTILUS_BURN_RECORDER_RESULT_ERROR);
                }
        }

        /* for mkisofs exec */
        if ((pos = strstr (line, "estimate finish"))) {
                char    fraction_str [7];
                gdouble fraction;

                if (sscanf (line, "%6c%% done, estimate finish", fraction_str) == 1) {
                        long secs;

                        fraction_str [6] = 0;
                        fraction = g_strtod (fraction_str, NULL);

                        secs = -1;
                        if (pos) {
                                struct tm tm;
                                time_t    t_finish;

                                pos += strlen ("estimate finish ");
                                /* mkisofs uses ctime for format time string */
                                strptime (pos, "%a %b %d %H:%M:%S %Y", &tm);
                                t_finish = mktime (&tm);
                                secs = difftime (t_finish, time (NULL));
                        }

                        g_signal_emit (recorder,
                                       nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                                       fraction / 100.0, secs);
                }
        }

        return TRUE;
}

static gboolean
cdrecord_stderr_line (NautilusBurnProcess   *process,
                      const char            *line,
                      gpointer               data)
{
        NautilusBurnRecorder *recorder = data;

        if (line && process->debug) {
                g_print ("cdrecord stderr: %s", line);
        }

        if (strstr (line, "No disk / Wrong disk!") != NULL) {
                gboolean  res;

                /* This is not supposed to happen since we checked for the cd
                   before starting, but we try to handle it anyway, since mmc
                   profiling can fail. */

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [INSERT_MEDIA_REQUEST],
                               0, TRUE, recorder->priv->can_rewrite,
                               FALSE, &res);

                process->expect_process_to_die = TRUE;

                insert_cd_retry (process, !res, FALSE, FALSE);
        } else if (strstr (line, "This means that we are checking recorded media.") != NULL) {
                nautilus_burn_process_set_error (process,
                                                 _("The CD has already been recorded."),
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        } else if (strstr (line, "Cannot blank disk, aborting.") != NULL) {
                gboolean  res;

                /* This is not supposed to happen since we checked for the cd
                   type before starting, but we try to handle it anyway, since
                   mmc profiling can fail. */

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [INSERT_MEDIA_REQUEST],
                               0, TRUE, TRUE,
                               FALSE, &res);

                process->expect_process_to_die = TRUE;

                insert_cd_retry (process, !res, FALSE, FALSE);
        } else if ((strstr (line, "Data may not fit on current disk") != NULL)
                   || (strstr (line, "Data may not fit on standard") != NULL)) {
                nautilus_burn_process_set_error (process,
                                                 _("The files selected did not fit on the CD."),
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        } else if (strstr (line, "Inappropriate audio coding") != NULL) {
                nautilus_burn_process_set_error (process,
                                                 _("All audio files must be stereo, 16-bit digital audio with 44100Hz samples."),
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        } else if (strstr (line, "cannot write medium - incompatible format") != NULL) {
                gboolean  res;

                /* This is not supposed to happen since we checked for the cd
                   type before starting, but we try to handle it anyway, since
                   mmc profiling can fail. */

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [INSERT_MEDIA_REQUEST],
                               0, TRUE, recorder->priv->can_rewrite,
                               FALSE, &res);

                process->expect_process_to_die = TRUE;

                insert_cd_retry (process, !res, FALSE, FALSE);
        } else if (strstr (line, "Sense flags: ") != NULL) {
                /*
                  closest thing to an error message that we get:
                  Sense flags: Blk 65469 (not valid)
                */
                if (strstr (line, "(not valid)") != NULL) {
                        nautilus_burn_process_set_error (process,
                                                         _("Error while writing to disc.  Try a lower speed."),
                                                         NAUTILUS_BURN_RECORDER_RESULT_ERROR);
                }
	} else if (strstr (line, "Sense Key:") != NULL) {
		if (strstr (line, "Medium Error") != NULL) {
			nautilus_burn_process_set_error (process,
							 _("Error while writing to disc.  The disc is damaged or unreadable."),
							 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
		}
        } else if ((strstr (line, "DMA speed too slow") != NULL)
        	   || (strstr (line, "wodim: A write error occured.") != NULL)
        	   || (strstr (line, "wodim: A write error occurred.") != NULL)) {
                nautilus_burn_process_set_error (process,
                                                 _("The system is too slow to write the CD at this speed. Try a lower speed."),
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        }


        return TRUE;
}

static NautilusBurnMediaType
nautilus_burn_recorder_wait_for_insertion (NautilusBurnRecorder *recorder,
                                           NautilusBurnDrive    *drive,
                                           gboolean             *can_rewrite,
                                           gboolean             *blank)
{
        NautilusBurnMediaType type;
        gint64                media_capacity;
        gboolean              reload;
        gboolean              is_rewritable;
        gboolean              can_write;
        gboolean              is_blank;
        gboolean              has_data;
        gboolean              has_audio;

        reload = FALSE;

 again:

        /* if drive is mounted then unmount before checking anything */
        if (nautilus_burn_drive_is_mounted (drive)) {
                gboolean res;
                res = nautilus_burn_drive_unmount (drive);
                if (! res) {
                        g_warning ("Couldn't unmount volume in drive: %s", nautilus_burn_drive_get_device (drive));
                }
        }

        media_capacity = nautilus_burn_drive_get_media_capacity (drive);
        type = nautilus_burn_drive_get_media_type_full (drive, &is_rewritable, &is_blank, &has_data, &has_audio);
        can_write = nautilus_burn_drive_media_type_is_writable (type, is_blank);

        if (can_rewrite) {
                *can_rewrite = is_rewritable;
        }

        if (blank) {
                *blank = is_blank;
        }

        if (! can_write) {
                gboolean is_mounted;
                int      res;

                reload = (media_capacity > 0);
                if (type == NAUTILUS_BURN_MEDIA_TYPE_ERROR) {
                        reload = FALSE;
                }

                /* check one more time */
                is_mounted = nautilus_burn_drive_is_mounted (drive);

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [INSERT_MEDIA_REQUEST],
                               0,
                               reload,
                               recorder->priv->can_rewrite,
                               is_mounted,
                               &res);

                if (res == FALSE) {
                        return NAUTILUS_BURN_MEDIA_TYPE_ERROR;
                }

                goto again;
        }

        if (! is_blank) {
                int res;

                res = 0;

                /* We need to warn the user that it's going to erase their stuff */
                g_signal_emit (G_OBJECT (recorder),
                               nautilus_burn_recorder_table_signals [WARN_DATA_LOSS],
                               0, &res);

                if (res == NAUTILUS_BURN_RECORDER_RESPONSE_RETRY) {
                        goto again;
                } else if (res == NAUTILUS_BURN_RECORDER_RESPONSE_CANCEL) {
                        type = NAUTILUS_BURN_MEDIA_TYPE_ERROR;
                }
        }

        return type;
}

/**
 * nautilus_burn_recorder_write_tracks:
 * @recorder: #NautilusBurnRecorder
 * @drive: #NautilusBurnDrive to write to
 * @tracks: list of tracks to write
 * @speed: speed at which to write data
 * @flags: #NautilusBurnRecorderWriteFlags
 * @error: return location for errors
 *
 * Write tracks to the specified drive.
 *
 * Return value: #NautilusBurnRecorderResult
 **/
int
nautilus_burn_recorder_write_tracks (NautilusBurnRecorder          *recorder,
                                     NautilusBurnDrive             *drive,
                                     GList                         *tracks,
                                     int                            speed,
                                     NautilusBurnRecorderWriteFlags flags,
                                     GError                       **error)
{
        NautilusBurnMediaType type;
        gboolean              can_rewrite;
        gboolean              is_blank;
        gboolean              is_locked;
        int                   ret;

        g_return_val_if_fail (recorder != NULL, NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        g_return_val_if_fail (tracks != NULL, NAUTILUS_BURN_RECORDER_RESULT_ERROR);

        recorder->priv->tracks = tracks;
        recorder->priv->track_count = g_list_length (tracks);
        recorder->priv->debug = (flags & NAUTILUS_BURN_RECORDER_WRITE_DEBUG);

        recorder->priv->can_rewrite = nautilus_burn_drive_can_rewrite (drive);

        if (recorder->priv->track_count > 99) {
                g_set_error (error,
                             NAUTILUS_BURN_RECORDER_ERROR,
                             NAUTILUS_BURN_RECORDER_ERROR_GENERAL,
                             _("You can only burn 99 tracks on one disc"));

                return NAUTILUS_BURN_RECORDER_RESULT_ERROR;
        } else if (recorder->priv->track_count < 1) {
                g_set_error (error,
                             NAUTILUS_BURN_RECORDER_ERROR,
                             NAUTILUS_BURN_RECORDER_ERROR_GENERAL,
                             _("No tracks given to write"));

                return NAUTILUS_BURN_RECORDER_RESULT_ERROR;
        }

        is_locked = nautilus_burn_drive_lock (drive, _("Burning CD/DVD"), NULL);

        type = nautilus_burn_recorder_wait_for_insertion (recorder,
                                                          drive,
                                                          &can_rewrite,
                                                          &is_blank);

        if (type == NAUTILUS_BURN_MEDIA_TYPE_ERROR) {
                if (is_locked)
                        nautilus_burn_drive_unlock (drive);

                return NAUTILUS_BURN_RECORDER_RESULT_CANCEL;
        }

        if (can_rewrite != FALSE) {
                flags |= NAUTILUS_BURN_RECORDER_WRITE_BLANK;
        }

        if (cd_write_needs_growisofs (drive, type, tracks)) {
                ret = nautilus_burn_recorder_write_growisofs (recorder,
                                                              drive,
                                                              tracks,
                                                              speed,
                                                              flags,
                                                              error);
        } else {
                ret = nautilus_burn_recorder_write_cdrecord (recorder,
                                                             drive,
                                                             tracks,
                                                             speed,
                                                             flags,
                                                             error);
        }

        if (is_locked) {
                nautilus_burn_drive_unlock (drive);
        }

        return ret;
}

static int
nautilus_burn_recorder_run_process (NautilusBurnRecorder       *recorder,
                                    NautilusBurnRecorderMedia   media_type,
                                    GPtrArray                  *argv,
                                    NautilusBurnProcessLineFunc out_line_func,
                                    NautilusBurnProcessLineFunc err_line_func,
                                    GError                    **error)
{
        int                  res;
        int                  result;
        GError              *local_error = NULL;
        NautilusBurnProcess *process;
        char                *error_message;
        int                  error_code;

 retry:

        process = nautilus_burn_process_new ();
        process->debug = recorder->priv->debug;

        nautilus_burn_process_free (recorder->priv->process);
        recorder->priv->process = process;

        process->result = 0;

        g_signal_emit (G_OBJECT (recorder),
                       nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                       NAUTILUS_BURN_RECORDER_ACTION_PREPARING_WRITE,
                       media_type);
        g_signal_emit (G_OBJECT (recorder),
                       nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                       0.0, (long)-1);
        g_signal_emit (G_OBJECT (recorder),
                       nautilus_burn_recorder_table_signals [ANIMATION_CHANGED], 0,
                       TRUE);

        local_error = NULL;
        res = nautilus_burn_process_run (process,
                                         argv,
                                         out_line_func,
                                         err_line_func,
                                         recorder,
                                         &local_error);

        if (process->result == NAUTILUS_BURN_RECORDER_RESULT_RETRY) {
                goto retry;
        }

        if (local_error != NULL) {
                g_set_error (error,
                             NAUTILUS_BURN_RECORDER_ERROR,
                             NAUTILUS_BURN_RECORDER_ERROR_GENERAL,
                             local_error->message);
                g_error_free (local_error);
        } else if (nautilus_burn_process_get_error (process, &error_message, &error_code)) {
                g_set_error (error,
                             NAUTILUS_BURN_RECORDER_ERROR,
                             NAUTILUS_BURN_RECORDER_ERROR_GENERAL,
                             error_message);
                g_free (error_message);
        }

        result = process->result;
        nautilus_burn_process_free (process);
        recorder->priv->process = NULL;

        g_signal_emit (G_OBJECT (recorder),
                       nautilus_burn_recorder_table_signals [ANIMATION_CHANGED], 0,
                       FALSE);

        if (result == 0) {
                result = NAUTILUS_BURN_RECORDER_RESULT_ERROR;
        }

        return result;
}

static char *
create_temp_file (void)
{
        char *template;
        char *template_path;
        int   fd;

        template = g_strdup_printf ("iso-%s.XXXXXX", g_get_user_name ());
        template_path = g_build_filename (g_get_tmp_dir (), template, NULL);
        g_free (template);

        fd = g_mkstemp (template_path);

        if (fd == -1) {
                g_free (template_path);
                template_path = NULL;
        } else {
                close (fd);
        }

        return template_path;
}

static char *
create_temp_graft_file (char **entries)
{
        char    *filename;
        char    *contents;
        gboolean res;

        filename = create_temp_file ();

        contents = g_strjoinv ("\n", entries);

        res = g_file_set_contents (filename, contents, -1, NULL);
        if (! res) {
                g_free (filename);
                filename = NULL;
        }

        g_free (contents);

        return filename;
}

/* this is an ugly hack until utf8/iconv support is added into upstream mkisofs */
static gboolean
ncb_mkisofs_supports_utf8 (void)
{
        static gboolean first = TRUE;
        static gboolean supported;

        if (first) {
                char    *standard_error;
                gboolean res;
                res = g_spawn_command_line_sync ("mkisofs -input-charset utf8", NULL, &standard_error, NULL, NULL);
                if (res && !g_strrstr (standard_error, "Unknown charset")) {
                        supported = TRUE;
                } else {
                        supported = FALSE;
                }
                g_free (standard_error);
        }

        return supported;
}

static int
nautilus_burn_recorder_write_growisofs (NautilusBurnRecorder          *recorder,
                                        NautilusBurnDrive             *drive,
                                        GList                         *tracks,
                                        int                            speed,
                                        NautilusBurnRecorderWriteFlags flags,
                                        GError                       **error)
{
        GPtrArray                 *argv;
        char                      *speed_str = NULL;
        char                      *dev_str;
        NautilusBurnRecorderTrack *t;
        int                        result;
        char                      *filelist;

        if (g_list_length (tracks) != 1) {
                g_warning ("Can only use growisofs on a single track");
                g_set_error (error,
                             NAUTILUS_BURN_RECORDER_ERROR,
                             NAUTILUS_BURN_RECORDER_ERROR_GENERAL,
                             _("Can only use growisofs on a single track"));

                return NAUTILUS_BURN_RECORDER_RESULT_ERROR;
        }

        t = (NautilusBurnRecorderTrack*)tracks->data;
        if (t->type != NAUTILUS_BURN_RECORDER_TRACK_TYPE_DATA
            && t->type != NAUTILUS_BURN_RECORDER_TRACK_TYPE_GRAFT_LIST) {
                g_set_error (error,
                             NAUTILUS_BURN_RECORDER_ERROR,
                             NAUTILUS_BURN_RECORDER_ERROR_GENERAL,
                             _("Can only use growisofs on a data or graft list track"));
                return NAUTILUS_BURN_RECORDER_RESULT_ERROR;
        }

        argv = g_ptr_array_new ();

        g_ptr_array_add (argv, "growisofs");
        if (speed != 0) {
                char    *current_locale = NULL;
                float    dvd_speed;

                dvd_speed = NAUTILUS_BURN_DRIVE_DVD_SPEED (speed);

                /* We want g_strdup_printf() to use dots as decimal
                 * point for the speed, otherwise growisofs wouldn't understand. */
                current_locale = setlocale (LC_NUMERIC, NULL);
                setlocale (LC_NUMERIC, "C");

                speed_str = g_strdup_printf ("-speed=%.1f", dvd_speed);

                setlocale (LC_NUMERIC, current_locale);

                g_ptr_array_add (argv, speed_str);
        }

        g_ptr_array_add (argv, "-dvd-compat");

        /* Weird, innit? We tell growisofs we have a tty so it ignores
         * the fact that the DVD+ has an ISO fs already */
        if (flags & NAUTILUS_BURN_RECORDER_WRITE_BLANK) {
                g_ptr_array_add (argv, "-use-the-force-luke=tty");
        }

        filelist = NULL;
        if (t->type == NAUTILUS_BURN_RECORDER_TRACK_TYPE_GRAFT_LIST) {
                filelist = create_temp_graft_file (t->contents.graft_list.entries);

                g_ptr_array_add (argv, "-graft-points");
                g_ptr_array_add (argv, "-path-list");
                g_ptr_array_add (argv, filelist);
        }

        g_ptr_array_add (argv, "-Z");

        dev_str = NULL;
        if (t->type == NAUTILUS_BURN_RECORDER_TRACK_TYPE_DATA) {
                dev_str = g_strdup_printf ("%s=%s", nautilus_burn_drive_get_device (drive), t->contents.data.filename);
                g_ptr_array_add (argv, dev_str);
        } else {
                g_ptr_array_add (argv, (char *)nautilus_burn_drive_get_device (drive));

                /* mkisofs options */
                if (ncb_mkisofs_supports_utf8 ()) {
                        g_ptr_array_add (argv, "-input-charset");
                        g_ptr_array_add (argv, "utf8");
                }

                g_ptr_array_add (argv, "-r");

                if (flags & NAUTILUS_BURN_RECORDER_WRITE_JOLIET) {
                        g_ptr_array_add (argv, "-J");
                }
                if (flags & NAUTILUS_BURN_RECORDER_WRITE_UDF) {
                        g_ptr_array_add (argv, "-udf");
                }

                g_ptr_array_add (argv, "-V");
                g_ptr_array_add (argv, (char *) t->contents.graft_list.label);
        }

        g_ptr_array_add (argv, NULL);

        result = nautilus_burn_recorder_run_process (recorder,
                                                     NAUTILUS_BURN_RECORDER_MEDIA_DVD,
                                                     argv,
                                                     growisofs_stdout_line,
                                                     growisofs_stderr_line,
                                                     error);

        g_free (speed_str);
        g_free (dev_str);
        g_ptr_array_free (argv, TRUE);
        argv = NULL;

        if (filelist != NULL) {
                g_remove (filelist);
                g_free (filelist);
        }

        g_signal_emit (recorder,
                       nautilus_burn_recorder_table_signals [ANIMATION_CHANGED], 0,
                       FALSE);

        if (flags & NAUTILUS_BURN_RECORDER_WRITE_EJECT && result == NAUTILUS_BURN_RECORDER_RESULT_FINISHED) {
                nautilus_burn_drive_eject (drive);
        }

        return result;
}

static gboolean
cdrdao_stderr_line (NautilusBurnProcess   *process,
                    const char            *line,
                    gpointer               data)
{
        NautilusBurnRecorder *recorder = data;
        unsigned int          written, total;

        if (line && process->debug) {
                g_print ("cdrdao stderr: %s", line);
        }

        if (sscanf (line, "Wrote %u of %u", &written, &total) == 2) {
                long    secs = -1;
                gdouble fraction;

                process->dangerous = TRUE;

                fraction = (gdouble) written / total;

                if (process->start_time == 0
                    && written > 2) {
                        /* let the speed regulate before keeping track */
                        process->start_time = time (NULL);
                        process->start_num = (gint64)written;
                }

                if (process->start_time > 0) {
                        guint64 elapsed;
                        gdouble rate;

                        elapsed = time (NULL) - process->start_time;
                        rate = (gdouble)(written - process->start_num) / (gdouble)elapsed;

                        if (rate > 0) {
                                secs = (total - written) / rate;
                        }
                }

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                               NAUTILUS_BURN_RECORDER_ACTION_WRITING,
                               NAUTILUS_BURN_RECORDER_MEDIA_CD);
                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                               fraction, secs);
        }

        if (strstr (line, "Writing track 01")) {
                process->dangerous = TRUE;
                process->start_time = 0;

                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [ACTION_CHANGED], 0,
                               NAUTILUS_BURN_RECORDER_ACTION_WRITING,
                               NAUTILUS_BURN_RECORDER_MEDIA_CD);
                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                               0.0, (long)-1);

        }

        if (strstr (line, "Operation not permitted")) {
                nautilus_burn_process_set_error (process,
                                                 line,
                                                 NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        }

        if (strstr (line, "Writing finished successfully")) {
                process->dangerous = FALSE;
                process->result = NAUTILUS_BURN_RECORDER_RESULT_FINISHED;
                g_signal_emit (recorder,
                               nautilus_burn_recorder_table_signals [PROGRESS_CHANGED], 0,
                               1.0, (long)-1);

        }

        return TRUE;
}

static int
nautilus_burn_recorder_write_cdrecord (NautilusBurnRecorder          *recorder,
                                       NautilusBurnDrive             *drive,
                                       GList                         *tracks,
                                       int                            speed,
                                       NautilusBurnRecorderWriteFlags flags,
                                       GError                       **error)
{
        NautilusBurnRecorderTrack  *track;
        GPtrArray                  *argv      = NULL;
        char                       *speed_str = NULL;
        char                       *dev_str   = NULL;
        char                       *cue_str   = NULL;
        GList                      *l;
        NautilusBurnProcessLineFunc out_line_func;
        NautilusBurnProcessLineFunc err_line_func;
        int                         result    = NAUTILUS_BURN_RECORDER_RESULT_ERROR;

        g_return_val_if_fail (tracks != NULL, NAUTILUS_BURN_RECORDER_RESULT_ERROR);

        track = tracks->data;

        if (flags & NAUTILUS_BURN_RECORDER_WRITE_BLANK) {
                NautilusBurnRecorderBlankFlags blank_flags = 0;
                int                            res;
                GError                        *err = NULL;

                if (flags & NAUTILUS_BURN_RECORDER_WRITE_DEBUG) {
                        blank_flags |= NAUTILUS_BURN_RECORDER_BLANK_DEBUG;
                }
                if (flags & NAUTILUS_BURN_RECORDER_WRITE_DUMMY_WRITE) {
                        blank_flags |= NAUTILUS_BURN_RECORDER_BLANK_DUMMY_WRITE;
                }

                /* cdrdao doesn't seem to have on-the-fly blank */
                /* cdrecord doesn't seem to correctly detect disc size when blanking */
                res = nautilus_burn_recorder_blank_disc (recorder,
                                                         drive,
                                                         NAUTILUS_BURN_RECORDER_BLANK_FAST,
                                                         blank_flags,
                                                         &err);
                if (err != NULL) {
                        g_propagate_error (error, err);
                }

                if (res != NAUTILUS_BURN_RECORDER_RESULT_FINISHED) {
                        return res;
                }
        }

        /* Turn the kB/s to a CD speed (for example 10 for "10x").
         * cdrecord supports only integer numbers for the speed. */
         speed = (int) NAUTILUS_BURN_DRIVE_CD_SPEED (speed);

        if (track->type ==  NAUTILUS_BURN_RECORDER_TRACK_TYPE_CUE) {
                argv = g_ptr_array_new ();
                g_ptr_array_add (argv, "cdrdao");
                g_ptr_array_add (argv, "write");
                g_ptr_array_add (argv, "--device");
                g_ptr_array_add (argv, (char *)nautilus_burn_drive_get_device (drive));
                g_ptr_array_add (argv, "--speed");
                speed_str = g_strdup_printf ("%d", speed);
                g_ptr_array_add (argv, speed_str);

                if (flags & NAUTILUS_BURN_RECORDER_WRITE_DUMMY_WRITE) {
                        g_ptr_array_add (argv, "--simulate");
                }
                if (flags & NAUTILUS_BURN_RECORDER_WRITE_EJECT) {
                        g_ptr_array_add (argv, "--eject");
                }
                if (flags & NAUTILUS_BURN_RECORDER_WRITE_OVERBURN) {
                        g_ptr_array_add (argv, "--overburn");
                }
                g_ptr_array_add (argv, "-v");
                g_ptr_array_add (argv, "2");

                g_ptr_array_add (argv, track->contents.cue.filename);
                g_ptr_array_add (argv, NULL);

                out_line_func = NULL;
                err_line_func = cdrdao_stderr_line;
        } else {
                argv = g_ptr_array_new ();
                g_ptr_array_add (argv, "cdrecord");

                g_ptr_array_add (argv, "fs=16m");

                speed_str = g_strdup_printf ("speed=%d", speed);
                if (speed != 0) {
                        g_ptr_array_add (argv, speed_str);
                }
                dev_str = g_strdup_printf ("dev=%s", nautilus_burn_drive_get_device (drive));
                g_ptr_array_add (argv, dev_str);
                if (flags & NAUTILUS_BURN_RECORDER_WRITE_DUMMY_WRITE) {
                        g_ptr_array_add (argv, "-dummy");
                }
                if (flags & NAUTILUS_BURN_RECORDER_WRITE_EJECT) {
                        g_ptr_array_add (argv, "-eject");
                }
                if (flags & NAUTILUS_BURN_RECORDER_WRITE_DISC_AT_ONCE) {
                        g_ptr_array_add (argv, "-dao");
                }
                if (flags & NAUTILUS_BURN_RECORDER_WRITE_OVERBURN) {
                        g_ptr_array_add (argv, "-overburn");
                }
                if (flags & NAUTILUS_BURN_RECORDER_WRITE_BURNPROOF) {
                        g_ptr_array_add (argv, "driveropts=burnfree");
                }
                g_ptr_array_add (argv, "-v");

                l = tracks;
                while (l != NULL && l->data != NULL) {

                        NautilusBurnRecorderTrack *track = l->data;

                        switch (track->type) {
                        case NAUTILUS_BURN_RECORDER_TRACK_TYPE_DATA:
                                g_ptr_array_add (argv, "-data");
                                g_ptr_array_add (argv, "-nopad");
                                g_ptr_array_add (argv, track->contents.data.filename);
                                break;
                        case NAUTILUS_BURN_RECORDER_TRACK_TYPE_AUDIO:
                                g_ptr_array_add (argv, "-copy");
                                g_ptr_array_add (argv, "-audio");
                                g_ptr_array_add (argv, "-pad");
                                g_ptr_array_add (argv, track->contents.audio.filename);
                                /* TODO: handle CD-TEXT somehow */
                                break;
                        case NAUTILUS_BURN_RECORDER_TRACK_TYPE_CUE:
                                /* skip silently if not the first track */
                                break;
                        default:
                                g_warning ("Unknown track type %d", track->type);
                        }

                        l = g_list_next (l);
                }

                g_ptr_array_add (argv, NULL);

                out_line_func = cdrecord_stdout_line;
                err_line_func = cdrecord_stderr_line;
        }

        if (argv != NULL) {
                GError *err = NULL;

                result = nautilus_burn_recorder_run_process (recorder,
                                                             NAUTILUS_BURN_RECORDER_MEDIA_CD,
                                                             argv,
                                                             out_line_func,
                                                             err_line_func,
                                                             &err);
                if (err != NULL) {
                        g_propagate_error (error, err);
                }
        }

        g_free (cue_str);
        g_free (speed_str);
        g_free (dev_str);
        g_ptr_array_free (argv, TRUE);
        argv = NULL;

        return result;
}

static int
nautilus_burn_recorder_blank_disc_cdrecord (NautilusBurnRecorder          *recorder,
                                            NautilusBurnDrive             *drive,
                                            NautilusBurnRecorderBlankType  type,
                                            NautilusBurnRecorderBlankFlags flags,
                                            GError                       **error)
{
        GPtrArray            *argv;
        NautilusBurnMediaType media_type;
        char                 *blank_str, *dev_str;
        gboolean              is_locked;
        gboolean              can_rewrite;
        gboolean              is_blank;
        int                   result = NAUTILUS_BURN_RECORDER_RESULT_ERROR;

        recorder->priv->can_rewrite = nautilus_burn_drive_can_rewrite (drive);

        if (!recorder->priv->can_rewrite) {
                return NAUTILUS_BURN_RECORDER_RESULT_CANCEL;
        }

        is_locked = nautilus_burn_drive_lock (drive, _("Blanking CD"), NULL);

        media_type = nautilus_burn_drive_get_media_type_full (drive, &can_rewrite, &is_blank, NULL, NULL);

        if (media_type == NAUTILUS_BURN_MEDIA_TYPE_ERROR || can_rewrite == FALSE) {
                if (is_locked)
                        nautilus_burn_drive_unlock (drive);

                return NAUTILUS_BURN_RECORDER_RESULT_CANCEL;
        }

        argv = g_ptr_array_new ();
        g_ptr_array_add (argv, "cdrecord");

        dev_str = g_strdup_printf ("dev=%s", nautilus_burn_drive_get_device (drive));
        g_ptr_array_add (argv, dev_str);
        g_ptr_array_add (argv, "-v");

        blank_str = g_strdup_printf ("blank=%s",
                                     (type == NAUTILUS_BURN_RECORDER_BLANK_FAST) ? "fast" : "all");
        g_ptr_array_add (argv, blank_str);

        if (flags & NAUTILUS_BURN_RECORDER_BLANK_DUMMY_WRITE) {
                g_ptr_array_add (argv, "-dummy");
        }

        g_ptr_array_add (argv, NULL);

        result = nautilus_burn_recorder_run_process (recorder,
                                                     NAUTILUS_BURN_RECORDER_MEDIA_CD,
                                                     argv,
                                                     cdrecord_blank_stdout_line,
                                                     cdrecord_stderr_line,
                                                     error);

        if (is_locked) {
                nautilus_burn_drive_unlock (drive);
        }

        g_free (dev_str);
        g_free (blank_str);
        g_ptr_array_free (argv, TRUE);

        return result;
}

static int
nautilus_burn_recorder_blank_disc_dvdrw_format (NautilusBurnRecorder          *recorder,
                                                NautilusBurnDrive             *drive,
                                                NautilusBurnRecorderBlankType  type,
                                                NautilusBurnRecorderBlankFlags flags,
                                                GError                       **error)
{
        NautilusBurnMediaType       media_type;
        NautilusBurnProcessLineFunc out_line_func = NULL;
        NautilusBurnProcessLineFunc err_line_func = NULL;
        GPtrArray                  *argv           = NULL;
        char                       *blank_str      = NULL;
        char                       *dev_str        = NULL;
        gboolean                    is_locked;
        gboolean                    can_rewrite;
        gboolean                    is_blank;
        int                         result         = NAUTILUS_BURN_RECORDER_RESULT_ERROR;

        recorder->priv->can_rewrite = nautilus_burn_drive_can_rewrite (drive);

        if (!recorder->priv->can_rewrite) {
                return NAUTILUS_BURN_RECORDER_RESULT_CANCEL;
        }

        is_locked = nautilus_burn_drive_lock (drive, _("Blanking DVD"), NULL);

        media_type = nautilus_burn_drive_get_media_type_full (drive, &can_rewrite, &is_blank, NULL, NULL);

        if (media_type == NAUTILUS_BURN_MEDIA_TYPE_ERROR || can_rewrite == FALSE) {
                if (is_locked)
                        nautilus_burn_drive_unlock (drive);

                return NAUTILUS_BURN_RECORDER_RESULT_CANCEL;
        }

        if (media_type == NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW
            && type == NAUTILUS_BURN_RECORDER_BLANK_FULL) {
                argv = g_ptr_array_new ();

                g_ptr_array_add (argv, "growisofs");
                g_ptr_array_add (argv, "-Z");

                dev_str = g_strdup_printf ("%s=%s", nautilus_burn_drive_get_device (drive), "/dev/zero");
                g_ptr_array_add (argv, dev_str);
                g_ptr_array_add (argv, NULL);

                out_line_func = growisofs_blank_stdout_line;
                err_line_func = growisofs_stderr_line;
        } else if (!is_blank && media_type == NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW) {
                if (recorder->priv->debug) {
                        g_print ("Skipping fast blank for already formatted DVD+RW media\n");
                }
        } else {
                argv = g_ptr_array_new ();

                g_ptr_array_add (argv, "dvd+rw-format");

                /* undocumented option to show progress */
                g_ptr_array_add (argv, "-gui");

                if (media_type != NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW) {
                        blank_str = g_strdup_printf ("-blank%s", (type == NAUTILUS_BURN_RECORDER_BLANK_FAST) ? "" : "=full");
                        g_ptr_array_add (argv, blank_str);
                }

                dev_str = g_strdup_printf ("%s", nautilus_burn_drive_get_device (drive));
                g_ptr_array_add (argv, dev_str);
                g_ptr_array_add (argv, NULL);

                err_line_func = dvdrw_format_stderr_line;
        }

        if (argv) {
                result = nautilus_burn_recorder_run_process (recorder,
                                                             NAUTILUS_BURN_RECORDER_MEDIA_DVD,
                                                             argv,
                                                             out_line_func,
                                                             err_line_func,
                                                             error);
                g_free (dev_str);
                g_free (blank_str);
                g_ptr_array_free (argv, TRUE);
        }

        if (is_locked) {
                nautilus_burn_drive_unlock (drive);
        }

        return result;
}

static gboolean
nautilus_burn_drive_format_needs_growisofs (NautilusBurnDrive    *drive,
                                            NautilusBurnMediaType type)
{
        if (can_burn_dvds (drive) == FALSE) {
                return FALSE;
        }

        if (type == NAUTILUS_BURN_MEDIA_TYPE_DVDRW
            || type == NAUTILUS_BURN_MEDIA_TYPE_DVD_PLUS_RW) {
                return TRUE;
        }

        return FALSE;
}

/**
 * nautilus_burn_recorder_blank_disc:
 * @recorder: #NautilusBurnRecorder
 * @drive: #NautilusBurnDrive to use
 * @type: #NautilusBurnRecorderBlankType
 * @flags: #NautilusBurnRecorderBlankFlags
 * @error: return location for errors
 *
 * Blank the media in the specified drive.
 *
 * Return value: #NautilusBurnRecorderResult
 **/
int
nautilus_burn_recorder_blank_disc (NautilusBurnRecorder          *recorder,
                                   NautilusBurnDrive             *drive,
                                   NautilusBurnRecorderBlankType  type,
                                   NautilusBurnRecorderBlankFlags flags,
                                   GError                       **error)
{
        NautilusBurnMediaType media_type;
        gboolean              can_rewrite;
        gboolean              is_blank;

        g_return_val_if_fail (recorder != NULL, NAUTILUS_BURN_RECORDER_RESULT_ERROR);
        g_return_val_if_fail (drive != NULL, NAUTILUS_BURN_RECORDER_RESULT_ERROR);

        media_type = nautilus_burn_drive_get_media_type_full (drive, &can_rewrite, &is_blank, NULL, NULL);

        if (media_type == NAUTILUS_BURN_MEDIA_TYPE_ERROR || media_type == NAUTILUS_BURN_MEDIA_TYPE_BUSY) {
                return NAUTILUS_BURN_RECORDER_RESULT_ERROR;
        } else if (can_rewrite == FALSE) {
                return NAUTILUS_BURN_RECORDER_RESULT_ERROR;
        }

        if (nautilus_burn_drive_format_needs_growisofs (drive, media_type)) {
                return nautilus_burn_recorder_blank_disc_dvdrw_format (recorder, drive, type, flags, error);
        } else {
                return nautilus_burn_recorder_blank_disc_cdrecord (recorder, drive, type, flags, error);
        }
}

static void
nautilus_burn_recorder_finalize (GObject *object)
{
        NautilusBurnRecorder *recorder = NAUTILUS_BURN_RECORDER (object);

        g_return_if_fail (object != NULL);

        if (recorder->priv->process) {
                nautilus_burn_process_free (recorder->priv->process);
        }

        if (G_OBJECT_CLASS (nautilus_burn_recorder_parent_class)->finalize != NULL) {
                (* G_OBJECT_CLASS (nautilus_burn_recorder_parent_class)->finalize) (object);
        }
}

static void
nautilus_burn_recorder_init (NautilusBurnRecorder *recorder)
{
        recorder->priv = NAUTILUS_BURN_RECORDER_GET_PRIVATE (recorder);
}

/**
 * nautilus_burn_recorder_new:
 *
 * Create a new #NautilusBurnRecorder.
 *
 * Return value: The new recorder.
 **/
NautilusBurnRecorder *
nautilus_burn_recorder_new (void)
{
        return g_object_new (NAUTILUS_BURN_TYPE_RECORDER, NULL);
}

static void
nautilus_burn_recorder_class_init (NautilusBurnRecorderClass *klass)
{
        GObjectClass *object_class;

        object_class = (GObjectClass *) klass;

        object_class->finalize = nautilus_burn_recorder_finalize;

        g_type_class_add_private (klass, sizeof (NautilusBurnRecorderPrivate));

        /* Signals */
        nautilus_burn_recorder_table_signals [PROGRESS_CHANGED] =
                g_signal_new ("progress-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnRecorderClass,
                                               progress_changed),
                              NULL, NULL,
                              nautilus_burn_recorder_marshal_VOID__DOUBLE_LONG,
                              G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_LONG);
        nautilus_burn_recorder_table_signals [ACTION_CHANGED] =
                g_signal_new ("action-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnRecorderClass,
                                               action_changed),
                              NULL, NULL,
                              nautilus_burn_recorder_marshal_VOID__INT_INT,
                              G_TYPE_NONE, 2, G_TYPE_INT, G_TYPE_INT);
        nautilus_burn_recorder_table_signals [ANIMATION_CHANGED] =
                g_signal_new ("animation-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnRecorderClass,
                                               animation_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
        nautilus_burn_recorder_table_signals [INSERT_MEDIA_REQUEST] =
                g_signal_new ("insert-media-request",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnRecorderClass,
                                               insert_cd_request),
                              NULL, NULL,
                              nautilus_burn_recorder_marshal_BOOLEAN__BOOLEAN_BOOLEAN_BOOLEAN,
                              G_TYPE_BOOLEAN, 3, G_TYPE_BOOLEAN,
                              G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
        nautilus_burn_recorder_table_signals [WARN_DATA_LOSS] =
                g_signal_new ("warn-data-loss",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnRecorderClass,
                                               warn_data_loss),
                              NULL, NULL,
                              nautilus_burn_recorder_marshal_INT__VOID,
                              G_TYPE_INT, 0);
}
