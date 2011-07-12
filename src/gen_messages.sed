1 {
i\
#ifndef _MESSAGES_H_ \
#define _MESSAGES_H_ \
\
#define MUMBLE_MSGS \\
}

# Duplicate & Seperate with ","
s/^.*$/\0, \0/

# Next two rules operate on substring after first the ","
# Prefix uppercase characters that follow a lowercase one
:a; s/\(, .*[a-z]\)\([A-Z]\)/\1_\2/g; ta
# Lowercase uppercase characters
s/,.*$/\L\0\E/

# Put template macro around
s/^.*$/\tMUMBLE_MSG(\0) \\/

$ {
a\
\
#endif /* _MESSAGES_H_ */
}
