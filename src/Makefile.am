bin_PROGRAMS = cmumble

noinst_HEADERS = cmumble.h messages.h mumble.pb-c.h varint.h
cmumble_SOURCES = cmumble.c messages.c mumble.pb-c.c varint.c
cmumble_LDADD = $(PROTOBUF_LIBS) $(GLIB_LIBS) $(GIO_LIBS) $(GSTREAMER_LIBS) $(CELT_LIBS)
EXTRA_DIST = mumble.proto messages.txt gen_message.sh

AM_CPPFLAGS = $(PROTOBUF_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(GSTREAMER_CFLAGS) $(CELT_CFLAGS)
AM_CFLAGS = $(GCC_FLAGS)

$(cmumble_SOURCES): messages.h mumble.pb-c.h

messages.h: messages.txt gen_message.sh
	./gen_message.sh < $< > $@

mumble.pb-c.c mumble.pb-c.h: mumble.proto
	$(PROTOBUF_EXECUTABLE) --c_out=. $<

CLEANFILES = mumble.pb-c.c mumble.pb-c.h messages.h
