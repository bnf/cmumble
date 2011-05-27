bin_PROGRAMS = cmumble

noinst_HEADERS = messages.h mumble.pb-c.h varint.h
cmumble_SOURCES = cmumble.c mumble.pb-c.c varint.c
cmumble_LDADD = $(PROTOBUF_LIBS) $(GLIB_LIBS) $(GIO_LIBS) $(GSTREAMER_LIBS) $(CELT_LIBS)

AM_CPPFLAGS = $(PROTOBUF_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) $(GSTREAMER_CFLAGS) $(CELT_CFLAGS)
AM_CFLAGS = $(GCC_FLAGS)

messages.h: messages.txt
	./gen_message.sh < $< > $@

mumble.pb-c.c mumble.pb-c.h: mumble.proto
	$(PROTOBUF_EXECUTABLE) --c_out=. $<

CLEANFILES = mumble.pb-c.c mumble.pb-c.h messages.h
