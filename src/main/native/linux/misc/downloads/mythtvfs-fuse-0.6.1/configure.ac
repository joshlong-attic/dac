AC_INIT(mythtvfs-fuse, 0.6.1)
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AM_PROG_CC_C_O
CFLAGS="$CFLAGS -Wall -Werror -Wformat -Wformat-security -D_FORTIFY_SOURCE=2"
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
PKG_CHECK_MODULES(MYTHTVFS, [fuse >= 2.2 glib-2.0])

dnl Check pod2man for creating man pages
AC_CHECK_PROG(POD2MAN, pod2man, pod2man)

have_fuse_opt_parse=no
AC_CHECK_FUNC([fuse_opt_parse], [have_fuse_opt_parse=yes])
if test "$have_fuse_opt_parse" = no; then
    CFLAGS="$CFLAGS -I$srcdir/compat"
fi
AM_CONDITIONAL(FUSE_OPT_COMPAT, test "$have_fuse_opt_parse" = no)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
