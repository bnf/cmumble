bin_PROGRAMS = cmumble

noinst_HEADERS = cmumble.h varint.h
nodist_noinst_HEADERS = messages.h mumble.pb-c.h

cmumble_SOURCES = cmumble.c messages.c varint.c
nodist_cmumble_SOURCES = mumble.pb-c.c

cmumble_LDADD = $(PROTOBUF_LIBS) $(GLIB_LIBS) $(GIO_LIBS) \
		$(GSTREAMER_LIBS) $(CELT_LIBS)
AM_CPPFLAGS = $(PROTOBUF_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) \
	      $(GSTREAMER_CFLAGS) $(CELT_CFLAGS)

AM_CFLAGS = $(GCC_FLAGS)

BUILT_SOURCES = $(nodist_cmumble_SOURCES) $(nodist_noinst_HEADERS)
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = mumble.proto messages.txt gen_messages.sed protoc-c.sh

messages.h: $(top_srcdir)/src/messages.txt $(top_srcdir)/src/gen_messages.sed
	$(AM_V_GEN) $(SED) -f $(top_srcdir)/src/gen_messages.sed $< > $@

mumble.pb-c.c mumble.pb-c.h: $(top_srcdir)/src/mumble.proto
	$(AM_V_GEN) PROTOC_C=$(PROTOBUF_EXECUTABLE) \
		$(top_srcdir)/src/protoc-c.sh $< $@ 

