#ifndef _COMMANDS_H_
#define _COMMANDS_H_

struct context;

struct cmumble_command {
	const char *name;
	void (*callback)(struct context *);
	const char *description;
};

void
cmumble_commands_init(struct context *ctx);

#endif /* _COMMANDS_H_ */
