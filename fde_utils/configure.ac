AC_PREREQ(2.61)
AC_INIT([fde-utils], [1.0.0], [security@qti.qualcomm.com])
AM_INIT_AUTOMAKE([-Wall gnu foreign subdir-objects])
AM_MAINTAINER_MODE
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AS
AM_PROG_AR
AC_PROG_LIBTOOL
AC_PROG_AWK
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_CXX

LT_INIT
PKG_PROG_PKG_CONFIG

AC_ARG_WITH([glib],
      AC_HELP_STRING([--with-glib],
         [enable glib, building HLOS systems which use glib]))

if (test "x${with_glib}" = "xyes"); then
        AC_DEFINE(ENABLE_USEGLIB, 1, [Define if HLOS systems uses glib])

        PKG_CHECK_MODULES(GTHREAD, gthread-2.0 >= 2.16, dummy=yes,
                                AC_MSG_ERROR(GThread >= 2.16 is required))
        PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.16, dummy=yes,
                                AC_MSG_ERROR(GLib >= 2.16 is required))
        GLIB_CFLAGS="$GLIB_CFLAGS $GTHREAD_CFLAGS"
        GLIB_LIBS="$GLIB_LIBS $GTHREAD_LIBS"
        AC_SUBST(GLIB_CFLAGS)
        AC_SUBST(GLIB_LIBS)
fi

AM_CONDITIONAL(USE_GLIB, test "x${with_glib}" = "xyes")

AC_CHECK_HEADERS([
    ext4_utils/ext4_utils.h
    ext4_utils/ext4_kernel_headers.h
], [], [AC_MSG_ERROR([ext4_utils headers not found])])

PKG_CHECK_MODULES(EXT4_UTILS, ext4_utils)

AC_SUBST(EXT4_UTILS_CFLAGS)
AC_SUBST(EXT4_UTILS_LIBS)

AC_CONFIG_FILES([
    Makefile
    fde-utils.pc
])

AC_OUTPUT