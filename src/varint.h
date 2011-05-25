#ifndef _VARINT_H_
#define _VARINT_H_

#include <stdint.h>

void
encode_varint(uint8_t *data, uint32_t *write, int64_t value, uint32_t left);

int64_t
decode_varint(uint8_t *data, uint32_t *read, uint32_t left);

#endif
