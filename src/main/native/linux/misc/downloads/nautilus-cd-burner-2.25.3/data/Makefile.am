NULL =

SUBDIRS = icons

@INTLTOOL_SCHEMAS_RULE@

schemasdir   = @GCONF_SCHEMA_FILE_DIR@
schemas_in_files = nautilus-cd-burner.schemas.in
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_DESKTOP_RULE@

desktopdir = $(datadir)/applications
desktop_in_files = 				\
	nautilus-cd-burner.desktop.in 		\
	nautilus-cd-burner-open-iso.desktop.in	\
	$(NULL)

desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

gladedir = $(pkgdatadir)
glade_DATA = 				\
	nautilus-cd-burner.glade	\
	$(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libnautilus-burn.pc

icondir = $(datadir)/nautilus-cd-burner
icon_DATA =			\
	cdspin1.png		\
	cdspin2.png		\
	cdspin3.png		\
	cdspin4.png		\
	cdspin5.png		\
	cdspin6.png		\
	cdspin7.png		\
	cdspin8.png		\
	$(NULL)

EXTRA_DIST = 				\
	$(schemas_in_files)		\
	$(desktop_in_files)		\
	$(glade_DATA)			\
	$(icon_DATA)			\
	$(modulesconf_DATA)		\
	libnautilus-burn.pc.in		\
	$(NULL)

DISTCLEANFILES = 			\
	$(schemas_DATA)			\
	$(desktop_DATA)			\
	$(NULL)

MAINTAINERCLEANFILES =			\
	*~				\
	Makefile.in

if GCONF_SCHEMAS_INSTALL
# don't do this if we are building in eg. rpm
install-data-local: $(schemas_DATA)
	if test -z "$(DESTDIR)" ; then \
		for p in $(schemas_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/data/$$p ; \
		done \
	fi
else
install-data-local:
endif
