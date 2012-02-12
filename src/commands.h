#ifndef _COMMANDS_H_
#define _COMMANDS_H_

struct cmumble;

struct cmumble_command {
	const char *name;
	void (*callback)(struct cmumble *,
			 int argc, char **argv);
	const char *description;
};

void
cmumble_commands_init(struct cmumble *cm);

const char *
cmumble_command_expand_shortcut(const char *text);

#endif /* _COMMANDS_H_ */
