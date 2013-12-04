#include "util.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>

gpointer
cmumble_find_by_id(GList *list, gsize member_offset, guint id)
{
	gpointer el = NULL;
	GList *l;

	for (l = list; l; l = l->next) {
		if (G_STRUCT_MEMBER(uint32_t, l->data, member_offset) == id) {
			el = l->data;
			break;
		}
	}

	return el;
}

gchar *
cmumble_get_os_name(void)
{
	FILE *f;
	char *line = NULL;
	char *os = NULL, *value, *end;
	size_t key_len = strlen("PRETTY_NAME=");
	size_t n;

	f = fopen("/etc/os-release", "r");
	if (f == NULL)
		return NULL;

	while (getline(&line, &n, f) != -1) {
		if (strncmp("PRETTY_NAME=", line, key_len) == 0 && (n - key_len) > 1) {
			value = &line[key_len];
			if (strlen(value) == 0)
				continue;

			end = &value[strlen(value) - 1];
			if (*end == '\n')
				*end-- = '\0';

			if (strlen(value) < 2)
				continue;

			if (value[0] == '"')
				value++;

			if (*end == '"')
				*end = '\0';

			os = strdup(value);
			goto out;
		}
	}

out:
	free(line);
	fclose(f);
	return os;
}
