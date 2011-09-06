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

AC_ARG_VAR([PROTOC_C], [path to protoc-c utility])

# Check for programs
AC_PROG_CC
AC_PROG_SED
AC_PATH_PROG([PROTOC_C], [protoc-c], [false])

PKG_PROG_PKG_CONFIG()
PKG_CHECK_MODULES(PROTOBUF, [libprotobuf-c],[], [
	AC_CHECK_LIB([protobuf-c], [protobuf_c_message_pack_to_buffer], [],
		     [AC_MSG_ERROR([libprotobuf-c not found])])
])
PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.28])
PKG_CHECK_MODULES(GIO,  [gio-2.0])
PKG_CHECK_MODULES(GSTREAMER, [gstreamer-0.10 gstreamer-app-0.10])
PKG_CHECK_MODULES(CELT, [celt])

GST_ELEMENTS="appsrc appsink celtdec celtenc capsfilter
	      audioconvert audioresample autoaudiosrc autoaudiosink"

for element in $GST_ELEMENTS
do
	AM_GST_ELEMENT_CHECK([$element], [],
			     AC_MSG_ERROR([gstreamer element $element not found]))
done

if test "x$GCC" = "xyes"; then
	GCC_CFLAGS="-Wall -g -Wstrict-prototypes -Wmissing-prototypes -fvisibility=hidden"
fi
AC_SUBST([GCC_CFLAGS])

AC_CONFIG_FILES([Makefile
		 src/Makefile])
AC_CONFIG_FILES([src/protoc-c.sh], [chmod +x src/protoc-c.sh])
AC_OUTPUT
