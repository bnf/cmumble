AC_PREREQ([2.64])
AC_INIT([cmumble],
        [0.1],
        [benjaminfranzke@googlemail.com],
        [cmumble],
        [https://gitorious.org/cmumble/])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11 foreign dist-bzip2])

AM_SILENT_RULES([yes])

# Check for programs
AC_PROG_CC

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

PKG_PROG_PKG_CONFIG()
PKG_CHECK_MODULES(PROTOBUF, [libprotobuf-c],,[AC_MSG_ERROR([libprotobuf missing])])
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.28],,[AC_MSG_ERROR([glib-2.0 missing])])
PKG_CHECK_MODULES(GIO,  [gio-2.0],,[AC_MSG_ERROR([gio-2.0 missing])])
PKG_CHECK_MODULES(GSTREAMER, [gstreamer-0.10 gstreamer-app-0.10],,
		  [AC_MSG_ERROR([gstreamer-0.10 or gstreamer-app-0.10 missing])])
PKG_CHECK_MODULES(CELT, [celt],,[AC_MSG_ERROR([celt missing])])

PROTOBUF_EXECUTABLE=`pkg-config --variable=exec_prefix libprotobuf-c`
if test -n "$PROTOBUF_EXECUTABLE"; then
	PROTOBUF_EXECUTABLE=protoc-c
else
	PROTOBUF_EXECUTABLE="${PROTBUF_EXECUTABLE}/bin/protoc-c"
fi
AC_SUBST([PROTOBUF_EXECUTABLE])

if test "x$GCC" = "xyes"; then
	GCC_CFLAGS="-Wall -g -Wstrict-prototypes -Wmissing-prototypes -fvisibility=hidden"
fi
AC_SUBST([GCC_CFLAGS])

AC_CONFIG_FILES([Makefile
		 src/Makefile])
AC_OUTPUT
