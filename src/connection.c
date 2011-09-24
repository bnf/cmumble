#include <glib.h>
#include <gio/gio.h>

#include "connection.h"
#include "cmumble.h"

static gboolean
read_cb(GObject *pollable_stream, gpointer data)
{
	GPollableInputStream *input = G_POLLABLE_INPUT_STREAM(pollable_stream);
	struct cmumble_context *ctx = data;
	gint count;

	do {
		count = cmumble_recv_msg(ctx);
	} while (count && g_pollable_input_stream_is_readable(input));

	return TRUE;
}

static void
connection_ready(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	struct cmumble_context *ctx = user_data;
	struct cmumble_connection *con = &ctx->con;
	GError *error = NULL;

	con->conn = g_socket_client_connect_to_host_finish (con->sock_client,
							    res, &error);
	if (error) {
		g_printerr("connect failed: %s\n", error->message);
		g_main_loop_quit(ctx->loop);
		g_error_free(error);
		return;
	}

	g_object_get(G_OBJECT(con->conn),
		     "input-stream", &con->input,
		     "output-stream", &con->output, NULL);

	if (!G_IS_POLLABLE_INPUT_STREAM(con->input) ||
	    !g_pollable_input_stream_can_poll(con->input)) {
		g_printerr("Error: GSocketConnection is not pollable\n");
		g_main_loop_quit(ctx->loop);
		return;
	}

	con->source = g_pollable_input_stream_create_source(con->input, NULL);
	g_source_set_callback(con->source, (GSourceFunc) read_cb, ctx, NULL);
	g_source_attach(con->source, NULL);
	g_source_unref(con->source);

	cmumble_protocol_init(ctx);
}

int
cmumble_connection_init(struct cmumble_context *ctx,
			const char *host, int port)
{
	struct cmumble_connection *con = &ctx->con;

	con->sock_client = g_socket_client_new();
	g_socket_client_set_tls(con->sock_client, TRUE);
	g_socket_client_set_tls_validation_flags(con->sock_client,
						 G_TLS_CERTIFICATE_INSECURE);
	g_socket_client_set_family(con->sock_client, G_SOCKET_FAMILY_IPV4);
	g_socket_client_set_protocol(con->sock_client,
				     G_SOCKET_PROTOCOL_TCP);
	g_socket_client_set_socket_type(con->sock_client,
					G_SOCKET_TYPE_STREAM);

	g_socket_client_connect_to_host_async(con->sock_client,
					      host, port, NULL,
					      connection_ready, ctx);

	return 0;
}

int
cmumble_connection_fini(struct cmumble_context *ctx)
{
	g_source_remove(g_source_get_id(ctx->con.source));
	g_source_unref(ctx->con.source);

	g_object_unref(G_OBJECT(ctx->con.input));
	g_object_unref(G_OBJECT(ctx->con.output));

	g_io_stream_close(G_IO_STREAM(ctx->con.conn), NULL, NULL);
	g_object_unref(G_OBJECT(ctx->con.conn));
	g_object_unref(G_OBJECT(ctx->con.sock_client));

	return 0;
}
