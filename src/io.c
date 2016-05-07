#include <stdio.h>
#include <stdlib.h>
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
stdin_callback(GIOChannel *source, GIOCondition condition, gpointer data)
{
	struct cmumble *cm = data;

	if (condition & G_IO_IN) {
		global_rl_user_data = cm;
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
	gboolean preserve = RL_ISSTATE(RL_STATE_TERMPREPPED);
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

static gboolean
is_escaped(char *text, int index, char escape)
{
	if (index <= 0)
		return FALSE;

	if (text[index-1] == escape)
		return !is_escaped(text, index-1, escape);

	return FALSE;
}

static char *
skip_non_whitespace(char *text, char delimiter)
{
	int i;

	for (i = 0; text[i] != '\0'; ++i) {
		if (text[i] == delimiter && !is_escaped(text, i, '\\'))
			return &text[i];
	}

	return &text[i];
}

static char *
skip_delimiter(char *text, char *delimiter)
{
	*delimiter = ' ';

	if (text[0] == '"' || text[0] == '\'') {
		*delimiter = text[0];
		text = &text[1];
	}

	return text;
}

static int
command_split(char *cmd, char ***argv)
{
	static char *av[16];
	int i;
	char delimiter;

	for (i = 0; i < 15; ++i) {
		cmd = skip_whitespace(cmd);
		if (strlen(cmd) == 0)
			break;
		cmd = skip_delimiter(cmd, &delimiter);
		av[i] = cmd;
		cmd = skip_non_whitespace(cmd, delimiter);
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
	struct cmumble *cm = global_rl_user_data;
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
		g_main_loop_quit(cm->loop);
		return;
	}
	line = g_strdup(line);

	if (strlen(line))
		add_history(line);

	argc = command_split(line, &argv);
	if (argc == 0)
		goto out;

	cmd = cmumble_command_expand_shortcut(argv[0]);
	for (i = 0; cm->commands[i].name; ++i) {
		if (strlen(cmd) == strlen(cm->commands[i].name) &&
		    strcmp(cmd, cm->commands[i].name) == 0) {
			cm->commands[i].callback(cm, argc, argv);
			break;
		}
	}

	if (cm->commands[i].name == NULL)
		g_print("Unknown command: %s\n", line);
out:
	g_free(line);
}

int
cmumble_io_init(struct cmumble *cm)
{
	struct termios term;

	cm->io.input_channel = g_io_channel_unix_new(STDIN_FILENO);

	g_io_add_watch(cm->io.input_channel, G_IO_IN | G_IO_HUP,
		       stdin_callback, cm);

	if (tcgetattr(STDIN_FILENO, &term) < 0) {
		g_printerr("tcgetattr failed");
		return -1;
	}

	/* TODO: Maybe add comments why tcsetattr is needed?
	         (as in readline/examples/excallback.c)
	         Rename io.term to io.term_backup? */
	cm->io.term = term;
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
cmumble_io_fini(struct cmumble *cm)
{
	rl_callback_handler_remove();
	g_io_channel_unref(cm->io.input_channel);

	if (tcsetattr(STDIN_FILENO, TCSANOW, &cm->io.term) < 0) {
		g_printerr("tcsetattr failed");
		return -1;
	}

	return 0;
}
