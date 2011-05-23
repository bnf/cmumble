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

#define PCM_SIZE (48000/100 * 1)
	int16_t pcm[PCM_SIZE];
	uint8_t buf[BUFSIZ];
	FILE *f;
	int frame_len, term;
	CELTDecoder *dec_state;
	JitterBuffer *jitter;
	CELTMode *mode;
	static int iseq = 0;

	session  = decode_varint(&data[pos], &read, len-pos);
	pos += read;
	sequence = decode_varint(&data[pos], &read, len-pos);
	pos += read;
	printf("session: %ld, sequence: %ld\n", session, sequence);

	f = fopen("foo", "a+");

	dec_state = celt_decoder_create(ctx->celt_mode,
					ctx->celt_header.nb_channels, NULL);

	jitter = jitter_buffer_init(ctx->celt_header.frame_size);
	jitter_buffer_ctl(jitter, JITTER_BUFFER_SET_MARGIN,
			  &ctx->celt_header.frame_size);

	do {
		frame_len = (data[pos] & 0x7F);
		term = (data[pos] & 0x80) == 0x80;
		printf("_len: %d, term: %d\n", frame_len, term);
		pos += 1;
		
		if (frame_len == 0)
			break;

		JitterBufferPacket packet;
		packet.data = &data[pos];
		packet.len = frame_len;
		packet.timestamp = ctx->celt_header.frame_size * iseq++;
		packet.span = ctx->celt_header.frame_size;
		packet.sequence = 0;

		jitter_buffer_put(jitter, &packet);
		
		packet.data = buf;
		packet.len = BUFSIZ;
		jitter_buffer_tick(jitter);
		jitter_buffer_get(jitter, &packet, ctx->celt_header.frame_size, NULL);

		if (packet.len == 0)
			packet.data = NULL;

		celt_decode(dec_state, packet.data, packet.len, pcm);
		fwrite(pcm, sizeof(int16_t), PCM_SIZE, f);

		pos += frame_len;
		sequence++;
	} while (term);

	fclose(f);
	celt_decoder_destroy(dec_state);
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
		return ;
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

#define SAMPLERATE 48000
#define CHANNELS 1
	ctx.celt_mode = celt_mode_create(SAMPLERATE, SAMPLERATE / 100, NULL);
	celt_header_init(&ctx.celt_header, ctx.celt_mode, CHANNELS);
	uint8_t celt_header_packet[sizeof(CELTHeader)];
	printf("extra headers: %d\n", ctx.celt_header.extra_headers);
	celt_header_to_packet(&ctx.celt_header, celt_header_packet, sizeof(CELTHeader));

	g_type_init();
	ctx.loop = g_main_loop_new(NULL, FALSE);

	ctx.sock_channel = g_io_channel_unix_new(ctx.sock);
	g_io_add_watch(ctx.sock_channel, G_IO_IN | G_IO_ERR, _recv, &ctx);

	g_main_loop_run(ctx.loop);

	g_main_loop_unref(ctx.loop);

	net_close(ctx.sock);
	ssl_free(&ctx.ssl);
	memset(&ctx.ssl, 0, sizeof(ctx.ssl));

}
