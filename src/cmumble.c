#include "../config.h"

#include <string.h>

#include "varint.h"
#include "cmumble.h"

static struct user *
find_user(struct context *ctx, uint32_t session)
{
	struct user *user = NULL, *tmp;
	GList *l;

	for (l = ctx->users; l; l = l->next) {
		tmp = l->data;
		if (tmp->session == session) {
			user = tmp;
			break;
		}
	}

	return user;
}

static void
appsrc_push(GstAppSrc *src, const void *mem, size_t size)
{
	GstBuffer *gstbuf;
	
	gstbuf = gst_app_buffer_new(g_memdup(mem, size), size,
				    g_free, NULL);
	gst_app_src_push_buffer(src, gstbuf);
}


static GstFlowReturn
pull_buffer(GstAppSink *sink, gpointer user_data)
{
	struct context *ctx = user_data;
	GstBuffer *buf;
	uint8_t data[1024];
	uint32_t write = 0;
	uint32_t pos = 0;
	GOutputStream *output = g_io_stream_get_output_stream(ctx->iostream);
	MumbleProto__UDPTunnel tunnel;
	static int seq = 0;

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

	mumble_proto__udptunnel__init(&tunnel);
	tunnel.packet.data = data;
	tunnel.packet.len = pos;
	send_msg(ctx, &tunnel.base);

	return GST_FLOW_OK;
}

static void
recv_udp_tunnel(MumbleProto__UDPTunnel *tunnel, struct context *ctx)
{
	int64_t session;
	int64_t sequence;
	uint32_t pos = 1;
	uint32_t read = 0;
	uint8_t frame_len, terminator;
	struct user *user = NULL;
	uint8_t *data = tunnel->packet.data;
	size_t len = tunnel->packet.len;

	session  = decode_varint(&data[pos], &read, len-pos);
	pos += read;
	sequence = decode_varint(&data[pos], &read, len-pos);
	pos += read;

	user = find_user(ctx, session);
	if (user == NULL) {
		g_printerr("received audio packet from unknown user, dropping.\n");
		return;
	}

	do {
		frame_len  = data[pos] & 0x7F;
		terminator = data[pos] & 0x80;
		pos += 1;
		
		if (frame_len == 0 || frame_len > len-pos)
			break;

		appsrc_push(user->src, &data[pos], frame_len);

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
recv_crypt_setup(MumbleProto__CryptSetup *crypt, struct context *ctx)
{
	int i;

	if (crypt->has_key) {
		printf("key: 0x");
		for (i = 0; i < crypt->key.len; ++i)
			printf("%x", crypt->key.data[i]);
		printf("\n");

	}
	if (crypt->has_client_nonce) {
		printf("client nonce: 0x");
		for (i = 0; i < crypt->client_nonce.len; ++i)
			printf("%x", crypt->client_nonce.data[i]);
		printf("\n");

	}
	if (crypt->has_server_nonce) {
		printf("server nonce: 0x");
		for (i = 0; i < crypt->server_nonce.len; ++i)
			printf("%x", crypt->server_nonce.data[i]);
		printf("\n");

	}
}

static void
recv_codec_version(MumbleProto__CodecVersion *codec, struct context *ctx)
{
	printf("Codec Version: alpha: %d, beta: %d, pefer_alpha: %d\n",
	       codec->alpha, codec->beta, codec->prefer_alpha);
}


static void
recv_user_remove(MumbleProto__UserRemove *remove, struct context *ctx)
{
	struct user *user = NULL;

	if ((user = find_user(ctx, remove->session))) {
		ctx->users = g_list_remove(ctx->users, user);
		g_free(user->name);
		/* FIXME: destroy playback pipeline */
		g_free(user);
	}
}

static int
user_create_playback_pipeline(struct context *ctx, struct user *user);

static void
recv_user_state(MumbleProto__UserState *state, struct context *ctx)
{
	struct user *user = NULL;

	if ((user = find_user(ctx, state->session))) {
		/* update */
		return;
	}

	user = g_new0(struct user, 1);
	if (user == NULL) {
		g_printerr("Out of memory.\n");
		exit(1);
	}

	user->session = state->session;
	user->name = g_strdup(state->name);
	user->user_id = state->user_id;

	user_create_playback_pipeline(ctx, user);
	g_print("receive user: %s\n", user->name);
	ctx->users = g_list_prepend(ctx->users, user);
}


static const callback_t callbacks[] = {
	[Version]		= (callback_t) recv_version,
	[UDPTunnel]		= (callback_t) recv_udp_tunnel,
	[Authenticate]		= (callback_t) NULL,
	[Ping]			= (callback_t) NULL,
	[Reject]		= (callback_t) NULL,
	[ServerSync]		= (callback_t) recv_server_sync,
	[ChannelRemove]		= (callback_t) NULL,
	[ChannelState]		= (callback_t) recv_channel_state,
	[UserRemove]		= (callback_t) recv_user_remove,
	[UserState]		= (callback_t) recv_user_state,
	[BanList]		= (callback_t) NULL,
	[TextMessage]		= (callback_t) NULL,
	[PermissionDenied]	= (callback_t) NULL,
	[ACL]			= (callback_t) NULL,
	[QueryUsers]		= (callback_t) NULL,
	[CryptSetup]		= (callback_t) recv_crypt_setup,
	[ContextActionModify]	= (callback_t) NULL,
	[ContextAction]		= (callback_t) NULL,
	[UserList]		= (callback_t) NULL,
	[VoiceTarget]		= (callback_t) NULL,
	[PermissionQuery]	= (callback_t) NULL,
	[CodecVersion]		= (callback_t) recv_codec_version,
	[UserStats]		= (callback_t) NULL,
	[RequestBlob]		= (callback_t) NULL,
	[ServerConfig]		= (callback_t) NULL,
	[SuggestConfig]		= (callback_t) NULL,
};

static gboolean
do_ping(struct context *ctx)
{
	MumbleProto__Ping ping;
	GTimeVal tv;

	g_get_current_time(&tv);
	mumble_proto__ping__init(&ping);

	ping.timestamp = tv.tv_sec;
	ping.resync = 1;

	send_msg(ctx, &ping.base);

	return TRUE;
}

static gboolean
read_cb(GSocket *socket, GIOCondition condition, gpointer data)
{
	struct context *ctx = data;
	GInputStream *input = g_io_stream_get_input_stream(ctx->iostream);

	do {
		recv_msg(ctx, callbacks, G_N_ELEMENTS(callbacks));
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
		g_print("unhandled message: %d %s\n", GST_MESSAGE_TYPE(msg),
			gst_message_type_get_name(GST_MESSAGE_TYPE(msg)));
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
user_create_playback_pipeline(struct context *ctx, struct user *user)
{
	GstElement *pipeline;
	GError *error = NULL;
	char *desc = "appsrc name=src ! celtdec ! audioconvert ! autoaudiosink";

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
	gst_app_src_set_callbacks(user->src, &app_callbacks, ctx, NULL);

	gst_element_set_state(pipeline, GST_STATE_PLAYING);

	/* Setup Celt Decoder */
	appsrc_push(user->src, ctx->celt_header_packet, sizeof(CELTHeader));
	/* fake vorbiscomment buffer */
	appsrc_push(user->src, NULL, 0);

	return 0;
}

static int
setup_playback_gst_pipeline(struct context *ctx)
{
#define SAMPLERATE 48000
#define CHANNELS 1
	ctx->celt_mode = celt_mode_create(SAMPLERATE,
					  SAMPLERATE / 100, NULL);
	celt_header_init(&ctx->celt_header, ctx->celt_mode, CHANNELS);
	celt_header_to_packet(&ctx->celt_header,
			      ctx->celt_header_packet, sizeof(CELTHeader));

	return 0;
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
	ctx->sink = GST_APP_SINK(sink);
	ctx->record_pipeline = pipeline;

	cutter = gst_bin_get_by_name(GST_BIN(pipeline), "cutter");
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
	char *host = "localhost";
	unsigned int port = 64738;
	struct context ctx;
	GError *error = NULL;
	GSource *source;

	if (argc >= 3)
		host = argv[2];
	if (argc >= 4)
		port = atoi(argv[3]);

	memset(&ctx, 0, sizeof(ctx));

	ctx.users = NULL;

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
	if (error) {
		g_printerr("connect failed: %s\n", error->message);
		return 1;
	}

	g_object_get(G_OBJECT(ctx.conn), "base-io-stream", &ctx.iostream, NULL);

	{
		MumbleProto__Version version;
		mumble_proto__version__init(&version);
		version.version = 0x010203;
		version.release = PACKAGE_STRING;
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
	source = g_socket_create_source(ctx.sock, G_IO_IN | G_IO_ERR, NULL);
	g_source_set_callback(source, (GSourceFunc)read_cb, &ctx, NULL);
	g_source_attach(source, NULL);
	g_source_unref(source);

	source = g_timeout_source_new_seconds(5);
	g_source_set_callback(source, (GSourceFunc)do_ping, &ctx, NULL);
	g_source_attach(source, NULL);
	g_source_unref(source);

	g_main_loop_run(ctx.loop);

	g_main_loop_unref(ctx.loop);

	return 0;
}
