1 {
i\
#ifndef _MESSAGE_LIST_H_ \
#define _MESSAGE_LIST_H_ \
\
#define MUMBLE_MSGS \\
}

$ {
a\
\
#endif /* _MESSAGES_H_ */
}

# Isolate message name from .proto file
/^message/!d
s/message[ 	]*\([^ 	]*\).*$/\1/

# This code attempts to generate "MUMBLE_MSG(FooBar, foo_bar)" from "FooBar"

# Backup original message name
h
# Prefix uppercase characters that follow a lowercase one
s/\([a-z]\)\([A-Z]\)/\1_\2/g
# Lowercase uppercase characters
y/ABCDEFGHIJKLMNOPQRSTUVWXYZ/abcdefghijklmnopqrstuvwxyz/
# Append backup to lowercase underscored message
G
# Put template macro around (delete newline between both msgs, swap order)
s/^\(.*\)\n\(.*\)$/	MUMBLE_MSG(\2, \1) \\/
