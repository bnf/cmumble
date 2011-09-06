bin_PROGRAMS = cmumble

noinst_HEADERS = cmumble.h varint.h
nodist_noinst_HEADERS = mumble.pb-c.h messages.h

cmumble_SOURCES = cmumble.c messages.c varint.c
nodist_cmumble_SOURCES = mumble.pb-c.c

cmumble_LDADD = $(PROTOBUF_LIBS) $(GLIB_LIBS) $(GIO_LIBS) \
		$(GSTREAMER_LIBS) $(CELT_LIBS)
AM_CPPFLAGS = $(PROTOBUF_CFLAGS) $(GLIB_CFLAGS) $(GIO_CFLAGS) \
	      $(GSTREAMER_CFLAGS) $(CELT_CFLAGS)

AM_CFLAGS = $(GCC_FLAGS)

BUILT_SOURCES = $(nodist_cmumble_SOURCES) $(nodist_noinst_HEADERS)
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = mumble.proto messages.txt gen_messages.sed

messages.h: $(srcdir)/messages.txt $(srcdir)/gen_messages.sed
	$(AM_V_GEN) LC_ALL=C $(SED) -f $(srcdir)/gen_messages.sed $< > $@

mumble.pb-c.c mumble.pb-c.h: $(srcdir)/mumble.proto
	$(AM_V_GEN) $(builddir)/protoc-c.sh $< $@ 

