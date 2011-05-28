#!/bin/sh
echo "#ifndef _MESSAGES_H_"
echo "#define _MESSAGES_H_"
echo "#define MUMBLE_MSGS \\"
while read message
do
	prefixed_lower_name=$(echo "$message" | sed "s/\(^\|[a-z]\)\([A-Z][A-Z]*\)/\1_\L\2/g")
	echo -e "\tMUMBLE_MSG(${message}, ${prefixed_lower_name}, \"${message}\") \\"
done
echo
echo "#endif"
