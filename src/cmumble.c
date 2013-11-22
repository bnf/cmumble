#include "../config.h"

#include <string.h>
#include <stdlib.h>

#include "varint.h"
#include "cmumble.h"
#include "io.h"
#include "connection.h"
#include "util.h"

static void
recv_udp_tunnel(mumble_udptunnel_t *tunnel, struct cmumble *cm)
{
	int64_t session, sequence;
	uint32_t pos = 0, read = 0;
	uint8_t frame_len, terminator;
	struct cmumble_user *user = NULL;
	uint8_t *data = tunnel->packet.data;
	size_t len = tunnel->packet.len;
	uint8_t type;

	type = data[pos] >> 5;
	pos += 1;
	session  = decode_varint(&data[pos], &read, len-pos);
	pos += read;
	sequence = decode_varint(&data[pos], &read, len-pos);
	pos += read;

	user = find_user(cm, session);
	if (user == NULL) {
		g_printerr("Received audio packet from unknown user, "
			   "dropping.\n");
		return;
	}

	if (type != udp_voice_celt_alpha) {
		g_printerr("Retrieved unimplemented codec %d from: %s\n",
			   type, user->name);
		return;
	}

	do {
		frame_len  = data[pos] & 0x7F;
		terminator = data[pos] & 0x80;
		pos += 1;

		if (frame_len == 0 || frame_len > len-pos)
			break;

		if (frame_len < 2)
			break;

		cmumble_audio_push(cm, user, &data[pos], frame_len);

		pos += frame_len;
		sequence++;
	} while (terminator);
}

static void
recv_version(mumble_version_t *version, struct cmumble *cm)
{
	if (cm->verbose) {
		g_print("version: 0x%x\n", version->version);
		g_print("release: %s\n", version->release);
	}
}

static void
recv_channel_state(mumble_channel_state_t *state, struct cmumble *cm)
{
	struct cmumble_channel *channel;

	channel = find_channel(cm, state->channel_id);
	if (channel == NULL) {
		channel = g_slice_new0(struct cmumble_channel);
		if (channel == NULL) {
			g_printerr("Out of memory.\n");
			exit(1);
		}
		cm->channels = g_list_prepend(cm->channels, channel);

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
recv_server_sync(mumble_server_sync_t *sync, struct cmumble *cm)
{
	cm->session = sync->session;
	cm->user = find_user(cm, cm->session);

	if (sync->welcome_text)
		g_print("Welcome Message: %s\n", sync->welcome_text);
	if (cm->verbose)
		g_print("got session: %d\n", cm->session);
}

static void
recv_crypt_setup(mumble_crypt_setup_t *crypt, struct cmumble *cm)
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
recv_codec_version(mumble_codec_version_t *codec, struct cmumble *cm)
{
	if (cm->verbose)
		g_print("Codec Version: alpha: %d, beta: %d, pefer_alpha: %d\n",
			codec->alpha, codec->beta, codec->prefer_alpha);
}

static void
recv_user_remove(mumble_user_remove_t *remove, struct cmumble *cm)
{
	struct cmumble_user *user = NULL;

	user = find_user(cm, remove->session);
	if (user) {
		cm->users = g_list_remove(cm->users, user);
		g_free(user->name);
		/* FIXME: destroy playback pipeline */
		g_slice_free(struct cmumble_user, user);
	}
}

static void
recv_user_state(mumble_user_state_t *state, struct cmumble *cm)
{
	struct cmumble_user *user = NULL;

	if (!state->has_session) {
		if (cm->verbose)
			g_print("%s: no session", __func__);
		return;
	}

	user = find_user(cm, state->session);
	if (user) {
		/* update */

		if (state->has_channel_id)
			user->channel = find_channel(cm, state->channel_id);

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
	user->channel = find_channel(cm, state->channel_id);

	if (cm->session == user->session)
		cm->user = user;

	/* FIXME: Rather than doing this ugly check by name here,
	 * we should rather create the pipeline ondemand?
	 */
	if (g_strcmp0(user->name, cm->user_name) != 0)
		cmumble_audio_create_playback_pipeline(cm, user);
	if (cm->verbose)
		g_print("receive user: %s\n", user->name);
	cm->users = g_list_prepend(cm->users, user);
}

static void
recv_text_message(mumble_text_message_t *text, struct cmumble *cm)
{
	struct cmumble_user *user;

	user = find_user(cm, text->actor);
	if (user != NULL)
		g_print("%s> %s\n", user->name, text->message);
}

static void
recv_reject(mumble_reject_t *reject, struct cmumble *cm)
{
	switch (reject->type) {
	case MUMBLE_REJECT_TYPE(None):
	case MUMBLE_REJECT_TYPE(WrongVersion):
	case MUMBLE_REJECT_TYPE(InvalidUsername):
	case MUMBLE_REJECT_TYPE(WrongUserPW):
	case MUMBLE_REJECT_TYPE(WrongServerPW):
	case MUMBLE_REJECT_TYPE(UsernameInUse):
	case MUMBLE_REJECT_TYPE(ServerFull):
	case MUMBLE_REJECT_TYPE(NoCertificate):
		g_printerr("Connection rejected: %s\n", reject->reason);
		break;
	default:
		break;
	}
	g_main_loop_quit(cm->loop);
}

static const struct {
#define MUMBLE_MSG(a, b) void (* a)(mumble_##b##_t *, struct cmumble *);
	MUMBLE_MSGS
#undef MUMBLE_MSG
} callbacks = {
	.Version		= recv_version,
	.UDPTunnel		= recv_udp_tunnel,
	.Authenticate		= NULL,
	.Ping			= NULL,
	.Reject			= recv_reject,
	.ServerSync		= recv_server_sync,
	.ChannelRemove		= NULL,
	.ChannelState		= recv_channel_state,
	.UserRemove		= recv_user_remove,
	.UserState		= recv_user_state,
	.BanList		= NULL,
	.TextMessage		= recv_text_message,
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
do_ping(struct cmumble *cm)
{
	mumble_ping_t ping;
	GTimeVal tv;

	cmumble_init_ping(&ping);
	g_get_current_time(&tv);
	ping.timestamp = tv.tv_sec;
	ping.resync = 1;
	cmumble_send_ping(cm, &ping);

	return TRUE;
}

void
cmumble_protocol_init(struct cmumble *cm)
{
	mumble_version_t version;
	mumble_authenticate_t authenticate;
	GSource *source;

	cmumble_init_version(&version);
	version.version = 0x010203;
	version.release = PACKAGE_STRING;
	version.os = "Gentoo/Linux";
	cmumble_send_version(cm, &version);

	cmumble_init_authenticate(&authenticate);
	authenticate.username = cm->user_name;
	authenticate.password = "";
	authenticate.n_celt_versions = 1;
	authenticate.celt_versions = (int32_t[]) { 0x8000000b };
	cmumble_send_authenticate(cm, &authenticate);

	source = g_timeout_source_new_seconds(5);
	g_source_set_callback(source, (GSourceFunc) do_ping, cm, NULL);
	g_source_attach(source, NULL);
	g_source_unref(source);
}

gchar *user = "unkown";
gchar *host = "localhost";
gint port = 64738;
gboolean verbose = FALSE;

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
	{
		"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
		"Turn on verbosity", NULL
	},
  	{ NULL }
};

int main(int argc, char **argv)
{
	struct cmumble cm;
	GError *error = NULL;
	GOptionContext *context;

	context = g_option_context_new("command line mumble client");
	g_option_context_add_main_entries(context, entries, "cmumble");
	g_option_context_add_group(context, gst_init_get_option_group());

	if (!g_option_context_parse(context, &argc, &argv, &error)) {
		g_printerr("option parsing failed: %s\n", error->message);
		exit(1);
	}

	memset(&cm, 0, sizeof(cm));

	cm.user_name = user;
	cm.users = NULL;
	cm.verbose = verbose;

	g_type_init();
	cm.loop = g_main_loop_new(NULL, FALSE);
	cm.callbacks = (const callback_t *) &callbacks;

	cm.async_queue = g_async_queue_new_full(g_free);
	if (cm.async_queue == NULL)
		return 1;

	cmumble_commands_init(&cm);
	if (cmumble_connection_init(&cm, host, port) < 0)
		return 1;

	gst_init(&argc, &argv);

	if (cmumble_audio_init(&cm) < 0)
		return 1;
	cmumble_io_init(&cm);

	g_main_loop_run(cm.loop);

	g_main_loop_unref(cm.loop);

	cmumble_io_fini(&cm);
	cmumble_audio_fini(&cm);
	cmumble_connection_fini(&cm);
	g_async_queue_unref(cm.async_queue);

	return 0;
}
