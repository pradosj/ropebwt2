#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include "mrope.h"

/*******************************
 *** Single-string insertion ***
 *******************************/

mrope_t *mr_init(int max_nodes, int block_len)
{
	int a;
	mrope_t *r;
	r = calloc(1, sizeof(mrope_t));
	for (a = 0; a != 6; ++a)
		r->r[a] = rope_init(max_nodes, block_len);
	return r;
}

void mr_destroy(mrope_t *r)
{
	int a;
	for (a = 0; a != 6; ++a)
		rope_destroy(r->r[a]);
}

void mr_insert_string_io(mrope_t *r, const uint8_t *str)
{
	const uint8_t *p;
	int64_t x;
	int a;
	for (a = 0, x = 0; a != 6; ++a)
		x += r->r[a]->c[0];
	for (p = str, a = 0; *p; a = *p++) {
		x = rope_insert_run(r->r[a], x, *p, 1, 0);
		while (--a >= 0) x += r->r[a]->c[*p];
	}
	rope_insert_run(r->r[a], x, *p, 1, 0);
}

void mr_insert_string_rlo(mrope_t *r, const uint8_t *str, int is_comp)
{
	int b;
	int64_t tl[6], tu[6], l, u;
	const uint8_t *p;
	for (l = u = 0, b = 0; b != 6; ++b) u += r->r[b]->c[0];
	for (p = str, b = 0; *p; b = *p++) {
		int a;
		if (l != u) {
			int64_t cnt = 0;
			rope_rank2a(r->r[b], l, u, tl, tu);
			if (is_comp && *p != 5) {
				for (a = 4; a > *p; --a) l += tu[a] - tl[a];
				l += tu[0] - tl[0];
			} else for (a = 0; a < *p; ++a) l += tu[a] - tl[a];
			rope_insert_run(r->r[b], l, *p, 1, 0);
			while (--b >= 0) cnt += r->r[b]->c[*p];
			l = cnt + tl[*p]; u = cnt + tu[*p];
		} else {
			l = rope_insert_run(r->r[b], l, *p, 1, 0);
			while (--b >= 0) l += r->r[b]->c[*p];
			u = l;
		}
	}
	rope_insert_run(r->r[b], l, 0, 1, 0);
}

/**********************
 *** Mrope iterator ***
 **********************/

void mr_itr_first(const mrope_t *r, mritr_t *i)
{
	i->a = 0; i->r = r;
	rope_itr_first(i->r->r[0], &i->i);
}

const uint8_t *mr_itr_next_block(mritr_t *i, int *n)
{
	const uint8_t *s;
	*n = 0;
	if (i->a >= 6) return 0;
	while ((s = rope_itr_next_block(&i->i, n)) == 0) {
		if (++i->a == 6) return 0;
		rope_itr_first(i->r->r[i->a], &i->i);
	}
	return i->a == 6? 0 : s;
}

/*****************************************
 *** Inserting multiple strings in RLO ***
 *****************************************/

typedef const uint8_t *cstr_t;

typedef struct {
	uint64_t l;
	uint64_t u:61, c:3;
	cstr_t p;
} triple64_t;

#define rstype_t triple64_t
#define rskey(x) ((x).l)

#define RS_MIN_SIZE 64

typedef struct {
	rstype_t *b, *e;
} rsbucket_t;

void rs_sort(rstype_t *beg, rstype_t *end, int n_bits, int s)
{
	rstype_t *i;
	int size = 1<<n_bits, m = size - 1;
	rsbucket_t *k, b[size], *be = b + size;

	for (k = b; k != be; ++k) k->b = k->e = beg;
	for (i = beg; i != end; ++i) ++b[rskey(*i)>>s&m].e; // count radix
	for (k = b + 1; k != be; ++k) // set start and end of each bucket
		k->e += (k-1)->e - beg, k->b = (k-1)->e;
	for (k = b; k != be;) { // in-place classification based on radix
		if (k->b != k->e) { // the bucket is not full
			rsbucket_t *l;
			if ((l = b + (rskey(*k->b)>>s&m)) != k) { // destination different
				rstype_t tmp = *k->b, swap;
				do { // swap until we find an element in $k
					swap = tmp; tmp = *l->b; *l->b++ = swap;
					l = b + (rskey(tmp)>>s&m);
				} while (l != k);
				*k->b++ = tmp;
			} else ++k->b;
		} else ++k;
	}
	for (b->b = beg, k = b + 1; k != be; ++k) k->b = (k-1)->e; // reset k->b
	if (s) { // if $s is non-zero, we need to sort buckets
		s = s > n_bits? s - n_bits : 0;
		for (k = b; k != be; ++k)
			if (k->e - k->b > RS_MIN_SIZE) rs_sort(k->b, k->e, n_bits, s);
			else if (k->e - k->b > 1) // then use an insertion sort
				for (i = k->b + 1; i < k->e; ++i)
					if (rskey(*i) < rskey(*(i - 1))) {
						rstype_t *j, tmp = *i;
						for (j = i; j > k->b && rskey(tmp) < rskey(*(j-1)); --j)
							*j = *(j - 1);
						*j = tmp;
					}
	}
}

void rs_classify(rstype_t *beg, rstype_t *end, int64_t c[8]) // very similar to the first half of rs_sort()
{
	rsbucket_t *k, b[8], *be = b + 8;
	rstype_t *p;
	memset(c, 0, 64);
	for (p = beg; p != end; ++p) ++c[p->c];
	for (b->b = beg, k = b + 1; k != be; ++k) k->b = (k-1)->b + c[k-b-1];
	for (k = b; k != be - 1; ++k) k->e = k[1].b;
	k->e = end;
	for (k = b; k != be;) {
		if (k->b != k->e) {
			rsbucket_t *l;
			if ((l = b + (*k->b).c) != k) {
				rstype_t tmp = *k->b, swap;
				do {
					swap = tmp; tmp = *l->b; *l->b++ = swap;
					l = b + tmp.c;
				} while (l != k);
				*k->b++ = tmp;
			} else ++k->b;
		} else ++k;
	}
}

#define rope_comp6(c) ((c) >= 1 && (c) <= 4? 5 - (c) : (c))

static void mr_insert_multi_aux(rope_t *rope, int64_t m, triple64_t *a, int is_comp)
{
	int64_t k, beg, max = 0;
	rpcache_t cache;
	if (m == 0) return;
	memset(&cache, 0, sizeof(rpcache_t));
	for (k = 0; k != m; ++k) { // set the base to insert
		a[k].c = *a[k].p++;
		max = max > a[k].l? max : a[k].l;
	}
	for (k = 0; max; max >>= 1, ++k);
	rs_sort(a, a + m, 8, k > 7? k - 7 : 0);
	for (k = 1, beg = 0; k <= m; ++k) {
		if (k == m || a[k].u != a[k-1].u) {
			int64_t x, i, l = a[beg].l, u = a[beg].u, tl[6], tu[6], c[6];
			int start, end, step, b;
			if (l == u) {
				memset(tl, 0, 48);
				memset(tu, 0, 48);
			} else rope_rank2a(rope, l, u, tl, tu);
			memset(c, 0, 48);
			for (i = beg; i < k; ++i) ++c[a[i].c];
			// insert sentinel
			if (c[0]) rope_insert_run(rope, l, 0, c[0], &cache);
			// insert A/C/G/T
			x =  l + c[0] + (tu[0] - tl[0]);
			if (is_comp) start = 4, end = 0, step = -1;
			else start = 1, end = 5, step = 1;
			for (b = start; b != end; b += step) {
				int64_t size = tu[b] - tl[b];
				if (c[b]) {
					tl[b] = rope_insert_run(rope, x, b, c[b], &cache);
					tu[b] = tl[b] + size;
				}
				x += c[b] + size;
			}
			// insert N
			if (c[5]) {
				tu[5] -= tl[5];
				tl[5] = rope_insert_run(rope, x, 5, c[5], &cache);
				tu[5] += tl[5];
			}
			// update a[]
			for (i = beg; i < k; ++i) {
				triple64_t *p = &a[i];
				p->l = tl[p->c], p->u = tu[p->c];
			}
			beg = k;
		}
	}
}

typedef struct {
	volatile int *n_fin_workers;
	volatile int to_run;
	int to_exit;
	mrope_t *mr;
	int b, is_comp;
	int64_t m;
	triple64_t *a;
} worker_t;

static void *worker(void *data)
{
	worker_t *w = (worker_t*)data;
	struct timespec req, rem;
	req.tv_sec = 0; req.tv_nsec = 1000000;
	while (!w->to_exit) {
		while (!__sync_bool_compare_and_swap(&w->to_run, 1, 0)) nanosleep(&req, &rem); // wait the signal from the master thread
		mr_insert_multi_aux(w->mr->r[w->b], w->m, w->a, w->is_comp);
		__sync_add_and_fetch(w->n_fin_workers, 1);
	}
	return 0;
}

void mr_insert_multi(mrope_t *mr, int64_t len, const uint8_t *s, int is_srt, int is_comp, int is_thr)
{
	int64_t k, m, n0;
	int b;
	volatile int n_fin_workers = 0;
	triple64_t *a;
	pthread_t *tid = 0;
	worker_t *w = 0;

	assert(len > 0 && s[len-1] == 0);
	if (!is_srt) is_comp = 0;
	{ // split into short strings
		cstr_t p, q, end = s + len;
		for (p = s, m = 0; p != end; ++p) // count #sentinels
			if (*p == 0) ++m;
		a = malloc(m * sizeof(triple64_t));
		for (p = q = s, k = 0; p != end; ++p) // find the start of each string
			if (*p == 0) a[k++].p = q, q = p + 1;
	}

	for (k = n0 = 0; k < 6; ++k) n0 += mr->r[k]->c[0];
	for (k = 0; k != m; ++k) {
		if (is_srt) a[k].l = 0, a[k].u = n0;
		else a[k].l = a[k].u = n0 + k;
		a[k].c = 0;
	}
	mr_insert_multi_aux(mr->r[0], m, a, is_comp); // insert the first (actually the last) column

	if (is_thr) {
		tid = alloca(4 * sizeof(pthread_t));
		w = alloca(4 * sizeof(worker_t));
		memset(w, 0, 4 * sizeof(worker_t));
		for (b = 0; b < 4; ++b) {
			w[b].mr = mr, w[b].b = b + 1, w[b].is_comp = is_comp;
			w[b].n_fin_workers = &n_fin_workers;
		}
		for (b = 0; b < 4; ++b) pthread_create(&tid[b], 0, worker, &w[b]);
	}

	n0 = 0;
	while (m) {
		int64_t c[8], ac[6];
		triple64_t *q[6];

		memset(c, 0, 48);
		rs_classify(a + n0, a + m, c);
		for (q[0] = a + n0, b = 1; b < 6; ++b) q[b] = q[b-1] + c[b-1];
		n0 += c[0];

		if (is_thr) {
			struct timespec req, rem;
			req.tv_sec = 0; req.tv_nsec = 1000000;
			for (b = 0; b < 4; ++b) {
				w[b].a = q[b+1], w[b].m = c[b+1];
				if (n0 == m) w[b].to_exit = 1; // signal the workers to exit
				while (!__sync_bool_compare_and_swap(&w[b].to_run, 0, 1)); // signal the workers to start
			}
			mr_insert_multi_aux(mr->r[5], c[5], q[5], is_comp); // the master thread processes the "N" bucket
			while (!__sync_bool_compare_and_swap(&n_fin_workers, 4, 0)) // wait until all 4 workers finish
				nanosleep(&req, &rem);
		} else {
			for (b = 1; b < 6; ++b)
				mr_insert_multi_aux(mr->r[b], c[b], q[b], is_comp);
		}
		if (n0 == m) break;

		memset(ac, 0, 48);
		for (b = 1; b < 6; ++b) {
			int a;
			for (a = 0; a < 6; ++a) ac[a] += mr->r[b-1]->c[a];
			for (k = 0; k < c[b]; ++k) {
				triple64_t *p = &q[b][k];
				p->l += ac[p->c]; p->u += ac[p->c];
			}
		}
	}
	if (is_thr) for (b = 0; b < 4; ++b) pthread_join(tid[b], 0);
	free(a);
}
