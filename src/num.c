/*
 * *****************************************************************************
 *
 * Copyright 2018 Gavin D. Howard
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * *****************************************************************************
 *
 * Code for the number type.
 *
 */

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <limits.h>

#include <status.h>
#include <num.h>
#include <vm.h>

static void bc_num_setToZero(BcNum *n, size_t scale) {
	assert(n);
	n->neg = (n->len = 0);
	n->rdx = scale;
}

void bc_num_zero(BcNum *n) {
	bc_num_setToZero(n, 0);
}

void bc_num_one(BcNum *n) {
	bc_num_setToZero(n, 0);
	n->len = 1;
	n->num[0] = 1;
}

void bc_num_ten(BcNum *n) {
	assert(n);
	bc_num_setToZero(n, 0);
	n->len = 2;
	n->num[0] = 0;
	n->num[1] = 1;
}

static BcStatus bc_num_subArrays(BcDig *restrict a, BcDig *restrict b,
                                 size_t len)
{
	size_t i, j;
	for (i = 0; !bcg.signe && i < len; ++i) {
		for (a[i] -= b[i], j = 0; !bcg.signe && a[i + j] < 0;) {
			a[i + j++] += 10;
			a[i + j] -= 1;
		}
	}
	return bcg.signe ? BC_STATUS_EXEC_SIGNAL : BC_STATUS_SUCCESS;
}

static ssize_t bc_num_compare(BcDig *restrict a, BcDig *restrict b, size_t len)
{
	size_t i;
	int c = 0;
	for (i = len - 1; !bcg.signe && i < len && !(c = a[i] - b[i]); --i);
	return BC_NUM_NEG(i + 1, c < 0);
}

ssize_t bc_num_cmp(BcNum *a, BcNum *b) {

	size_t i, min, a_int, b_int, diff;
	BcDig *max_num, *min_num;
	bool a_max, neg = false;
	ssize_t cmp;

	assert(a && b);

	if (a == b) return 0;
	else if (!a->len) return BC_NUM_NEG(!!b->len, !b->neg);
	else if (!b->len) return BC_NUM_NEG(1, a->neg);
	else if (a->neg) {
		if (b->neg) neg = true;
		else return -1;
	}
	else if (b->neg) return 1;

	a_int = BC_NUM_INT(a);
	b_int = BC_NUM_INT(b);

	if ((a_int -= b_int)) return (ssize_t) a_int;

	if ((a_max = (a->rdx > b->rdx))) {
		min = b->rdx;
		diff = a->rdx - b->rdx;
		max_num = a->num + diff;
		min_num = b->num;
	}
	else {
		min = a->rdx;
		diff = b->rdx - a->rdx;
		max_num = b->num + diff;
		min_num = a->num;
	}

	if ((cmp = bc_num_compare(max_num, min_num, b_int + min)))
		return BC_NUM_NEG(cmp, (!a_max) != neg);

	for (max_num -= diff, i = diff - 1; !bcg.signe && i < diff; --i) {
		if (max_num[i]) return BC_NUM_NEG(1, (!a_max) != neg);
	}

	return 0;
}

void bc_num_truncate(BcNum *n, size_t places) {

	assert(places <= n->rdx && (!n->len || places <= n->len));

	if (!places) return;

	n->rdx -= places;

	if (n->len) {
		n->len -= places;
		memmove(n->num, n->num + places, n->len * sizeof(BcDig));
	}
}

static BcStatus bc_num_extend(BcNum *n, size_t places) {

	BcStatus s;
	size_t len = n->len + places;

	if (!places) return BC_STATUS_SUCCESS;
	if (n->cap < len && (s = bc_num_expand(n, len))) return s;

	memmove(n->num + places, n->num, sizeof(BcDig) * n->len);
	memset(n->num, 0, sizeof(BcDig) * places);

	n->len += places;
	n->rdx += places;

	return BC_STATUS_SUCCESS;
}

static void bc_num_clean(BcNum *n) {
	while (n->len > 0 && !n->num[n->len - 1]) --n->len;
	if (!n->len) n->neg = false;
	else if (n->len < n->rdx) n->len = n->rdx;
}

static BcStatus bc_num_retireMul(BcNum *n, size_t scale, bool neg1, bool neg2) {

	BcStatus s = BC_STATUS_SUCCESS;

	if (n->rdx < scale) s = bc_num_extend(n, scale - n->rdx);
	else bc_num_truncate(n, n->rdx - scale);

	bc_num_clean(n);
	if (n->len) n->neg = !neg1 != !neg2;

	return s;
}

static BcStatus bc_num_split(BcNum *restrict n, size_t idx, BcNum *restrict a,
                             BcNum *restrict b)
{
	BcStatus s = BC_STATUS_SUCCESS;

	if (idx < n->len) {

		b->len = n->len - idx;
		a->len = idx;
		a->rdx = b->rdx = 0;

		memcpy(b->num, n->num + idx, b->len * sizeof(BcDig));
		memcpy(a->num, n->num, idx * sizeof(BcDig));
	}
	else {
		bc_num_zero(b);
		s = bc_num_copy(a, n);
	}

	bc_num_clean(a);
	bc_num_clean(b);

	return s;
}

static BcStatus bc_num_shift(BcNum *n, size_t places) {

	BcStatus s = BC_STATUS_SUCCESS;

	if (!places || !n->len) return BC_STATUS_SUCCESS;
	if (places + n->len > BC_MAX_NUM) return BC_STATUS_EXEC_NUM_LEN;

	if (n->rdx >= places) n->rdx -= places;
	else {
		s = bc_num_extend(n, places - n->rdx);
		n->rdx = 0;
	}

	bc_num_clean(n);

	return s;
}

static BcStatus bc_num_inv(BcNum *a, BcNum *b, size_t scale) {

	BcNum one;
	BcDig num[2];

	one.cap = 2;
	one.num = num;
	bc_num_one(&one);

	return bc_num_div(&one, a, b, scale);
}

static BcStatus bc_num_a(BcNum *a, BcNum *b, BcNum *restrict c, size_t sub) {

	BcDig *ptr, *ptr_a, *ptr_b, *ptr_c;
	size_t i, max, min_rdx, min_int, diff, a_int, b_int;
	int carry, in;

	// Because this function doesn't need to use scale (per the bc spec),
	// I am hijacking it to say whether it's doing an add or a subtract.

	if (!a->len) {
		BcStatus s = bc_num_copy(c, b);
		if (sub && c->len) c->neg = !c->neg;
		return s;
	}
	else if (!b->len) return bc_num_copy(c, a);

	c->neg = a->neg;
	c->rdx = BC_MAX(a->rdx, b->rdx);
	min_rdx = BC_MIN(a->rdx, b->rdx);
	c->len = 0;

	if (a->rdx > b->rdx) {
		diff = a->rdx - b->rdx;
		ptr = a->num;
		ptr_a = a->num + diff;
		ptr_b = b->num;
	}
	else {
		diff = b->rdx - a->rdx;
		ptr = b->num;
		ptr_a = a->num;
		ptr_b = b->num + diff;
	}

	for (ptr_c = c->num, i = 0; i < diff; ++i, ++c->len) ptr_c[i] = ptr[i];

	ptr_c += diff;

	if ((a_int = BC_NUM_INT(a)) > (b_int = BC_NUM_INT(b))) {
		min_int = b_int;
		max = a_int;
		ptr = ptr_a;
	}
	else {
		min_int = a_int;
		max = b_int;
		ptr = ptr_b;
	}

	for (carry = 0, i = 0; !bcg.signe && i < min_rdx + min_int; ++i, ++c->len) {
		in = ((int) ptr_a[i]) + ((int) ptr_b[i]) + carry;
		carry = in / 10;
		ptr_c[i] = (BcDig) (in % 10);
	}

	for (; !bcg.signe && i < max + min_rdx; ++i, ++c->len) {
		in = ((int) ptr[i]) + carry;
		carry = in / 10;
		ptr_c[i] = (BcDig) (in % 10);
	}

	if (bcg.signe) return BC_STATUS_EXEC_SIGNAL;

	if (carry) c->num[c->len++] = (BcDig) carry;

	return BC_STATUS_SUCCESS;
}

static BcStatus bc_num_s(BcNum *a, BcNum *b, BcNum *restrict c, size_t sub) {

	BcStatus s;
	ssize_t cmp;
	BcNum *minuend, *subtrahend;
	size_t start;
	bool aneg, bneg, neg;

	// Because this function doesn't need to use scale (per the bc spec),
	// I am hijacking it to say whether it's doing an add or a subtract.

	if (!a->len) {
		s = bc_num_copy(c, b);
		if (sub && c->len) c->neg = !c->neg;
		return s;
	}
	else if (!b->len) return bc_num_copy(c, a);

	aneg = a->neg;
	bneg = b->neg;
	a->neg = b->neg = false;

	cmp = bc_num_cmp(a, b);

	a->neg = aneg;
	b->neg = bneg;

	if (!cmp) {
		bc_num_setToZero(c, BC_MAX(a->rdx, b->rdx));
		return BC_STATUS_SUCCESS;
	}
	else if (cmp > 0) {
		neg = a->neg;
		minuend = a;
		subtrahend = b;
	}
	else {
		neg = b->neg;
		if (sub) neg = !neg;
		minuend = b;
		subtrahend = a;
	}

	if ((s = bc_num_copy(c, minuend))) return s;
	c->neg = neg;

	if (c->rdx < subtrahend->rdx) {
		if ((s = bc_num_extend(c, subtrahend->rdx - c->rdx))) return s;
		start = 0;
	}
	else start = c->rdx - subtrahend->rdx;

	s = bc_num_subArrays(c->num + start, subtrahend->num, subtrahend->len);

	bc_num_clean(c);

	return s;
}

static BcStatus bc_num_k(BcNum *restrict a, BcNum *restrict b,
                         BcNum *restrict c)
{
	BcStatus s;
	int carry;
	size_t i, j, len, max = BC_MAX(a->len, b->len), max2 = (max + 1) / 2;
	BcNum l1, h1, l2, h2, m2, m1, z0, z1, z2, temp;
	bool aone = BC_NUM_ONE(a);

	if (!a->len || !b->len) {
		bc_num_zero(c);
		return BC_STATUS_SUCCESS;
	}
	else if (aone || BC_NUM_ONE(b)) return bc_num_copy(c, aone ? b : a);

	if (a->len + b->len < BC_NUM_KARATSUBA_LEN ||
	    a->len < BC_NUM_KARATSUBA_LEN || b->len < BC_NUM_KARATSUBA_LEN)
	{
		if ((s = bc_num_expand(c, a->len + b->len + 1))) return s;

		memset(c->num, 0, sizeof(BcDig) * c->cap);
		c->len = carry = len = 0;

		for (i = 0; !bcg.signe && i < b->len; ++i) {

			for (j = 0; !bcg.signe && j < a->len; ++j) {
				int in = (int) c->num[i + j];
				in += ((int) a->num[j]) * ((int) b->num[i]) + carry;
				carry = in / 10;
				c->num[i + j] = (BcDig) (in % 10);
			}

			if (bcg.signe) return BC_STATUS_EXEC_SIGNAL;

			c->num[i + j] += (BcDig) carry;
			len = BC_MAX(len, i + j + !!carry);
			carry = 0;
		}

		c->len = len;

		return bcg.signe ? BC_STATUS_EXEC_SIGNAL : BC_STATUS_SUCCESS;
	}

	if ((s = bc_num_init(&l1, max))) return s;
	if ((s = bc_num_init(&h1, max))) goto high1_err;
	if ((s = bc_num_init(&l2, max))) goto low2_err;
	if ((s = bc_num_init(&h2, max))) goto high2_err;
	if ((s = bc_num_init(&m1, max))) goto mix1_err;
	if ((s = bc_num_init(&m2, max))) goto mix2_err;
	if ((s = bc_num_init(&z0, max))) goto z0_err;
	if ((s = bc_num_init(&z1, max))) goto z1_err;
	if ((s = bc_num_init(&z2, max))) goto z2_err;
	if ((s = bc_num_init(&temp, max + max))) goto temp_err;

	if ((s = bc_num_split(a, max2, &l1, &h1))) goto err;
	if ((s = bc_num_split(b, max2, &l2, &h2))) goto err;

	if ((s = bc_num_add(&h1, &l1, &m1, 0))) goto err;
	if ((s = bc_num_add(&h2, &l2, &m2, 0))) goto err;

	if ((s = bc_num_k(&h1, &h2, &z0))) goto err;
	if ((s = bc_num_k(&m1, &m2, &z1))) goto err;
	if ((s = bc_num_k(&l1, &l2, &z2))) goto err;

	if ((s = bc_num_sub(&z1, &z0, &temp, 0))) goto err;
	if ((s = bc_num_sub(&temp, &z2, &z1, 0))) goto err;

	if ((s = bc_num_shift(&z0, max2 * 2))) goto err;
	if ((s = bc_num_shift(&z1, max2))) goto err;
	if ((s = bc_num_add(&z0, &z1, &temp, 0))) goto err;
	s = bc_num_add(&temp, &z2, c, 0);

err:
	bc_num_free(&temp);
temp_err:
	bc_num_free(&z2);
z2_err:
	bc_num_free(&z1);
z1_err:
	bc_num_free(&z0);
z0_err:
	bc_num_free(&m2);
mix2_err:
	bc_num_free(&m1);
mix1_err:
	bc_num_free(&h2);
high2_err:
	bc_num_free(&l2);
low2_err:
	bc_num_free(&h1);
high1_err:
	bc_num_free(&l1);
	return s;
}

static BcStatus bc_num_m(BcNum *a, BcNum *b, BcNum *restrict c, size_t scale) {

	BcStatus s;
	BcNum cpa, cpb;
	size_t maxrdx = BC_MAX(a->rdx, b->rdx);

	scale = BC_MAX(scale, a->rdx);
	scale = BC_MAX(scale, b->rdx);
	scale = BC_MIN(a->rdx + b->rdx, scale);
	maxrdx = BC_MAX(maxrdx, scale);

	if ((s = bc_num_init(&cpa, a->len))) return s;
	if ((s = bc_num_init(&cpb, b->len))) goto b_err;

	if ((s = bc_num_copy(&cpa, a))) goto err;
	if ((s = bc_num_copy(&cpb, b))) goto err;
	cpa.neg = cpb.neg = false;
	if ((s = bc_num_shift(&cpa, maxrdx))) goto err;
	if ((s = bc_num_shift(&cpb, maxrdx))) goto err;

	if ((s = bc_num_k(&cpa, &cpb, c))) goto err;

	if ((s = bc_num_expand(c, c->len + (maxrdx += scale)))) goto err;
	if (c->len < maxrdx) {
		memset(c->num + c->len, 0, (c->cap - c->len) * sizeof(BcDig));
		c->len += maxrdx;
	}

	c->rdx = maxrdx;
	s = bc_num_retireMul(c, scale, a->neg, b->neg);

err:
	bc_num_free(&cpb);
b_err:
	bc_num_free(&cpa);
	return s;
}

static BcStatus bc_num_d(BcNum *a, BcNum *b, BcNum *restrict c, size_t scale) {

	BcStatus s;
	BcDig *n, *bptr, q;
	size_t len, end, i;
	BcNum cp;

	if (!b->len) return BC_STATUS_MATH_DIVIDE_BY_ZERO;
	else if (!a->len) {
		bc_num_setToZero(c, scale);
		return BC_STATUS_SUCCESS;
	}
	else if (BC_NUM_ONE(b)) {
		if ((s = bc_num_copy(c, a))) return s;
		return bc_num_retireMul(c, scale, a->neg, b->neg);
	}

	if ((s = bc_num_init(&cp, BC_NUM_MREQ(a, b, scale)))) return s;
	if ((s = bc_num_copy(&cp, a))) goto err;

	if ((len = b->len) > cp.len) {
		if ((s = bc_num_expand(&cp, len + 2))) goto err;
		if ((s = bc_num_extend(&cp, len - cp.len))) goto err;
	}

	if (b->rdx > cp.rdx && (s = bc_num_extend(&cp, b->rdx - cp.rdx))) goto err;
	cp.rdx -= b->rdx;
	if (scale > cp.rdx && (s = bc_num_extend(&cp, scale - cp.rdx))) goto err;

	if (b->rdx == b->len) {

		bool zero = true;
		for (i = 0; zero && i < len; ++i) zero = !b->num[len - i - 1];

		assert(i != len || !zero);

		len -= i - 1;
	}

	if (cp.cap == cp.len && (s = bc_num_expand(&cp, cp.len + 1))) goto err;

	// We want an extra zero in front to make things simpler.
	cp.num[cp.len++] = 0;
	end = cp.len - len;

	if ((s = bc_num_expand(c, cp.len))) goto err;

	bc_num_zero(c);
	memset(c->num + end, 0, (c->cap - end) * sizeof(BcDig));
	c->rdx = cp.rdx;
	c->len = cp.len;
	bptr = b->num;

	for (i = end - 1; !bcg.signe  && !s && i < end; --i) {

		n = cp.num + i;

		for (q = 0; (!s && n[len]) || bc_num_compare(n, bptr, len) >= 0; ++q)
			s = bc_num_subArrays(n, bptr, len);

		c->num[i] = q;
	}

	if (!s) s = bc_num_retireMul(c, scale, a->neg, b->neg);

err:
	bc_num_free(&cp);
	return s;
}

static BcStatus bc_num_r(BcNum *a, BcNum *b, BcNum *restrict c,
                         BcNum *restrict d, size_t scale, size_t ts)
{
	BcStatus s;
	BcNum temp;
	bool neg;

	if (!b->len) return BC_STATUS_MATH_DIVIDE_BY_ZERO;

	if (!a->len) {
		bc_num_setToZero(d, ts);
		return BC_STATUS_SUCCESS;
	}

	if ((s = bc_num_init(&temp, d->cap))) return s;

	if ((s = bc_num_d(a, b, c, scale))) goto err;

	if (scale) scale = ts;

	if ((s = bc_num_m(c, b, &temp, scale))) goto err;
	if ((s = bc_num_sub(a, &temp, d, scale))) goto err;

	if (ts > d->rdx && d->len && (s = bc_num_extend(d, ts - d->rdx))) goto err;

	neg = d->neg;
	s = bc_num_retireMul(d, ts, a->neg, b->neg);
	d->neg = neg;

err:
	bc_num_free(&temp);
	return s;
}

static BcStatus bc_num_rem(BcNum *a, BcNum *b, BcNum *restrict c, size_t scale)
{
	BcStatus s;
	BcNum c1;
	size_t ts = BC_MAX(scale + b->rdx, a->rdx), len = BC_NUM_MREQ(a, b, ts);

	if ((s = bc_num_init(&c1, len))) return s;

	s = bc_num_r(a, b, &c1, c, scale, ts);

	bc_num_free(&c1);

	return s;
}

static BcStatus bc_num_p(BcNum *a, BcNum *b, BcNum *restrict c, size_t scale) {

	BcStatus s;
	BcNum copy;
	unsigned long pow;
	size_t i, powrdx, resrdx;
	bool neg, zero;

	if (b->rdx) return BC_STATUS_MATH_NON_INTEGER;

	if (!b->len) {
		bc_num_one(c);
		return BC_STATUS_SUCCESS;
	}
	else if (!a->len) {
		bc_num_setToZero(c, scale);
		return BC_STATUS_SUCCESS;
	}
	else if (BC_NUM_ONE(b)) {

		if (!b->neg) s = bc_num_copy(c, a);
		else s = bc_num_inv(a, c, scale);

		return s;
	}

	neg = b->neg;
	b->neg = false;

	if ((s = bc_num_ulong(b, &pow))) return s;
	if ((s = bc_num_init(&copy, a->len))) return s;
	if ((s = bc_num_copy(&copy, a))) goto err;

	if (!neg) scale = BC_MIN(a->rdx * pow, BC_MAX(scale, a->rdx));

	b->neg = neg;

	for (powrdx = a->rdx; !bcg.signe && !(pow & 1); pow >>= 1) {
		powrdx <<= 1;
		if ((s = bc_num_mul(&copy, &copy, &copy, powrdx))) goto err;
	}

	if ((s = bc_num_copy(c, &copy))) goto err;
	if (bcg.signe) {
		s = BC_STATUS_EXEC_SIGNAL;
		goto err;
	}

	for (resrdx = powrdx, pow >>= 1; !bcg.signe && pow != 0; pow >>= 1) {
		if ((s = bc_num_mul(&copy, &copy, &copy, (powrdx <<= 1)))) goto err;
		if (pow & 1) {
			resrdx += powrdx;
			if ((s = bc_num_mul(c, &copy, c, resrdx))) goto err;
		}
	}

	if (neg && (s = bc_num_inv(c, c, scale))) goto err;
	if (bcg.signe) {
		s = BC_STATUS_EXEC_SIGNAL;
		goto err;
	}

	if (c->rdx > scale) bc_num_truncate(c, c->rdx - scale);

	for (zero = true, i = 0; zero && i < c->len; ++i) zero = !c->num[i];
	if (zero) bc_num_setToZero(c, scale);

err:
	bc_num_free(&copy);
	return s;
}

static BcStatus bc_num_binary(BcNum *a, BcNum *b, BcNum *c, size_t scale,
                              BcNumBinaryOp op, size_t req)
{
	BcStatus s;
	BcNum num2, *ptr_a, *ptr_b;
	bool init = false;

	assert(a && b && c && op);

	if ((init = (c == a))) {
		ptr_a = &num2;
		memcpy(ptr_a, c, sizeof(BcNum));
	}
	else ptr_a = a;

	if (c == b) {

		ptr_b = &num2;

		if (c != a) {
			memcpy(ptr_b, c, sizeof(BcNum));
			init = true;
		}
	}
	else ptr_b = b;

	if (init) s = bc_num_init(c, req);
	else s = bc_num_expand(c, req);

	if (!s) s = op(ptr_a, ptr_b, c, scale);

	assert(!c->neg || c->len);

	if (init) bc_num_free(&num2);
	return s;
}

static bool bc_num_strValid(const char *val, size_t base) {

	BcDig b;
	bool small, radix = false;
	size_t i, len = strlen(val);

	if (!len) return true;

	small = base <= 10;
	b = (BcDig) (small ? base + '0' : base - 10 + 'A');

	for (i = 0; i < len; ++i) {

		BcDig c = val[i];

		if (c == '.') {

			if (radix) return false;

			radix = true;
			continue;
		}

		if (c < '0' || (small && c >= b) || (c > '9' && (c < 'A' || c >= b)))
			return false;
	}

	return true;
}

static BcStatus bc_num_parseDecimal(BcNum *n, const char *val) {

	BcStatus s;
	size_t len, i;
	const char *ptr;
	bool zero = true;

	for (i = 0; val[i] == '0'; ++i);

	val += i;
	len = strlen(val);
	bc_num_zero(n);

	if (len) {
		for (i = 0; zero && i < len; ++i) zero = val[i] == '0' || val[i] == '.';
		if ((s = bc_num_expand(n, len))) return s;
	}

	ptr = strchr(val, '.');

	// Explicitly test for NULL here to produce either a 0 or 1.
	n->rdx = (size_t) ((ptr != NULL) * ((val + len) - (ptr + 1)));

	if (zero) return BC_STATUS_SUCCESS;

	for (i = len - 1; i < len; ++n->len, i -= 1 + (i && val[i - 1] == '.'))
		n->num[n->len] = val[i] - '0';

	return BC_STATUS_SUCCESS;
}

static BcStatus bc_num_parseBase(BcNum *n, const char *val, BcNum *base) {

	BcStatus s;
	BcNum temp, mult, result;
	BcDig c = '\0';
	bool zero = true;
	unsigned long v;
	size_t i, digits, len = strlen(val);

	bc_num_zero(n);

	for (i = 0; zero && i < len; ++i) zero = (val[i] == '.' || val[i] == '0');
	if (zero) return BC_STATUS_SUCCESS;

	if ((s = bc_num_init(&temp, BC_NUM_DEF_SIZE))) return s;
	if ((s = bc_num_init(&mult, BC_NUM_DEF_SIZE))) goto mult_err;

	for (i = 0; i < len && (c = val[i]) != '.'; ++i) {

		v = (unsigned long) (c <= '9' ? c - '0' : c - 'A' + 10);

		if ((s = bc_num_mul(n, base, &mult, 0))) goto int_err;
		if ((s = bc_num_ulong2num(&temp, v))) goto int_err;
		if ((s = bc_num_add(&mult, &temp, n, 0))) goto int_err;
	}

	if (i == len && !(c = val[i])) goto int_err;
	assert(c == '.');
	if ((s = bc_num_init(&result, base->len))) goto int_err;

	bc_num_zero(&result);
	bc_num_one(&mult);

	for (i += 1, digits = 0; i < len && (c = val[i]); ++i, ++digits) {

		v = (unsigned long) (c <= '9' ? c - '0' : c - 'A' + 10);

		if ((s = bc_num_mul(&result, base, &result, 0))) goto err;
		if ((s = bc_num_ulong2num(&temp, v))) goto err;
		if ((s = bc_num_add(&result, &temp, &result, 0))) goto err;
		if ((s = bc_num_mul(&mult, base, &mult, 0))) goto err;
	}

	if ((s = bc_num_div(&result, &mult, &result, digits))) goto err;
	if ((s = bc_num_add(n, &result, n, digits))) goto err;

	if (n->len) {
		if (n->rdx < digits && n->len) s = bc_num_extend(n, digits - n->rdx);
	}
	else bc_num_zero(n);

err:
	bc_num_free(&result);
int_err:
	bc_num_free(&mult);
mult_err:
	bc_num_free(&temp);
	return s;
}

static BcStatus bc_num_printNewline(size_t *nchars, size_t line_len) {
	if (*nchars == line_len - 1) {
		if (putchar('\\') == EOF) return BC_STATUS_IO_ERR;
		if (putchar('\n') == EOF) return BC_STATUS_IO_ERR;
		*nchars = 0;
	}
	return BC_STATUS_SUCCESS;
}

#ifdef DC_ENABLED
static BcStatus bc_num_printChar(size_t num, size_t width, bool radix,
                                 size_t *nchars, size_t line_len)
{
	(void) radix, (void) line_len;
	if (putchar((char) num) == EOF) return BC_STATUS_IO_ERR;
	*nchars = *nchars + width;
	return BC_STATUS_SUCCESS;
}
#endif // DC_ENABLED

static BcStatus bc_num_printDigits(size_t num, size_t width, bool radix,
                                   size_t *nchars, size_t line_len)
{
	BcStatus s;
	size_t exp, pow, div;

	if ((s = bc_num_printNewline(nchars, line_len))) return s;
	if (putchar(radix ? '.' : ' ') == EOF) return BC_STATUS_IO_ERR;
	++(*nchars);

	if ((s = bc_num_printNewline(nchars, line_len))) return s;
	for (exp = 0, pow = 1; exp < width - 1; ++exp, pow *= 10);

	for (exp = 0; exp < width; pow /= 10, ++(*nchars), ++exp) {
		if ((s = bc_num_printNewline(nchars, line_len))) return s;
		div = num / pow;
		num -= div * pow;
		if (putchar(((char) div) + '0') == EOF) return BC_STATUS_IO_ERR;
	}

	return BC_STATUS_SUCCESS;
}

static BcStatus bc_num_printHex(size_t num, size_t width, bool radix,
                                size_t *nchars, size_t line_len)
{
	BcStatus s;

	assert(width == 1);

	if (radix) {
		if ((s = bc_num_printNewline(nchars, line_len))) return s;
		if (putchar('.') == EOF) return BC_STATUS_IO_ERR;
		*nchars += 1;
	}

	if ((s = bc_num_printNewline(nchars, line_len))) return s;
	if (putchar(bc_num_hex_digits[num]) == EOF) return BC_STATUS_IO_ERR;
	*nchars = *nchars + width;

	return BC_STATUS_SUCCESS;
}

static BcStatus bc_num_printDecimal(BcNum *n, size_t *nchars, size_t len) {

	BcStatus s = BC_STATUS_SUCCESS;
	size_t i, rdx = n->rdx - 1;

	if (n->neg && putchar('-') == EOF) return BC_STATUS_IO_ERR;
	(*nchars) += n->neg;

	for (i = n->len - 1; !s && i < n->len; --i)
		s = bc_num_printHex((size_t) n->num[i], 1, i == rdx, nchars, len);

	return s;
}

static BcStatus bc_num_printNum(BcNum *n, BcNum *base, size_t width,
                                size_t *nchars, size_t len, BcNumDigitOp print)
{
	BcStatus s;
	BcVec stack;
	BcNum intp, fracp, digit, frac_len;
	unsigned long dig, *ptr;
	size_t i;
	bool radix;

	if (!n->len) return print(0, width, false, nchars, len);

	if ((s = bc_vec_init(&stack, sizeof(long), NULL))) return s;
	if ((s = bc_num_init(&intp, n->len))) goto int_err;
	if ((s = bc_num_init(&fracp, n->rdx))) goto frac_err;
	if ((s = bc_num_init(&digit, width))) goto digit_err;
	if ((s = bc_num_init(&frac_len, BC_NUM_INT(n)))) goto frac_len_err;
	if ((s = bc_num_copy(&intp, n))) goto err;
	bc_num_one(&frac_len);

	bc_num_truncate(&intp, intp.rdx);
	if ((s = bc_num_sub(n, &intp, &fracp, 0))) goto err;

	while (intp.len) {
		if ((s = bc_num_divmod(&intp, base, &intp, &digit, 0))) goto err;
		if ((s = bc_num_ulong(&digit, &dig))) goto err;
		if ((s = bc_vec_push(&stack, &dig))) goto err;
	}

	for (i = 0; i < stack.len; ++i) {
		ptr = bc_vec_item_rev(&stack, i);
		assert(ptr);
		if ((s = print(*ptr, width, false, nchars, len))) goto err;
	}

	if (!n->rdx) goto err;

	for (radix = true; frac_len.len <= n->rdx; radix = false) {
		if ((s = bc_num_mul(&fracp, base, &fracp, n->rdx))) goto err;
		if ((s = bc_num_ulong(&fracp, &dig))) goto err;
		if ((s = bc_num_ulong2num(&intp, dig))) goto err;
		if ((s = bc_num_sub(&fracp, &intp, &fracp, 0))) goto err;
		if ((s = print(dig, width, radix, nchars, len))) goto err;
		if ((s = bc_num_mul(&frac_len, base, &frac_len, 0))) goto err;
	}

err:
	bc_num_free(&frac_len);
frac_len_err:
	bc_num_free(&digit);
digit_err:
	bc_num_free(&fracp);
frac_err:
	bc_num_free(&intp);
int_err:
	bc_vec_free(&stack);
	return s;
}

static BcStatus bc_num_printBase(BcNum *n, BcNum *base, size_t base_t,
                                 size_t *nchars, size_t line_len)
{
	BcStatus s;
	size_t width, i;
	BcNumDigitOp print;
	bool neg = n->neg;

	if (neg && putchar('-') == EOF) return BC_STATUS_IO_ERR;
	(*nchars) += neg;

	n->neg = false;

	if (base_t <= BC_NUM_MAX_IBASE) {
		width = 1;
		print = bc_num_printHex;
	}
	else {
		for (i = base_t - 1, width = 0; i != 0; i /= 10, ++width);
		print = bc_num_printDigits;
	}

	s = bc_num_printNum(n, base, width, nchars, line_len, print);
	n->neg = neg;

	return s;
}

#ifdef DC_ENABLED
BcStatus bc_num_stream(BcNum *n, BcNum *base, size_t *nchars, size_t len) {
	return bc_num_printNum(n, base, 1, nchars, len, bc_num_printChar);
}
#endif // DC_ENABLED

BcStatus bc_num_init(BcNum *n, size_t request) {

	assert(n);

	request = request >= BC_NUM_DEF_SIZE ? request : BC_NUM_DEF_SIZE;
	memset(n, 0, sizeof(BcNum));
	if (!(n->num = malloc(request))) return BC_STATUS_ALLOC_ERR;

	n->cap = request;

	return BC_STATUS_SUCCESS;
}

BcStatus bc_num_expand(BcNum *n, size_t req) {

	BcDig *temp;

	assert(n);

	req = req >= BC_NUM_DEF_SIZE ? req : BC_NUM_DEF_SIZE;

	if (req <= n->cap) return BC_STATUS_SUCCESS;
	if (!(temp = realloc(n->num, req))) return BC_STATUS_ALLOC_ERR;

	n->num = temp;
	n->cap = req;

	return BC_STATUS_SUCCESS;
}

void bc_num_free(void *num) {
	BcNum *n = (BcNum*) num;
	assert(n);
	free(n->num);
}

BcStatus bc_num_copy(BcNum *d, BcNum *s) {

	BcStatus status;

	assert(d && s);

	if (d == s) return BC_STATUS_SUCCESS;
	if ((status = bc_num_expand(d, s->cap))) return status;

	d->len = s->len;
	d->neg = s->neg;
	d->rdx = s->rdx;

	memcpy(d->num, s->num, sizeof(BcDig) * d->len);

	return status;
}

BcStatus bc_num_parse(BcNum *n, const char *val, BcNum *base, size_t base_t) {

	BcStatus s;

	assert(n && val && base);
	assert(base_t >= BC_NUM_MIN_BASE && base_t <= BC_NUM_MAX_IBASE);

	if (!bc_num_strValid(val, base_t)) return BC_STATUS_MATH_BAD_STRING;

	if (base_t == 10) s = bc_num_parseDecimal(n, val);
	else s = bc_num_parseBase(n, val, base);

	return s;
}

BcStatus bc_num_print(BcNum *n, BcNum *base, size_t base_t, bool newline,
                      size_t *nchars, size_t line_len)
{
	BcStatus s;

	assert(n && base && nchars);
	assert(base_t >= BC_NUM_MIN_BASE && base_t <= BC_MAX_OBASE);

	if ((s = bc_num_printNewline(nchars, line_len))) return s;

	if (!n->len) {
		if (putchar('0') == EOF) return BC_STATUS_IO_ERR;
		++(*nchars);
	}
	else if (base_t == 10) s = bc_num_printDecimal(n, nchars, line_len);
	else s = bc_num_printBase(n, base, base_t, nchars, line_len);

	if (s) return s;

	if (newline) {
		if (putchar('\n') == EOF) return BC_STATUS_IO_ERR;
		*nchars = 0;
	}

	return s;
}

BcStatus bc_num_ulong(BcNum *n, unsigned long *result) {

	size_t i;
	unsigned long pow;

	assert(n && result);

	if (n->neg) return BC_STATUS_MATH_NEGATIVE;

	for (*result = 0, pow = 1, i = n->rdx; i < n->len; ++i) {

		unsigned long prev = *result, powprev = pow;

		*result += ((unsigned long) n->num[i]) * pow;
		pow *= 10;

		if (*result < prev || pow < powprev) return BC_STATUS_MATH_OVERFLOW;
	}

	return BC_STATUS_SUCCESS;
}

BcStatus bc_num_ulong2num(BcNum *n, unsigned long val) {

	BcStatus s;
	size_t len;
	BcDig *ptr;
	unsigned long i;

	assert(n);

	bc_num_zero(n);

	if (!val) return BC_STATUS_SUCCESS;

	for (len = 1, i = ULONG_MAX; i != 0; i /= 10, ++len)
	if ((s = bc_num_expand(n, len))) return s;
	for (ptr = n->num, i = 0; val; ++i, ++n->len, val /= 10) ptr[i] = val % 10;

	return BC_STATUS_SUCCESS;
}

BcStatus bc_num_add(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
	BcNumBinaryOp op = (!a->neg == !b->neg) ? bc_num_a : bc_num_s;
	(void) scale;
	return bc_num_binary(a, b, c, false, op, BC_NUM_AREQ(a, b));
}

BcStatus bc_num_sub(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
	BcNumBinaryOp op = (!a->neg == !b->neg) ? bc_num_s : bc_num_a;
	(void) scale;
	return bc_num_binary(a, b, c, true, op, BC_NUM_AREQ(a, b));
}

BcStatus bc_num_mul(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
	size_t req = BC_NUM_MREQ(a, b, scale);
	return bc_num_binary(a, b, c, scale, bc_num_m, req);
}

BcStatus bc_num_div(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
	size_t req = BC_NUM_MREQ(a, b, scale);
	return bc_num_binary(a, b, c, scale, bc_num_d, req);
}

BcStatus bc_num_mod(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
	size_t req = BC_NUM_MREQ(a, b, scale);
	return bc_num_binary(a, b, c, scale, bc_num_rem, req);
}

BcStatus bc_num_pow(BcNum *a, BcNum *b, BcNum *c, size_t scale) {
	return bc_num_binary(a, b, c, scale, bc_num_p, a->len * b->len + 1);
}

BcStatus bc_num_sqrt(BcNum *a, BcNum *restrict b, size_t scale) {

	BcStatus s;
	BcNum num1, num2, half, f, fprime, *x0, *x1, *temp;
	size_t pow, len, digits, digits1, resrdx, req, times = 0;
	ssize_t cmp = 1, cmp1 = SSIZE_MAX, cmp2 = SSIZE_MAX;

	assert(a && b && a != b);

	req = BC_MAX(scale, a->rdx) + ((BC_NUM_INT(a) + 1) >> 1) + 1;

	if ((s = bc_num_expand(b, req))) goto init_err;

	if (!a->len) {
		bc_num_setToZero(b, scale);
		goto init_err;
	}
	else if (a->neg) {
		s = BC_STATUS_MATH_NEGATIVE;
		goto init_err;
	}
	else if (BC_NUM_ONE(a)) {
		bc_num_one(b);
		s = bc_num_extend(b, scale);
		goto init_err;
	}

	len = a->len + (scale = BC_MAX(scale, a->rdx) + 1);

	if ((s = bc_num_init(&num1, len))) goto init_err;
	if ((s = bc_num_init(&num2, len))) goto num2_err;
	if ((s = bc_num_init(&half, BC_NUM_DEF_SIZE))) goto two_err;

	bc_num_one(&half);
	half.num[0] = 5;
	half.rdx = 1;

	if ((s = bc_num_init(&f, len))) goto f_err;
	if ((s = bc_num_init(&fprime, len))) goto fprime_err;

	x0 = &num1;
	x1 = &num2;

	bc_num_one(x0);

	if ((pow = BC_NUM_INT(a))) {

		if (pow & 1) x0->num[0] = 2;
		else x0->num[0] = 6;

		pow -= 2 - (pow & 1);

		if ((s = bc_num_extend(x0, pow))) goto err;

		// Make sure to move the radix back.
		x0->rdx -= pow;
	}

	x0->rdx = digits = digits1 = 0;
	resrdx = scale + 2;
	len = BC_NUM_INT(x0) + resrdx - 1;

	while (!bcg.signe && (cmp || digits < len)) {

		if ((s = bc_num_div(a, x0, &f, resrdx))) goto err;
		if ((s = bc_num_add(x0, &f, &fprime, resrdx))) goto err;
		if ((s = bc_num_mul(&fprime, &half, x1, resrdx))) goto err;

		cmp = bc_num_cmp(x1, x0);
		digits = x1->len - (unsigned long long) llabs(cmp);

		if (cmp == cmp2 && digits == digits1) times += 1;
		else times = 0;

		resrdx += times > 4;

		cmp2 = cmp1;
		cmp1 = cmp;
		digits1 = digits;

		temp = x0;
		x0 = x1;
		x1 = temp;
	}

	if (bcg.signe) {
		s = BC_STATUS_EXEC_SIGNAL;
		goto err;
	}

	if ((s = bc_num_copy(b, x0))) goto err;

	if (b->rdx > --scale) bc_num_truncate(b, b->rdx - scale);

err:
	bc_num_free(&fprime);
fprime_err:
	bc_num_free(&f);
f_err:
	bc_num_free(&half);
two_err:
	bc_num_free(&num2);
num2_err:
	bc_num_free(&num1);
init_err:
	assert(!b->neg || b->len);
	return s;
}

BcStatus bc_num_divmod(BcNum *a, BcNum *b, BcNum *c, BcNum *d, size_t scale) {

	BcStatus s;
	BcNum num2, *ptr_a;
	bool init;
	size_t ts = BC_MAX(scale + b->rdx, a->rdx), len = BC_NUM_MREQ(a, b, ts);

	assert(c != d && a != b && a != d && b != d && b != c);

	if ((init = (c == a))) {
		memcpy(&num2, c, sizeof(BcNum));
		ptr_a = &num2;
	}
	else ptr_a = a;

	if (init) s = bc_num_init(c, len);
	else s = bc_num_expand(d, ptr_a->len);

	if (s) return s;

	s = bc_num_r(ptr_a, b, c, d, scale, ts);

	assert(!c->neg || c->len);
	assert(!d->neg || d->len);

	if (init) bc_num_free(&num2);

	return s;
}

#ifdef DC_ENABLED
BcStatus bc_num_modexp(BcNum *a, BcNum *b, BcNum *c, BcNum *restrict d) {

	BcStatus s;
	BcNum base, exp, two, temp;

	assert(a && b && c && d && a != d && b != d && c != d);

	if ((s = bc_num_expand(d, c->len))) return s;

	if (!c->len) return BC_STATUS_MATH_DIVIDE_BY_ZERO;
	if (a->rdx || b->rdx || c->rdx) return BC_STATUS_MATH_NON_INTEGER;
	if (b->neg) return BC_STATUS_MATH_NEGATIVE;

	if ((s = bc_num_init(&base, c->len))) return s;
	if ((s = bc_num_init(&exp, b->len))) goto exp_err;
	if ((s = bc_num_init(&two, BC_NUM_DEF_SIZE))) goto two_err;
	if ((s = bc_num_init(&temp, b->len))) goto temp_err;

	bc_num_one(&two);
	two.num[0] = 2;
	bc_num_one(d);

	if ((s = bc_num_rem(a, c, &base, 0))) goto err;
	if ((s = bc_num_copy(&exp, b))) goto err;

	while (exp.len) {

		if ((s = bc_num_divmod(&exp, &two, &exp, &temp, 0))) goto err;

		if (BC_NUM_ONE(&temp)) {
			if ((s = bc_num_mul(d, &base, &temp, 0))) goto err;
			if ((s = bc_num_rem(&temp, c, d, 0))) goto err;
		}

		if ((s = bc_num_mul(&base, &base, &temp, 0))) goto err;
		if ((s = bc_num_rem(&temp, c, &base, 0))) goto err;
	}

err:
	bc_num_free(&temp);
temp_err:
	bc_num_free(&two);
two_err:
	bc_num_free(&exp);
exp_err:
	bc_num_free(&base);
	assert(!d->neg || d->len);
	return s;
}
#endif // DC_ENABLED
