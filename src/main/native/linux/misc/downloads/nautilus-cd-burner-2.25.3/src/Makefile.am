module_flags = -avoid-version -module

lib_LTLIBRARIES = libnautilus-burn.la

bin_PROGRAMS = nautilus-cd-burner
noinst_PROGRAMS = 	\
	list_cddrives 	\
	bcs_test 	\
	test_blank 	\
	test_monitor	\
	$(NULL)

if MKDTEMP_MISSING
MKDTEMP_FILES = mkdtemp.c mkdtemp.h
else
MKDTEMP_FILES =
endif

NULL =

INCLUDES =					\
	$(GUI_CFLAGS)				\
	$(EXTENSION_CFLAGS)			\
	$(WERROR)				\
	-D_FILE_OFFSET_BITS=64			\
	-D_GNU_SOURCE				\
	-D_LARGEFILE64_SOURCE			\
	-D_REENTRANT				\
	-D_POSIX_PTHREAD_SEMANTICS		\
	-DPREFIX=\"$(prefix)\"			\
	-DBINDIR=\""$(bindir)"\"	\
	-DLIBEXECDIR=\""$(libexecdir)"\"	\
	-DSHAREDIR=\""$(datadir)"\"		\
	-DDATADIR=\""$(datadir)/nautilus-cd-burner"\"

CLEANFILES = 			\
	$(RECMARSHALFILES)	\
	$(NULL)

DISTCLEANFILES = 		\
	$(NULL)

### GUI burner app
nautilus_cd_burner_SOURCES = 	\
	nautilus-cd-burner.c 	\
	nautilus-cd-burner.h 	\
	ncb-operation.h		\
	ncb-operation.c		\
	ncb-progress-dialog.h 	\
	ncb-progress-dialog.c 	\
	ncb-rename-dialog.h	\
	ncb-rename-dialog.c	\
	ncb-selection.h 	\
	ncb-selection.c 	\
	ncb-selection-dialog.h 	\
	ncb-selection-dialog.c 	\
	nautilus-file-operations-progress-icons.h	\
	make-iso.c 		\
	make-iso.h 		\
	$(MKDTEMP_FILES) 	\
	$(NULL)

EXTRA_DIST =			\
	nautilus-burn-features.h.in		\
	mkdtemp.c 		\
	mkdtemp.h		\
	cdrecorder-marshal.list	\
	$(NULL)

nautilus_cd_burner_LDADD = 	\
	libnautilus-burn.la 	\
	$(GUI_LIBS)		\
	$(NULL)

### Test programs
list_cddrives_SOURCES = list_cddrives.c
list_cddrives_LDFLAGS = `pkg-config --libs gthread-2.0`
list_cddrives_LDADD = libnautilus-burn.la
bcs_test_SOURCES = bcs-test.c
bcs_test_LDADD = libnautilus-burn.la
test_monitor_SOURCES = test-monitor.c
test_monitor_LDADD = libnautilus-burn.la
test_blank_SOURCES = test_blank.c
test_blank_LDFLAGS = `pkg-config --libs gthread-2.0`
test_blank_LDADD = libnautilus-burn.la


### Nautilus Extension
nautilus_extensiondir = $(NAUTILUSDIR)
nautilus_extension_LTLIBRARIES = libnautilus-burn-extension.la

libnautilus_burn_extension_la_SOURCES = 	\
	nautilus-burn-bar.c			\
	nautilus-burn-bar.h			\
	burn-extension.c			\
	$(NULL)

libnautilus_burn_extension_la_LIBADD = $(EXTENSION_LIBS)
libnautilus_burn_extension_la_LDFLAGS = $(module_flags)


### Our burning library
GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
RECMARSHALFILES = nautilus-burn-recorder-marshal.h nautilus-burn-recorder-marshal.c
nautilus-burn-recorder-marshal.h: cdrecorder-marshal.list
	( $(GLIB_GENMARSHAL) --prefix=nautilus_burn_recorder_marshal $(srcdir)/cdrecorder-marshal.list --header > nautilus-burn-recorder-marshal.h )
nautilus-burn-recorder-marshal.c: nautilus-burn-recorder-marshal.h
	( $(GLIB_GENMARSHAL) --prefix=nautilus_burn_recorder_marshal $(srcdir)/cdrecorder-marshal.list --body --header > nautilus-burn-recorder-marshal.c )

libnautilus_burn_la_SOURCES =			\
	nautilus-burn.h				\
	nautilus-burn-init.c			\
	nautilus-burn-init.h			\
	nautilus-burn-drive-selection.c		\
	nautilus-burn-drive-selection.h		\
	$(RECMARSHALFILES)			\
	nautilus-burn-process.c			\
	nautilus-burn-process.h			\
	nautilus-burn-recorder.c		\
	nautilus-burn-recorder.h		\
	nautilus-burn-drive.c			\
	nautilus-burn-drive.h			\
	nautilus-burn-drive-private.h		\
	nautilus-burn-drive-monitor.c		\
	nautilus-burn-drive-monitor.h		\
	nautilus-burn-drive-monitor-private.h	\
	nautilus-burn-features.h		\
	$(NULL)

libnautilus_burn_la_LDFLAGS =			\
	-version-info $(BURN_LT_VERSION)	\
	-no-undefined
libnautilus_burn_la_LIBADD = $(BURN_LIBS)
libnautilus_burnincludedir = $(includedir)/libnautilus-burn/
libnautilus_burninclude_HEADERS =		\
	nautilus-burn.h				\
	nautilus-burn-init.h			\
	nautilus-burn-drive.h			\
	nautilus-burn-drive-monitor.h		\
	nautilus-burn-recorder.h		\
	nautilus-burn-drive-selection.h		\
	nautilus-burn-features.h		\
	$(NULL)
