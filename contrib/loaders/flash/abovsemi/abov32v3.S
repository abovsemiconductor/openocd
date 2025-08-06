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
	 * r0 - data length
	 * r1 - working memory start address
	 * r2 - working memory end address
	 * r3 - flash register base address
	 * r4 - destination flash address
	 * r5 - flash register status offset
	 * Clobbered:
	 * r6 - rp
	 * r7 - wp, tmp
	 * r8 - tmp
	 */

/* flash register offset */
#define FLASH_STAT_REG			0x00000018

/* flash status reg bit */
#define FLASH_BUSY_FLAG			0x00000001

	.thumb_func
	.global _start

_start:
	cpsid	i
wait_fifo:
	ldr		r7, [r1, #0]	/* read wp */
	cmp		r7, #0			/* abort if wp == 0 */
	beq		exit
	ldr		r6, [r1, #4]	/* read rp */
	cmp		r6, r7			/* wait until rp != wp */
	beq		wait_fifo

	/* write 4 bytes data to data register */
	ldr		r8, [r6]
	str		r8, [r4]

busy:
	ldr		r7, [r3, r5]
	movs	r8, #FLASH_BUSY_FLAG
	ands	r7, r8
	cmp		r7, #FLASH_BUSY_FLAG
	beq		busy

	adds	r6, #4			/* increment 4 bytes address */
	adds	r4, #4			/* increment 4 bytes flash address */

	cmp		r6, r2			/* wrap rp at end of work area buffer */
	bcc		no_wrap
	mov		r6, r1
	adds	r6, #8			/* skip rp,wp at start of work area */
no_wrap:
	str		r6, [r1, #4]	/* write back rp */
	subs	r0, #4			/* decrement 4 bytes count */
	bne		wait_fifo		/* loop if not done */
exit:
	cpsie	i
	bkpt	#0
