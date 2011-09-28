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
	struct cmumble_context *ctx = data;

	if (condition & G_IO_IN) {
		global_rl_user_data = ctx;
		rl_callback_read_char();
		global_rl_user_data = NULL;
	}

	return TRUE;
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

static char *
skip_whitespace(char *text)
{
	int i;
	
	for (i = 0; text[i] != '\0'; ++i) {
		if (text[i] != ' ')
			return &text[i];
	}

	return &text[i];
}

static char *
skip_non_whitespace(char *text)
{
	int i;
	
	for (i = 0; text[i] != '\0'; ++i) {
		if (text[i] == ' ')
			return &text[i];
	}

	return &text[i];
}

static int
command_split(char *cmd, char ***argv)
{
	static char *av[16];
	int i;

	for (i = 0; i < 15; ++i) {
		cmd = skip_whitespace(cmd);
		av[i] = cmd;
		cmd = skip_non_whitespace(cmd);
		if (cmd[0] == '\0') {
			++i;
			break;
		} else {
			cmd[0] = '\0';
			cmd++;
		}
	}
	av[i] = NULL;
	*argv = av;

	return i;
}

static void
process_line(char *line)
{
	struct cmumble_context *ctx = global_rl_user_data;
	int i;
	int argc;
	char **argv;
	const char *cmd;

	g_assert(global_rl_user_data);


	rl_reset_line_state();

	if (line == NULL) {
		printf("quit");
		rl_already_prompted = 1;
		rl_crlf();
		g_main_loop_quit(ctx->loop);
		return;
	}
	line = g_strdup(line);

	if (strlen(line))
		add_history(line);

	argc = command_split(line, &argv);
	if (argc == 0)
		goto out;

	cmd = cmumble_command_expand_shortcut(argv[0]);
	for (i = 0; ctx->commands[i].name; ++i) {
		if (strlen(cmd) == strlen(ctx->commands[i].name) &&
		    strcmp(cmd, ctx->commands[i].name) == 0) {
			ctx->commands[i].callback(ctx, argc, argv);
			break;
		}
	}

	if (ctx->commands[i].name == NULL)
		g_print("Unknown command: %s\n", line);
out:
	free(line);
}

int
cmumble_io_init(struct cmumble_context *ctx)
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

	return 0;
}

int
cmumble_io_fini(struct cmumble_context *ctx)
{
	rl_callback_handler_remove();
	g_io_channel_unref(ctx->io.input_channel);

	if (tcsetattr(STDIN_FILENO, TCSANOW, &ctx->io.term) < 0) {
		g_printerr("tcsetattr failed");
		return -1;
	}

	return 0;
}
