#ifndef _MESSAGE_H_
#define _MESSAGE_H_

#include "message_list.h"
#include "mumble.pb-c.h"

enum cmumble_message {
#define MUMBLE_MSG(a,b) a,
	MUMBLE_MSGS
#undef MUMBLE_MSG
};

#define PREAMBLE_SIZE 6
struct mumble_msg_base {
	uint8_t preamble[PREAMBLE_SIZE];
};

struct __attribute__ ((__packed__)) mumble_message {
	struct mumble_msg_base base;
	ProtobufCMessage msg;
};

#define MUMBLE_MSG(cname, name) \
	struct __attribute__ ((__packed__)) mumble_##name { \
		struct mumble_msg_base base; \
		MumbleProto__##cname m; \
	};
MUMBLE_MSGS
#undef MUMBLE_MSG

/* Makro to hide ugly protobuf-c constat names. */
#define MUMBLE_REJECT_TYPE(type) MUMBLE_PROTO__REJECT__REJECT_TYPE__##type


struct cmumble;

void
cmumble_send_msg(struct cmumble *cm, ProtobufCMessage *msg);

int
cmumble_recv_msg(struct cmumble *cm);


#define MUMBLE_MSG(cname, name) \
	static inline void \
	cmumble_init_##name(struct mumble_##name *msg) \
	{ \
		mumble_proto__##name##__init(&msg->m); \
	} \
	\
	static inline void \
	cmumble_send_##name(struct cmumble *cm, struct mumble_##name *msg) \
	{ \
		cmumble_send_msg(cm, &msg->m.base); \
	}
MUMBLE_MSGS
#undef MUMBLE_MSG

#endif /* _MESSAGE_H_ */
