/*	$OpenBSD: siphash.c,v 1.1 2014/11/04 03:01:14 dlg Exp $ */

/*-
 * Copyright (c) 2013 Andre Oppermann <andre@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * SipHash is a family of PRFs SipHash-c-d where the integer parameters c and d
 * are the number of compression rounds and the number of finalization rounds.
 * A compression round is identical to a finalization round and this round
 * function is called SipRound.  Given a 128-bit key k and a (possibly empty)
 * byte string m, SipHash-c-d returns a 64-bit value SipHash-c-d(k; m).
 *
 * Implemented from the paper "SipHash: a fast short-input PRF", 2012.09.18,
 * by Jean-Philippe Aumasson and Daniel J. Bernstein,
 * Permanent Document ID b9a943a805fbfc6fde808af9fc0ecdfa
 * https://131002.net/siphash/siphash.pdf
 * https://131002.net/siphash/
 */

#include <sys/param.h>
#ifdef __FreeBSD__
/*
 * The presence of this header breaks the build on FreeBSD 12.4.
 */
#if __FreeBSD_version >= 1300000
#include <sys/systm.h>
#endif /* __FreeBSD_version < 1300000 */
#endif


#ifdef __FreeBSD__
#include "siphash.h"
#include <sys/endian.h>
#include <string.h>
#else
#include <crypto/siphash.h>
#endif


void		SipHash_CRounds(SIPHASH_CTX *, int);
void		SipHash_Rounds(SIPHASH_CTX *, int);

void
SipHash_Init(SIPHASH_CTX *ctx, const SIPHASH_KEY *key)
{
	uint64_t k0, k1;

#ifdef __FreeBSD__
	k0 = le64toh(&key->k0);
	k1 = le64toh(&key->k1);
#else
	k0 = lemtoh64(&key->k0);
	k1 = lemtoh64(&key->k1);
#endif

	ctx->v[0] = 0x736f6d6570736575ULL ^ k0;
	ctx->v[1] = 0x646f72616e646f6dULL ^ k1;
	ctx->v[2] = 0x6c7967656e657261ULL ^ k0;
	ctx->v[3] = 0x7465646279746573ULL ^ k1;

	memset(ctx->buf, 0, sizeof(ctx->buf));
	ctx->bytes = 0;
}

void
SipHash_Update(SIPHASH_CTX *ctx, int rc, int rf, const void *src, size_t len)
{
	const u_int8_t *ptr = src;
	size_t free, used;

	if (len == 0)
		return;

	used = ctx->bytes % sizeof(ctx->buf);
	ctx->bytes += len;

	if (used > 0) {
		free = sizeof(ctx->buf) - used;

		if (len >= free) {
			memcpy(&ctx->buf[used], ptr, free);
			SipHash_CRounds(ctx, rc);
			len -= free;
			ptr += free;
		} else {
			memcpy(&ctx->buf[used], ptr, len);
			return;
		}
	}

	while (len >= sizeof(ctx->buf)) {
		memcpy(ctx->buf, ptr, sizeof(ctx->buf));
		SipHash_CRounds(ctx, rc);
		len -= sizeof(ctx->buf);
		ptr += sizeof(ctx->buf);
	}

	if (len > 0)
		memcpy(&ctx->buf[used], ptr, len);
}

void
SipHash_Final(void *dst, SIPHASH_CTX *ctx, int rc, int rf)
{
	u_int64_t r;

	r = SipHash_End(ctx, rc, rf);

#ifdef __FreeBSD__
	dst = (u_int64_t *)htole64(r);
#else
	htolem64((u_int64_t *)dst, r);
#endif
}

u_int64_t
SipHash_End(SIPHASH_CTX *ctx, int rc, int rf)
{
	u_int64_t r;
	size_t free, used;

	used = ctx->bytes % sizeof(ctx->buf);
	free = sizeof(ctx->buf) - used;
	memset(&ctx->buf[used], 0, free - 1);
	ctx->buf[7] = ctx->bytes;

	SipHash_CRounds(ctx, rc);
	ctx->v[2] ^= 0xff;
	SipHash_Rounds(ctx, rf);

	r = (ctx->v[0] ^ ctx->v[1]) ^ (ctx->v[2] ^ ctx->v[3]);
	explicit_bzero(ctx, sizeof(*ctx));
	return (r);
}

u_int64_t
SipHash(const SIPHASH_KEY *key, int rc, int rf, const void *src, size_t len)
{
	SIPHASH_CTX ctx;

	SipHash_Init(&ctx, key);
	SipHash_Update(&ctx, rc, rf, src, len);
	return (SipHash_End(&ctx, rc, rf));
}

#define SIP_ROTL(x, b) ((x) << (b)) | ( (x) >> (64 - (b)))

void
SipHash_Rounds(SIPHASH_CTX *ctx, int rounds)
{
	while (rounds--) {
		ctx->v[0] += ctx->v[1];
		ctx->v[2] += ctx->v[3];
		ctx->v[1] = SIP_ROTL(ctx->v[1], 13);
		ctx->v[3] = SIP_ROTL(ctx->v[3], 16);

		ctx->v[1] ^= ctx->v[0];
		ctx->v[3] ^= ctx->v[2];
		ctx->v[0] = SIP_ROTL(ctx->v[0], 32);

		ctx->v[2] += ctx->v[1];
		ctx->v[0] += ctx->v[3];
		ctx->v[1] = SIP_ROTL(ctx->v[1], 17);
		ctx->v[3] = SIP_ROTL(ctx->v[3], 21);

		ctx->v[1] ^= ctx->v[2];
		ctx->v[3] ^= ctx->v[0];
		ctx->v[2] = SIP_ROTL(ctx->v[2], 32);
	}
}

void
SipHash_CRounds(SIPHASH_CTX *ctx, int rounds)
{
#ifdef __FreeBSD__
	u_int64_t m = le64toh((u_int64_t *)ctx->buf);
#else
	u_int64_t m = lemtoh64((u_int64_t *)ctx->buf);
#endif

	ctx->v[3] ^= m;
	SipHash_Rounds(ctx, rounds);
	ctx->v[0] ^= m;
}
