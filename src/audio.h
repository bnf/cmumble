#ifndef _AUDIO_H_
#define _AUDIO_H_

#include "../config.h"

#include <glib.h>

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include <speex/speex.h>
#include <speex/speex_jitter.h>
#include <opus/opus.h>

#ifdef HAVE_CELT071
#include <celt071/celt.h>
#include <celt071/celt_header.h>
#else
#include <celt/celt.h>
#include <celt/celt_header.h>
#endif

struct cmumble_audio {
	//GstElement *record_pipeline;
	//GstAppSink *sink;

	guint8 celt_header_packet[sizeof(CELTHeader)];
	CELTEncoder *celt_encoder;
	CELTHeader celt_header;
	CELTMode *celt_mode;

	gint32 celt_bitstream_version;

	void *record_buffer;
	size_t record_buffer_length;
	size_t record_buffer_index;
	size_t record_buffer_decode_pos;

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
