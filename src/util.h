#ifndef _UTIL_H_
#define _UTIL_H_

#include <glib.h>
#include "cmumble.h"

gpointer
cmumble_find_by_id(GList *list, gsize member_offset, guint id);

static inline struct cmumble_user *
find_user(struct cmumble_context *ctx, uint32_t session)
{
	return cmumble_find_by_id(ctx->users,
				  G_STRUCT_OFFSET(struct cmumble_user, id),
				  session);
}

static inline struct cmumble_channel *
find_channel(struct cmumble_context *ctx, guint channel_id)
{
	return cmumble_find_by_id(ctx->channels,
				  G_STRUCT_OFFSET(struct cmumble_channel, id),
				  channel_id);
}

#endif /* _UTIL_H_ */
