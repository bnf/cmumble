#include "audio.h"
#include "varint.h"
#include "cmumble.h"
#include <string.h>

#define SAMPLERATE 48000
#define CHANNELS 1

void
cmumble_audio_push(struct context *ctx, struct user *user,
		   const uint8_t *data, gsize size)
{
	GstBuffer *gstbuf;
	
	gstbuf = gst_app_buffer_new(g_memdup(data, size), size, g_free, NULL);
	gst_app_src_push_buffer(user->src, gstbuf);
}

static GstFlowReturn
pull_buffer(GstAppSink *sink, gpointer user_data)
{
	struct context *ctx = user_data;
	GstBuffer *buf;
	uint8_t data[1024];
	uint32_t write = 0, pos = 0;
	MumbleProto__UDPTunnel tunnel;
	static int seq = 0;

	buf = gst_app_sink_pull_buffer(ctx->audio.sink);

	if (++seq <= 2) {
		gst_buffer_unref(buf);
		return GST_FLOW_OK;
	}
	if (GST_BUFFER_SIZE(buf) > 127) {
		g_printerr("GOT TOO BIG BUFFER\n");
		return GST_FLOW_ERROR;
	}

	data[pos++] = (udp_voice_celt_alpha) | (0 << 4);

	encode_varint(&data[pos], &write, ++ctx->sequence, 1024-pos);
	pos += write;

	data[pos++] = 0x00 /*: 0x80 */ | (GST_BUFFER_SIZE(buf) & 0x7F);
	memcpy(&data[pos], GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
	pos += GST_BUFFER_SIZE(buf);

	gst_buffer_unref(buf);

	mumble_proto__udptunnel__init(&tunnel);
	tunnel.packet.data = data;
	tunnel.packet.len = pos;
	cmumble_send_msg(ctx, &tunnel.base);

	return GST_FLOW_OK;
}

static int
setup_recording_gst_pipeline(struct context *ctx)
{
	GstElement *pipeline, *cutter, *sink;
	GError *error = NULL;
	GstCaps *caps;

	char *desc = "autoaudiosrc ! cutter name=cutter ! audioresample ! audioconvert ! "
		     "audio/x-raw-int,channels=1,depth=16,rate=48000,signed=TRUE,width=16 ! "
		     "celtenc ! appsink name=sink";

	pipeline = gst_parse_launch(desc, &error);
	if (error) {
		g_printerr("Failed to create pipeline: %s\n", error->message);
		return -1;
	}
	sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
	ctx->audio.sink = GST_APP_SINK(sink);
	ctx->audio.record_pipeline = pipeline;

	cutter = gst_bin_get_by_name(GST_BIN(pipeline), "cutter");
	g_object_set(G_OBJECT(cutter),
		     "threshold_dB", -45.0, "leaky", TRUE, NULL);
	
	gst_app_sink_set_emit_signals(ctx->audio.sink, TRUE);
	gst_app_sink_set_drop(ctx->audio.sink, FALSE);;
	g_signal_connect(sink, "new-buffer", G_CALLBACK(pull_buffer), ctx);
	
	caps = gst_caps_new_simple("audio/x-celt",
				   "rate", G_TYPE_INT, SAMPLERATE,
				   "channels", G_TYPE_INT, 1,
				   "frame-size", G_TYPE_INT, SAMPLERATE/100,
				   NULL);
	gst_app_sink_set_caps(ctx->audio.sink, caps);
	gst_caps_unref(caps);

	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	ctx->sequence = 0;

	return 0;
}

static void
set_pulse_states(gpointer data, gpointer user_data)
{
	GstElement *elm = data;
	struct user *user = user_data;
	GstStructure *props;
	gchar *name;

	if (g_strcmp0(G_OBJECT_TYPE_NAME(elm), "GstPulseSink") != 0 ||
	    g_object_class_find_property(G_OBJECT_GET_CLASS(elm),
					 "stream-properties") == NULL)
		goto out;

	/* configure pulseaudio to use:
	 * load-module module-device-manager "do_routing=1"
	 * or new users may join to default output which is not headset?
	 * Also consider setting device.intended_roles = "phone" for your
	 * wanted default output (if you dont have a usb headset dev). */

	name = g_strdup_printf("cmumble [%s]", user->name);

	props = gst_structure_new("props",
				  "application.name", G_TYPE_STRING, name,
				  "media.role", G_TYPE_STRING, "phone",
				  NULL);
					
	g_object_set(elm, "stream-properties", props, NULL);
	gst_structure_free(props);
	g_free(name);

out:
	g_object_unref(G_OBJECT(elm));
}

int
cmumble_audio_create_playback_pipeline(struct context *ctx, struct user *user)
{
	GstElement *pipeline, *sink_bin;
	GError *error = NULL;
	char *desc = "appsrc name=src ! celtdec ! audioconvert ! autoaudiosink name=sink";

	pipeline = gst_parse_launch(desc, &error);
	if (error) {
		g_printerr("Failed to create pipeline: %s\n", error->message);
		return -1;
	}

	user->pipeline = pipeline;
	user->src = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline), "src"));

	/* Important! */
	gst_base_src_set_live(GST_BASE_SRC(user->src), TRUE); 
	gst_base_src_set_do_timestamp(GST_BASE_SRC(user->src), TRUE);
	gst_base_src_set_format(GST_BASE_SRC(user->src), GST_FORMAT_TIME);

	gst_app_src_set_stream_type(user->src, GST_APP_STREAM_TYPE_STREAM); 

	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	sink_bin = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
	GstIterator *iter = gst_bin_iterate_sinks(GST_BIN(sink_bin));
	gst_iterator_foreach(iter, set_pulse_states, user);
	gst_iterator_free(iter);

	/* Setup Celt Decoder */
	cmumble_audio_push(ctx, user,
			   ctx->audio.celt_header_packet, sizeof(CELTHeader));
	/* fake vorbiscomment buffer */
	cmumble_audio_push(ctx, user, NULL, 0);

	return 0;
}

static int
setup_playback_gst_pipeline(struct context *ctx)
{
	ctx->audio.celt_mode = celt_mode_create(SAMPLERATE,
					  SAMPLERATE / 100, NULL);
	celt_header_init(&ctx->audio.celt_header, ctx->audio.celt_mode, CHANNELS);
	celt_header_to_packet(&ctx->audio.celt_header,
			      ctx->audio.celt_header_packet, sizeof(CELTHeader));

	return 0;
}

int
cmumble_audio_init(struct context *ctx)
{
	if (setup_playback_gst_pipeline(ctx) < 0)
		return -1;

	if (setup_recording_gst_pipeline(ctx) < 0)
		return -1;

	return 0;
}

int
cmumble_audio_fini(struct context *ctx)
{

	return 0;
}

