#ifndef _CMUMBLE_H_
#define _CMUMBLE_H_

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappbuffer.h>

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <celt/celt.h>
#include <celt/celt_header.h>

#include "mumble.pb-c.h"
#include "messages.h"
#include "io.h"
#include "connection.h"

typedef void (*callback_t)(ProtobufCMessage *msg, struct context *);

struct context {
	struct cmumble_connection con;
	struct cmumble_io io;
	const callback_t *callbacks;
	GMainLoop *loop;

	uint32_t session;
	gboolean authenticated;

	uint8_t celt_header_packet[sizeof(CELTHeader)];
	CELTHeader celt_header;
	CELTMode *celt_mode;

	GstElement *record_pipeline;
	GstAppSink *sink;

	int64_t sequence;

	GList *users;
};

struct user {
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

enum mumble_message {
#define MUMBLE_MSG(a,b) a,
	MUMBLE_MSGS
#undef MUMBLE_MSG
};

void
send_msg(struct context *ctx, ProtobufCMessage *msg);

int
recv_msg(struct context *ctx);

#endif
