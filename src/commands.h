#ifndef _COMMANDS_H_
#define _COMMANDS_H_

struct cmumlbe;

struct cmumble_command {
	const char *name;
	void (*callback)(struct cmumlbe *,
			 int argc, char **argv);
	const char *description;
};

void
cmumble_commands_init(struct cmumlbe *cm);

const char *
cmumble_command_expand_shortcut(const char *text);

#endif /* _COMMANDS_H_ */
