#ifndef _MESSAGES_H_
#define _MESSAGES_H_

#include "message_list.h"
#include "mumble.pb-c.h"

enum cmumble_message {
#define MUMBLE_MSG(a,b) a,
	MUMBLE_MSGS
#undef MUMBLE_MSG
};

struct context;

void
cmumble_send_msg(struct context *ctx, ProtobufCMessage *msg);

int
cmumble_recv_msg(struct context *ctx);

#endif /* _MESSAGES_H_ */
