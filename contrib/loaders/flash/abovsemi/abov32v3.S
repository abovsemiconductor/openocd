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
	 * Clobbered:
	 * r5 - rp
	 * r6 - wp, tmp
	 * r7 - tmp
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
	ldr		r6, [r1, #0]	/* read wp */
	cmp		r6, #0			/* abort if wp == 0 */
	beq		exit
	ldr		r5, [r1, #4]	/* read rp */
	cmp		r5, r6			/* wait until rp != wp */
	beq		wait_fifo

	/* write 4 bytes data to data register */
	ldr		r7, [r5]
	str		r7, [r4]

busy:
	ldr		r6, [r3, #FLASH_STAT_REG]
	movs	r7, #FLASH_BUSY_FLAG
	ands	r6, r7
	cmp		r6, #FLASH_BUSY_FLAG
	beq		busy

	adds	r5, #4			/* increment 4 bytes address */
    adds    r4, #4			/* increment 4 bytes flash address */

	cmp		r5, r2			/* wrap rp at end of work area buffer */
	bcc		no_wrap
	mov		r5, r1
	adds	r5, #8			/* skip rp,wp at start of work area */
no_wrap:
	str		r5, [r1, #4]	/* write back rp */
	subs	r0, #4			/* decrement 4 bytes count */
	bne		wait_fifo		/* loop if not done */
exit:
	cpsie	i
	bkpt	#0
