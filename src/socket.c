#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/time.h>

#include <celt/celt.h>
#include <celt/celt_header.h>
#include <speex/speex_jitter.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappbuffer.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "mumble.pb-c.h"
#include "varint.h"
#include "messages.h"

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

#define PREAMBLE_SIZE 6

struct context {
	GMainLoop *loop;

	uint32_t session;
	bool authenticated;

	GSocketClient *sock_client;
	GSocketConnection *conn;
	GSocket *sock;
	GIOStream *iostream;

	CELTHeader celt_header;
	CELTMode *celt_mode;

	GstElement *playback_pipeline;
	GstElement *record_pipeline;
	GstAppSrc *src;
	GstAppSink *sink;

	int64_t sequence;
};

enum udp_message_type {
	udp_voice_celt_alpha,
	udp_ping,
	udp_voice_speex,
	udp_voice_celt_beta
};

static void
appsrc_push(GstAppSrc *src, const void *mem, size_t size)
{
	GstBuffer *gstbuf;
	
	gstbuf = gst_app_buffer_new(g_memdup(mem, size), size,
				    g_free, NULL);
	gst_app_src_push_buffer(src, gstbuf);
}

static void
add_preamble(uint8_t *buffer, uint16_t type, uint32_t len)
{
	buffer[1] = (type) & 0xff;
	buffer[0] = (type >> 8) & 0xff;

	buffer[5] = (len) & 0xff;
	buffer[4] = (len >> 8) & 0xff;
	buffer[3] = (len >> 16) & 0xff;
	buffer[2] = (len >> 24) & 0xff;	
}

static void
get_preamble(uint8_t *buffer, int *type, int *len)
{
	uint16_t msgType;
	uint32_t msgLen;

	msgType = buffer[1] | (buffer[0] << 8);
	msgLen = buffer[5] | (buffer[4] << 8) | (buffer[3] << 16) | (buffer[2] << 24);
	*type = (int)msgType;
	*len = (int)msgLen;
}

GStaticMutex write_mutex = G_STATIC_MUTEX_INIT;

static GstFlowReturn
pull_buffer(GstAppSink *sink, gpointer user_data)
{
	struct context *ctx = user_data;
	GstBuffer *buf;
	uint8_t data[1024];
	uint32_t write = 0;
	uint32_t pos = 0;
	GOutputStream *output = g_io_stream_get_output_stream(ctx->iostream);

	static uint64_t seq = 0;

	/* header will be written at the end */
	pos = PREAMBLE_SIZE;

	buf = gst_app_sink_pull_buffer(ctx->sink);

	++seq;
	if (seq <= 2) {
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

	add_preamble(&data[0], 1, pos-PREAMBLE_SIZE);
	g_static_mutex_lock(&write_mutex);
	g_output_stream_write(output, data, PREAMBLE_SIZE, NULL, NULL);
	g_output_stream_write(output, &data[PREAMBLE_SIZE], pos-PREAMBLE_SIZE, NULL, NULL);
	g_static_mutex_unlock(&write_mutex);

	return GST_FLOW_OK;
}

static void
handle_udp(struct context *ctx, uint8_t *data, uint32_t len)
{
	int64_t session;
	int64_t sequence;
	uint32_t pos = 1;
	uint32_t read = 0;
	uint8_t frame_len, terminator;

	printf("data[0]: 0x%x\n", data[0]);
	printf("len: %u\n", len);
	session  = decode_varint(&data[pos], &read, len-pos);
	pos += read;
	sequence = decode_varint(&data[pos], &read, len-pos);
	pos += read;
	printf("session: %ld, sequence: %ld\n", session, sequence);

	do {
		frame_len  = data[pos] & 0x7F;
		terminator = data[pos] & 0x80;
		pos += 1;
		
		if (frame_len == 0 || frame_len > len-pos)
			break;

		appsrc_push(ctx->src, &data[pos], frame_len);

		pos += frame_len;
		sequence++;
	} while (terminator);
}

static void
recv_version(MumbleProto__Version *version, struct context *ctx)
{
	printf("version: 0x%x\n", version->version);
	printf("release: %s\n", version->release);
}

static void
recv_channel_state(MumbleProto__ChannelState *state, struct context *ctx)
{
	printf("channel: id: %u, parent: %u, name: %s, description: %s, temporary: %d, position: %d\n",
	       state->channel_id, state->parent, state->name, state->description, state->temporary, state->position);
}

static void
recv_server_sync(MumbleProto__ServerSync *sync, struct context *ctx)
{
	ctx->session = sync->session;

	printf("got session: %d\n", ctx->session);

}

static void
send_msg(struct context *ctx, ProtobufCMessage *msg)
{
	uint8_t pad[128];
	uint8_t preamble[PREAMBLE_SIZE];
	int type = -1;
	int i;
	ProtobufCBufferSimple buffer = PROTOBUF_C_BUFFER_SIMPLE_INIT(pad);
	GOutputStream *output = g_io_stream_get_output_stream(ctx->iostream);
	
	for (i = 0; i < ARRAY_SIZE(messages); ++i)
		if (messages[i].descriptor == msg->descriptor)
			type = i;
	assert(type >= 0);

	protobuf_c_message_pack_to_buffer(msg, &buffer.base);
	add_preamble(preamble, type, buffer.len);

	g_static_mutex_lock(&write_mutex);
	g_output_stream_write(output, preamble, PREAMBLE_SIZE, NULL, NULL);
	g_output_stream_write(output, buffer.data, buffer.len, NULL, NULL);
	g_static_mutex_unlock(&write_mutex);

	PROTOBUF_C_BUFFER_SIMPLE_CLEAR(&buffer);
}

typedef void (*callback_t)(void *, void *);

static void
recv_msg(struct context *ctx, const callback_t *callbacks, uint32_t callback_size)
{
	uint8_t preamble[PREAMBLE_SIZE];
	ProtobufCMessage *msg;
	void *data;
	int type, len;
	gssize ret;
	GInputStream *input = g_io_stream_get_input_stream(ctx->iostream);

	ret = g_input_stream_read(input, preamble, PREAMBLE_SIZE, NULL, NULL);

	if (ret <= 0) {
		printf("read failed: %ld\n", ret);
		return;
	}
	
	get_preamble(preamble, &type, &len);

	if (!(type >= 0 && type < ARRAY_SIZE(messages))) {
		printf("unknown message type: %d\n", type);
		return;
	}

	if (len <= 0) {
		printf("length 0\n");
		return;
	}

	data = malloc(len);
	if (data == NULL) {
		printf("out of mem\n");
		abort();
	}
	ret = g_input_stream_read(input, data, len, NULL, NULL);
	printf("read ret: %d len: %d\n", ret, len);

	/* tunneled udp data - not a regular protobuf message */
	if (type == 1) {
		handle_udp(ctx, data, len);
		free(data);
		return;
	}

	msg = protobuf_c_message_unpack(messages[type].descriptor, NULL,
					len, data);
	if (msg == NULL) {
		printf("message unpack failure\n");
		return;
	}

	printf("debug: received message: %s type:%d, len:%d\n", messages[type].name, type, len);
	if (callbacks[type])
		callbacks[type](msg, ctx);

	protobuf_c_message_free_unpacked(msg, NULL);
	free(data);
}

static void
do_ping(struct context *ctx)
{
	MumbleProto__Ping ping;
	struct timeval tv;

	gettimeofday(&tv, NULL);
	mumble_proto__ping__init(&ping);

	ping.timestamp = tv.tv_sec;
	ping.resync = 1;

	send_msg(ctx, &ping.base);
}

static const callback_t callbacks[] = {
	/* VERSION */ (callback_t) recv_version,
	[5] = (callback_t) recv_server_sync,
	[7] = (callback_t) recv_channel_state,
	[127] = NULL,
};

static gboolean
read_cb(GSocket *socket, GIOCondition condition, gpointer data)
{
	struct context *ctx = data;
	GInputStream *input = g_io_stream_get_input_stream(ctx->iostream);

	do {
		recv_msg(ctx, callbacks, ARRAY_SIZE(callbacks));
	} while (g_input_stream_has_pending(input));


	/* FIXME */
	static int i = 0;
	if (i++ < 2)
		do_ping(ctx);
	return TRUE;
}

static gboolean
bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
	struct context *ctx = data;
	GMainLoop *loop = ctx->loop;

	switch (GST_MESSAGE_TYPE (msg)) {

	case GST_MESSAGE_EOS:
		g_print ("End of stream\n");
		g_main_loop_quit (loop);
		break;

	case GST_MESSAGE_ERROR:
		{
			char  *debug;
			GError *error;

			gst_message_parse_error (msg, &error, &debug);
			g_free (debug);

			g_printerr ("Error: %s\n", error->message);
			g_error_free (error);

			g_main_loop_quit (loop);
			break;
		}
	default:
		g_print("unhandled message: %d %s\n", GST_MESSAGE_TYPE(msg), gst_message_type_get_name(GST_MESSAGE_TYPE(msg)));
		break;
	}

	return TRUE;
}

static void
app_need_data(GstAppSrc *src, guint length, gpointer user_data)
{
#if 0
	struct context *ctx = user_data;
#endif
}

static void
app_enough_data(GstAppSrc *src, gpointer user_data)
{
#if 0
	struct context *ctx = user_data;
#endif
}

static GstAppSrcCallbacks app_callbacks = {
	app_need_data,
	app_enough_data,
	NULL
};

static int
setup_playback_gst_pipeline(struct context *ctx)
{
	GstElement *pipeline, *src, *decoder, *conv, *sink;
	GstBus *bus;

	pipeline = gst_pipeline_new("cmumble-output");
	src = gst_element_factory_make("appsrc", "input");
	decoder = gst_element_factory_make("celtdec", "celt-decoder");
	conv = gst_element_factory_make("audioconvert", "converter");
	sink = gst_element_factory_make("autoaudiosink", "audio-output");

	if (!pipeline || !src || !decoder || !conv || !sink) {
		g_printerr("failed to initialize pipeline\n");
		return -1;
	}

	bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
	gst_bus_add_watch(bus, bus_call, ctx);
	gst_object_unref(bus);

	gst_bin_add_many(GST_BIN(pipeline),
			 src, decoder, conv, sink, NULL);
	gst_element_link_many(src, decoder, conv, sink, NULL);

	ctx->src = GST_APP_SRC(src);
	ctx->playback_pipeline = pipeline;

	/* Important! */
	gst_base_src_set_live(GST_BASE_SRC(ctx->src), TRUE); 
	gst_base_src_set_do_timestamp(GST_BASE_SRC(ctx->src), TRUE);
	gst_base_src_set_format(GST_BASE_SRC(ctx->src), GST_FORMAT_TIME);

	gst_app_src_set_stream_type(ctx->src, GST_APP_STREAM_TYPE_STREAM); 
	gst_app_src_set_callbacks(ctx->src, &app_callbacks, ctx, NULL);

	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	{ /* Setup Celt Decoder */
#define SAMPLERATE 48000
#define CHANNELS 1
		uint8_t celt_header_packet[sizeof(CELTHeader)];

		ctx->celt_mode = celt_mode_create(SAMPLERATE,
						  SAMPLERATE / 100, NULL);
		celt_header_init(&ctx->celt_header, ctx->celt_mode, CHANNELS);
		celt_header_to_packet(&ctx->celt_header,
				      celt_header_packet, sizeof(CELTHeader));

		appsrc_push(ctx->src, celt_header_packet, sizeof(CELTHeader));
		/* fake vorbiscomment buffer */
		appsrc_push(ctx->src, NULL, 0);
	}

	return 0;
}

static int
setup_recording_gst_pipeline(struct context *ctx)
{
	GstElement *pipeline, *src, *cutter, *resample,
		   *conv, *capsfilter, *encoder, *sink;
	GstBus *bus;
	GstCaps *caps;

	pipeline = gst_pipeline_new("cmumble-input");
	src = gst_element_factory_make("autoaudiosrc", "audio-input");
	cutter = gst_element_factory_make("cutter", "cutter");
	resample = gst_element_factory_make("audioresample", "resample");
	conv = gst_element_factory_make("audioconvert", "converter");
	capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
	encoder = gst_element_factory_make("celtenc", "celt-encoder");
	sink = gst_element_factory_make("appsink", "output");

	if (!pipeline || !src || !cutter || !resample || !conv ||
	    !capsfilter || !encoder || !sink) {
		g_printerr("failed to initialize pipeline\n");
		return -1;
	}

	bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
	gst_bus_add_watch(bus, bus_call, ctx);
	gst_object_unref(bus);

	gst_bin_add_many(GST_BIN(pipeline),
			 src, cutter, resample, conv, capsfilter, encoder, sink, NULL);
	gst_element_link_many(src, cutter, resample, conv, capsfilter, encoder, sink, NULL);

	ctx->sink = GST_APP_SINK(sink);
	ctx->record_pipeline = pipeline;

	caps = gst_caps_new_simple("audio/x-raw-int",
				   "channels", G_TYPE_INT, 1,
				   "depth", G_TYPE_INT, 16,
				   "rate", G_TYPE_INT, 48000,
				   "width", G_TYPE_INT, 16,
				   "signed", G_TYPE_BOOLEAN, TRUE,
				   NULL);
	g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
	gst_caps_unref(caps);

	g_object_set(G_OBJECT(cutter),
		     "threshold_dB", -45.0, "leaky", TRUE, NULL);
	
	gst_app_sink_set_emit_signals(ctx->sink, TRUE);
	gst_app_sink_set_drop(ctx->sink, FALSE);;
	g_signal_connect(sink, "new-buffer", G_CALLBACK(pull_buffer), ctx);
	
	caps = gst_caps_new_simple("audio/x-celt",
				   "rate", G_TYPE_INT, SAMPLERATE,
				   "channels", G_TYPE_INT, 1,
				   "frame-size", G_TYPE_INT, SAMPLERATE/100,
				   NULL);
	gst_app_sink_set_caps(ctx->sink, caps);
	gst_caps_unref(caps);

	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	ctx->sequence = 0;

	return 0;
}

int main(int argc, char **argv)
{
#if 1
	char *host = "localhost";
	unsigned int port = 64738;
#else
	char *host = "85.214.21.153";
	unsigned int port = 33321;
#endif
	struct context ctx;
	GError *error = NULL;

	memset(&ctx, 0, sizeof(ctx));

	g_type_init();
	ctx.sock_client = g_socket_client_new();
	g_socket_client_set_tls(ctx.sock_client, TRUE);
	g_socket_client_set_tls_validation_flags(ctx.sock_client,
						 G_TLS_CERTIFICATE_INSECURE);
	g_socket_client_set_family(ctx.sock_client, G_SOCKET_FAMILY_IPV4);
	g_socket_client_set_protocol(ctx.sock_client, G_SOCKET_PROTOCOL_TCP);
	g_socket_client_set_socket_type(ctx.sock_client, G_SOCKET_TYPE_STREAM);

	ctx.conn = g_socket_client_connect_to_host(ctx.sock_client,
						   host, port, NULL, &error);
	ctx.iostream = g_tcp_wrapper_connection_get_base_io_stream(G_TCP_WRAPPER_CONNECTION(ctx.conn));

	{
		MumbleProto__Version version;
		mumble_proto__version__init(&version);
		version.version = 0x010203;
		version.release = "cmumble 0.1";
		version.os = "Gentoo/Linux";
		send_msg(&ctx, &version.base);
	}

	{
		MumbleProto__Authenticate authenticate;
		mumble_proto__authenticate__init(&authenticate);
		authenticate.username = argv[1];
		authenticate.password = "";
		authenticate.n_celt_versions = 1;
		authenticate.celt_versions = (int32_t[]) { 0x8000000b };
		send_msg(&ctx, &authenticate.base);
	}

	gst_init(&argc, &argv);

	ctx.loop = g_main_loop_new(NULL, FALSE);

	if (setup_playback_gst_pipeline(&ctx) < 0)
		return 1;

	if (setup_recording_gst_pipeline(&ctx) < 0)
		return 1;

	ctx.sock = g_socket_connection_get_socket(ctx.conn);
	GSource *source = g_socket_create_source(ctx.sock, G_IO_IN | G_IO_ERR, NULL);
	g_source_set_callback(source, (GSourceFunc)read_cb, &ctx, NULL);
	g_source_attach(source, NULL);
	g_source_unref(source);

	g_main_loop_run(ctx.loop);

	g_main_loop_unref(ctx.loop);

	return 0;
}
