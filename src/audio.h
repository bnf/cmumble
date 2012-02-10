#ifndef _AUDIO_H_
#define _AUDIO_H_

#include <glib.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappbuffer.h>

#include <celt/celt.h>
#include <celt/celt_header.h>

struct cmumble_audio {
	GstElement *record_pipeline;
	GstAppSink *sink;

	guint8 celt_header_packet[sizeof(CELTHeader)];
	CELTHeader celt_header;
	CELTMode *celt_mode;
};

struct cmumlbe;
struct cmumble_user;

int
cmumble_audio_init(struct cmumlbe *cm);

int
cmumble_audio_fini(struct cmumlbe *cm);

int
cmumble_audio_create_playback_pipeline(struct cmumlbe *cm,
				       struct cmumble_user *user);

void
cmumble_audio_push(struct cmumlbe *cm, struct cmumble_user *user,
		   const guint8 *data, gsize size);

#endif /* _AUDIO_H_ */
