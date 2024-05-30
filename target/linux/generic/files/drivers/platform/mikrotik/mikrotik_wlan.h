/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Thibaut VARÈNE <hacks+kernel@slashdirt.org>
 */

#ifndef __MIKROTIK_WLAN_H__
#define __MIKROTIK_WLAN_H__

#include <linux/errno.h>

#ifdef CONFIG_MIKROTIK_WLAN_DECOMPRESS

/*
 * mikrotik_wlan_lzor_prefixed_decompress() - Mirotik WLAN BDF LZO decode,
 * using known LZO prefix.
 * @inbuf: input buffer to decode
 * @inlen: size of in
 * @outbuf: output buffer to write decoded data to
 * @outlen: pointer to out size when function is called, will be updated with
 * size of decoded output on return
 * Return: lzo1x_decompress_safe return code. LZO_E_INPUT_NOT_CONSUMED is acceptable as
 * input data is padded to be byte aligned.
 */
int mikrotik_wlan_lzor_prefixed_decompress(const u8 *inbuf, size_t inlen,
					   void *outbuf, size_t *outlen);

/**
 * mikrotik_wlan_rle_decompress() - Simple RLE (MikroTik variant) decoding routine.
 * @in: input buffer to decode
 * @inlen: size of in
 * @out: output buffer to write decoded data to
 * @outlen: pointer to out size when function is called, will be updated with
 * size of decoded output on return
 *
 * MikroTik's variant of RLE operates as follows, considering a signed run byte:
 * - positive run => classic RLE
 * - negative run => the next -<run> bytes must be copied verbatim
 * The API is matched to the lzo1x routines for convenience.
 *
 * NB: The output buffer cannot overlap with the input buffer.
 *
 * Return: 0 on success or errno
 */
int mikrotik_wlan_rle_decompress(const u8 *in, size_t inlen,
				 u8 *out, size_t *outlen);

#else /* CONFIG_MIKROTIK_WLAN_DECODE */

static inline int mikrotik_wlan_lzor_prefixed_decompress(const u8 *inbuf,
							 size_t inlen,
							 void *outbuf,
							 size_t *outlen)
{
	return -EOPNOTSUPP;
}

static inline int mikrotik_wlan_rle_decompress(const u8 *in, size_t inlen,
					       u8 *out, size_t *outlen)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_MIKROTIK_WLAN_DECOMPRESS */

#ifdef CONFIG_MIKROTIK_WLAN_DECOMPRESS_LZ77
/**
 * mikrotik_wlan_lz77_decompress
 *
 * @in:			compressed data ptr
 * @in_len:		length of compressed data
 * @out:		buffer ptr to decompress into
 * @out_len:		length of decompressed buffer in input,
 *			length of decompressed data in success
 *
 * Returns 0 on success, or negative error
 */
int mikrotik_wlan_lz77_decompress(const unsigned char *in,
				  size_t in_len,
				  unsigned char *out,
				  size_t *out_len);

#else /* CONFIG_MIKROTIK_WLAN_DECOMPRESS_LZ77 */

static inline int mikrotik_wlan_lz77_decompress(const unsigned char *in,
						size_t in_len,
						unsigned char *out,
						size_t *out_len)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_MIKROTIK_WLAN_DECOMPRESS_LZ77 */
#endif /* __MIKROTIK_WLAN_H__ */
