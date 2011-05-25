#include <stdint.h>

void
encode_varint(uint8_t *data, uint32_t *write, int64_t value, uint32_t left)
{
	uint32_t pos = 0;

	if (value < 0) {
		*write = 0;
		return;
	}

	if (value < 0x80) {
		data[pos++] = value & 0xFF;
	} else if (value < 0x4000) {
		data[pos++] = 0x80 | ((value & 0xFF00 ) >> 8);
		data[pos++] = value & 0xFF;
	} else if (value < 0x200000) {
		data[pos++] = 0xC0 | ((value & 0xFF0000) >> 16);
		data[pos++] = ((value & 0xFF00) >> 8) & 0xFF;
		data[pos++] = value & 0xFF;
	} else if (value < 0x10000000) {
		data[pos++] = 0xE0 | ((value ) >> 24);
		data[pos++] = (value >> 16) & 0xFF;
		data[pos++] = (value >> 8) & 0xFF;
		data[pos++] = value & 0xFF;
	} else if (value < 0x100000000LL) {
		data[pos++] = 0xF0;
		data[pos++] = (value >> 24) & 0xFF;
		data[pos++] = (value >> 16) & 0xFF;
		data[pos++] = (value >> 8) & 0xFF;
		data[pos++] = value & 0xFF;
	} else {
		data[pos++] = 0xF4;
		data[pos++] = (value >> 56) & 0xFF;
		data[pos++] = (value >> 48) & 0xFF;
		data[pos++] = (value >> 40) & 0xFF;
		data[pos++] = (value >> 32) & 0xFF;
		data[pos++] = (value >> 24) & 0xFF;
		data[pos++] = (value >> 16) & 0xFF;
		data[pos++] = (value >> 8) & 0xFF;
		data[pos++] = value & 0xFF;
	}

	*write = pos;
}

int64_t
decode_varint(uint8_t *data, uint32_t *read, uint32_t left)
{
	int64_t varint = 0;

	/* 1 byte with 7 · 8 + 1 leading zeroes */
	if ((data[0] & 0x80) == 0x00) {
		varint = data[0] & 0x7F;
		*read = 1;
	/* 2 bytes with 6 · 8 + 2 leading zeroes */
	} else if ((data[0] & 0xC0) == 0x80) {
		varint = ((data[0] & 0x3F) << 8) | data[1];
		*read = 2;
	/* 3 bytes with 5 · 8 + 3 leading zeroes */
	} else if ((data[0] & 0xE0) == 0xC0) {
		varint = (((data[0] & 0x1F) << 16) |
			    (data[1] << 8) | (data[2]));
		*read = 3;
	/* 4 bytes with 4 · 8 + 4 leading zeroes */
	} else if ((data[0] & 0xF0) == 0xE0) {
		varint = (((data[0] & 0x0F) << 24) | (data[1] << 16) |
			  (data[2] << 8) | (data[3]));
		*read = 4;
	} else /* if ((data[pos] & 0xF0) == 0xF0) */ {
		switch (data[0] & 0xFC) {
		/* 32-bit positive number */
		case 0xF0:
			varint = ((data[1] << 24) | (data[2] << 16) |
				  (data[3] << 8) | data[4]);
			*read = 1 + 4;
			break;
		/* 64-bit number */
		case 0xF4:
			varint =
				((int64_t)data[1] << 56) | ((int64_t)data[2] << 48) |
				((int64_t)data[3] << 40) | ((int64_t)data[4] << 32) |
				(data[5] << 24) | (data[6] << 16) |
				(data[7] <<  8) | (data[8] <<  0);
			*read = 1 + 8;
			break;
		/* Negative varint */
		case 0xF8:
			/* FIXME: handle endless recursion */
			varint = -decode_varint(&data[1], read, left - 1);
			*read += 1;
			break;
		/* Negative two bit number */
		case 0xFC:
			varint = -(int)(data[0] & 0x03);
			*read = 1;
			break;
		}
	}

	return varint;
}
