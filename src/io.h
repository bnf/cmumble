#ifndef _IO_H_
#define _IO_H_

#include <glib.h>
#include <termios.h>

struct cmumble_io {
	GIOChannel *input_channel;

	struct termios term;
};

struct cmumble;

int
cmumble_io_init(struct cmumble *cm);

int
cmumble_io_fini(struct cmumble *cm);

#endif /* _IO_H_ */
