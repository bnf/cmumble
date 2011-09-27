#include "../config.h"
#include "commands.h"
#include "cmumble.h"

#include <glib.h>
#include <string.h>

#include <readline/readline.h>
#include <readline/history.h>

static void
list_users(struct cmumble_context *ctx)
{
	struct cmumble_user *user = NULL;
	GList *l;

	for (l = ctx->users; l; l = l->next) {
		user = l->data;

		g_print("%4d: %s\n", user->session, user->name);
	}
}

static void
list_channels(struct cmumble_context *ctx)
{
	struct cmumble_channel *channel = NULL;
	GList *l;

	for (l = ctx->channels; l; l = l->next) {
		channel = l->data;

		g_print("%4d: %s\n", channel->id, channel->name);
	}
}

static void
quit(struct cmumble_context *ctx)
{
	rl_already_prompted = 1;
	g_main_loop_quit(ctx->loop);
}

static void
clear(struct cmumble_context *ctx)
{
	rl_clear_screen(0,0);
	rl_reset_line_state();
	g_print("\n");
}

static void
help(struct cmumble_context *ctx)
{
	int i;

	for (i = 0; ctx->commands[i].name; ++i)
		g_print("%s\t%s\n",
			ctx->commands[i].name, ctx->commands[i].description);
}

static const struct cmumble_command commands[] = {
	{ "lu", list_users, "list users" },
	{ "lc", list_channels, "list channels" },
	{ "clear", clear, "clear screen" },
	{ "help", help, "show this help" },
	{ "quit", quit, "quit " PACKAGE },
	{ NULL, NULL , NULL}
};

const char *
cmumble_command_complete(const char *text)
{
	int i = 0;
	int found_index = -1;

	do {
		if (strncmp(commands[i].name, text, strlen(text)) == 0) {
			/* Found at least two matches, so do not complete. */
			if (found_index >= 0)
				return text;
			found_index = i;
		}
	} while (commands[++i].name);

	return found_index >= 0 ? commands[found_index].name : text;
}

static char *
complete(const char *in, int n)
{
	static int index = 0, len;
	const char *name;

	if (n == 0) {
		index = 0;
		len = strlen(in);
	}

	while (commands[index].name) {
		name = commands[index++].name;
		if (strncmp(name, in, len) == 0)
			return g_strdup(name);
	}
	
	return NULL;
}

void
cmumble_commands_init(struct cmumble_context *ctx)
{
	ctx->commands = commands;

	rl_completion_entry_function = complete;
}
