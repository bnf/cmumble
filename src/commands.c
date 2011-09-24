#include "../config.h"
#include "commands.h"
#include "cmumble.h"

#include <glib.h>

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

void
cmumble_commands_init(struct cmumble_context *ctx)
{
	ctx->commands = commands;
}
