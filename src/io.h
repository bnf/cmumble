#ifndef _IO_H_
#define _IO_H_

#include <glib.h>
#include <termios.h>

struct cmumble_io {
	GIOChannel *input_channel;

	struct termios term;
};

struct context;

int
cmumble_io_init(struct context *ctx);

int
cmumble_io_fini(struct context *ctx);

#endif /* _IO_H_ */
