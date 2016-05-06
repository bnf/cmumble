#ifndef _AUDIO_H_
#define _AUDIO_H_

#include <glib.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappbuffer.h>

#ifdef HAVE_CELT071
#include <celt071/celt.h>
#include <celt071/celt_header.h>
#else
#include <celt/celt.h>
#include <celt/celt_header.h>
#endif

struct cmumble_audio {
	GstElement *record_pipeline;
	GstAppSink *sink;

	guint8 celt_header_packet[sizeof(CELTHeader)];
	CELTHeader celt_header;
	CELTMode *celt_mode;

	gint32 celt_bitstream_version;
};

struct cmumble;
struct cmumble_user;

int
cmumble_audio_init(struct cmumble *cm);

int
cmumble_audio_fini(struct cmumble *cm);

int
cmumble_audio_create_playback_pipeline(struct cmumble *cm,
				       struct cmumble_user *user);

void
cmumble_audio_push(struct cmumble *cm, struct cmumble_user *user,
		   const guint8 *data, gsize size);

#endif /* _AUDIO_H_ */
