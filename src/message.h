#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "message_list.h"
#include "mumble.pb-c.h"

enum cmumble_message {
#define MUMBLE_MSG(a,b) a,
	MUMBLE_MSGS
#undef MUMBLE_MSG
};

struct cmumble_context;

void
cmumble_send_msg(struct cmumble_context *ctx, ProtobufCMessage *msg);

int
cmumble_recv_msg(struct cmumble_context *ctx);

#endif /* _MESSAGE_H_ */
