1 {
i\
#ifndef _MESSAGES_H_ \
#define _MESSAGES_H_ \
\
#define MUMBLE_MSGS \\
}

# Backup original message name
h
# Prefix uppercase characters that follow a lowercase one
s/\([a-z]\)\([A-Z]\)/\1_\2/g
# Lowercase uppercase characters
y/ABCDEFGHIJKLMNOPQRSTUVWXYZ/abcdefghijklmnopqrstuvwxyz/
# Append backup to lowercase underscored message
G
# Put template macro around (delete newline between both msgs, swap order)
s/^\(.*\)\n\(.*\)$/\tMUMBLE_MSG(\2, \1) \\/

$ {
a\
\
#endif /* _MESSAGES_H_ */
}
