1 {
i\
#ifndef _MESSAGES_H_ \
#define _MESSAGES_H_ \
\
#define MUMBLE_MSGS \\
}

# Duplicate & Seperate with ","
s/^.*$/\0, \0/

# Lowercase and prefix uppercase characters that follow a lowercase one
:a; s/^\([^,]*, .*[a-z]\)\([A-Z]\)/\1_\L\2\E/g; ta

# Lowercase remaining uppercase characters
s/\([^,]*, [a-z_]*\)\([A-Z]*\)/\1\L\2\E/g

# Put template macro around
s/^.*$/\tMUMBLE_MSG(\0) \\/

$ {
a\
\
#endif /* _MESSAGES_H_ */
}
