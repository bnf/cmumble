#ifndef _COMMANDS_H_
#define _COMMANDS_H_

struct cmumble_context;

struct cmumble_command {
	const char *name;
	void (*callback)(struct cmumble_context *);
	const char *description;
};

void
cmumble_commands_init(struct cmumble_context *ctx);

#endif /* _COMMANDS_H_ */
