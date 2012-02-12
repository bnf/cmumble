#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "message_list.h"
#include "mumble.pb-c.h"

enum cmumble_message {
#define MUMBLE_MSG(a,b) a,
	MUMBLE_MSGS
#undef MUMBLE_MSG
};

struct cmumble;

void
cmumble_send_msg(struct cmumble *cm, ProtobufCMessage *msg);

int
cmumble_recv_msg(struct cmumble *cm);

#endif /* _MESSAGE_H_ */
