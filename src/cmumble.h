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

	int64_t sequence;

	GList *users;
};

struct cmumble_user {
	uint32_t session;
	char *name;
	uint32_t user_id;

	GstElement *pipeline;
	GstAppSrc *src;
};

enum udp_message_type {
	udp_voice_celt_alpha,
	udp_ping,
	udp_voice_speex,
	udp_voice_celt_beta
};

#endif
