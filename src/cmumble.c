#include "../config.h"

#include <string.h>

#include "varint.h"
#include "cmumble.h"
#include "io.h"
#include "connection.h"

static struct user *
find_user(struct context *ctx, uint32_t session)
{
	struct user *user = NULL;
	GList *l;

	for (l = ctx->users; l; l = l->next)
		if (((struct user *) l->data)->session == session) {
			user = l->data;
			break;
		}

	return user;
}

static void
appsrc_push(GstAppSrc *src, const void *mem, size_t size)
{
	GstBuffer *gstbuf;
	
	gstbuf = gst_app_buffer_new(g_memdup(mem, size), size, g_free, NULL);
	gst_app_src_push_buffer(src, gstbuf);
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

	buf = gst_app_sink_pull_buffer(ctx->sink);

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
	send_msg(ctx, &tunnel.base);

	return GST_FLOW_OK;
}

static void
recv_udp_tunnel(MumbleProto__UDPTunnel *tunnel, struct context *ctx)
{
	int64_t session, sequence;
	uint32_t pos = 1, read = 0;
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
	g_print("version: 0x%x\n", version->version);
	g_print("release: %s\n", version->release);
}

static void
recv_channel_state(MumbleProto__ChannelState *state, struct context *ctx)
{
	g_print("channel: id: %u, parent: %u, name: %s, description: %s, temporary: %d, position: %d\n",
		state->channel_id, state->parent, state->name, state->description, state->temporary, state->position);
}

static void
recv_server_sync(MumbleProto__ServerSync *sync, struct context *ctx)
{
	ctx->session = sync->session;

	g_print("got session: %d\n", ctx->session);
}

static void
recv_crypt_setup(MumbleProto__CryptSetup *crypt, struct context *ctx)
{
#if 0
	int i;

	if (crypt->has_key) {
		g_print("key: 0x");
		for (i = 0; i < crypt->key.len; ++i)
			g_print("%x", crypt->key.data[i]);
		g_print("\n");
	}
	if (crypt->has_client_nonce) {
		g_print("client nonce: 0x");
		for (i = 0; i < crypt->client_nonce.len; ++i)
			g_print("%x", crypt->client_nonce.data[i]);
		g_print("\n");
	}
	if (crypt->has_server_nonce) {
		g_print("server nonce: 0x");
		for (i = 0; i < crypt->server_nonce.len; ++i)
			g_print("%x", crypt->server_nonce.data[i]);
		g_print("\n");
	}
#endif
}

static void
recv_codec_version(MumbleProto__CodecVersion *codec, struct context *ctx)
{
	g_print("Codec Version: alpha: %d, beta: %d, pefer_alpha: %d\n",
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
		g_slice_free(struct user, user);
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

	user = g_slice_new0(struct user);
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


static const struct {
#define MUMBLE_MSG(a,b) void (* a)(MumbleProto__##a *, struct context *);
	MUMBLE_MSGS
#undef MUMBLE_MSG
} callbacks = {
	.Version		= recv_version,
	.UDPTunnel		= recv_udp_tunnel,
	.Authenticate		= NULL,
	.Ping			= NULL,
	.Reject			= NULL,
	.ServerSync		= recv_server_sync,
	.ChannelRemove		= NULL,
	.ChannelState		= recv_channel_state,
	.UserRemove		= recv_user_remove,
	.UserState		= recv_user_state,
	.BanList		= NULL,
	.TextMessage		= NULL,
	.PermissionDenied	= NULL,
	.ACL			= NULL,
	.QueryUsers		= NULL,
	.CryptSetup		= recv_crypt_setup,
	.ContextActionModify	= NULL,
	.ContextAction		= NULL,
	.UserList		= NULL,
	.VoiceTarget		= NULL,
	.PermissionQuery	= NULL,
	.CodecVersion		= recv_codec_version,
	.UserStats		= NULL,
	.RequestBlob		= NULL,
	.ServerConfig		= NULL,
	.SuggestConfig		= NULL,
};

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

static int
user_create_playback_pipeline(struct context *ctx, struct user *user)
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

	if (argc >= 3)
		host = argv[2];
	if (argc >= 4)
		port = atoi(argv[3]);

	memset(&ctx, 0, sizeof(ctx));

	ctx.users = NULL;

	g_type_init();
	ctx.loop = g_main_loop_new(NULL, FALSE);
	ctx.callbacks = (const callback_t *) &callbacks;

	if (cmumble_connection_init(&ctx, host, port) < 0)
		return 1;

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

	if (setup_playback_gst_pipeline(&ctx) < 0)
		return 1;

	if (setup_recording_gst_pipeline(&ctx) < 0)
		return 1;

	cmumble_io_init(&ctx);

	g_main_loop_run(ctx.loop);

	g_main_loop_unref(ctx.loop);

	cmumble_io_fini(&ctx);
	cmumble_connection_fini(&ctx);

	return 0;
}
