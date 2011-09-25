#include "message.h"
#include "cmumble.h"

#define PREAMBLE_SIZE 6

GStaticMutex write_mutex = G_STATIC_MUTEX_INIT;

static const struct {
	const ProtobufCMessageDescriptor *descriptor;
	const char *name;
} messages[] = {
#define MUMBLE_MSG(a,b) { &mumble_proto__##b##__descriptor, #a },
	MUMBLE_MSGS
#undef MUMBLE_MSG
};

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

void
cmumble_send_msg(struct cmumble_context *ctx, ProtobufCMessage *msg)
{
	uint8_t pad[128];
	uint8_t preamble[PREAMBLE_SIZE];
	int type = -1;
	int i;
	ProtobufCBufferSimple buffer = PROTOBUF_C_BUFFER_SIMPLE_INIT(pad);

	for (i = 0; i < G_N_ELEMENTS(messages); ++i)
		if (messages[i].descriptor == msg->descriptor)
			type = i;
	assert(type >= 0);

	if (type == UDPTunnel) {
		MumbleProto__UDPTunnel *tunnel = (MumbleProto__UDPTunnel *) msg;
		buffer.data = tunnel->packet.data;
		buffer.len = tunnel->packet.len;
		buffer.must_free_data = 0;
	} else {
		protobuf_c_message_pack_to_buffer(msg, &buffer.base);
	}

	add_preamble(preamble, type, buffer.len);

	g_static_mutex_lock(&write_mutex);
	g_output_stream_write(ctx->con.output, preamble, PREAMBLE_SIZE, NULL, NULL);
	g_output_stream_write(ctx->con.output, buffer.data, buffer.len, NULL, NULL);
	g_static_mutex_unlock(&write_mutex);

	PROTOBUF_C_BUFFER_SIMPLE_CLEAR(&buffer);
}

int
cmumble_recv_msg(struct cmumble_context *ctx)
{
	uint8_t preamble[PREAMBLE_SIZE];
	ProtobufCMessage *msg;
	gchar *data;
	int type, len;
	gssize ret;
	GError *error = NULL;

	g_assert(ctx->callbacks);

	ret = g_pollable_input_stream_read_nonblocking(ctx->con.input,
						       preamble, PREAMBLE_SIZE,
						       NULL, &error);

	if (ret <= 0) {
		if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
			return 0;
		g_printerr("read failed: %ld\n", ret);
		return 0;
	}

	get_preamble(preamble, &type, &len);

	if (!(type >= 0 && type < G_N_ELEMENTS(messages))) {
		printf("unknown message type: %d\n", type);
		return 0;
	}

	if (len <= 0) {
		g_printerr("length 0\n");
		return 0;
	}

	data = g_malloc(len);
	if (data == NULL) {
		g_printerr("out of mem\n");
		g_main_loop_quit (ctx->loop);
	}
	ret = g_input_stream_read(G_INPUT_STREAM(ctx->con.input),
				  data, len, NULL, NULL);

	/* tunneled udp data - not a regular protobuf message
	 * create dummy ProtobufCMessage */
	if (type == UDPTunnel) {
		MumbleProto__UDPTunnel udptunnel;
		mumble_proto__udptunnel__init(&udptunnel);

		udptunnel.packet.len = len;
		udptunnel.packet.data = (uint8_t *) data;

		if (ctx->callbacks[UDPTunnel])
			ctx->callbacks[UDPTunnel](&udptunnel.base, ctx);

		g_free(data);
		return 0;
	}

	msg = protobuf_c_message_unpack(messages[type].descriptor, NULL,
					len, (uint8_t *) data);
	if (msg == NULL) {
		g_printerr("message unpack failure\n");
		return 0;
	}

	g_print("debug: received message: %s type:%d, len:%d\n", messages[type].name, type, len);
	if (ctx->callbacks[type])
		ctx->callbacks[type](msg, ctx);

	protobuf_c_message_free_unpacked(msg, NULL);
	g_free(data);

	return 1;
}