#ifndef _UTIL_H_
#define _UTIL_H_

#include <glib.h>
#include "cmumble.h"

gpointer
cmumble_find_by_id(GList *list, gsize member_offset, guint id);

static inline struct cmumble_user *
find_user(struct cmumble *cm, uint32_t session_id)
{
	return cmumble_find_by_id(cm->users,
				  G_STRUCT_OFFSET(struct cmumble_user, session),
				  session_id);
}

static inline struct cmumble_channel *
find_channel(struct cmumble *cm, guint channel_id)
{
	return cmumble_find_by_id(cm->channels,
				  G_STRUCT_OFFSET(struct cmumble_channel, id),
				  channel_id);
}

#endif /* _UTIL_H_ */
