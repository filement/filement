/* 
 * MD5 Message Digest Algorithm (RFC1321).
 *
 * Derived from cryptoapi implementation, originally based on the
 * public domain implementation written by Colin Plumb in 1993.
 *
 * Copyright (c) Cryptoapi developers.
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2012 Martin Kunev <martinkunev@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option) 
 * any later version.
 *
 */

#include <string.h>

#include "md5.h"

// Calculates MD5 sum of a 512 bit chunk
static void md5_transform(uint32_t hash[restrict], const uint32_t chunk[])
{
	uint32_t a, b, c, d;

	a = hash[0];
	b = hash[1];
	c = hash[2];
	d = hash[3];

	#define F1(x, y, z)	(z ^ (x & (y ^ z)))
	#define F2(x, y, z)	F1(z, x, y)
	#define F3(x, y, z)	(x ^ y ^ z)
	#define F4(x, y, z)	(y ^ (x | ~z))

	#define MD5STEP(f, w, x, y, z, in, s) (w += f(x, y, z) + in, w = (w<<s | w>>(32-s)) + x)

	MD5STEP(F1, a, b, c, d, chunk[0] + 0xd76aa478, 7);
	MD5STEP(F1, d, a, b, c, chunk[1] + 0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, chunk[2] + 0x242070db, 17);
	MD5STEP(F1, b, c, d, a, chunk[3] + 0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, chunk[4] + 0xf57c0faf, 7);
	MD5STEP(F1, d, a, b, c, chunk[5] + 0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, chunk[6] + 0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, chunk[7] + 0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, chunk[8] + 0x698098d8, 7);
	MD5STEP(F1, d, a, b, c, chunk[9] + 0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, chunk[10] + 0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, chunk[11] + 0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, chunk[12] + 0x6b901122, 7);
	MD5STEP(F1, d, a, b, c, chunk[13] + 0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, chunk[14] + 0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, chunk[15] + 0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, chunk[1] + 0xf61e2562, 5);
	MD5STEP(F2, d, a, b, c, chunk[6] + 0xc040b340, 9);
	MD5STEP(F2, c, d, a, b, chunk[11] + 0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, chunk[0] + 0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, chunk[5] + 0xd62f105d, 5);
	MD5STEP(F2, d, a, b, c, chunk[10] + 0x02441453, 9);
	MD5STEP(F2, c, d, a, b, chunk[15] + 0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, chunk[4] + 0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, chunk[9] + 0x21e1cde6, 5);
	MD5STEP(F2, d, a, b, c, chunk[14] + 0xc33707d6, 9);
	MD5STEP(F2, c, d, a, b, chunk[3] + 0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, chunk[8] + 0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, chunk[13] + 0xa9e3e905, 5);
	MD5STEP(F2, d, a, b, c, chunk[2] + 0xfcefa3f8, 9);
	MD5STEP(F2, c, d, a, b, chunk[7] + 0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, chunk[12] + 0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, chunk[5] + 0xfffa3942, 4);
	MD5STEP(F3, d, a, b, c, chunk[8] + 0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, chunk[11] + 0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, chunk[14] + 0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, chunk[1] + 0xa4beea44, 4);
	MD5STEP(F3, d, a, b, c, chunk[4] + 0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, chunk[7] + 0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, chunk[10] + 0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, chunk[13] + 0x289b7ec6, 4);
	MD5STEP(F3, d, a, b, c, chunk[0] + 0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, chunk[3] + 0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, chunk[6] + 0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, chunk[9] + 0xd9d4d039, 4);
	MD5STEP(F3, d, a, b, c, chunk[12] + 0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, chunk[15] + 0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, chunk[2] + 0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, chunk[0] + 0xf4292244, 6);
	MD5STEP(F4, d, a, b, c, chunk[7] + 0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, chunk[14] + 0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, chunk[5] + 0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, chunk[12] + 0x655b59c3, 6);
	MD5STEP(F4, d, a, b, c, chunk[3] + 0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, chunk[10] + 0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, chunk[1] + 0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, chunk[8] + 0x6fa87e4f, 6);
	MD5STEP(F4, d, a, b, c, chunk[15] + 0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, chunk[6] + 0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, chunk[13] + 0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, chunk[4] + 0xf7537e82, 6);
	MD5STEP(F4, d, a, b, c, chunk[11] + 0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, chunk[2] + 0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, chunk[9] + 0xeb86d391, 21);

	hash[0] += a;
	hash[1] += b;
	hash[2] += c;
	hash[3] += d;
}

// Calculates the hash of in, which is size bytes long
// WARNING: works only for size < 2^29
// TODO: better names and better comments
// TODO: check variable types
// TODO: check whether the different types of pointers are compatible
void md5(uint32_t hash[restrict], const uint8_t input[restrict], uint32_t size)
{
	#define MASK_CHUNK 0x3f

	// Calculate message length in bits
	uint32_t length = size * 8;

	uint8_t rest = size & MASK_CHUNK;
	size &= ~MASK_CHUNK;

	uint8_t chunk[128];
	memcpy(chunk, input + size, rest);

	// Add padding
	{
		uint8_t padding;

		chunk[rest++] = 0x80;

		padding = (64 - ((rest + 8) & MASK_CHUNK));
		memset(chunk + rest, 0, padding);
		rest += padding;

		while (rest & MASK_CHUNK)
		{
			chunk[rest++] = length & 0xff;
			length >>= 8;
		}
	}

	// Calculate the hash
	{
		unsigned start;

		hash[0] = 0x67452301;
		hash[1] = 0xefcdab89;
		hash[2] = 0x98badcfe;
		hash[3] = 0x10325476;

		for(start = 0; start < size; start += 64)
			md5_transform(hash, (uint32_t *)(input + start));

		for(start = 0; start < rest; start += 64)
			md5_transform(hash, (uint32_t *)(chunk + start));
	}
}

/*char digit(unsigned num)
{
	if (num < 10) return (num + '0');
	else return ((num - 10) + 'a');
}

#include <stdio.h>

int main(void)
{
	uint32_t hash[4];
	uint8_t in[] = "but an infinite number of possible inputs, it has long been known that such collisions must exist, but it had been previously believed to be impractically difficult to find one. ";

	md5(hash, in, sizeof(in) - 1);

	uint32_t i, j;
	uint8_t byte;
	for(i = 0; i < 4; ++i)
	{
		for(j = 0; j < 32; j += 8)
		{
			byte = (hash[i] & (0xff << j)) >> j;
			printf("%c%c", digit((byte & 0xf0) >> 4), digit(byte & 0x0f));
		}
	}
	printf("\n");

	return 0;
}*/
