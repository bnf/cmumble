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

struct cmumlbe;

int
cmumble_connection_init(struct cmumlbe *cm,
			const char *host, int port);

int
cmumble_connection_fini(struct cmumlbe *cm);

#endif /* _CONNECTION_H_ */
