#ifndef _IO_H_
#define _IO_H_

#include <glib.h>
#include <termios.h>

struct cmumble_io {
	GIOChannel *input_channel;

	struct termios term;
};

struct cmumlbe;

int
cmumble_io_init(struct cmumlbe *cm);

int
cmumble_io_fini(struct cmumlbe *cm);

#endif /* _IO_H_ */
