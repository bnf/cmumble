#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <glib.h>

struct cmumble_connection {
	GSocketClient *sock_client;
	GSocketConnection *conn;
	GSocket *sock;

	GPollableInputStream *input;
	GOutputStream *output;

	GSource *source;
};

struct context;

int
cmumble_connection_init(struct context *ctx,
			const char *host, int port);

int
cmumble_connection_fini(struct context *ctx);

#endif /* _CONNECTION_H_ */
