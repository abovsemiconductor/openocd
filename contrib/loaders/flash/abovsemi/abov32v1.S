/* SPDX-License-Identifier: GPL-2.0-or-later */
/***************************************************************************
 *   Copyright (C) 2023 by HyunSeok Choi                                   *
 *   ABOVSemiconductor R&D Division                                        *
 *   Derived from cortex-m0.S:                                             *
 ***************************************************************************/
	.text
	.syntax unified
	.cpu cortex-m3
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
#define FLASH_CODE_CTRL_REG		0x00000004
#define FLASH_CODE_DATA_REG		0x00000008
#define FLASH_CODE_ADDR_REG		0x00000010

/* flash control bits */
#define FLASH_CODE_CTRL_AE		0x00002000
#define FLASH_CODE_CTRL_NVSTR	0x00000080

#define FLASH_CODE_CTRL_BUSY	0x00000100

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

	/* write destination addr */
	str		r4, [r3, #FLASH_CODE_ADDR_REG]

	/* write data */
	ldr		r6, [r5]
	str		r6, [r3, #FLASH_CODE_DATA_REG]

	/* enable latch signal bit */
	movs	r6, #FLASH_CODE_CTRL_AE
	ldr		r7, [r3, #FLASH_CODE_CTRL_REG]
	orrs	r7, r6
	str		r7, [r3, #FLASH_CODE_CTRL_REG]

	/* enable high voltage bit */
	movs	r6, #FLASH_CODE_CTRL_NVSTR
	ldr		r7, [r3, #FLASH_CODE_CTRL_REG]
	orrs	r7, r6
	str		r7, [r3, #FLASH_CODE_CTRL_REG]

busy:
	movs	r6, #FLASH_CODE_CTRL_BUSY
	ldr		r7, [r3, #FLASH_CODE_CTRL_REG]
	ands	r7, r6
	cmp		r7, #FLASH_CODE_CTRL_BUSY
	beq		busy

	/* disable high voltage bit */
	movs	r6, #FLASH_CODE_CTRL_NVSTR
	ldr		r7, [r3, #FLASH_CODE_CTRL_REG]
	bics	r7, r6
	str		r7, [r3, #FLASH_CODE_CTRL_REG]

	/* disable latch signal bit */
	movs	r6, #FLASH_CODE_CTRL_AE
	ldr		r7, [r3, #FLASH_CODE_CTRL_REG]
	bics	r7, r6
	str		r7, [r3, #FLASH_CODE_CTRL_REG]

	adds	r5, #4			/* increment 4 bytes address */
	adds	r4, #4			/* increment 4 bytes address */

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
