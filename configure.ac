AC_PREREQ([2.64])

AC_INIT([cmumble],
        [0.1],
        [benjaminfranzke@googlemail.com],
        [cmumble],
        [https://gitorious.org/cmumble/])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([foreign dist-bzip2])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Check for programs
AC_PROG_CC
AC_PROG_SED

AC_ARG_WITH([protoc],
	    [AS_HELP_STRING([--with-protoc=COMMAND],
	                    [use the given protoc command])],
	                    [PROTOC_C=$withval],
	                    [AC_PATH_PROG([PROTOC_C], [protoc-c], [false])])

AS_IF([test "x$PROTOC_C" = "xfalse"],
      [AC_MSG_ERROR([protoc-c compiler not found])])

AC_CHECK_LIB([ncurses], [tputs],
	     [], [AC_MSG_ERROR([ncurses needed for libreadline])])
AC_CHECK_LIB([readline], [readline],
	     [], [AC_MSG_ERROR([libreadline not found])], [-lncurses])

PKG_PROG_PKG_CONFIG()
PKG_CHECK_MODULES(PROTOBUF, [libprotobuf-c],[], [
	AC_CHECK_LIB([protobuf-c], [protobuf_c_message_pack_to_buffer], [],
		     [AC_MSG_ERROR([libprotobuf-c not found])])
])
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.28])
PKG_CHECK_MODULES(GIO,  [gio-2.0])
PKG_CHECK_MODULES(GSTREAMER, [gstreamer-0.10 gstreamer-app-0.10])
PKG_CHECK_MODULES(CELT, [celt], [have_celt=yes], [have_celt=no])
if test "x$have_celt" = xno; then
	PKG_CHECK_MODULES(CELT, [celt071], [have_celt071=yes], [have_celt071=no])
	if test "x$have_celt071" = xno; then
		AC_MSG_ERROR([Neither celt nor celt071 found.])
	fi
	AC_DEFINE([HAVE_CELT071], [1], [Defined if we're using celt071])
fi

PKG_CHECK_MODULES(SPEEX, [speex speexdsp])

if test "x$GCC" = "xyes"; then
	GCC_CFLAGS="-Wall -g -Wstrict-prototypes -Wmissing-prototypes -fvisibility=hidden"
fi
AC_SUBST([GCC_CFLAGS])

AC_CONFIG_FILES([Makefile
		 src/Makefile])
AC_CONFIG_FILES([src/protoc-c.sh], [chmod +x src/protoc-c.sh])
AC_OUTPUT
