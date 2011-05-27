#!/bin/sh

echo "static const struct { const ProtobufCMessageDescriptor *descriptor; const char *name; } messages[] = {"
while read message
do
	#lower_name=$(echo $message | tr '[:upper:]' '_[:lower:]' | sed "s/^_\(.*\)$/\1/")

	prefixed_lower_name=$(echo "$message" | sed "s/\(^\|[a-z]\)\([A-Z][A-Z]*\)/\1_\L\2/g")
	echo -e "\t/* ${message} */ { &mumble_proto_${prefixed_lower_name}__descriptor, \"$message\" },"
done
echo "};"
