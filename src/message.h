#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "message_list.h"
#include "mumble.pb-c.h"

enum cmumble_message {
#define MUMBLE_MSG(a,b) CMUMBLE_MESSAGE_##a,
	MUMBLE_MSGS
#undef MUMBLE_MSG
	CMUMBLE_MESSAGE_COUNT
};

/* Makro to hide ugly protobuf-c constat names. */
#define MUMBLE_REJECT_TYPE(type) MUMBLE_PROTO__REJECT__REJECT_TYPE__##type


struct cmumble;

void
cmumble_send_msg(struct cmumble *cm, ProtobufCMessage *msg,
		 enum cmumble_message type);

int
cmumble_recv_msg(struct cmumble *cm);


#define MUMBLE_MSG(cname, name) \
	typedef MumbleProto__##cname mumble_##name##_t; \
	\
	static inline void \
	cmumble_init_##name(mumble_##name##_t *msg) \
	{ \
		mumble_proto__##name##__init(msg); \
	} \
	\
	static inline void \
	cmumble_send_##name(struct cmumble *cm, mumble_##name##_t *msg) \
	{ \
		cmumble_send_msg(cm, &msg->base, CMUMBLE_MESSAGE_##cname); \
	}
MUMBLE_MSGS
#undef MUMBLE_MSG

#endif /* _MESSAGE_H_ */
