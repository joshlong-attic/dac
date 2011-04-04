/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 8; tab-width: 8 -*-
 *
 * make-iso.c: code to generate iso files
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

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>
#ifdef sun
#include <sys/statfs.h>
#endif
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <math.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "nautilus-burn-process.h"
#include "nautilus-burn-drive.h"
#include "nautilus-burn-recorder-marshal.h"
#include "make-iso.h"

#ifndef HAVE_MKDTEMP
#include "mkdtemp.h"
#endif

#define MAX_ISO_NAME_LEN  32
#define BURN_URI "burn:///"

static void nautilus_burn_iso_class_init      (NautilusBurnIsoClass     *class);
static void nautilus_burn_iso_init            (NautilusBurnIso          *iso);

#define NAUTILUS_BURN_ISO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NAUTILUS_BURN_TYPE_ISO, NautilusBurnIsoPrivate))

struct NautilusBurnIsoPrivate {
        NautilusBurnProcess *process;

        char                *filename;
        char                *filelist;
        guint64              iso_size;
        gboolean             debug;
};

/* Signals */
enum {
        PROGRESS_CHANGED,
        ANIMATION_CHANGED,
        LAST_SIGNAL
};

static int nautilus_burn_iso_table_signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (NautilusBurnIso, nautilus_burn_iso, G_TYPE_OBJECT)

GQuark
nautilus_burn_iso_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark)
                quark = g_quark_from_static_string ("nautilus_burn_iso_error");

        return quark;
}

static gboolean
make_iso_get_free_space (const char *filename,
                         goffset *size)
{
        GFile *file;
        GFileInfo *info;
        
        *size = 0;

        file = g_file_new_for_path (filename);

        info = g_file_query_filesystem_info (file,
                                             G_FILE_ATTRIBUTE_FILESYSTEM_FREE,
                                             NULL, NULL);

        if (info) {
                if (g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE))  {
                        *size = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
                        g_object_unref (info);
                        return FALSE;
                }
                g_object_unref (info);
        }

        return TRUE;
}

/**
 * nautilus_burn_make_iso_cancel:
 *
 * Cancel current creation process
 *
 **/
gboolean
nautilus_burn_iso_cancel (NautilusBurnIso *iso)
{
        gboolean res;

        g_return_val_if_fail (iso != NULL, FALSE);

        res = nautilus_burn_process_cancel (iso->priv->process,
                                            FALSE);

        if (res) {
                if (iso->priv->filename != NULL) {
                        g_unlink (iso->priv->filename);
                }
                iso->priv->process->result = NAUTILUS_BURN_ISO_RESULT_CANCEL;
        }

        return res;
}

static void
write_all (int   fd,
           char *buf,
           int   len)
{
        int bytes;
        int res;

        bytes = 0;
        while (bytes < len) {
                res = write (fd, buf + bytes, len - bytes);
                if (res <= 0) {
                        return;
                }
                bytes += res;
        }
        return;
}

static void
copy_file (const char *source,
           const char *dest)
{
        int         sfd, dfd;
        struct stat stat_buf;
        char        buffer [1024 * 8];
        ssize_t     len;

        if (link (source, dest) == 0) {
                return;
        }

        if (g_stat (source, &stat_buf) != 0) {
                g_warning ("Trying to copy nonexisting file\n");
                return;
        }

        sfd = g_open (source, O_RDONLY, 0);
        if (sfd == -1) {
                g_warning ("Can't copy file (open source failed)\n");
                return;
        }

        dfd = g_open (dest, O_WRONLY | O_CREAT, stat_buf.st_mode);
        if (dfd == -1) {
                close (sfd);
                g_warning ("Can't copy file (open dest '%s' failed)\n", dest);
                perror ("error:");
                return;
        }

        while ( (len = read (sfd, &buffer, sizeof (buffer))) > 0) {
                write_all (dfd, buffer, len);
        }
        close (dfd);
        close (sfd);
}

static char *
escape_path (const char *str)
{
        char       *escaped, *d;
        const char *s;
        int         len;

        s = str;
        len = 1;
        while (*s != 0) {
                if (*s == '\\' ||
                    *s == '=') {
                        len++;
                }

                len++;
                s++;
        }

        escaped = g_malloc (len);

        s = str;
        d = escaped;
        while (*s != 0) {
                if (*s == '\\' ||
                    *s == '=') {
                        *d++ = '\\';
                }

                *d++ = *s++;
        }
        *d = 0;

        return escaped;
}

static gboolean
dir_is_empty (GFile *file)
{
        gboolean                 found_file;
        GFileEnumerator         *enumerator;
        GError                  *error;
        GFileInfo               *info;

        error = NULL;
        enumerator = g_file_enumerate_children (file,
                                                G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                0,
                                                NULL, &error);
        

        if (enumerator != NULL) {
                g_error_free (error);
                return TRUE;
        }

        found_file = FALSE;

        info = g_file_enumerator_next_file (enumerator, NULL, NULL);
        if (info) {
                found_file = TRUE;
                g_object_unref (info);
        }

        g_object_unref (enumerator); /* Also closes it */
        
        return !found_file;
}

static char *
get_backing_file (GFile *file)
{
        char           *mapping;
        GFileInfo *info;

        mapping = NULL;
        info = g_file_query_info (file, "burn::backing-file",
                                  0, NULL, NULL);
        if (info) {
                mapping = g_strdup (g_file_info_get_attribute_byte_string (info,
                                                                           "burn::backing-file"));
                g_object_unref (info);
        }
        
        return mapping;
}

static gboolean
ask_disable_joliet (GtkWindow *parent)
{
        GtkWidget *dialog;
        int        res;

        dialog = gtk_message_dialog_new (parent,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_QUESTION,
                                         GTK_BUTTONS_OK_CANCEL,
                                         _("Disable Microsoft Windows compatibility?"));
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                  "%s",
                                                  _("Some files don't have a suitable name for a Windows-compatible CD.\nDo you want to continue with Windows compatibility disabled?"));
        gtk_window_set_title (GTK_WINDOW (dialog), _("Windows compatibility"));
        res = gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
        return (res == GTK_RESPONSE_OK);
}

struct graft_state {
        GPtrArray *graft_list;
        char      *emptydir;
        int        depth;
        char      *tmpdir;
        char      *copy_to_dir;
        int        copy_depth;
        GList     *remove_files;
        gboolean   found_file;
};

static void
graft_file_end_dir_visitor (struct graft_state *state)
{
        char *last_slash;

        if (state->copy_depth > 0) {
                state->copy_depth--;
                if (state->copy_depth == 0) {
                        g_free (state->copy_to_dir);
                        state->copy_to_dir = NULL;
                } else {
                        last_slash = strrchr (state->copy_to_dir, G_DIR_SEPARATOR);
                        if (last_slash != NULL) {
                                *last_slash = 0;
                        }
                }
        }
}

/* FIXME: This should probably be an inline */
static gboolean
file_info_is_allowed (GFileInfo *info)
{
        GFileType type;
        
        g_return_val_if_fail (info != NULL, FALSE);

        type = g_file_info_get_file_type (info);
        
        /* only allow regular,directory,symlink files */
        if (type != G_FILE_TYPE_REGULAR
            && type != G_FILE_TYPE_DIRECTORY
            && type != G_FILE_TYPE_SYMBOLIC_LINK) {
                return FALSE;
        } else if (type == G_FILE_TYPE_REGULAR
                   && !g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_READ)) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
graft_file_visitor (GFile                *file,
                    const char           *rel_path,
                    GFileInfo            *info,
                    struct graft_state   *state,
                    gboolean             *recurse)
{
        char *mapping, *path1, *path2;
        char *new_copy_dir;
        char *copy_path;
        const char *name;
        GFileType type;

        *recurse = TRUE;

        if (! file_info_is_allowed (info)) {
                g_warning ("Skipping file: %s", rel_path);
                return TRUE;
        }

        name = g_file_info_get_name (info);
        type = g_file_info_get_file_type (info);
        if (state->copy_to_dir != NULL) {
                if (type == G_FILE_TYPE_DIRECTORY) {
                        new_copy_dir = g_build_filename (state->copy_to_dir,
                                                         name,
                                                         NULL);
                        g_free (state->copy_to_dir);
                        state->copy_to_dir = new_copy_dir;
                        g_mkdir (state->copy_to_dir, 0777);
                        state->remove_files = g_list_prepend (state->remove_files, g_strdup (state->copy_to_dir));
                        state->copy_depth++;
                } else {
                        copy_path = g_build_filename (state->copy_to_dir, name, NULL);
                        mapping = get_backing_file (file);
                        if (mapping != NULL) {
                                copy_file (mapping, copy_path);
                                state->remove_files = g_list_prepend (state->remove_files, g_strdup (copy_path));
                        }
                }
                return TRUE;
        }

        if (type != G_FILE_TYPE_DIRECTORY) {
                mapping = get_backing_file (file);
                if (mapping != NULL) {
                        path1 = escape_path (rel_path);
                        path2 = escape_path (mapping);
                        state->found_file = TRUE;
                        g_ptr_array_add (state->graft_list, g_strdup_printf ("%s=%s", path1, path2));
                        g_free (path1);
                        g_free (path2);
                        g_free (mapping);
                }
        } else {
                if (dir_is_empty (file)) {
                        path1 = escape_path (rel_path);
                        path2 = escape_path (state->emptydir);
                        state->found_file = TRUE;
                        g_ptr_array_add (state->graft_list, g_strdup_printf ("%s/=%s", path1, path2));
                        g_free (path1);
                        g_free (path2);
                } else if (state->depth >= 6) {
                        new_copy_dir = g_build_filename (state->tmpdir, "subdir.XXXXXX", NULL);
                        copy_path = mkdtemp (new_copy_dir);
                        if (copy_path != NULL) {
                                state->remove_files = g_list_prepend (state->remove_files, g_strdup (copy_path));
                                state->copy_depth = 1;
                                state->copy_to_dir = copy_path;
                                path1 = escape_path (rel_path);
                                path2 = escape_path (copy_path);
                                state->found_file = TRUE;
                                g_ptr_array_add (state->graft_list, g_strdup_printf ("%s/=%s", path1, path2));
                                g_free (path1);
                                g_free (path2);
                        } else {
                                g_free (new_copy_dir);
                                g_warning ("Couldn't create temp subdir\n");
                                *recurse = FALSE;
                        }
                }
        }

        return TRUE;
}

static void
create_graft_file (GFile                *file,
                   const char           *prefix,
                   struct graft_state   *state)
{
        GFile                   *child;
        GFileInfo               *info;
        gboolean                 stop;
        GFileEnumerator         *enumerator;

        enumerator = g_file_enumerate_children (file,
                                                G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                                G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                                G_FILE_ATTRIBUTE_ACCESS_CAN_READ,
                                                0,
                                                NULL, NULL);
        if (enumerator == NULL) {
                return;
        }

        stop = FALSE;
        while (! stop) {
                char    *rel_path;
                gboolean recurse;

                info = g_file_enumerator_next_file (enumerator, NULL, NULL);
                if (info == NULL) {
                        break;
                }

                child = g_file_get_child (file, g_file_info_get_name (info));

                if (prefix == NULL) {
                        rel_path = g_strdup (g_file_info_get_name (info));
                } else {
                        rel_path = g_strconcat (prefix, g_file_info_get_name (info), NULL);
                }

                recurse = FALSE;
                stop = ! graft_file_visitor (child, rel_path, info, state, &recurse);

                if (! stop
                    && recurse
                    && g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY) {
                        char        *new_prefix;

                        if (prefix == NULL) {
                                new_prefix = g_strconcat (g_file_info_get_name (info), "/",
                                                          NULL);
                        } else {
                                new_prefix = g_strconcat (prefix, g_file_info_get_name (info),
                                                          "/", NULL);
                        }

                        state->depth++;
                        create_graft_file (child,
                                           new_prefix,
                                           state);
                        state->depth--;
                        graft_file_end_dir_visitor (state);

                        g_free (new_prefix);
                }

                g_object_unref (child);
                g_free (rel_path);

                g_object_unref (info);
                
                if (stop) {
                        break;
                }
        }

        g_object_unref (enumerator);
}

void
nautilus_burn_iso_graft_free (NautilusBurnIsoGraft *graft,
                              gboolean              cleanup)
{
        GList *l;

        g_ptr_array_foreach (graft->array, (GFunc)g_free, NULL);
        g_ptr_array_free (graft->array, TRUE);

        for (l = graft->remove_files; l != NULL; l = l->next) {
                if (cleanup && l->data != NULL) {
                        g_remove ((char *)l->data);
                }
                g_free (l->data);
        }
        g_list_free (graft->remove_files);

        g_free (graft);
        graft = NULL;
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
create_temp_dir (void)
{
        char *template;
        char *template_dir;
        char *temp_dir;

        template = g_strdup_printf ("iso-%s.XXXXXX", g_get_user_name ());
        template_dir = g_build_filename (g_get_tmp_dir (), template, NULL);
        g_free (template);
        temp_dir = mkdtemp (template_dir);

        if (temp_dir == NULL) {
                g_free (template_dir);
        }

        return temp_dir;
}

static char *
create_temp_graft_file (NautilusBurnIsoGraft *graft)
{
        char    *filename;
        char    *contents;
        gboolean res;

        filename = create_temp_file ();

        contents = g_strjoinv ("\n", (char **) (graft->array)->pdata);

        res = g_file_set_contents (filename, contents, -1, NULL);
        if (! res) {
                g_free (filename);
                filename = NULL;
        }

        g_free (contents);

        return filename;
}

NautilusBurnIsoGraft *
nautilus_burn_iso_graft_new (const char *uri_string)
{
        NautilusBurnIsoGraft *graft;
        GFile *file;
        struct graft_state    state = { NULL };
        int                   res;

        graft = NULL;

        state.tmpdir = create_temp_dir ();
        if (state.tmpdir == NULL) {
                goto done;
        }

        state.emptydir = g_build_filename (state.tmpdir, "emptydir", NULL);
        res = g_mkdir (state.emptydir, 0777);
        if (res != 0) {
                goto done;
        }

        state.remove_files = g_list_prepend (state.remove_files, g_strdup (state.tmpdir));
        state.remove_files = g_list_prepend (state.remove_files, g_strdup (state.emptydir));
        state.found_file = FALSE;
        state.graft_list = g_ptr_array_new ();

        file = g_file_new_for_uri (uri_string);
        create_graft_file (file, NULL, &state);
        g_object_unref (file);

        /* terminate the array */
        g_ptr_array_add (state.graft_list, NULL);

        graft = g_new0 (NautilusBurnIsoGraft, 1);
        graft->array = state.graft_list;
        graft->remove_files = state.remove_files;

 done:
        g_free (state.emptydir);
        g_free (state.tmpdir);

        return graft;
}

static void
process_error (NautilusBurnIso *iso,
               const char      *message)
{
        g_signal_emit (G_OBJECT (iso),
                       nautilus_burn_iso_table_signals [PROGRESS_CHANGED], 0,
                       1.0, (long)-1);

        nautilus_burn_process_set_error (iso->priv->process,
                                         message,
                                         NAUTILUS_BURN_ISO_RESULT_ERROR);
}

/* Originally eel_make_valid_utf8 */
static char *
ncb_make_valid_utf8 (const char *name)
{
        GString    *string;
        const char *remainder, *invalid;
        int         remaining_bytes, valid_bytes;

        string = NULL;
        remainder = name;
        remaining_bytes = strlen (name);

        while (remaining_bytes != 0) {
                if (g_utf8_validate (remainder, remaining_bytes, &invalid)) {
                        break;
                }
                valid_bytes = invalid - remainder;

                if (string == NULL) {
                        string = g_string_sized_new (remaining_bytes);
                }
                g_string_append_len (string, remainder, valid_bytes);
                g_string_append_c (string, '?');

                remaining_bytes -= valid_bytes + 1;
                remainder = invalid + 1;
        }

        if (string == NULL) {
                return g_strdup (name);
        }

        g_string_append (string, remainder);
        g_string_append (string, _(" (invalid Unicode)"));
        g_assert (g_utf8_validate (string->str, -1, NULL));

        return g_string_free (string, FALSE);
}

static gboolean
readcd_stderr_line (NautilusBurnProcess   *process,
                    const char            *line,
                    gpointer               data)
{
        NautilusBurnIso *iso = data;
        char            *pos;

        if (line && iso->priv->debug) {
                g_print ("readcd stderr: %s", line);
        }

        pos = strstr (line, "addr:");
        if (pos) {
                guint64 byte;
                double  fraction;

                pos += strlen ("addr:");
                byte = (guint64) atol (pos) * 2048; /* reports blocks of 2048 bytes */
                if (iso->priv->iso_size > 0) {
                        fraction = (double) byte / iso->priv->iso_size;
                        g_signal_emit (G_OBJECT (iso),
                                       nautilus_burn_iso_table_signals [PROGRESS_CHANGED], 0,
                                       fraction, (long)-1);

                }
        }

        if (strstr (line, "Time total:")) {
                process->result = NAUTILUS_BURN_ISO_RESULT_FINISHED;
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
cdrdao_stderr_line (NautilusBurnProcess   *process,
                    const char            *line,
                    gpointer               data)
{
        NautilusBurnIso *iso = data;
        int t1, t2, s1, s2, s3;
        int min, sec, sub;

        if (line && iso->priv->debug) {
                g_print ("cdrdao stderr: %s", line);
        }

        if (sscanf (line, "Copying audio tracks %d-%d: start %d:%d:%d, length %d:%d:%d",
                    &t1, &t2, &s1, &s2, &s3, &min, &sec, &sub) == 8) {
                iso->priv->iso_size = (guint64) min * 60 + sec;
        }

        if (sscanf (line, "%d:%d:%d", &min, &sec, &sub) == 3) {
                if (iso->priv->iso_size > 0) {
                        gdouble fraction;
                        guint64 secs = (guint64) min * 60 + sec;
                        guint64 remaining;

                        remaining = -1;
                        if (process->start_time > 0) {
                                guint64 elapsed;
                                gdouble rate;

                                elapsed = time (NULL) - process->start_time;
                                rate = (gdouble)secs / (gdouble)elapsed;

                                if (rate > 0) {
                                        gdouble ave_rate;

                                        ave_rate = get_average_rate (&process->rates, rate);
                                        remaining = (iso->priv->iso_size - secs) / ave_rate;
                                }
                        }

                        fraction = (gdouble) secs / iso->priv->iso_size;

                        g_signal_emit (G_OBJECT (iso),
                                       nautilus_burn_iso_table_signals [PROGRESS_CHANGED], 0,
                                       fraction, (long)remaining);
                }
        }

        if (strstr (line, "Operation not permitted")) {
                process->result = NAUTILUS_BURN_ISO_RESULT_ERROR;
                process_error (iso, line);
        }

        if (strstr (line, "toc and track data finished successfully")) {
                process->result = NAUTILUS_BURN_ISO_RESULT_FINISHED;
        }

        return TRUE;
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

static char *
get_image_space_error_message (guint64 iso_size,
                               guint64 space_available)
{
        char   *space_needed_string;
        char   *message;
        guint64 space_needed;

        space_needed = iso_size - space_available;
        space_needed_string = g_strdup_printf ("%" G_GINT64_FORMAT, space_needed / 1048576);
        message = g_strdup_printf (_("The selected location does not have enough space to store the disc image (%s MiB needed)."),
                                   space_needed_string);
        g_free (space_needed_string);

        return message;
}

static int
nautilus_burn_iso_run_process (NautilusBurnIso            *iso,
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
        process->debug = iso->priv->debug;

        nautilus_burn_process_free (iso->priv->process);
        iso->priv->process = process;

        process->result = 0;

        g_signal_emit (G_OBJECT (iso),
                       nautilus_burn_iso_table_signals [PROGRESS_CHANGED], 0,
                       0.0, (long)-1);
        g_signal_emit (G_OBJECT (iso),
                       nautilus_burn_iso_table_signals [ANIMATION_CHANGED], 0,
                       TRUE);

        local_error = NULL;
        res = nautilus_burn_process_run (process,
                                         argv,
                                         out_line_func,
                                         err_line_func,
                                         iso,
                                         &local_error);

        if (process->result == NAUTILUS_BURN_ISO_RESULT_RETRY) {
                goto retry;
        }

        if (local_error != NULL) {
                g_set_error (error,
                             NAUTILUS_BURN_ISO_ERROR,
                             NAUTILUS_BURN_ISO_ERROR_GENERAL,
                             local_error->message);
                g_error_free (local_error);
        } else if (nautilus_burn_process_get_error (process, &error_message, &error_code)) {
                g_set_error (error,
                             NAUTILUS_BURN_ISO_ERROR,
                             NAUTILUS_BURN_ISO_ERROR_GENERAL,
                             error_message);
                g_free (error_message);
        }

        result = process->result;
        nautilus_burn_process_free (process);
        iso->priv->process = NULL;

        g_signal_emit (G_OBJECT (iso),
                       nautilus_burn_iso_table_signals [ANIMATION_CHANGED], 0,
                       FALSE);

        if (result == 0) {
                result = NAUTILUS_BURN_ISO_RESULT_ERROR;
        }

        return result;
}

gboolean
nautilus_burn_iso_graft_get_info (NautilusBurnIsoGraft   *graft,
                                  guint64                *size,
                                  gboolean               *use_joliet_out,
                                  gboolean               *use_udf,
                                  GError                **error)
{
        GPtrArray            *argv;
        gboolean              use_joliet;
        char                 *stdout_data;
        char                 *stderr_data;
        int                   exit_status;
        guint64               iso_size;
        GError               *sub_error;
        char                 *filelist;
        gboolean              result;

        use_joliet = TRUE;
        argv = NULL;
        filelist = create_temp_graft_file (graft);
        iso_size = 0;

 retry:
        if (argv != NULL) {
                g_ptr_array_free (argv, TRUE);
        }
        argv = g_ptr_array_new ();

        g_ptr_array_add (argv, "mkisofs");
        g_ptr_array_add (argv, "-r");
        if (use_joliet) {
                g_ptr_array_add (argv, "-J");
        }
        if (use_udf) {
                g_ptr_array_add (argv, "-udf");
        }
        if (ncb_mkisofs_supports_utf8 ()) {
                g_ptr_array_add (argv, "-input-charset");
                g_ptr_array_add (argv, "utf8");
        }
        g_ptr_array_add (argv, "-q");
        g_ptr_array_add (argv, "-graft-points");
        g_ptr_array_add (argv, "-path-list");
        g_ptr_array_add (argv, filelist);

        g_ptr_array_add (argv, "-print-size");

        g_ptr_array_add (argv, NULL);

        sub_error = NULL;
        stdout_data = NULL;
        stderr_data = NULL;
        exit_status = 0;

        if (! g_spawn_sync (NULL,
                           (char **)argv->pdata,
                           NULL,
                           G_SPAWN_SEARCH_PATH,
                           NULL, NULL,
                           &stdout_data,
                           &stderr_data,
                           &exit_status,
                           &sub_error)) {
                g_set_error (error, NAUTILUS_BURN_ISO_ERROR,
                             NAUTILUS_BURN_ISO_ERROR_GENERAL,
                             _("Could not run sub process: %s."),
                             sub_error->message);
                g_error_free (sub_error);

                g_ptr_array_free (argv, TRUE);
                argv = NULL;

                result = FALSE;
                goto done;
        }

        g_ptr_array_free (argv, TRUE);
        argv = NULL;

        if (exit_status != 0 && use_joliet) {
                if (strstr (stderr_data, "Joliet tree sort failed.") != NULL) {
                        g_free (stdout_data);
                        g_free (stderr_data);
                        stdout_data = NULL;
                        stderr_data = NULL;
                        if (ask_disable_joliet (NULL)) {
                                use_joliet = FALSE;
                                goto retry;
                        } else {
                                g_set_error (error,
                                             NAUTILUS_BURN_ISO_ERROR,
                                             NAUTILUS_BURN_ISO_ERROR_GENERAL,
                                             _("The operation was cancelled by the user."));
                                result = FALSE;
                                goto done;
                        }
                }
        }

        iso_size = (guint64)atol (stdout_data) * 2048 ; /* mkisofs reports blocks of 2048 bytes */

        result = TRUE;
 done:
        g_remove (filelist);
        g_free (filelist);
        g_free (stderr_data);
        g_free (stdout_data);

        if (size != NULL) {
                *size = iso_size;
        }
        if (use_joliet_out != NULL) {
                *use_joliet_out = use_joliet;
        }

        return result;
}

static gboolean
mkisofs_stdout_line (NautilusBurnProcess   *process,
                     const char            *line,
                     gpointer               data)
{
        NautilusBurnIso *iso = data;

        if (line && iso->priv->debug) {
                g_print ("make_iso stdout: %s", line);
        }

        return TRUE;
}

static gboolean
mkisofs_stderr_line (NautilusBurnProcess   *process,
                     const char            *line,
                     gpointer               data)
{
        NautilusBurnIso *iso = data;
        char            *pos;

        if (line && iso->priv->debug) {
                g_print ("make_iso stderr: %s", line);
        }

        if (strncmp (line, "Total translation table size", 28) == 0) {
                g_signal_emit (G_OBJECT (iso),
                               nautilus_burn_iso_table_signals [PROGRESS_CHANGED], 0,
                               1.0, (long)-1);
                process->result = NAUTILUS_BURN_ISO_RESULT_FINISHED;
        }

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

                        g_signal_emit (G_OBJECT (iso),
                                       nautilus_burn_iso_table_signals [PROGRESS_CHANGED], 0,
                                       fraction / 100.0, secs);
                }
        }

        if (strstr (line, "Incorrectly encoded string")) {
                GString *filename;
                char    *utf8_filename;
                char    *msg;

                filename = g_string_sized_new (32);

                g_string_assign (filename, line);
                /* Erase up to the first ( */
                g_string_erase (filename, 0, strchr (filename->str, '(') - filename->str + 1);
                /* Truncate down to the last ) */
                g_string_truncate (filename, strrchr (filename->str, ')') - filename->str - 1);

                utf8_filename = ncb_make_valid_utf8 (filename->str);
                msg = g_strdup_printf (_("Some files have invalid filenames: \n%s"), utf8_filename);
                if (iso->priv->debug) {
                        g_print ("mkisofs error: %s\n", msg);
                }

                process_error (iso, msg);

                g_free (msg);
                g_free (utf8_filename);
                g_string_free (filename, TRUE);
        }

        if (strstr (line, "Unknown charset")) {
                process_error (iso, _("Unknown character encoding."));
        }

        if (strstr (line, "No space left on device")) {
                process_error (iso, _("There is no space left on the device."));
        }

        if (strstr (line, "Value too large for defined data type")) {
                /* TODO: get filename from error message */
                process_error (iso, _("File too large for filesystem."));
        }

        return TRUE;
}

/**
 * nautilus_burn_iso_make_from_graft:
 * @filename: name of a file to use for the image
 * @graft: a #NautilusBurnIsoGraft
 * @label: a string to use as the label for the image
 * @flags: #NautilusBurnImageCreateFlags
 * @type: #NautilusBurnImageType
 * @error: a return location for errors
 *
 * Create an ISO image in filename from the data files in burn:///
 *
 * Return value: #NautilusBurnIsoResult
 **/
int
nautilus_burn_iso_make_from_graft (NautilusBurnIso             *iso,
                                   NautilusBurnIsoGraft        *graft,
                                   const char                  *filename,
                                   const char                  *label,
                                   NautilusBurnImageCreateFlags flags,
                                   NautilusBurnImageType       *type,
                                   GError                     **error)
{
        GPtrArray            *argv;
        guint64               iso_size;
        goffset               size;
        gboolean              use_joliet;
        gboolean              use_udf   = FALSE;
        int                   result    = NAUTILUS_BURN_ISO_RESULT_ERROR;
        char                 *dirname;
        GError               *sub_error;
        int                   res;
        char                 *filelist;

        filelist = create_temp_graft_file (graft);

        use_joliet = (flags & NAUTILUS_BURN_IMAGE_CREATE_JOLIET);

        use_udf = (flags & NAUTILUS_BURN_IMAGE_CREATE_UDF);

        if (type) {
                *type = NAUTILUS_BURN_IMAGE_TYPE_ISO9660;
        }

        if (label && (strlen (label) > MAX_ISO_NAME_LEN)) {
                g_set_error (error,
                             G_FILE_ERROR,
                             0,
                             _("The label for the image is too long."));
                return NAUTILUS_BURN_ISO_RESULT_ERROR;
        }

        iso->priv->debug = flags & NAUTILUS_BURN_IMAGE_CREATE_DEBUG;
        iso->priv->filename = g_strdup (filename);

        sub_error = NULL;
        if (! nautilus_burn_iso_graft_get_info (graft, &iso_size, &use_joliet, &use_udf, &sub_error)) {
                g_propagate_error (error, sub_error);
                return NAUTILUS_BURN_ISO_RESULT_ERROR;
        }

        dirname = g_path_get_dirname (filename);
        res = make_iso_get_free_space (dirname, &size);

        if (res == -1) {
                g_warning ("Cannot get free space at %s\n", dirname);
                size = 0;
        }

        g_free (dirname);

        if (iso_size > (guint64)size) {
                char *message;

                message = get_image_space_error_message (iso_size,
                                                         size);

                g_set_error (error,
                             NAUTILUS_BURN_ISO_ERROR,
                             NAUTILUS_BURN_ISO_ERROR_GENERAL,
                             "%s", message);
                g_free (message);

                result = NAUTILUS_BURN_ISO_RESULT_RETRY;

                goto cleanup;
        }

        argv = g_ptr_array_new ();
        g_ptr_array_add (argv, "mkisofs");
        g_ptr_array_add (argv, "-r");
        if (use_joliet) {
                g_ptr_array_add (argv, "-J");
        }
        if (use_udf) {
                g_ptr_array_add (argv, "-udf");
        }
        if (ncb_mkisofs_supports_utf8 ()) {
                g_ptr_array_add (argv, "-input-charset");
                g_ptr_array_add (argv, "utf8");
        }
        g_ptr_array_add (argv, "-graft-points");
        g_ptr_array_add (argv, "-path-list");
        g_ptr_array_add (argv, filelist);

        if (label) {
                g_ptr_array_add (argv, "-V");
                g_ptr_array_add (argv, (char *)label);
        }
        g_ptr_array_add (argv, "-o");
        g_ptr_array_add (argv, (char *)filename);

        g_ptr_array_add (argv, NULL);

        result = nautilus_burn_iso_run_process (iso,
                                                argv,
                                                mkisofs_stdout_line,
                                                mkisofs_stderr_line,
                                                error);
        g_ptr_array_free (argv, TRUE);

 cleanup:
        g_remove (filelist);
        g_free (filelist);

        return result;
}

/**
 * nautilus_burn_iso_make:
 * @filename: name of a file to use for the image
 * @label: a string to use as the label for the image
 * @flags: #NautilusBurnImageCreateFlags
 * @type: #NautilusBurnImageType
 * @error: a return location for errors
 *
 * Create an ISO image in filename from the data files in burn:///
 *
 * Return value: #NautilusBurnIsoResult
 **/
int
nautilus_burn_iso_make (NautilusBurnIso             *iso,
                        const char                  *filename,
                        const char                  *label,
                        NautilusBurnImageCreateFlags flags,
                        NautilusBurnImageType       *type,
                        GError                     **error)
{
        int                   result;
        NautilusBurnIsoGraft *graft;

        graft = nautilus_burn_iso_graft_new (BURN_URI);
        if (graft == NULL || graft->array == NULL) {
                g_set_error (error,
                             NAUTILUS_BURN_ISO_ERROR,
                             NAUTILUS_BURN_ISO_ERROR_GENERAL,
                             _("There are no files to write to disc."));
                result = NAUTILUS_BURN_ISO_RESULT_ERROR;
                goto cleanup;
        }

        result = nautilus_burn_iso_make_from_graft (iso,
                                                    graft,
                                                    filename,
                                                    label,
                                                    flags,
                                                    type,
                                                    error);
 cleanup:
        nautilus_burn_iso_graft_free (graft, TRUE);

        return result;
}

static gboolean
get_disc_info (NautilusBurnIso   *iso,
               NautilusBurnDrive *drive,
               guint64           *size,
               gboolean          *has_audio,
               GError           **error)
{
        char                 *stdout_data;
        char                 *stderr_data;
        char                 *pos;
        NautilusBurnMediaType media_type;
        gboolean              is_rewritable;
        gboolean              is_blank;
        gboolean              has_data;
        gboolean              _has_audio;
        char                 *device_arg;
        int                   exit_status;
        gboolean              ret;
        GError               *sub_error;
        GPtrArray            *argv;
        guint64               iso_size;

        media_type = nautilus_burn_drive_get_media_type_full (drive,
                                                              &is_rewritable,
                                                              &is_blank,
                                                              &has_data,
                                                              &_has_audio);
        if (_has_audio) {
                device_arg = g_strdup_printf ("%s", nautilus_burn_drive_get_device (drive));

                argv = g_ptr_array_new ();
                g_ptr_array_add (argv, "cdrdao");
                g_ptr_array_add (argv, "disk-info");
                g_ptr_array_add (argv, "--device");
                g_ptr_array_add (argv, device_arg);
                g_ptr_array_add (argv, NULL);

        } else {
                device_arg = g_strdup_printf ("-dev=%s", nautilus_burn_drive_get_device (drive));

                argv = g_ptr_array_new ();
                g_ptr_array_add (argv, "readcd");
                g_ptr_array_add (argv, "-sectors=0-0");
                g_ptr_array_add (argv, device_arg);
                g_ptr_array_add (argv, "-f=/dev/null");
                g_ptr_array_add (argv, NULL);
        }

        sub_error = NULL;

        if (! g_spawn_sync (NULL,
                            (char **)argv->pdata,
                            NULL,
                            G_SPAWN_SEARCH_PATH,
                            NULL, NULL,
                            &stdout_data,
                            &stderr_data,
                            &exit_status,
                            &sub_error)) {
                g_set_error (error, NAUTILUS_BURN_ISO_ERROR,
                             NAUTILUS_BURN_ISO_ERROR_GENERAL,
                             _("Could not run sub process: %s."),
                             sub_error->message);
                g_error_free (sub_error);
                ret = FALSE;
                goto cleanup;
        }

        g_ptr_array_free (argv, TRUE);
        argv = NULL;
        g_free (stdout_data);

        if (_has_audio) {
                /* assume audio CD is 800 MiB */
                iso_size = 838860800;
        } else {
                pos = strstr (stderr_data, "Capacity:");
                if (pos) {
                        pos += strlen ("Capacity:");
                        iso_size = (guint64) atol (pos) * 2048; /* reports blocks of 2048 bytes */
                } else {
                        iso_size = 0;
                }
        }

        ret = TRUE;

 cleanup:
        if (argv) {
                g_ptr_array_free (argv, TRUE);
                argv = NULL;
        }
        g_free (stderr_data);
        g_free (device_arg);

        if (has_audio != NULL) {
                *has_audio = _has_audio;
        }

        if (size != NULL) {
                *size = iso_size;
        }

        return ret;
}

/**
 * nautilus_burn_iso_make_from_drive:
 * @filename: name of a file to use for the image
 * @drive: #NautilusBurnDrive from which to read the source media
 * @warn_low_space: set %TRUE issue a warning when disk space is low
 *
 * Create an ISO image in filename from the data in @drive
 *
 * Return value: #NautilusBurnIsoResult
 **/
int
nautilus_burn_iso_make_from_drive (NautilusBurnIso             *iso,
                                   const char                  *filename,
                                   NautilusBurnDrive           *drive,
                                   NautilusBurnImageCreateFlags flags,
                                   NautilusBurnImageType       *type,
                                   char                       **toc_filename,
                                   GError                     **error)
{
        GPtrArray            *argv = NULL;
        GError               *sub_error;
        char                 *dirname;
        guint64               iso_size;
        goffset               size;
        NautilusBurnProcessLineFunc out_watch_func = NULL;
        NautilusBurnProcessLineFunc err_watch_func = NULL;
        int                   result;
        int                   res;
        gboolean              has_audio;
        char                 *filename_arg;
        char                 *toc_filename_arg;
        char                 *dev_arg;

        if (toc_filename) {
                *toc_filename = NULL;
        }
        if (type) {
                *type = NAUTILUS_BURN_IMAGE_TYPE_UNKNOWN;
        }

        iso->priv->debug = (flags & NAUTILUS_BURN_IMAGE_CREATE_DEBUG);
        iso->priv->filename = g_strdup (filename);

        sub_error = NULL;
        if (! get_disc_info (iso, drive, &iso_size, &has_audio, &sub_error)) {
                g_propagate_error (error, sub_error);
                return NAUTILUS_BURN_ISO_RESULT_ERROR;
        }

        if (has_audio) {
                if (type != NULL) {
                        *type = NAUTILUS_BURN_IMAGE_TYPE_BINCUE;
                }
        } else {
                if (type != NULL) {
                        *type = NAUTILUS_BURN_IMAGE_TYPE_ISO9660;
                }
        }

        iso->priv->iso_size = iso_size;

        dirname = g_path_get_dirname (filename);
        res = make_iso_get_free_space (dirname, &size);

        if (res == -1) {
                g_warning ("Cannot get free space at %s\n", dirname);

                size = 0;
        }

        g_free (dirname);

        if (iso_size > (guint64)size) {
                char *message;

                message = get_image_space_error_message (iso_size,
                                                         size);

                g_set_error (error, NAUTILUS_BURN_ISO_ERROR,
                             NAUTILUS_BURN_ISO_ERROR_GENERAL,
                             "%s", message);
                g_free (message);

                result = NAUTILUS_BURN_ISO_RESULT_RETRY;

                goto cleanup;
        }

        filename_arg = NULL;
        dev_arg = NULL;
        toc_filename_arg = g_strdup_printf ("%s.toc", filename);
        if (toc_filename) {
                *toc_filename = g_strdup (toc_filename_arg);
        }
        if (has_audio) {
                argv = g_ptr_array_new ();
                g_ptr_array_add (argv, "cdrdao");
                g_ptr_array_add (argv, "read-cd");
                g_ptr_array_add (argv, "--read-raw");
                g_ptr_array_add (argv, "--datafile");
                g_ptr_array_add (argv, (char *)filename);
                g_ptr_array_add (argv, "--device");
                g_ptr_array_add (argv, (char *)nautilus_burn_drive_get_device (drive));
                g_ptr_array_add (argv, "-v");
                g_ptr_array_add (argv, "2");
                g_ptr_array_add (argv, (char *)toc_filename_arg);
                g_ptr_array_add (argv, NULL);

                out_watch_func = NULL;
                err_watch_func = cdrdao_stderr_line;
        } else {

                filename_arg = g_strdup_printf ("f=%s", filename);

                dev_arg = g_strdup_printf ("dev=%s", nautilus_burn_drive_get_device (drive));

                argv = g_ptr_array_new ();
                g_ptr_array_add (argv, "readcd");
                g_ptr_array_add (argv, (char *)dev_arg);
                g_ptr_array_add (argv, (char *)filename_arg);
                g_ptr_array_add (argv, NULL);

                out_watch_func = NULL;
                err_watch_func = readcd_stderr_line;
        }

        result = nautilus_burn_iso_run_process (iso,
                                                argv,
                                                out_watch_func,
                                                err_watch_func,
                                                error);
        g_ptr_array_free (argv, TRUE);
        g_free (dev_arg);
        g_free (filename_arg);
        g_free (toc_filename_arg);

 cleanup:

        return result;
}

/**
 * nautilus_burn_iso_verify:
 * @filename: name of a file to use for the image
 * @iso_label: return location for the image label
 * @error: return location for errors
 *
 * Verify that filename is a valid ISO image
 *
 * Return value: %TRUE if filename is a valid ISO image, otherwise %FALSE
 **/
gboolean
nautilus_burn_iso_verify (NautilusBurnIso *iso,
                          const char      *filename,
                          char           **iso_label,
                          GError         **error)
{
        FILE  *file;
#define BUFFER_SIZE 128
        char  buf [BUFFER_SIZE+1];
        int   res;
        char *str, *str2;

        file = fopen (filename, "rb");
        if (file == NULL) {
                int err = errno;
                if (error != NULL) {
                        *error = g_error_new_literal (g_file_error_quark (),
                                                      g_file_error_from_errno (err),
                                                      strerror (err));
                }
                return FALSE;
        }
        /* Verify we have an ISO image */
        /* This check is for the raw sector images */
        res = fseek (file, 37633L, SEEK_SET);
        if (res) {
                goto bail;
        }
        res = fread (buf, sizeof (char), 5, file);
        if (res != 5 || strncmp (buf, "CD001", 5) != 0) {
                /* Standard ISO images */
                res = fseek (file, 32769L, SEEK_SET);
                if (res) {
                        goto bail;
                }
                res = fread (buf, sizeof (char), 5, file);
                if (res != 5 || strncmp (buf, "CD001", 5) != 0) {
                        /* High Sierra images */
                        res = fseek (file, 32776L, SEEK_SET);
                        if (res) {
                                goto bail;
                        }
                        res = fread (buf, sizeof (char), 5, file);
                        if (res != 5 || strncmp (buf, "CDROM", 5) != 0) {
                                goto bail;
                        }
                }
        }
        /* Extract the volume label from the image */
        res = fseek (file, 32808L, SEEK_SET);
        if (res) {
                goto bail;
        }
        res = fread (buf, sizeof(char), BUFFER_SIZE, file);
        if (res != BUFFER_SIZE) {
                goto bail;
        }
        buf [BUFFER_SIZE] = '\0';
        str = g_strdup (g_strstrip (buf));
        if (!g_utf8_validate (str, -1, NULL)) {
                /* Hmm, not UTF-8. Try the current locale. */
                str2 = g_locale_to_utf8 (str, -1, NULL, NULL, NULL);
                if (str2 == NULL) {
                        str2 = ncb_make_valid_utf8 (str);
                }
                g_free (str);
                str = str2;
        }
        fclose (file);
        *iso_label = str;
        return TRUE;

 bail:
        if (error != NULL) {
                *error = g_error_new_literal (NAUTILUS_BURN_ISO_ERROR,
                                              NAUTILUS_BURN_ISO_ERROR_GENERAL,
                                              _("Not a valid disc image."));
        }

        return FALSE;
}

static void
nautilus_burn_iso_finalize (GObject *object)
{
        NautilusBurnIso *iso = NAUTILUS_BURN_ISO (object);

        g_return_if_fail (object != NULL);

        if (iso->priv->process) {
                nautilus_burn_process_free (iso->priv->process);
        }

        G_OBJECT_CLASS (nautilus_burn_iso_parent_class)->finalize (object);
}

static void
nautilus_burn_iso_init (NautilusBurnIso *iso)
{
        iso->priv = NAUTILUS_BURN_ISO_GET_PRIVATE (iso);
}

/**
 * nautilus_burn_iso_new:
 *
 * Create a new #NautilusBurnIso.
 *
 * Return value: The new iso.
 **/
NautilusBurnIso *
nautilus_burn_iso_new (void)
{
        return g_object_new (NAUTILUS_BURN_TYPE_ISO, NULL);
}

static void
nautilus_burn_iso_class_init (NautilusBurnIsoClass *klass)
{
        GObjectClass *object_class;

        object_class = (GObjectClass *) klass;

        object_class->finalize = nautilus_burn_iso_finalize;

        g_type_class_add_private (klass, sizeof (NautilusBurnIsoPrivate));

        /* Signals */
        nautilus_burn_iso_table_signals [PROGRESS_CHANGED] =
                g_signal_new ("progress-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnIsoClass,
                                               progress_changed),
                              NULL, NULL,
                              nautilus_burn_recorder_marshal_VOID__DOUBLE_LONG,
                              G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_LONG);
        nautilus_burn_iso_table_signals [ANIMATION_CHANGED] =
                g_signal_new ("animation-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (NautilusBurnIsoClass,
                                               animation_changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}
