#include <string.h>
#include <assert.h>
#include <stdio.h>
#include "rle6.h"


/******************
 *** 43+3 codec ***
 ******************/

// insert symbol $a after $x symbols in $str; marginal counts added to $cnt; returns the size increase
int rle_insert_core(int len, uint8_t *str, int64_t x, int a, int64_t rl, int64_t cnt[6])
{
	memset(cnt, 0, 48);
	if (len == 0) {
		return rle_enc(str, a, rl);
	} else {
		uint8_t *p = str, *end = str + len, *q;
		int64_t pre, z = 0, l = 0;
		int c = -1, n_bytes = 0, n_bytes2;
		uint8_t tmp[24];
		while (z < x) {
			q = p;
			rle_dec1(p, c, l);
			z += l; cnt[c] += l;
		}
		n_bytes = p - q;
		if (x == z && a != c && p < end) { // then try the next run
			int tc;
			int64_t tl;
			q = p;
			rle_dec1(q, tc, tl);
			if (a == tc)
				c = tc, n_bytes = q - p, l = tl, z += l, p = q, cnt[tc] += tl;
		}
		if (c < 0) c = a, l = 0, pre = 0; // in this case, x==0 and the next run is different from $a
		else cnt[c] -= z - x, pre = x - (z - l), p -= n_bytes;
		if (a == c) { // insert to the same run
			n_bytes2 = rle_enc(tmp, c, l + rl);
		} else if (x == z) { // at the end; append to the existing run
			p += n_bytes; n_bytes = 0;
			n_bytes2 = rle_enc(tmp, a, rl);
		} else { // break the current run
			n_bytes2 = rle_enc(tmp, c, pre);
			n_bytes2 += rle_enc(tmp + n_bytes2, a, rl);
			n_bytes2 += rle_enc(tmp + n_bytes2, c, l - pre);
		}
		if (n_bytes != n_bytes2) // size changed
			memmove(p + n_bytes2, p + n_bytes, end - p - n_bytes);
		memcpy(p, tmp, n_bytes2);
		return n_bytes2 - n_bytes;
	}
}

int rle_insert(int block_len, uint8_t *block, int64_t x, int a, int64_t rl, int64_t cnt[6], const int64_t end_cnt[6])
{
	int diff;
	uint16_t *p = (uint16_t*)(block + block_len - 2);
	diff = rle_insert_core(*p, block, x, a, rl, cnt);
	*p += diff;
	return *p + 18 > block_len? 1 : 0;
}

int rle_insert1(int block_len, uint8_t *block, int64_t x, int a, int64_t cnt[6], const int64_t end_cnt[6])
{
	return rle_insert(block_len, block, x, a, 1, cnt, end_cnt);
}

void rle_split(int block_len, uint8_t *block, uint8_t *new_block)
{
	uint16_t *r, *p = (uint16_t*)(block + block_len - 2);
	uint8_t *q = block, *end = block + (*p>>4>>1);
	while (q < end) q += rle_bytes(q);
	end = block + *p;
	memcpy(new_block, q, end - q);
	r = (uint16_t*)(new_block + block_len - 2);
	*r = end - q; *p = q - block;
}

void rle_count(int block_len, const uint8_t *block, int64_t cnt[6])
{
	uint16_t *p = (uint16_t*)(block + block_len - 2);
	const uint8_t *q = block, *end = block + *p;
	while (q < end) {
		int c;
		int64_t l;
		rle_dec1(q, c, l);
		cnt[c] += l;
	}
}

void rle_print(int block_len, const uint8_t *block)
{
	uint16_t *p = (uint16_t*)(block + block_len - 2);
	const uint8_t *q = block, *end = block;
	printf("%d\t", *p);
	while (q < end) {
		int c;
		int64_t l;
		rle_dec1(q, c, l);
		printf("%c%ld", "$ACGTN"[c], (long)l);
	}
	putchar('\n');
}
