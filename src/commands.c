#include "../config.h"
#include "commands.h"
#include "cmumble.h"

#include <glib.h>
#include <string.h>

#include <readline/readline.h>
#include <readline/history.h>

static void
list_users(struct cmumlbe *cm,
	   int argc, char **argv)
{
	struct cmumble_user *user = NULL;
	GList *l;

	for (l = cm->users; l; l = l->next) {
		user = l->data;

		g_print("%4d: %s\n", user->session, user->name);
	}
}

static void
list_channels(struct cmumlbe *cm,
	      int argc, char **argv)
{
	struct cmumble_channel *channel = NULL;
	GList *l;

	for (l = cm->channels; l; l = l->next) {
		channel = l->data;

		g_print("%4d: %s\n", channel->id, channel->name);
	}
}

static void
quit(struct cmumlbe *cm,
     int argc, char **argv)
{
	rl_already_prompted = 1;
	g_main_loop_quit(cm->loop);
}

static void
clear(struct cmumlbe *cm,
      int argc, char **argv)
{
	rl_clear_screen(0,0);
	rl_reset_line_state();
	g_print("\n");
}

static void
help(struct cmumlbe *cm,
     int argc, char **argv)
{
	int i;

	for (i = 0; cm->commands[i].name; ++i)
		g_print("%s\t%s\n",
			cm->commands[i].name, cm->commands[i].description);
}

static void
msg(struct cmumlbe *cm,
    int argc, char **argv)
{
	MumbleProto__TextMessage message;

	mumble_proto__text_message__init(&message);
	message.actor = cm->session;

	if (argc < 2) {
		g_print("usage: msg message\n");
		return;
	}

	message.message = argv[argc-1];

	/* FIXME: cache this in general somehow? */
	if (!cm->user || !cm->user->channel) {
		g_printerr("No information about current CHannel available\n");
		return;
	}

	message.n_channel_id = 1;
	message.channel_id = (uint32_t[]) { cm->user->channel->id };
	message.n_session = 0;
	message.n_tree_id = 0;

	cmumble_send_msg(cm, &message.base);
}

static const struct cmumble_command commands[] = {
	{ "lu", list_users, "list users" },
	{ "lc", list_channels, "list channels" },
	{ "clear", clear, "clear screen" },
	{ "help", help, "show this help" },
	{ "msg", msg, "send a text message" },
	{ "quit", quit, "quit " PACKAGE },
	{ NULL, NULL , NULL}
};

const char *
cmumble_command_expand_shortcut(const char *text)
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
cmumble_commands_init(struct cmumlbe *cm)
{
	cm->commands = commands;

	rl_completion_entry_function = complete;
}
