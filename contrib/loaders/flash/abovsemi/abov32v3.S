/* SPDX-License-Identifier: GPL-2.0-or-later */
/***************************************************************************
 *   Copyright (C) 2023 by HyunSeok Choi                                   *
 *   ABOVSemiconductor R&D Division                                        *
 *   Derived from cortex-m0.S:                                             *
 ***************************************************************************/
	.text
	.syntax unified
	.cpu cortex-m4
	.thumb
	.thumb_func

	/* Params:
	 * r0 - data length (bytes, multiple of 4, > 0)
	 * r1 - source data address (RAM)
	 * r3 - flash status register address
	 * r4 - destination flash address
	 * Clobbered:
	 * r6, r7 - tmp
	 */

/* flash status reg bit */
#define FLASH_BUSY_FLAG			0x00000001

	.thumb_func
	.global _start

_start:
	cpsid	i
write_word:
	ldr		r7, [r1]		/* read 4 bytes data */
	str		r7, [r4]		/* write 4 bytes data to flash */

busy:
	ldr		r6, [r3]
	movs	r7, #FLASH_BUSY_FLAG
	ands	r6, r7
	cmp		r6, #FLASH_BUSY_FLAG
	beq		busy

	adds	r1, #4			/* increment source address */
	adds	r4, #4			/* increment flash address */
	subs	r0, #4			/* decrement 4 bytes count */
	bne		write_word
	cpsie	i
	bkpt	#0
