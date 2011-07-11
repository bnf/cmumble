#!/bin/sh
echo "#ifndef _MESSAGES_H_"
echo "#define _MESSAGES_H_"
echo "#define MUMBLE_MSGS \\"
while read message
do
	underscore_name=$(echo "$message" | sed -e "s/\([a-z]\)\([A-Z]\)/\1_\L\2/g" -e "s/[A-Z]*/\L\0/g")
	echo -e "\tMUMBLE_MSG(${message}, ${underscore_name}) \\"
done
echo
echo "#endif"
