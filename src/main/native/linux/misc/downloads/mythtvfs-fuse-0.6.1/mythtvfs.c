/*
    MythTV FUSE Module
    Copyright (C) 2005-2009  Kees Cook <kees@outflux.net>

    FUSE: Filesystem in Userspace
    Copyright (C) 2001-2005  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    This module implements a filename overlay between MythTV and
    Galleon.

    TODO:
        * switch to GString for all the string manipulations
        * add option for logging to syslog

*/

#include <config.h>

#define _GNU_SOURCE
#if defined(linux) || defined(__GLIBC__)
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

// report on the directory aiming calculations
#undef MYTHTVFS_DEBUG_AIM
// report on the calling paths
#undef MYTHTVFS_DEBUG_FUNC

// duplicated from programinfo.h
#define FL_CUTLIST 0x0002

#include <glib.h>
#include <fuse.h>
#include <fuse_opt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statfs.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#include <malloc.h>  // realloc
#include <stdlib.h>  // exit
#include <strings.h> // rindex
#include <time.h>    // strftime
#include <string.h>  // memset
#include <inttypes.h> // formatting
#include <pthread.h> // thread locking

// networking
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

// elements we're interested in from the MythTV database of programs
struct show_t {
    char * title;
    char * subtitle;
    char * description;
    char * callsign;
    char * pathname;
    char * chanid;
    char * starttime;
    char * endtime;
    char * airdate;
    int flags;
};

// options specific to mythtvfs
enum {
    KEY_HELP,
    KEY_VERSION,
};

// global state structure
struct mythtvfs {
    /* configurable */
    char * base_path;
    char * host;
    int    port;
    char * format;
    char * date_format;
    char * datetime_format;
    int short_display_flag;
    int backend_version_override; // manually override compile-time defaults
    int program_width_override;   // manually override compile-time defaults
    char * invalid_chars;         // list of invalid characters
    char * replacement_char;      // character to replace invalid chars with
    char * logfile_name;

    /* runtime state trackers */
    pthread_mutex_t lock;
    size_t base_len;  // cached strlen(base_path)
    int backend;      // backend fd
    int program_cols; // how many columns are reported from this backend version
    FILE * logfile;
    GHashTable *recordings; // hash table of recordings
};

static struct mythtvfs mythtvfs;

#define MYTHTVFS_OPT(t, p, v) { t, offsetof(struct mythtvfs, p), v }

static struct fuse_opt mythtvfs_opts[] = {
    /* -o options */
    MYTHTVFS_OPT("host=%s",            host, 0),
    MYTHTVFS_OPT("port=%d",            port, 0),
    MYTHTVFS_OPT("format=%s",          format, 0),
    MYTHTVFS_OPT("date_format=%s",     date_format, 0),
    MYTHTVFS_OPT("datetime_format=%s", datetime_format, 0),
    MYTHTVFS_OPT("short-display",      short_display_flag, 1),
    MYTHTVFS_OPT("logfile=%s",         logfile_name, 0),
    MYTHTVFS_OPT("backend-version=%d", backend_version_override, 0),
    MYTHTVFS_OPT("program-width=%d",   program_width_override, 0),
    MYTHTVFS_OPT("invalid-chars=%s",   invalid_chars, 0),
    MYTHTVFS_OPT("replacement-char=%s",replacement_char, 0),

    FUSE_OPT_KEY("-V",                KEY_VERSION),
    FUSE_OPT_KEY("--version",         KEY_VERSION),
    FUSE_OPT_KEY("-h",                KEY_HELP),
    FUSE_OPT_KEY("--help",            KEY_HELP),
    FUSE_OPT_END
};

static void usage(const char *progname)
{
    fprintf(stderr,
        "Usage: %s [options] -o host=BACKEND original-path mythtvfs-mount\n"
        "\n"
        "general options:\n"
        "    -o opt,[opt...]        mount options\n"
        "    -h   --help            print help\n"
        "    -V   --version         print version\n"
        "\n"
        "MythTVfs options:\n"
        "    -o host=HOST          mythtv backend hostname\n"
        "    -o port=PORT          mythtv backend host port\n"
        "    -o format=FORMAT      format for mythtvfs filenames\n"
        "    -o date-format=FORMAT format for date components of filenames\n"
        "    -o datetime-format=S  format for combined date and time components\n"
        "    -o short-display      shorter filename display\n"
        "    -o logfile=FILE       write verbose logging to FILE\n"
        "    -o backend-version=N  override compile-time backend version\n"
        "    -o program-width=N    override compile-time program width\n"
        "    -o invalid-chars=STR  list of characters not allowed in filenames\n"
        "    -o replacement-char=C character to replace invalid characters with\n"
        "\n",progname);
}


//
// Filesystem overlay handling
//
static char * aimed_path = NULL;
static size_t aimed_len  = 0;
static char * aim_to_base(const char * oldpath)
{
    size_t len = 0;
    char * path = (char *)oldpath;

#ifdef MYTHTVFS_DEBUG_AIM
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"aim '%s'\n",oldpath);
#endif

    /* skip leading slashes */
    while (path && *path=='/') path++;
    /* make sure path is NULL for "empty" */
    if (path && *path=='\0') path=NULL;

    /* calculate new path length */
    if (path) len = strlen(path); 
    len += mythtvfs.base_len + 1 /* "/" */ + 1 /* NULL */;

    /* grow path length */
    if (aimed_len < len) {
        if (!(aimed_path = realloc(aimed_path,len))) {
            perror("realloc");
            exit(1);
        }
        aimed_len = len;
    }

    snprintf(aimed_path,aimed_len,"%s%s%s",
             mythtvfs.base_path,
             path ? "/" : "",
             path ? path : "");

#ifdef MYTHTVFS_DEBUG_AIM
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"aimed '%s'\n",aimed_path);
#endif

    return aimed_path;
}

// hash table item
struct mythtv_recording
{
    char * path;
    gboolean stale;
};

static char * fixup(const char * oldpath)
{
    char * path = (char *)oldpath;
    struct mythtv_recording * recording;

#ifdef MYTHTVFS_DEBUG_AIM
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"fix '%s'\n",oldpath);
#endif

    /* make sure path is "" for "empty" */
    if (!path) path="";
    while (*path == '/') { path++; }

    recording = g_hash_table_lookup(mythtvfs.recordings, path);
    if (recording) {
        return aim_to_base(recording->path);
    }
    else {
        return aim_to_base(path);
    }
}

//
// Backend communication handling
//
void backend_close()
{
    if (mythtvfs.backend != -1) close(mythtvfs.backend);
    mythtvfs.backend = -1;
}

void backend_client_check();

int backend_init()
{
    struct sockaddr_in host_addr;
    struct hostent *hostinfo;

    if (mythtvfs.backend != -1) close(mythtvfs.backend);

    if ((mythtvfs.backend = socket(AF_INET, SOCK_STREAM, 0))<0) {
        perror("socket");
        exit(1);
    }
    if (!(hostinfo = gethostbyname(mythtvfs.host))) {
        perror(mythtvfs.host);
        exit(1);
    }
    bzero((char*)&host_addr,sizeof(host_addr));
    host_addr.sin_family = AF_INET;
    bcopy((char*)hostinfo->h_addr_list[0],
          (char*)&host_addr.sin_addr.s_addr,
          hostinfo->h_length);
    host_addr.sin_port = htons(mythtvfs.port);
    if (connect(mythtvfs.backend,(struct sockaddr*)&host_addr,sizeof(host_addr))<0) {
        fprintf(stderr,"%s:%d ",mythtvfs.host,mythtvfs.port);
        perror("connect");
        backend_close();
        return 0;
    }

    // must attach as a client
    backend_client_check();

    return 1;
}

int backend_cmd(char * cmd, char ** output)
{
    char * send;
    uint32_t len, cmdlen, bytes;
    char size[8];

    if (!output) return 0;

    if (mythtvfs.backend == -1 && !backend_init()) return 0;

    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"Sending '%s'\n",cmd);

    cmdlen = strlen(cmd);
    len = cmdlen + 8 /* length prefix */ + 1 /* NULL */;
    if (!(send = malloc(len))) {
        perror("malloc");
        exit(1);
    }
    snprintf(send,len,"%-8d%s",cmdlen,cmd);
    len--; /* don't send the NULL */

    if ((bytes = write(mythtvfs.backend,send,len)) != len) {
        free(send);
        perror("write");
        backend_close();
        return 0;
    }
    free(send);

    if (!strcmp(cmd,"DONE")) return 1;

    if ((bytes = read(mythtvfs.backend,size,8)) != 8) {
        perror("read size");
        backend_close();
        return 0;
    }

    len = atoi(size);

    if (!(*output=realloc(*output,len+1))) {
        perror("realloc");
        exit(1);
    }

    bytes = 0;
    while (bytes != len) {
        int got = 0;
        if ((got = read(mythtvfs.backend,*output+bytes,len))<0) {
            perror("read data");
            backend_close();
            return 0;
        }
        bytes+=got;
    }
    // terminate it
    (*output)[len]='\0';

    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"Got %d '%s'\n",len,*output);

    return 1;
}

void backend_client_check()
{
    char *check = NULL;

    if (!backend_cmd("ANN Playback myself 0",&check)) {
        fprintf(stderr,"Cannot register with backend!\n");
        exit(1);
    }
    if (strcmp(check,"OK")) {
        fprintf(stderr,"Backend would not accept another playback client\n");
        exit(1);
    }
    if (check) free(check);
}

#define MYTH_SEP "[]:[]"

int _version_check(int requested_version)
{
    char *check = NULL, *result, *version="unknown";
    char version_request[80];
    int rc = 0;

    snprintf(version_request, 80, "MYTH_PROTO_VERSION %d", requested_version);
    if (!backend_cmd(version_request, &check)) {
            fprintf(stderr,"Cannot check backend version!\n");
            exit(1);
    }
    result = check;
    version = strstr(check,MYTH_SEP);
    *version = '\0';
    version += strlen(MYTH_SEP);
    rc = atoi(version);
    if (strcmp(result,"ACCEPT")) {
        backend_close();
        rc = 0 - rc;
    }

    if (check) free(check);
    return rc;
}

struct backend_widths {
    int version;
    int width;
};

int guess_program_cols(void);

void backend_version_check()
{
    /*
     * Version found in libs/libmythdb/mythversion.h MYTH_PROTO_VERSION
     * Columns found in libs/libmyth/programinfo.h NUMPROGRAMLINES
     */
    struct backend_widths versions[] = {
        { 50, 47 },
        { 40, 46 },
        { 31, 42 },
        { 26, 41 },
        { 15, 39 },
        { 0,  0  },
    };
    int idx, version = 0;
    int found=0;

    // check for override version first
    if (mythtvfs.backend_version_override &&
        (version = _version_check(mythtvfs.backend_version_override))>0) {
        mythtvfs.program_cols = versions[0].width;
        found = 1;
    }

    // check for known versions
    for (idx = 0; !found && versions[idx].version; idx++) {
        if ( (version = _version_check(versions[idx].version))<0 ) continue;

        mythtvfs.program_cols = versions[idx].width;
        found = 1;
    }

    if (!found) {
        version = -version;
        if (mythtvfs.program_width_override) {
            fprintf(stderr,"Warning: unfamiliar backend version (%d), attempting width %d anyway.\n",version, mythtvfs.program_width_override);
            if (_version_check(version) == version) {
                mythtvfs.program_cols = mythtvfs.program_width_override;
                found = 1;
            }
        }

        if (!found) {
            int guess = guess_program_cols();
            if (guess > 0) {
                mythtvfs.program_cols = guess;
                found = 1;
                fprintf(stderr,"Warning: unfamiliar backend version (%d), attempting guessed width %d.\n",version, mythtvfs.program_cols);
            }
            else {
                fprintf(stderr,"Could not find compatible protocol version (mythtvfs=%d, backend=%d)\nPerhaps try with '-o program-width=N' where N is the new protocol width?\n",versions[0].version,version);
                exit(1);
            }
        }
    }

    if (mythtvfs.program_width_override) {
        mythtvfs.program_cols = mythtvfs.program_width_override;
    }
}

void backend_done()
{
    char * answer = NULL;
    backend_cmd("DONE",&answer);
    if (answer) free(answer);
    backend_close();
}

struct cutlist_t {
    int endmark;
    long long location;
    struct cutlist_t * next;
};

void free_cutlist(struct cutlist_t *cutlist)
{
    struct cutlist_t *next;

    while (cutlist) {
        next = cutlist->next;
        free(cutlist);
        cutlist = next;
    }
}

// this function destroys the contents of *buffer (writes NULLs on separator)
char * answer_chunk(char **buffer)
{
    if (!buffer) return NULL;
    char * data = *buffer;
    if (!data) return NULL;
    char *sep;

    if ((sep = strstr(data,MYTH_SEP))!=NULL) {
        *sep = '\0';
        sep+=strlen(MYTH_SEP);
    }

    *buffer = sep;
    return data;
}

struct cutlist_t * cutlist_from_answer(char *answer)
{
    char * buffer = answer;
    struct cutlist_t *cutlist_head = NULL;
    struct cutlist_t *cutlist_tail = NULL;
    struct cutlist_t *cutlist = NULL;
    char * strval;
    int items = 0;

    if (!answer) return NULL;

    strval = answer_chunk(&buffer);
    items = atoi(strval);

    while (items-- > 0) {
        cutlist = calloc(1, sizeof *cutlist);
        if (!cutlist) goto freak_out;
        if (!cutlist_head) cutlist_head = cutlist;
        if (!cutlist_tail) cutlist_tail = cutlist;
        else {
            cutlist_tail->next = cutlist;
            cutlist_tail = cutlist_tail->next;
        }

        strval = answer_chunk(&buffer);
        if (!strval) {
            goto freak_out;
        }
        cutlist->endmark = atoi(strval);

        strval = answer_chunk(&buffer);
        if (!strval) {
            goto freak_out;
        }
        cutlist->location = (long long)(atol(strval)) << 32;

        strval = answer_chunk(&buffer);
        if (!strval) {
            goto freak_out;
        }
        cutlist->location |= (long long)(atol(strval)) & 0xffffffffLL;
    }

    free(answer);
    return cutlist_head;

freak_out:
    free(answer);
    free_cutlist(cutlist_head);
    return NULL;
}

struct cutlist_t * backend_query_cutlist(char *chanid, char *starttime)
{
    char *answer = NULL, *request = NULL;
    if (asprintf(&request, "QUERY_CUTLIST %s %s", chanid, starttime)<0 ||
        !request) return NULL;
    backend_cmd(request, &answer);
    free(request);

    return cutlist_from_answer(answer);
}

char * backend_query()
{
    char * answer = NULL;
    backend_cmd("QUERY_RECORDINGS View",&answer);

    return answer;
}

void clear_show(struct show_t * show)
{
    if (show->title) free(show->title);
    if (show->subtitle) free(show->subtitle);
    if (show->description) free(show->description);
    if (show->callsign) free(show->callsign);
    if (show->pathname) free(show->pathname);
    if (show->chanid) free(show->chanid);
    if (show->starttime) free(show->starttime);
    if (show->endtime) free(show->endtime);
    if (show->airdate) free(show->airdate);
    memset(show,0,sizeof(*show));
}

char * backend_pop_program(char * buffer, struct show_t * show, int col_width)
{
    int col;

    for (col = 0; buffer && col < col_width; col++) {
        char * sep = NULL;
        char * data = buffer;
        if ((sep = strstr(data,MYTH_SEP))!=NULL) {
            *sep = '\0';
            sep+=strlen(MYTH_SEP);
        }

        // make sure we're not null
        if (!data) data="";

        /* Found in libs/libmyth/programinfo.cpp ProgramInfo::ToStringList */
        switch (col) {
            case 1: show->title = strdup(data); break;
            case 2: show->subtitle = strdup(data); break;
            case 3: show->description = strdup(data); break;
            /*
            case 4: show->category = strdup(data); break;
            */
            case 5: show->chanid = strdup(data); break;
            /*
            case 6: show->channum = strdup(data); break;
            */
            case 7: show->callsign = strdup(data); break;
            /*
            case 8: show->channame = strdup(data); break;
            */
            case 9: show->pathname = strdup(data); break;
            /*
            case 10: show->filesize_hi = strdup(data); break;
            case 11: show->filesize_lo = strdup(data); break;
            */
            case 12: show->starttime = strdup(data); break;
            case 13: show->endtime = strdup(data); break;
            /*
            case 14: show->duplicate = strdup(data); break;
            case 15: show->shareable = strdup(data); break;
            case 16: show->findid = strdup(data); break;
            case 17: show->hostname = strdup(data); break;
            case 18: show->sourceid = strdup(data); break;
            case 19: show->cardid = strdup(data); break;
            case 20: show->inputid = strdup(data); break;
            case 21: show->recpriority = strdup(data); break;
            case 22: show->recstatus = strdup(data); break;
            case 23: show->recordid = strdup(data); break;
            case 24: show->rectype = strdup(data); break;
            case 25: show->dupin = strdup(data); break;
            case 26: show->dupmethod = strdup(data); break;
            case 27: show->recstartts = strdup(data); break;
            case 28: show->recendts = strdup(data); break;
            case 29: show->repeat = strdup(data); break;
            */
            case 30: show->flags = atoi(data); break;
            /*
            case 31: show->recgroup = strdup(data); break;
            case 32: show->commfree = strdup(data); break;
            case 33: show->outputfilters = strdup(data); break;
            case 34: show->seriesid = strdup(data); break;
            case 35: show->programid = strdup(data); break;
            case 36: show->lastmodified = strdup(data); break;
            case 37: show->stars = strdup(data); break;
            */
            case 38: show->airdate = strdup(data); break;
            /*
            case 39: show->hasairdate = strdup(data); break;
            case 40: show->playgroup = strdup(data); break;
            case 41: show->recpriority = strdup(data); break;
            case 42: show->parentid = strdup(data); break;
            case 43: show->storagegroup = strdup(data); break;
            case 44: show->audioprop = strdup(data); break;
            case 45: show->videoprop = strdup(data); break;
            case 46: show->subtitletype = strdup(data); break;
            case 47: show->year = strdup(data); break;
            ...
            */
            default:
                break;
        }
        buffer = sep;
    }

    return buffer;
}

/* return guessed program column width, or -1 for failure */
int guess_program_cols(void)
{
    /* Since most of the program data are either arbitrary numbers of
       free-form text, we need to find something that stands out as
       system-specific to calculate program column width.  This requires
       that at least 2 recordings exist (to determine a delta), and that
       the recording pathname is fully qualified (which makes the
       assumption that other text strings do not start with "/").  Since
       storage pools may be different, we cannot use more than the first
       character (though different storage pools would break the current
       mythtvfs anyway, since it assumes that all recordings are in the
       same place).
     */

    char * buffer, * data;
    int guessed = 0;
    struct show_t show = { };

    /* First, extract the pathname, which will be at col 9 */
    data = buffer = backend_query();
    data = backend_pop_program(data, &show, 10);
    /* Make sure path has a leading slash */
    if (!show.pathname || strncmp(show.pathname, "myth://", 7)!=0) return -1;

    while (data) {
        char * sep = NULL;

        guessed ++;
        if ((sep = strstr(data,MYTH_SEP))!=NULL) {
            *sep = '\0';
            sep+=strlen(MYTH_SEP);
            if (strncmp(data, "myth://", 7) == 0) break;
        }
        data = sep;
    }

    if (!data) guessed = -1;
    free(buffer);

    return guessed;
}

void mark_stale(gpointer key, gpointer value, gpointer user_data)
{
    struct mythtv_recording * recording = (struct mythtv_recording*)value;
    recording->stale = TRUE;
}

struct readdir_info
{
    fuse_fill_dir_t filler;
    void * fuse_buf;
    struct stat st;
};

void fill_readdir_struct(gpointer key, gpointer value, gpointer user_data)
{
    char *name = (char *)key;
    struct readdir_info * info = (struct readdir_info*)user_data;

    info->st.st_ino++;
    info->st.st_mode = S_IFREG|S_IRUSR|S_IRGRP|S_IROTH;
    info->filler(info->fuse_buf, name, &(info->st), 0);
}

gboolean is_stale(gpointer key, gpointer value, gpointer user_data)
{
    struct mythtv_recording * recording = (struct mythtv_recording*)value;
    return recording->stale;
}

void recording_free(gpointer ptr)
{
    struct mythtv_recording *recording = (struct mythtv_recording*)ptr;
    if (!recording) return;
    if (recording->path) free(recording->path);
    free(recording);
}

void refresh_recordings()
{
    struct show_t show;
    memset(&show, 0, sizeof(show));

    char * buffer = backend_query();
    char * data = buffer;
    char smallbuf[2]; // handy single-char string for copying
    smallbuf[0]=' '; smallbuf[1]='\0';

    /* mark all hash items stale */
    g_hash_table_foreach(mythtvfs.recordings, mark_stale, NULL);

    while ( (data = backend_pop_program(data,&show,mythtvfs.program_cols)) ) {
        char *metaname = NULL, *ptr = NULL;

#ifdef MYTHTVFS_DEBUG_FUNC
        if (mythtvfs.logfile) {
            fprintf(mythtvfs.logfile,
                "Title   : %s\n" \
                "Subtitle: %s\n" \
                "Callsign: %s\n" \
                "Pathname: %s\n" \
                "ChanId  : %s\n" \
                "Start   : %s\n" \
                "End     : %s\n" \
                "Airdate : %s\n",
                show.title,
                show.subtitle,
                show.callsign,
                show.pathname,
                show.chanid,
                show.starttime,
                show.endtime,
                show.airdate);
        }
#endif

        char * filename;
        if (!(filename = rindex(show.pathname,'/'))) {
            clear_show(&show);
            continue;
        }

        filename++;
        /* make sure we can read the file */
        if (access(aim_to_base(filename),R_OK)) {
            if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"Skipping '%s'\n",filename);
            clear_show(&show);
            continue;
        }

        if ((show.flags & FL_CUTLIST)==FL_CUTLIST && mythtvfs.logfile) {
            fprintf(mythtvfs.logfile,"Path: '%s' ChanId: %s Starttime: %s\n",show.pathname,show.chanid,show.starttime);
            struct cutlist_t *cutlist = backend_query_cutlist(show.chanid,show.starttime);
            struct cutlist_t *walk;
            for (walk=cutlist; walk; walk=walk->next) {
                fprintf(mythtvfs.logfile,"%s: %lld\n",walk->endmark ? "end" : "start", walk->location);
            }
            free_cutlist(cutlist);
        }

        char duration[32];
        char start[64];
        char air[64];
        time_t start_time = atoi(show.starttime);
        time_t end_time = atoi(show.endtime);
        time_t air_time = atoi(show.airdate);
        snprintf(duration,32,"%ld",(end_time-start_time));
        strftime(start,64,mythtvfs.datetime_format,localtime(&start_time));
        if (strchr(show.airdate,'-')==NULL) {
            strftime(air,64,mythtvfs.date_format,localtime(&air_time));
        } else {
            strncpy(air,show.airdate,64);
        }

        // copy show info to metaname using format
        int i; // index into format
        int pos; // position in output; may be different than i
        int len=2; // len of output so far; init with space for NULL and some spare
        char *what=NULL; // what we are to copy
        int formatlen = strlen(mythtvfs.format);

        for (i=0, pos=0, len=2; i<formatlen; i++) {
            if (mythtvfs.format[i]!='%') {
                // copy literally
                what=smallbuf;
                what[0]=mythtvfs.format[i];
            } else {
                i++; // let's look at next character
                if (i>=formatlen) {
                    fprintf(stderr, "error: format ends with %%\n");
                    exit(1);
                }
                switch (mythtvfs.format[i]) {
                case 'T':
                    what=show.title; break;
                case 'a':
                    what=air; break;
                case 'S':
                    what=show.subtitle; break;
                case 's':
                    what=start; break;
                case 'c':
                    what=show.callsign; break;
                case 'd':
                    what=duration; break;
                case 'D':
                    what=show.description; break;
                case 'f':
                    what=filename; break;
                default:
                    fprintf(stderr, "unknown format char %c\n", mythtvfs.format[i]);
                 exit (1);
                    break;
                }
            }

            int whatlen=strlen(what);
            len+=whatlen;

            // realloc the space
            if (!(metaname=realloc(metaname, len))) {
                perror("malloc");
                exit(1);
            }

            // copy what to current position
            strncpy(metaname+pos, what, len-pos);

            // update position
            pos+=whatlen;
        }

        // filter out invalid characters
        for (ptr = metaname; *ptr; ptr++) {
            if (*ptr == '/' || strchr(mythtvfs.invalid_chars,*ptr))
                *ptr=*mythtvfs.replacement_char;
        }

        struct mythtv_recording *recording = g_hash_table_lookup(mythtvfs.recordings, metaname);
        if (recording) {
            free(metaname);
        }
        else {
            recording = malloc(sizeof(*recording));
            recording->path = strdup(filename);
            g_hash_table_insert(mythtvfs.recordings, metaname, recording);
        }
        recording->stale = FALSE;
        clear_show(&show);
    }
    free(buffer);

    /* remove anything that vanished from the recordings list */
    g_hash_table_foreach_remove(mythtvfs.recordings, is_stale, NULL);
}


/*
 * All the mythtvfs_* function are threaded entry points.  For now, lock
 * all entry since the mythtvfs internals are not thread-safe yet.
 */
#define DO_LOCK if (pthread_mutex_lock(&(mythtvfs.lock))!=0) { perror("pthread_mutex_lock"); }
#define DO_UNLOCK if (pthread_mutex_unlock(&(mythtvfs.lock))!=0) { perror("pthread_mutex_unlock"); }

static int mythtvfs_getattr(const char *path, struct stat *stbuf)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = lstat(fixup(path), stbuf);
    DO_UNLOCK

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_readlink(const char *path, char *buf, size_t size)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = readlink(fixup(path), buf, size - 1);
    DO_UNLOCK

    if(res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}

static int mythtvfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;
    (void) path;
    struct readdir_info info;

#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    memset(&(info.st), 0, sizeof(info.st));
    info.fuse_buf = buf;
    info.filler = filler;

    info.st.st_ino++;
    info.st.st_mode = S_IFDIR|S_IRUSR|S_IRGRP|S_IROTH|S_IXUSR|S_IXGRP|S_IXOTH;
    info.filler(info.fuse_buf, ".", &(info.st), 0);
    info.st.st_ino++;
    info.filler(info.fuse_buf, "..", &(info.st), 0);

    DO_LOCK
    refresh_recordings();
    g_hash_table_foreach(mythtvfs.recordings, fill_readdir_struct, &info);
    DO_UNLOCK

    return 0;
}


static int mythtvfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = mknod(fixup(path), mode, rdev);
    DO_UNLOCK

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_mkdir(const char *path, mode_t mode)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = mkdir(fixup(path), mode);
    DO_UNLOCK

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_unlink(const char *path)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = unlink(fixup(path));
    DO_UNLOCK

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_rmdir(const char *path)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = rmdir(fixup(path));
    DO_UNLOCK

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_symlink(const char *from, const char *to)
{
    int res;
    char * fixed_from = NULL;
    char * fixed_to = NULL;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s %s\n",__FUNCTION__,from,to);
#endif

    DO_LOCK
    fixed_from = strdup(fixup(from));
    fixed_to   = strdup(fixup(to));
    DO_UNLOCK

    res = symlink(fixed_from, fixed_to);

    free(fixed_from);
    free(fixed_to);

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_rename(const char *from, const char *to)
{
    int res;
    char * fixed_from = NULL;
    char * fixed_to = NULL;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s %s\n",__FUNCTION__,from,to);
#endif

    DO_LOCK
    fixed_from = strdup(fixup(from));
    fixed_to   = strdup(fixup(to));
    DO_UNLOCK

    res = rename(fixed_from, fixed_to);

    free(fixed_from);
    free(fixed_to);

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_link(const char *from, const char *to)
{
    int res;
    char * fixed_from = NULL;
    char * fixed_to = NULL;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s %s\n",__FUNCTION__,from,to);
#endif

    DO_LOCK
    fixed_from = strdup(fixup(from));
    fixed_to   = strdup(fixup(to));
    DO_UNLOCK

    res = link(from, to);

    free(fixed_from);
    free(fixed_to);

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_chmod(const char *path, mode_t mode)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = chmod(fixup(path), mode);
    DO_UNLOCK

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = lchown(fixup(path), uid, gid);
    DO_UNLOCK

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_truncate(const char *path, off_t size)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s @%" PRId64 "\n",__FUNCTION__,path,size);
#endif

    DO_LOCK
    res = truncate(fixup(path), size);
    DO_UNLOCK

    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_utime(const char *path, struct utimbuf *buf)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = utime(fixup(path), buf);
    DO_UNLOCK
    if(res == -1)
        return -errno;

    return 0;
}


static int mythtvfs_open(const char *path, struct fuse_file_info *fi)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = open(fixup(path), fi->flags);
    DO_UNLOCK
    if(res == -1)
        return -errno;

    close(res);
    return 0;
}

static int mythtvfs_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    int fd;
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s @%" PRId64 " %" PRId64 "\n",__FUNCTION__,path,offset,size);
#endif

    (void) fi;

/*
    // redirect .edl files
    int len = strlen(path);
    if (len>4 && strcmp(path+len-4,".edl")==0) {

        ...
        return res;
    }
*/

    DO_LOCK
    fd = open(fixup(path), O_RDONLY);
    DO_UNLOCK
    if(fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if(res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int mythtvfs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{
    int fd;
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s @%" PRId64 " %" PRId64 "\n",__FUNCTION__,offset,size);
#endif

    (void) fi;
    DO_LOCK
    fd = open(fixup(path), O_WRONLY);
    DO_UNLOCK
    if(fd == -1)
        return -errno;

    res = pwrite(fd, buf, size, offset);
    if(res == -1)
        res = -errno;

    close(fd);
    return res;
}

#if FUSE_VERSION >= 25
# define whichever_stat    statvfs
#else
# define whichever_stat    statfs
#endif

static int mythtvfs_statfs(const char *path, struct whichever_stat *stbuf)
{
    int res;
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    DO_LOCK
    res = whichever_stat(fixup(path), stbuf);
    DO_UNLOCK
    if(res == -1)
        return -errno;

    return 0;
}

static int mythtvfs_release(const char *path, struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    (void) path;
    (void) fi;
    return 0;
}

static int mythtvfs_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif

    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int mythtvfs_setxattr(const char *path, const char *name, const char *value,
                        size_t size, int flags)
{
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif
    DO_LOCK
    int res = lsetxattr(fixup(path), name, value, size, flags);
    DO_UNLOCK
    if(res == -1)
        return -errno;
    return 0;
}

static int mythtvfs_getxattr(const char *path, const char *name, char *value,
                    size_t size)
{
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif
    DO_LOCK
    int res = lgetxattr(fixup(path), name, value, size);
    DO_UNLOCK
    if(res == -1)
        return -errno;
    return res;
}

static int mythtvfs_listxattr(const char *path, char *list, size_t size)
{
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif
    DO_LOCK
    int res = llistxattr(fixup(path), list, size);
    DO_UNLOCK
    if(res == -1)
        return -errno;
    return res;
}

static int mythtvfs_removexattr(const char *path, const char *name)
{
#ifdef MYTHTVFS_DEBUG_FUNC
    if (mythtvfs.logfile) fprintf(mythtvfs.logfile,"%s %s\n",__FUNCTION__,path);
#endif
    DO_LOCK
    int res = lremovexattr(fixup(path), name);
    DO_UNLOCK
    if(res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */

static struct fuse_operations mythtvfs_oper = {
    .getattr     = mythtvfs_getattr,
    .readlink    = mythtvfs_readlink,
    .readdir     = mythtvfs_readdir,
    .mknod       = mythtvfs_mknod,
    .mkdir       = mythtvfs_mkdir,
    .symlink     = mythtvfs_symlink,
    .unlink      = mythtvfs_unlink,
    .rmdir       = mythtvfs_rmdir,
    .rename      = mythtvfs_rename,
    .link        = mythtvfs_link,
    .chmod       = mythtvfs_chmod,
    .chown       = mythtvfs_chown,
    .truncate    = mythtvfs_truncate,
    .utime       = mythtvfs_utime,
    .open        = mythtvfs_open,
    .read        = mythtvfs_read,
    .write       = mythtvfs_write,
    .statfs      = mythtvfs_statfs,
    .release     = mythtvfs_release,
    .fsync       = mythtvfs_fsync,
#ifdef HAVE_SETXATTR
    .setxattr    = mythtvfs_setxattr,
    .getxattr    = mythtvfs_getxattr,
    .listxattr   = mythtvfs_listxattr,
    .removexattr = mythtvfs_removexattr,
#endif
};

static int mythtvfs_opt_proc(void *data, const char *arg, int key,
                             struct fuse_args *outargs)
{
    (void) data;

    switch (key) {
    case FUSE_OPT_KEY_OPT:
        return 1;

    case FUSE_OPT_KEY_NONOPT:
        if (!mythtvfs.base_path) {
            mythtvfs.base_path = strdup(arg);
            mythtvfs.base_len  = strlen(mythtvfs.base_path);
            return 0;
        }

        return 1;

    case KEY_HELP:
        usage(outargs->argv[0]);
        fuse_opt_add_arg(outargs, "-ho");
        fuse_main(outargs->argc, outargs->argv, &mythtvfs_oper, NULL);
        exit(1);

    case KEY_VERSION:
        fprintf(stderr, "MythTVfs version %s\n", PACKAGE_VERSION);
#if FUSE_VERSION >= 25
        fuse_opt_add_arg(outargs, "--version");
        fuse_main(outargs->argc, outargs->argv, &mythtvfs_oper, NULL);
#endif
        exit(0);

    default:
        fprintf(stderr, "internal error\n");
        abort();
    }
}

int main(int argc, char *argv[])
{
    int rc;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    // setup "defaults"
    mythtvfs.base_path = NULL;
    mythtvfs.host      = NULL;
    mythtvfs.format    = "{%T}{%a}{%S}{%s}{%c}{%d}{%D}%f.mpg";
    mythtvfs.date_format    = "%Y-%m-%d";
    mythtvfs.datetime_format    = "%I.%M %p %a %b %d, %Y";
    mythtvfs.port      = 6543;
    mythtvfs.short_display_flag = 0;
    mythtvfs.backend_version_override = 0;
    mythtvfs.program_width_override = 0;
    mythtvfs.invalid_chars = strdup("/");
    mythtvfs.replacement_char = strdup("-");
    mythtvfs.backend   = -1;
    mythtvfs.base_len  = 0;
    mythtvfs.logfile   = NULL;
    mythtvfs.logfile_name = NULL;
    mythtvfs.program_cols = 0;
    mythtvfs.recordings = g_hash_table_new_full(g_str_hash, g_str_equal, free, recording_free);
    if (pthread_mutex_init(&(mythtvfs.lock), NULL)!=0) {
        perror("pthread_mutex_init");
        exit(1);
    }

    // process options
    if (fuse_opt_parse(&args,&mythtvfs,mythtvfs_opts,mythtvfs_opt_proc) == -1)
        exit(1);

    // validate required options (would be nicer to dump usage...)
    if (!mythtvfs.host) {
        fprintf(stderr,"Error: backend not specified!  Run with --help\n");
        exit(1);
    }
    if (!mythtvfs.base_path) {
        fprintf(stderr,"Error: original path not specified!  Run with --help\n");
        exit(1);
    }

    if (mythtvfs.short_display_flag) {
        mythtvfs.format="{%T}{%a}{%S}{%s}{%c}{%d}%f.mpg";
        mythtvfs.datetime_format = "%c";
    }

    if (mythtvfs.logfile_name) {
        if (!(mythtvfs.logfile = fopen(mythtvfs.logfile_name,"a"))) {
            perror(mythtvfs.logfile_name);
            exit(1);
        }
        setvbuf(mythtvfs.logfile,NULL,_IONBF,0);
    }

    umask(0);

    // attempt to talk to mythtv backend before daemonizing
    backend_version_check();

    rc = fuse_main(args.argc, args.argv, &mythtvfs_oper, NULL);

    backend_done();
    if (mythtvfs.logfile) fclose(mythtvfs.logfile);
    return rc;
}

