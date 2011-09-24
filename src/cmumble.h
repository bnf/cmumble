#ifndef _CMUMBLE_H_
#define _CMUMBLE_H_

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "mumble.pb-c.h"
#include "message.h"
#include "io.h"
#include "connection.h"
#include "audio.h"
#include "commands.h"

typedef void (*callback_t)(ProtobufCMessage *msg, struct cmumble_context *);

struct cmumble_context {
	struct cmumble_connection con;
	struct cmumble_io io;
	struct cmumble_audio audio;
	const callback_t *callbacks;
	const struct cmumble_command *commands;

	GMainLoop *loop;

	uint32_t session;
	gboolean authenticated;

	char *user_name;

	int64_t sequence;

	GList *users;
	GList *channels;
};

struct cmumble_user {
	uint32_t session;
	char *name;
	uint32_t id;

	GstElement *pipeline;
	GstAppSrc *src;
};

struct cmumble_channel {
	uint32_t id;
	uint32_t parent;
	char *name;
	char *description;
	
	gboolean temporary;
	int32_t position;
};

enum udp_message_type {
	udp_voice_celt_alpha,
	udp_ping,
	udp_voice_speex,
	udp_voice_celt_beta
};

void
cmumble_protocol_init(struct cmumble_context *ctx);

#endif
