#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <unistd.h>
#include <termios.h>

#include "io.h"
#include "cmumble.h"

/* Readline has no user_data in callbacks,
 * so set global_rl_user_data before dispatching
 * (and unset afterwards - so we can assert user_data,
 *  and find where callbacks are called, that we didnt
 *  expect)
 */
static gpointer global_rl_user_data = NULL;

static gboolean
read_cb(GIOChannel *source, GIOCondition condition, gpointer data)
{
	struct context *ctx = data;

	if (condition & G_IO_IN) {
		global_rl_user_data = ctx;
		rl_callback_read_char();
		global_rl_user_data = NULL;
	}
}

static void
print_preserve_prompt(const gchar *string)
{
	int point;
	char *line;

	/* FIXME */
	gboolean preserve = (rl_readline_state & RL_STATE_TERMPREPPED);
	//preserve = 1;

	if (preserve) {
		point = rl_point;
		line = rl_copy_text(0, rl_end);
		rl_save_prompt();
		rl_replace_line("", 0);
		rl_redisplay();
	}

	fputs(string, stdout);

	if (preserve) {
		rl_restore_prompt();
		rl_replace_line(line, 0);
		rl_point = point;
		rl_redisplay();
		free(line);
	}
}

static void
process_line(char *line)
{
	struct context *ctx = global_rl_user_data;

	g_assert(global_rl_user_data);
	
	rl_reset_line_state();

	if (line == NULL) {
		printf("quit");
		rl_already_prompted = 1;
		rl_crlf();
		g_main_loop_quit(ctx->loop);
		return;
	}

	if (strcmp(line, "quit") == 0) {
		rl_already_prompted = 1;
		g_main_loop_quit(ctx->loop);
	} else if (strcmp(line, "clear") == 0) {
		rl_clear_screen(0,0);
		rl_reset_line_state();
	} else
		g_print("readline: %s\n", line);

	if (strlen(line))
		add_history(line);
}

int
cmumble_io_init(struct context *ctx)
{
	struct termios term;
	
	ctx->io.input_channel = g_io_channel_unix_new(STDIN_FILENO);
	
	g_io_add_watch(ctx->io.input_channel, G_IO_IN | G_IO_HUP,
		       read_cb, ctx);

	if (tcgetattr(STDIN_FILENO, &term) < 0) {
		g_printerr("tcgetattr failed");
		return -1;
	}

	ctx->io.term = term;
	term.c_lflag &= ~ICANON;
	term.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &term) < 0) {
		g_printerr("tcsetattr failed");
		return -1;
	}

	rl_callback_handler_install("cmumble> ", process_line);

	g_set_print_handler(print_preserve_prompt);
}

int
cmumble_io_fini(struct context *ctx)
{
	rl_callback_handler_remove();
	g_io_channel_unref(ctx->io.input_channel);

	if (tcsetattr(STDIN_FILENO, TCSANOW, &ctx->io.term) < 0) {
		g_printerr("tcsetattr failed");
		return -1;
	}

}
