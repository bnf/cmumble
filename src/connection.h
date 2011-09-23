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

struct cmumble_context;

int
cmumble_connection_init(struct cmumble_context *ctx,
			const char *host, int port);

int
cmumble_connection_fini(struct cmumble_context *ctx);

#endif /* _CONNECTION_H_ */
