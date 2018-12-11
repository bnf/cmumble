#include "../config.h"
#include "audio.h"
#include "varint.h"
#include "cmumble.h"
#include <string.h>

#define SAMPLERATE 48000
#define FRAMESIZE (SAMPLERATE/100)
#define CHANNELS 1
#define CHUNK_SIZE (FRAMESIZE * CHANNELS * sizeof(uint16_t))










/* Write some data to the stream */
/*
static void do_stream_write(size_t length) {
    size_t l;
    assert(length);

    if (!buffer || !buffer_length)
        return;

    l = length;
    if (l > buffer_length)
        l = buffer_length;

    if (pa_stream_write(stream, (uint8_t*) buffer + buffer_index, l, NULL, 0, PA_SEEK_RELATIVE) < 0) {
        g_printerr("pa_stream_write() failed: %s\n", pa_strerror(pa_context_errno(context)));
        quit(1);
        return;
    }

    buffer_length -= l;
    buffer_index += l;

    if (!buffer_length) {
        pa_xfree(buffer);
        buffer = NULL;
        buffer_index = buffer_length = 0;
    }
}
*/

/* This is called whenever new data may be written to the stream */
static void stream_write_callback(pa_stream *s, size_t length, void *userdata)
{
	//struct cmumble *cm = userdata;
	assert(s);
	assert(length > 0);

	/*
	if (stdio_event)
		cm->pulse_mainloop_api->io_enable(stdio_event, PA_IO_EVENT_INPUT);
		*/

	//if (!buffer)
	//	return;

	//do_stream_write(length);
}

static int
try_encode_pcm(struct cmumble *cm)
{
	struct cmumble_audio *a = &cm->audio;
	gint len;
	uint8_t data[1024];
	uint32_t written = 0, pos = 0;
	mumble_udptunnel_t tunnel;

	if (!(a->record_buffer_length - a->record_buffer_decode_pos > 4 * CHUNK_SIZE)) {
		return 0;
	}

	data[pos++] = (udp_voice_celt_alpha << 5) | (udp_normal_talking);
	encode_varint(&data[pos], &written, cm->sequence + 1, sizeof(data)-pos);
	pos += written;
	celt_encoder_ctl(a->celt_encoder, CELT_RESET_STATE);

#define BITRATE 40000 /* balanced */
//#define BITRATE 72000 /* ultra */
	int frames_per_packet = 4;

	int i;
	for (i = 0; i < frames_per_packet; ++i) {
	//while (a->record_buffer_length - a->record_buffer_decode_pos > CHUNK_SIZE) {

		/* 127 since the mumble protocol allows to store only a 7bit length header for speex/celt */
		uint8_t buffer[127];
#ifdef CELT_SET_VBR_RATE
		size_t max_size = BITRATE / 8 / 100;
		if (sizeof(buffer) < max_size) {
			g_printerr("Warning: The current bitrate results in more than 127 compressed bytes.\n");
			max_size = sizeof(buffer);
			//return -1;
		}
		celt_encoder_ctl(a->celt_encoder, CELT_SET_VBR_RATE(BITRATE));
#endif
#ifdef CELT_SET_PREDICTION
		celt_encoder_ctl(a->celt_encoder, CELT_SET_PREDICTION(0));
#endif
		void *p = (uint8_t*)(a->record_buffer) + a->record_buffer_decode_pos;
		len = celt_encode(a->celt_encoder, p, NULL, buffer, max_size);
		if (len < 0) {
			g_printerr("celt_encode failed: %s\n", celt_strerror(len));
			return -1;

		}
		a->record_buffer_decode_pos += CHUNK_SIZE;
		cm->sequence++;

		data[pos++] = (i == frames_per_packet - 1 ? 0x00 : 0x80) | (len & 0x7F);
		memcpy(&data[pos], buffer, len);
		pos += len;
	}

	cmumble_init_udptunnel(&tunnel);
	tunnel.packet.data = data;
	tunnel.packet.len = pos;
	/* TODO: Use a util function that allows to check whether we're currently connected.
	 * check cm->user is not really obvious */
	if (cm->user) {
		cmumble_send_udptunnel(cm, &tunnel);
	}

	return 0;
}

/* This is called whenever new data may is available */
static void stream_read_callback(pa_stream *s, size_t length, void *userdata)
{
	struct cmumble *cm = userdata;
	struct cmumble_audio *a = &cm->audio;
	const void *data;

	assert(s);
	assert(length > 0);

	/*
	if (stdio_event)
		mainloop_api->io_enable(stdio_event, PA_IO_EVENT_OUTPUT);
	*/

	if (pa_stream_peek(s, &data, &length) < 0) {
		g_printerr("pa_stream_peek() failed: %s\n", pa_strerror(pa_context_errno(cm->pulse_context)));
		//quit(1);
		return;
	}

	assert(data);
	assert(length > 0);

	/* TODO replace this endlessly growing buffer with a ring buffer? */
	if (a->record_buffer) {
		a->record_buffer = pa_xrealloc(a->record_buffer,
					       a->record_buffer_length + length);
		memcpy((uint8_t*) a->record_buffer + a->record_buffer_length,
		       data,
		       length);
		a->record_buffer_length += length;
	} else {
		a->record_buffer = pa_xmalloc(length);
		memcpy(a->record_buffer, data, length);
		a->record_buffer_length = length;
		a->record_buffer_index = 0;
	}
	pa_stream_drop(s);

	try_encode_pcm(cm);
}


/* This routine is called whenever the stream state changes */
static void stream_state_callback(pa_stream *s, void *userdata)
{
	struct cmumble *cm = userdata;
	assert(s);

	switch (pa_stream_get_state(s)) {
	case PA_STREAM_CREATING:
	case PA_STREAM_TERMINATED:
		break;

	case PA_STREAM_READY:
		if (cm->verbose) {
			const pa_buffer_attr *a;
			char cmt[PA_CHANNEL_MAP_SNPRINT_MAX], sst[PA_SAMPLE_SPEC_SNPRINT_MAX];

			g_printerr("Stream successfully created.\n");

			if (!(a = pa_stream_get_buffer_attr(s)))
				g_printerr("pa_stream_get_buffer_attr() failed: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
			else {

				//if (mode == PLAYBACK)
				//	g_printerr("Buffer metrics: maxlength=%u, tlength=%u, prebuf=%u, minreq=%u\n", a->maxlength, a->tlength, a->prebuf, a->minreq);
				//else {
					//assert(mode == RECORD);
					g_printerr("Buffer metrics: maxlength=%u, fragsize=%u\n", a->maxlength, a->fragsize);
				//}
			}

			g_printerr("Using sample spec '%s', channel map '%s'.\n",
				pa_sample_spec_snprint(sst, sizeof(sst), pa_stream_get_sample_spec(s)),
				pa_channel_map_snprint(cmt, sizeof(cmt), pa_stream_get_channel_map(s)));

			g_printerr("Connected to device %s (%u, %ssuspended).\n",
				pa_stream_get_device_name(s),
				pa_stream_get_device_index(s),
				pa_stream_is_suspended(s) ? "" : "not ");
		}

		break;

	case PA_STREAM_FAILED:
	default:
		g_printerr("Stream error: %s\n", pa_strerror(pa_context_errno(pa_stream_get_context(s))));
		//quit(1);
	}
}


static void stream_suspended_callback(pa_stream *s, void *userdata)
{
	struct cmumble *cm = userdata;
	assert(s);

	if (cm->verbose) {
		if (pa_stream_is_suspended(s))
			g_printerr("Stream device suspended.\n");
		else
			g_printerr("Stream device resumed.\n");
	}
}

static void stream_underflow_callback(pa_stream *s, void *userdata)
{
	struct cmumble *cm = userdata;
	assert(s);

	if (cm->verbose)
		g_printerr("Stream underrun.\n");
}

static void stream_overflow_callback(pa_stream *s, void *userdata)
{
	struct cmumble *cm = userdata;
	assert(s);

	if (cm->verbose)
		g_printerr("Stream overrun.\n");
}

static void stream_started_callback(pa_stream *s, void *userdata)
{
	struct cmumble *cm = userdata;
	assert(s);

	if (cm->verbose)
		g_printerr("Stream started.\n");
}

static void stream_moved_callback(pa_stream *s, void *userdata)
{
	struct cmumble *cm = userdata;
	assert(s);

	if (cm->verbose)
		g_printerr("Stream moved to device %s (%u, %ssuspended).\n",
			   pa_stream_get_device_name(s),
			   pa_stream_get_device_index(s),
			   pa_stream_is_suspended(s) ? "" : "not ");
}

static void stream_buffer_attr_callback(pa_stream *s, void *userdata)
{
	struct cmumble *cm = userdata;
	assert(s);

	if (cm->verbose)
		g_printerr("Stream buffer attributes changed.\n");
}

static void stream_event_callback(pa_stream *s, const char *name, pa_proplist *pl, void *userdata)
{
	//struct cmumble *cm = userdata;
	char *t;

	assert(s);
	assert(name);
	assert(pl);

	t = pa_proplist_to_string_sep(pl, ", ");
	g_printerr("Got event '%s', properties '%s'\n", name, t);
	pa_xfree(t);
}












void
cmumble_audio_push(struct cmumble *cm, struct cmumble_user *user,
		   const guint8 *data, gsize size)
{
	/*
	GstBuffer *gstbuf;

	gstbuf = gst_app_buffer_new(g_memdup(data, size), size, g_free, NULL);
	gst_app_src_push_buffer(user->src, gstbuf);
	*/
	/*
	if (cm->verbose)
		g_print("Dropping buffer");
		*/
}

//static GstFlowReturn
//pull_buffer(GstAppSink *sink, gpointer user_data)
//{
//	struct cmumble *cm = user_data;
//	GstBuffer *buf;
//	uint8_t data[1024];
//	uint32_t write = 0, pos = 0;
//	mumble_udptunnel_t tunnel;
//	static int seq = 0;
//
//	/* FIXME: Make this more generic/disable pulling
//	 * the pipeline completely if not connected?
//	 */
//	if (cm->con.conn == NULL)
//		return GST_FLOW_OK;
//
//	buf = gst_app_sink_pull_buffer(cm->audio.sink);
//
//	if (++seq <= 2) {
//		gst_buffer_unref(buf);
//		return GST_FLOW_OK;
//	}
//	if (GST_BUFFER_SIZE(buf) > 127) {
//		g_printerr("GOT TOO BIG BUFFER\n");
//		return GST_FLOW_ERROR;
//	}
//
//	data[pos++] = (udp_voice_celt_alpha << 5) | (udp_normal_talking);
//
//	encode_varint(&data[pos], &write, ++cm->sequence, sizeof(data)-pos);
//	pos += write;
//
//	data[pos++] = 0x00 /*: 0x80 */ | (GST_BUFFER_SIZE(buf) & 0x7F);
//	memcpy(&data[pos], GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
//	pos += GST_BUFFER_SIZE(buf);
//
//	gst_buffer_unref(buf);
//
//	cmumble_init_udptunnel(&tunnel);
//	tunnel.packet.data = data;
//	tunnel.packet.len = pos;
//	cmumble_send_udptunnel(cm, &tunnel);
//
//	return GST_FLOW_OK;
//}

/*
static gboolean
idle(gpointer user_data)
{
	struct cmumble *cm = user_data;
	GstAppSink *sink;

	while ((sink = g_async_queue_try_pop(cm->async_queue)) != NULL)
		pull_buffer(sink, cm);

	return FALSE;
}

static GstFlowReturn
new_buffer(GstAppSink *sink, gpointer user_data)
{
	struct cmumble *cm = user_data;

	g_async_queue_push(cm->async_queue, sink);
	g_idle_add(idle, cm);

	return GST_FLOW_OK;
}
*/

/* TODO pulseaudio with echo cancellation/webrtc audio processing?
 *
 * https://www.freedesktop.org/software/pulseaudio/webrtc-audio-processing/
 *
 */

//static int
//setup_recording_gst_pipeline(struct cmumble *cm)
//{
//	GstElement *pipeline, *cutter, *sink;
//	GError *error = NULL;
//	GstCaps *caps;
//
//	char *desc = "autoaudiosrc ! cutter name=cutter ! audioresample ! audioconvert ! "
//		"audio/x-raw-int,channels=1,depth=16,rate=48000,signed=TRUE,width=16 ! "
//		"celtenc ! appsink name=sink";
//
//	pipeline = gst_parse_launch(desc, &error);
//	if (error) {
//		g_printerr("Failed to create pipeline: %s\n", error->message);
//		return -1;
//	}
//	sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
//	cm->audio.sink = GST_APP_SINK(sink);
//	cm->audio.record_pipeline = pipeline;
//
//	cutter = gst_bin_get_by_name(GST_BIN(pipeline), "cutter");
//	g_object_set(G_OBJECT(cutter),
//		     "threshold_dB", -45.0, "leaky", TRUE, NULL);
//
//	gst_app_sink_set_emit_signals(cm->audio.sink, TRUE);
//	gst_app_sink_set_drop(cm->audio.sink, FALSE);;
//	g_signal_connect(sink, "new-buffer", G_CALLBACK(new_buffer), cm);
//
//	caps = gst_caps_new_simple("audio/x-celt",
//				   "rate", G_TYPE_INT, SAMPLERATE,
//				   "channels", G_TYPE_INT, 1,
//				   "frame-size", G_TYPE_INT, SAMPLERATE/100,
//				   NULL);
//	gst_app_sink_set_caps(cm->audio.sink, caps);
//	gst_caps_unref(caps);
//
//	gst_element_set_state(pipeline, GST_STATE_PLAYING);
//
//	cm->sequence = 0;
//
//	return 0;
//}

//static void
//set_pulse_states(gpointer data, gpointer user_data)
//{
//	GstElement *elm = data;
//	struct cmumble_user *user = user_data;
//	GstStructure *props;
//	gchar *name;
//
//	if (g_strcmp0(G_OBJECT_TYPE_NAME(elm), "GstPulseSink") != 0 ||
//	    g_object_class_find_property(G_OBJECT_GET_CLASS(elm),
//					 "stream-properties") == NULL)
//		goto out;
//
//	/* FIXME: Move this into a man-page or so:
//	 * Dear User: Add the following to the pulseaudio configuration:
//	 * load-module module-device-manager "do_routing=1"
//	 * This is to let new join users default to e.g. a headset output.
//	 * Also consider setting device.intended_roles = "phone" for your
//	 * output to be marked as headset (if you dont have a usb headset dev). */
//
//	name = g_strdup_printf("cmumble [%s]", user->name);
//
//	props = gst_structure_new("props",
//				  "application.name", G_TYPE_STRING, name,
//				  "media.role", G_TYPE_STRING, "phone",
//				  NULL);
//
//	g_object_set(elm, "stream-properties", props, NULL);
//	gst_structure_free(props);
//	g_free(name);
//
//out:
//	g_object_unref(G_OBJECT(elm));
//}

/* This is called whenever the context status changes */
static void context_state_callback(pa_context *c, void *userdata)
{
	struct cmumble *cm = userdata;
	assert(c);

	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_CONNECTING:
	case PA_CONTEXT_AUTHORIZING:
	case PA_CONTEXT_SETTING_NAME:
		break;

	case PA_CONTEXT_READY: {
		int r;
		pa_stream *stream = NULL;

		static const pa_sample_spec ss = {
			.format   = PA_SAMPLE_S16NE,
			.rate     = SAMPLERATE,
			.channels = CHANNELS
		};


		gchar *stream_name = "cmumble";
		//stream_name = g_strdup_printf("cmumble [%s]", user->name);
		/* TODO: use pa_stream_new_with_proplist and set filter.want=echo-cancel */
		if (!(stream = pa_stream_new(c, stream_name, &ss, NULL))) {
			g_printerr("pa_stream_new() failed: %s\n", pa_strerror(pa_context_errno(c)));
			//g_free(name);
			goto fail;
		}
		//g_free(stream_name);
		cm->pulse_stream_record = stream;

		pa_stream_set_state_callback(stream, stream_state_callback, cm);
		pa_stream_set_write_callback(stream, stream_write_callback, cm);
		pa_stream_set_read_callback(stream, stream_read_callback, cm);
		pa_stream_set_suspended_callback(stream, stream_suspended_callback, cm);
		pa_stream_set_moved_callback(stream, stream_moved_callback, cm);
		pa_stream_set_underflow_callback(stream, stream_underflow_callback, cm);
		pa_stream_set_overflow_callback(stream, stream_overflow_callback, cm);
		pa_stream_set_started_callback(stream, stream_started_callback, cm);
		pa_stream_set_event_callback(stream, stream_event_callback, cm);
		pa_stream_set_buffer_attr_callback(stream, stream_buffer_attr_callback, cm);

		//if ((r = pa_stream_connect_playback(stream, NULL, NULL, 0, NULL, NULL)) < 0) {
		//
		if ((r = pa_stream_connect_record(stream, NULL, NULL, 0)) < 0) {
			g_printerr("pa_stream_connect_record() failed: %s\n", pa_strerror(pa_context_errno(c)));
			goto fail;
		}

		break;
	}

	case PA_CONTEXT_TERMINATED:
		//quit(0);
		//TODO: destroy user pipeline
		break;

	case PA_CONTEXT_FAILED:
	default:
		g_printerr("Connection failure: %s\n", pa_strerror(pa_context_errno(c)));
		goto fail;
	}

	return;

fail:
	//quit(1);
	return;

}

int
cmumble_audio_create_playback_pipeline(struct cmumble *cm,
				       struct cmumble_user *user)
{
//	GstElement *pipeline, *sink_bin;
//	GError *error = NULL;
//	char *desc = "appsrc name=src ! celtdec ! audioconvert ! autoaudiosink name=sink";
//	GstIterator *it;
//
//	pipeline = gst_parse_launch(desc, &error);
//	if (error) {
//		g_printerr("Failed to create pipeline: %s\n", error->message);
//		return -1;
//	}
//
//	user->pipeline = pipeline;
//	user->src = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline), "src"));
//
//	/* Important! */
//	gst_base_src_set_live(GST_BASE_SRC(user->src), TRUE); 
//	gst_base_src_set_do_timestamp(GST_BASE_SRC(user->src), TRUE);
//	gst_base_src_set_format(GST_BASE_SRC(user->src), GST_FORMAT_TIME);
//
//	gst_app_src_set_stream_type(user->src, GST_APP_STREAM_TYPE_STREAM); 
//
//	gst_element_set_state(pipeline, GST_STATE_PLAYING);
//
//	/* FIXME: Use a recursive name for sink-actual-sink-pluse instead? like:
//	 * gst_bin_get_by_name(GST_BIN(pipeline), "sink-actual-sink-pulse"); */
//	sink_bin = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
//	it = gst_bin_iterate_sinks(GST_BIN(sink_bin));
//	gst_iterator_foreach(it, set_pulse_states, user);
//	gst_iterator_free(it);
//
//	/* Setup Celt Decoder */
//	cmumble_audio_push(cm, user,
//			   cm->audio.celt_header_packet, sizeof(CELTHeader));
//	/* fake vorbiscomment buffer */
//	cmumble_audio_push(cm, user, NULL, 0);
//
	return 0;
}

/*
static int
setup_playback_gst_pipeline(struct cmumble *cm)
{
	cm->audio.celt_mode = celt_mode_create(SAMPLERATE,
					       SAMPLERATE / 100, NULL);
#ifdef HAVE_CELT071
	celt_header_init(&cm->audio.celt_header, cm->audio.celt_mode, CHANNELS);
#else
	celt_header_init(&cm->audio.celt_header, cm->audio.celt_mode, SAMPLERATE/100, CHANNELS);
#endif
	celt_header_to_packet(&cm->audio.celt_header,
			      cm->audio.celt_header_packet, sizeof(CELTHeader));

	celt_mode_info(cm->audio.celt_mode, CELT_GET_BITSTREAM_VERSION,
		       &cm->audio.celt_bitstream_version);

	return 0;
}
*/


int
cmumble_audio_init(struct cmumble *cm)
{
	if (!(cm->pulse_glib_mainloop = pa_glib_mainloop_new(NULL))) {
		g_printerr("pa_glib_mainloop_new failed\n");
		return -1;
	}
	cm->pulse_mainloop_api = pa_glib_mainloop_get_api(cm->pulse_glib_mainloop);

	if (!(cm->pulse_context = pa_context_new(cm->pulse_mainloop_api, "cmumble"))) {
		g_printerr("pa_context_new failed: %s\n",
			   pa_strerror(pa_context_errno(cm->pulse_context)));
		return -1;
	}

	pa_context_set_state_callback(cm->pulse_context,
				      context_state_callback, cm);
	if (pa_context_connect(cm->pulse_context, NULL,
			       PA_CONTEXT_NOFLAGS, NULL)) {
		g_printerr("pa_context_connect failed: %s\n",
			   pa_strerror(pa_context_errno(cm->pulse_context)));
		return -1;
	}

	/*
	if (setup_playback_gst_pipeline(cm) < 0)
		return -1;

	if (setup_recording_gst_pipeline(cm) < 0)
		return -1;
	*/

	gint error = CELT_OK;
	cm->audio.celt_mode = celt_mode_create(SAMPLERATE, FRAMESIZE, NULL);
#ifdef HAVE_CELT071
	cm->audio.celt_encoder = celt_encoder_create(cm->audio.celt_mode, CHANNELS, &error);
#else
	cm->audio.celt_encoder = celt_encoder_create_custom(cm->audio.celt_mode, CHANNELS, &error);
#endif

//celt_encoder_ctl(cm->audio.celt_encoder, CELT_RESET_STATE);



	return 0;

//celt_encoder_ctl(cm->audio.celt_encoder, CELT_SET_VBR_RATE (enc->bitrate / 1000), 0);


#ifdef HAVE_CELT071
	celt_header_init(&cm->audio.celt_header, cm->audio.celt_mode, CHANNELS);
#else
	celt_header_init(&cm->audio.celt_header, cm->audio.celt_mode, SAMPLERATE/100, CHANNELS);
#endif
	celt_header_to_packet(&cm->audio.celt_header,
			      cm->audio.celt_header_packet, sizeof(CELTHeader));

	celt_mode_info(cm->audio.celt_mode, CELT_GET_BITSTREAM_VERSION,
		       &cm->audio.celt_bitstream_version);


	return 0;
}

int
cmumble_audio_fini(struct cmumble *cm)
{
	if (cm->pulse_stream_record) {
		/* TODO: handle return value(?) */
		pa_stream_disconnect(cm->pulse_stream_record);
		pa_stream_unref(cm->pulse_stream_record);
		cm->pulse_stream_record = NULL;
	}

	if (cm->pulse_context) {
		pa_context_unref(cm->pulse_context);
		cm->pulse_context = NULL;
	}
	if (cm->pulse_glib_mainloop) {
		pa_glib_mainloop_free(cm->pulse_glib_mainloop);
		cm->pulse_glib_mainloop = NULL;
	}

	return 0;
}
