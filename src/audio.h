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

	uint8_t celt_header_packet[sizeof(CELTHeader)];
	CELTHeader celt_header;
	CELTMode *celt_mode;
};

struct cmumble_context;
struct cmumble_user;

int
cmumble_audio_init(struct cmumble_context *ctx);

int
cmumble_audio_fini(struct cmumble_context *ctx);

int
cmumble_audio_create_playback_pipeline(struct cmumble_context *ctx,
				       struct cmumble_user *user);

void
cmumble_audio_push(struct cmumble_context *ctx, struct cmumble_user *user,
		   const uint8_t *data, gsize size);

#endif /* _AUDIO_H_ */
