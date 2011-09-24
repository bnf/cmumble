#include "util.h"
#include <glib.h>

gpointer
cmumble_find_by_id(GList *list, gsize member_offset, guint id)
{
	gpointer el = NULL;
	GList *l;

	for (l = list; l; l = l->next) {
		if (G_STRUCT_MEMBER(uint32_t, l, member_offset) == id) {
			el = l->data;
			break;
		}
	}

	return el;
}
