#ifndef _CMUMBLE_H_
#define _CMUMBLE_H_

#include "messages.h"

enum mumble_message {
#define MUMBLE_MSG(a,b,c) a,
	MUMBLE_MSGS
#undef MUMBLE_MSG
};

static const struct {
	const ProtobufCMessageDescriptor *descriptor;
	const char *name;
} messages[] = {
#define MUMBLE_MSG(a,b,c) { &mumble_proto_##b##__descriptor, c },
	MUMBLE_MSGS
#undef MUMBLE_MSG
};

#endif
