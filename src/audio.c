#include "audio.h"
#include "varint.h"
#include "cmumble.h"
#include <string.h>

#define SAMPLERATE 48000
#define CHANNELS 1

void
cmumble_audio_push(struct cmumble *cm, struct cmumble_user *user,
		   const guint8 *data, gsize size)
{
	GstBuffer *gstbuf;

	gstbuf = gst_app_buffer_new(g_memdup(data, size), size, g_free, NULL);
	gst_app_src_push_buffer(user->src, gstbuf);
}

static GstFlowReturn
pull_buffer(GstAppSink *sink, gpointer user_data)
{
	struct cmumble *cm = user_data;
	GstBuffer *buf;
	uint8_t data[1024];
	uint32_t write = 0, pos = 0;
	mumble_udptunnel_t tunnel;
	static int seq = 0;

	/* FIXME: Make this more generic/disable pulling
	 * the pipeline completely if not connected?
	 */
	if (cm->con.conn == NULL)
		return GST_FLOW_OK;

	buf = gst_app_sink_pull_buffer(cm->audio.sink);

	if (++seq <= 2) {
		gst_buffer_unref(buf);
		return GST_FLOW_OK;
	}
	if (GST_BUFFER_SIZE(buf) > 127) {
		g_printerr("GOT TOO BIG BUFFER\n");
		return GST_FLOW_ERROR;
	}

	data[pos++] = (udp_voice_celt_alpha << 5) | (udp_normal_talking);

	encode_varint(&data[pos], &write, ++cm->sequence, sizeof(data)-pos);
	pos += write;

	data[pos++] = 0x00 /*: 0x80 */ | (GST_BUFFER_SIZE(buf) & 0x7F);
	memcpy(&data[pos], GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
	pos += GST_BUFFER_SIZE(buf);

	gst_buffer_unref(buf);

	cmumble_init_udptunnel(&tunnel);
	tunnel.packet.data = data;
	tunnel.packet.len = pos;
	cmumble_send_udptunnel(cm, &tunnel);

	return GST_FLOW_OK;
}

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

static int
setup_recording_gst_pipeline(struct cmumble *cm)
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
	cm->audio.sink = GST_APP_SINK(sink);
	cm->audio.record_pipeline = pipeline;

	cutter = gst_bin_get_by_name(GST_BIN(pipeline), "cutter");
	g_object_set(G_OBJECT(cutter),
		     "threshold_dB", -45.0, "leaky", TRUE, NULL);

	gst_app_sink_set_emit_signals(cm->audio.sink, TRUE);
	gst_app_sink_set_drop(cm->audio.sink, FALSE);;
	g_signal_connect(sink, "new-buffer", G_CALLBACK(new_buffer), cm);

	caps = gst_caps_new_simple("audio/x-celt",
				   "rate", G_TYPE_INT, SAMPLERATE,
				   "channels", G_TYPE_INT, 1,
				   "frame-size", G_TYPE_INT, SAMPLERATE/100,
				   NULL);
	gst_app_sink_set_caps(cm->audio.sink, caps);
	gst_caps_unref(caps);

	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	cm->sequence = 0;

	return 0;
}

static void
set_pulse_states(gpointer data, gpointer user_data)
{
	GstElement *elm = data;
	struct cmumble_user *user = user_data;
	GstStructure *props;
	gchar *name;

	if (g_strcmp0(G_OBJECT_TYPE_NAME(elm), "GstPulseSink") != 0 ||
	    g_object_class_find_property(G_OBJECT_GET_CLASS(elm),
					 "stream-properties") == NULL)
		goto out;

	/* FIXME: Move this into a man-page or so:
	 * Dear User: Add the following to the pulseaudio configuration:
	 * load-module module-device-manager "do_routing=1"
	 * This is to let new join users default to e.g. a headset output.
	 * Also consider setting device.intended_roles = "phone" for your
	 * output to be marked as headset (if you dont have a usb headset dev). */

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
cmumble_audio_create_playback_pipeline(struct cmumble *cm,
				       struct cmumble_user *user)
{
	GstElement *pipeline, *sink_bin;
	GError *error = NULL;
	char *desc = "appsrc name=src ! celtdec ! audioconvert ! autoaudiosink name=sink";
	GstIterator *it;

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

	/* FIXME: Use a recursive name for sink-actual-sink-pluse instead? like:
	 * gst_bin_get_by_name(GST_BIN(pipeline), "sink-actual-sink-pulse"); */
	sink_bin = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
	it = gst_bin_iterate_sinks(GST_BIN(sink_bin));
	gst_iterator_foreach(it, set_pulse_states, user);
	gst_iterator_free(it);

	/* Setup Celt Decoder */
	cmumble_audio_push(cm, user,
			   cm->audio.celt_header_packet, sizeof(CELTHeader));
	/* fake vorbiscomment buffer */
	cmumble_audio_push(cm, user, NULL, 0);

	return 0;
}

static int
setup_playback_gst_pipeline(struct cmumble *cm)
{
	cm->audio.celt_mode = celt_mode_create(SAMPLERATE,
					       SAMPLERATE / 100, NULL);
	celt_header_init(&cm->audio.celt_header, cm->audio.celt_mode, CHANNELS);
	celt_header_to_packet(&cm->audio.celt_header,
			      cm->audio.celt_header_packet, sizeof(CELTHeader));

	celt_mode_info(cm->audio.celt_mode, CELT_GET_BITSTREAM_VERSION,
		       &cm->audio.celt_bitstream_version);

	return 0;
}

int
cmumble_audio_init(struct cmumble *cm)
{
	if (setup_playback_gst_pipeline(cm) < 0)
		return -1;

	if (setup_recording_gst_pipeline(cm) < 0)
		return -1;

	return 0;
}

int
cmumble_audio_fini(struct cmumble *cm)
{

	return 0;
}

