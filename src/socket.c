#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/types.h>

#include "mumble.pb-c.h"

#include "polarssl/net.h"
#include "polarssl/ssl.h"
#include "polarssl/havege.h"

#include <celt/celt.h>
#include <celt/celt_header.h>
#include <speex/speex_jitter.h>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappbuffer.h>

#include <glib.h>
#include <glib-object.h>

#include "messages.h"

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

#define PREAMBLE_SIZE 6

struct context {
	GMainLoop *loop;

	uint32_t session;
	bool authenticated;

	ssl_context ssl;
	havege_state hs;
	ssl_session ssn;
	int sock;
	GIOChannel *sock_channel;

	CELTHeader celt_header;
	CELTMode *celt_mode;

	GstElement *pipeline;
	GstAppSrc *src;
};

enum udp_message_type {
	udp_voice_celt_alpha,
	udp_ping,
	udp_voice_speex,
	udp_voice_celt_beta,
};

int64_t
decode_varint(uint8_t *data, uint32_t *read, uint32_t left)
{
	int64_t varint = 0;

	/* 1 byte with 7 · 8 + 1 leading zeroes */
	if ((data[0] & 0x80) == 0x00) {
		varint = data[0] & 0x7F;
		*read = 1;
	/* 2 bytes with 6 · 8 + 2 leading zeroes */
	} else if ((data[0] & 0xC0) == 0x80) {
		varint = ((data[0] & 0x3F) << 8) | data[1];
		*read = 2;
	/* 3 bytes with 5 · 8 + 3 leading zeroes */
	} else if ((data[0] & 0xE0) == 0xC0) {
		varint = (((data[0] & 0x1F) << 16) |
			    (data[1] << 8) | (data[2]));
		*read = 3;
	/* 4 bytes with 4 · 8 + 4 leading zeroes */
	} else if ((data[0] & 0xF0) == 0xE0) {
		varint = (((data[0] & 0x0F) << 24) | (data[1] << 16) |
			  (data[2] << 8) | (data[3]));
		*read = 4;
	} else /* if ((data[pos] & 0xF0) == 0xF0) */ {
		switch (data[0] & 0xFC) {
		/* 32-bit positive number */
		case 0xF0:
			varint = ((data[1] << 24) | (data[2] << 16) |
				  (data[3] << 8) | data[4]);
			*read = 1 + 4;
			break;
		/* 64-bit number */
		case 0xF4:
			varint =
				((int64_t)data[1] << 56) | ((int64_t)data[2] << 48) |
				((int64_t)data[3] << 40) | ((int64_t)data[4] << 32) |
				(data[5] << 24) | (data[6] << 16) |
				(data[7] <<  8) | (data[8] <<  0);
			*read = 1 + 8;
			break;
		/* Negative varint */
		case 0xF8:
			/* FIXME: handle endless recursion */
			varint = -decode_varint(&data[1], read, left - 1);
			*read += 1;
			break;
		/* Negative two bit number */
		case 0xFC:
			varint = -(int)(data[0] & 0x03);
			*read = 1;
			break;
		}
	}

	return varint;
}

static void
handle_udp(struct context *ctx, uint8_t *data, uint32_t len)
{
	int64_t session;
	int64_t sequence;
	int pos = 1;
	int read = 0;

	int frame_len, term;
	static int iseq = 0;

	session  = decode_varint(&data[pos], &read, len-pos);
	pos += read;
	sequence = decode_varint(&data[pos], &read, len-pos);
	pos += read;
	printf("session: %ld, sequence: %ld\n", session, sequence);

	do {
		frame_len = (data[pos] & 0x7F);
		term = (data[pos] & 0x80) == 0x80;
		printf("_len: %d, term: %d\n", frame_len, term);
		pos += 1;
		
		if (frame_len == 0)
			break;

		void *src = malloc(frame_len);
		memcpy(src, &data[pos], frame_len);

		GstBuffer *gstbuf = gst_app_buffer_new(src, frame_len, free, NULL);
		gst_app_src_push_buffer(ctx->src, gstbuf);

		pos += frame_len;
		sequence++;
	} while (term);
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
send_msg(struct context *ctx, ProtobufCMessage *msg)
{
	uint8_t pad[128];
	uint8_t preamble[PREAMBLE_SIZE];
	int ret = 0;
	int type = -1;
	int i;
	ProtobufCBufferSimple buffer = PROTOBUF_C_BUFFER_SIMPLE_INIT(pad);
	
	for (i = 0; i < ARRAY_SIZE(messages); ++i)
		if (messages[i].descriptor == msg->descriptor)
			type = i;
	assert(type >= 0);

	protobuf_c_message_pack_to_buffer(msg, &buffer.base);
	add_preamble(preamble, type, buffer.len);

	while ((ret = ssl_write(&ctx->ssl, preamble, PREAMBLE_SIZE)) <= 0) {
		if (ret != POLARSSL_ERR_NET_TRY_AGAIN) {
			printf("write failed: %d\n", ret);
			abort();
		}
	}
	while ((ret = ssl_write(&ctx->ssl, buffer.data, buffer.len)) < buffer.len) {
		if (ret != POLARSSL_ERR_NET_TRY_AGAIN) {
			printf("write failed: %d\n", ret);
			abort();
		}
	}

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
	int ret, i;

	printf("recv msg\n");

	do {
		ret = ssl_read(&ctx->ssl, preamble, 6);
		if (ret == POLARSSL_ERR_NET_CONN_RESET) {
			printf("conn reset\n");
			exit(1);
		}
	} while (ret == POLARSSL_ERR_NET_TRY_AGAIN);

	if (ret <= 0) {
		printf("read failed: %d\n", ret);
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
	ret = ssl_read(&ctx->ssl, data, len);
	if (ret == POLARSSL_ERR_NET_CONN_RESET) {
		printf("conn reset\n");
		exit(1);
	}

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
my_debug(void *ctx, int level, const char *str)
{
	if (level <= 1)
		printf("polarssl [level %d]: %s\n", level, str);
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
	[7] = (callback_t) recv_channel_state,
	[127] = NULL,
};

static gboolean
_recv(GIOChannel *source, GIOCondition condition, gpointer data)
{
	struct context *ctx = data;

	do {
		recv_msg(ctx, callbacks, ARRAY_SIZE(callbacks));
	} while (ssl_get_bytes_avail(&ctx->ssl) > 0);

	do_ping(ctx);

	return TRUE;
}
static gboolean
bus_call(GstBus *bus, GstMessage *msg, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;

	switch (GST_MESSAGE_TYPE (msg)) {

	case GST_MESSAGE_EOS:
		g_print ("End of stream\n");
		//g_main_loop_quit (loop);
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
		g_print("unhandled message: %d\n", GST_MESSAGE_TYPE(msg));
		break;
	}
}

static void
app_need_data(GstAppSrc *src, guint length, gpointer user_data)
{
	struct context *ctx = user_data;
}

static void
app_enough_data(GstAppSrc *src, gpointer user_data)
{
	struct context *ctx = user_data;
}

static GstAppSrcCallbacks app_callbacks = {
	app_need_data,
	app_enough_data,
	NULL
};

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
	int ret;

	memset(&ctx, 0, sizeof(ctx));

	ssl_init(&ctx.ssl);
	havege_init( &ctx.hs );

	ret = net_connect(&ctx.sock, host, port);
	ssl_set_endpoint(&ctx.ssl, SSL_IS_CLIENT);
	ssl_set_authmode(&ctx.ssl, SSL_VERIFY_NONE);

	ssl_set_rng(&ctx.ssl, havege_rand, &ctx.hs);
	ssl_set_dbg(&ctx.ssl, my_debug, NULL);
	ssl_set_bio(&ctx.ssl, net_recv, &ctx.sock, net_send, &ctx.sock);

	//ssl_set_session(&ctx.ssl, 1, 600, &ssn);
	ssl_set_session(&ctx.ssl, 0, 0, &ctx.ssn);
	ssl_set_ciphers(&ctx.ssl, ssl_default_ciphers);

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
		authenticate.username = "ben2";
		authenticate.password = "";
		authenticate.n_celt_versions = 1;
		authenticate.celt_versions = (int32_t[]) { 0x8000000b };
		send_msg(&ctx, &authenticate.base);
	}

	do_ping(&ctx);

	GstElement *pipeline, *src, *decoder, *conv, *sink;

	g_type_init();
	gst_init(&argc, &argv);

	ctx.loop = g_main_loop_new(NULL, FALSE);

	pipeline = gst_pipeline_new("cmumble-output");
	src = gst_element_factory_make("appsrc", "input");
	decoder = gst_element_factory_make("celtdec", "celt-decoder");
	conv = gst_element_factory_make("audioconvert", "converter");
	sink = gst_element_factory_make("autoaudiosink", "audio-output");
	//sink = gst_element_factory_make("filesink", "output");
	g_object_set(G_OBJECT(sink), "location", "foo.raw", NULL);
	//sink = gst_element_factory_make("alsasink", "audio-output");
	

	if (!pipeline || !src || !decoder || !conv || !sink) {
		g_printerr("failed to initialize pipeline\n");
		return 1;
	}

	{
		GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
		gst_bus_add_watch(bus, bus_call, ctx.loop);
		gst_object_unref(bus);
	}

	gst_bin_add_many(GST_BIN(pipeline),
			 src, decoder, conv, sink, NULL);
	gst_element_link_many(src, decoder, conv, sink, NULL);

	ctx.src = GST_APP_SRC(src);
	ctx.pipeline = pipeline;

	/* Important! */
	gst_base_src_set_live(GST_BASE_SRC(ctx.src), TRUE); 
	gst_base_src_set_do_timestamp(GST_BASE_SRC(ctx.src), TRUE);
	gst_base_src_set_format(GST_BASE_SRC(ctx.src), GST_FORMAT_TIME);

	gst_app_src_set_stream_type(ctx.src, GST_APP_STREAM_TYPE_STREAM); 
	gst_app_src_set_callbacks(ctx.src, &app_callbacks, &ctx, NULL);

	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	{ /* Setup Celt Decoder */
#define SAMPLERATE 48000
#define CHANNELS 1
		GstBuffer *gst_buf;
		uint8_t celt_header_packet[sizeof(CELTHeader)];

		ctx.celt_mode = celt_mode_create(SAMPLERATE,
						 SAMPLERATE / 100, NULL);
		celt_header_init(&ctx.celt_header, ctx.celt_mode, CHANNELS);
		celt_header_to_packet(&ctx.celt_header,
				      celt_header_packet, sizeof(CELTHeader));

		gst_buf = gst_app_buffer_new(celt_header_packet,
					     sizeof(CELTHeader), NULL, &ctx);
		gst_app_src_push_buffer(ctx.src, gst_buf);
		/* fake vorbiscomment buffer */
		gst_buf = gst_app_buffer_new(NULL, 0, NULL, &ctx);
		gst_app_src_push_buffer(ctx.src, gst_buf);
	}

	ctx.sock_channel = g_io_channel_unix_new(ctx.sock);
	g_io_add_watch(ctx.sock_channel, G_IO_IN | G_IO_ERR, _recv, &ctx);

	g_main_loop_run(ctx.loop);

	g_main_loop_unref(ctx.loop);

	net_close(ctx.sock);
	ssl_free(&ctx.ssl);
	memset(&ctx.ssl, 0, sizeof(ctx.ssl));

}
