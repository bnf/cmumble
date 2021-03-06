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

typedef void (*callback_t)(ProtobufCMessage *msg, struct cmumble *);

struct cmumble {
	struct cmumble_connection con;
	struct cmumble_io io;
	struct cmumble_audio audio;
	const callback_t *callbacks;
	const struct cmumble_command *commands;

	GMainLoop *loop;
	GAsyncQueue *async_queue;

	uint32_t session;
	gboolean authenticated;

	const char *user_name;

	int64_t sequence;

	GList *users;
	GList *channels;

	struct cmumble_user *user;

	gboolean verbose;
};

struct cmumble_user {
	uint32_t session;
	char *name;
	uint32_t id;
	struct cmumble_channel *channel;

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

enum udp_message_target {
	udp_normal_talking = 0,
	udp_whisper_to_channel = 1,
	udp_direct_whisper_min = 2,
	udp_direct_whisper_max = 30,
	udp_server_loopback = 31
};

void
cmumble_protocol_init(struct cmumble *cm);

#endif
