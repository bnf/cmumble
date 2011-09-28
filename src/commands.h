#ifndef _COMMANDS_H_
#define _COMMANDS_H_

struct cmumble_context;

struct cmumble_command {
	const char *name;
	void (*callback)(struct cmumble_context *,
			 int argc, const char *argv);
	const char *description;
};

void
cmumble_commands_init(struct cmumble_context *ctx);

const char *
cmumble_command_expand_shortcut(const char *text);

#endif /* _COMMANDS_H_ */
