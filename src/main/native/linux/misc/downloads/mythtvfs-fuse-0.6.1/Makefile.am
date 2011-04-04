## Process this file with automake to produce Makefile.in

man_MANS = mythtvfs.1

%.1: %.pod
	$(POD2MAN) --release='$(VERSION)' --center='MythTVFS Documentation' $< $@

clean-local:
	-$(RM) $(man_MANS)

EXTRA_DIST = mythtvfs.pod mythtvfs.init

bin_PROGRAMS = mythtvfs

mythtvfs_SOURCES = mythtvfs.c
if FUSE_OPT_COMPAT
mythtvfs_SOURCES += compat/fuse_opt.c compat/fuse_opt.h
endif

mythtvfs_LDADD = $(MYTHTVFS_LIBS)
mythtvfs_CFLAGS = $(MYTHTVFS_CFLAGS)
mythtvfs_CPPFLAGS = -D_REENTRANT -DFUSE_USE_VERSION=26
