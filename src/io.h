#ifndef _IO_H_
#define _IO_H_

#include <glib.h>
#include <termios.h>

struct cmumble_io {
	GIOChannel *input_channel;

	struct termios term;
};

struct cmumble_context;

int
cmumble_io_init(struct cmumble_context *ctx);

int
cmumble_io_fini(struct cmumble_context *ctx);

#endif /* _IO_H_ */
