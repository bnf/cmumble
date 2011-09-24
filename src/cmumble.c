#include "../config.h"

#include <string.h>

#include "varint.h"
#include "cmumble.h"
#include "io.h"
#include "connection.h"
#include "util.h"

static void
recv_udp_tunnel(MumbleProto__UDPTunnel *tunnel, struct cmumble_context *ctx)
{
	int64_t session, sequence;
	uint32_t pos = 1, read = 0;
	uint8_t frame_len, terminator;
	struct cmumble_user *user = NULL;
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

		cmumble_audio_push(ctx, user, &data[pos], frame_len);

		pos += frame_len;
		sequence++;
	} while (terminator);
}

static void
recv_version(MumbleProto__Version *version, struct cmumble_context *ctx)
{
	g_print("version: 0x%x\n", version->version);
	g_print("release: %s\n", version->release);
}

static void
recv_channel_state(MumbleProto__ChannelState *state, struct cmumble_context *ctx)
{
	struct cmumble_channel *channel;

	channel = find_channel(ctx, state->channel_id);
	if (channel == NULL) {
		channel = g_slice_new0(struct cmumble_channel);
		if (channel == NULL) {
			g_printerr("Out of memory.\n");
			exit(1);
		}
		ctx->channels = g_list_prepend(ctx->channels, channel);

		if (channel->name)
			g_free(channel->name);
		if (channel->description)
			g_free(channel->description);
	}

	channel->id = state->channel_id;
	if (state->name)
		channel->name = g_strdup(state->name);
	channel->parent = state->parent;
	if (state->description)
		channel->description = g_strdup(state->description);
	
	channel->temporary = state->temporary;
	channel->position = state->position;
}

static void
recv_server_sync(MumbleProto__ServerSync *sync, struct cmumble_context *ctx)
{
	ctx->session = sync->session;

	g_print("got session: %d\n", ctx->session);
}

static void
recv_crypt_setup(MumbleProto__CryptSetup *crypt, struct cmumble_context *ctx)
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
recv_codec_version(MumbleProto__CodecVersion *codec, struct cmumble_context *ctx)
{
	g_print("Codec Version: alpha: %d, beta: %d, pefer_alpha: %d\n",
		codec->alpha, codec->beta, codec->prefer_alpha);
}


static void
recv_user_remove(MumbleProto__UserRemove *remove, struct cmumble_context *ctx)
{
	struct cmumble_user *user = NULL;

	if ((user = find_user(ctx, remove->session))) {
		ctx->users = g_list_remove(ctx->users, user);
		g_free(user->name);
		/* FIXME: destroy playback pipeline */
		g_slice_free(struct cmumble_user, user);
	}
}

static void
recv_user_state(MumbleProto__UserState *state, struct cmumble_context *ctx)
{
	struct cmumble_user *user = NULL;

	if ((user = find_user(ctx, state->session))) {
		/* update */
		return;
	}

	user = g_slice_new0(struct cmumble_user);
	if (user == NULL) {
		g_printerr("Out of memory.\n");
		exit(1);
	}

	user->session = state->session;
	user->name = g_strdup(state->name);
	user->id = state->user_id;


	cmumble_audio_create_playback_pipeline(ctx, user);
	g_print("receive user: %s\n", user->name);
	ctx->users = g_list_prepend(ctx->users, user);
}


static const struct {
#define MUMBLE_MSG(a,b) void (* a)(MumbleProto__##a *, struct cmumble_context *);
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

static gboolean
do_ping(struct cmumble_context *ctx)
{
	MumbleProto__Ping ping;
	GTimeVal tv;

	g_get_current_time(&tv);
	mumble_proto__ping__init(&ping);
	ping.timestamp = tv.tv_sec;
	ping.resync = 1;
	cmumble_send_msg(ctx, &ping.base);

	return TRUE;
}

void
cmumble_protocol_init(struct cmumble_context *ctx)
{
	MumbleProto__Version version;
	MumbleProto__Authenticate authenticate;
	GSource *source;

	mumble_proto__version__init(&version);
	version.version = 0x010203;
	version.release = PACKAGE_STRING;
	version.os = "Gentoo/Linux";
	cmumble_send_msg(ctx, &version.base);

	mumble_proto__authenticate__init(&authenticate);
	authenticate.username = ctx->user_name;
	authenticate.password = "";
	authenticate.n_celt_versions = 1;
	authenticate.celt_versions = (int32_t[]) { 0x8000000b };
	cmumble_send_msg(ctx, &authenticate.base);

	source = g_timeout_source_new_seconds(5);
	g_source_set_callback(source, (GSourceFunc) do_ping, ctx, NULL);
	g_source_attach(source, NULL);
	g_source_unref(source);
}

gchar *user = "unkown";
gchar *host = "localhost";
gint port = 64738;

static GOptionEntry entries[] = {
	{
		"user", 'u', 0, G_OPTION_ARG_STRING, &user,
		"user name", "N"
	},
	{
		"host", 'h', 0, G_OPTION_ARG_STRING, &host,
		"Host name or ip address of the mumble server", "N"
	},
	{
		"port", 'p', 0, G_OPTION_ARG_INT, &port,
		"port of the mumble server", "N"
	},
  	{ NULL }
};

int main(int argc, char **argv)
{
	struct cmumble_context ctx;
	GError *error = NULL;
	GOptionContext *context;

	context = g_option_context_new ("command line mumble client");
	g_option_context_add_main_entries (context, entries, "cmumble");

	if (!g_option_context_parse (context, &argc, &argv, &error)) {
		g_print("option parsing failed: %s\n", error->message);
		exit(1);
	}

	memset(&ctx, 0, sizeof(ctx));

	ctx.user_name = user;
	ctx.users = NULL;

	g_type_init();
	ctx.loop = g_main_loop_new(NULL, FALSE);
	ctx.callbacks = (const callback_t *) &callbacks;

	cmumble_commands_init(&ctx);
	if (cmumble_connection_init(&ctx, host, port) < 0)
		return 1;

	gst_init(&argc, &argv);

	if (cmumble_audio_init(&ctx) < 0)
		return 1;
	cmumble_io_init(&ctx);

	g_main_loop_run(ctx.loop);

	g_main_loop_unref(ctx.loop);

	cmumble_io_fini(&ctx);
	cmumble_audio_init(&ctx);
	cmumble_connection_fini(&ctx);

	return 0;
}
