/* SPDX-License-Identifier: GPL-2.0-or-later */
/***************************************************************************
 *   Copyright (C) 2023 by HyunSeok Choi                                   *
 *   ABOVSemiconductor R&D Division                                        *
 *   Derived from cortex-m0.S:                                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/
	.text
	.syntax unified
	.cpu cortex-m0
	.thumb
	.thumb_func

	/* Params:
	 * r0 - data length
	 * r1 - working memory start address
	 * r2 - working memory end address
	 * r3 - flash register base address
	 * Clobbered:
	 * r4 - rp
	 * r5 - wp, tmp
	 * r6 - tmp
	 */

/* flash register offset */
#define FLASH_CR_REG		0x00000008
#define FLASH_DR_REG		0x00000010
#define FLASH_WBUSY_REG		0x00000018

/* flash control bits */
#define FLASH_CR_WADCK          0x00000008
#define FLASH_CR_HVEN		0x00000001

#define FLASH_BUSY_FLAG		0x00000001

	.thumb_func
	.global _start

_start:
	cpsid	i
wait_fifo:
	ldr 	r5, [r1, #0]	/* read wp */
	cmp 	r5, #0	        /* abort if wp == 0 */
	beq 	exit
	ldr 	r4, [r1, #4]	/* read rp */
	cmp 	r4, r5		/* wait until rp != wp */
	beq 	wait_fifo

	/* write 4 bytes data to data register */
	ldr	r6, [r4]
	str	r6, [r3, #FLASH_DR_REG]

	/* enable latch signal bit */
	movs	r6, #FLASH_CR_WADCK
	ldr	r5, [r3, #FLASH_CR_REG]
	orrs	r5, r6
	str	r5, [r3, #FLASH_CR_REG]

	/* enable high voltage bit */
	movs	r6, #FLASH_CR_HVEN
	ldr	r5, [r3, #FLASH_CR_REG]
	orrs	r5, r6
	str	r5, [r3, #FLASH_CR_REG]

busy:
	ldr	r5, [r3, #FLASH_WBUSY_REG]
	movs	r7, #FLASH_BUSY_FLAG
	ands	r5, r7
	cmp	r5, #FLASH_BUSY_FLAG
	beq	busy

	/* disable high voltage bit */
	movs	r6, #FLASH_CR_HVEN
	ldr	r5, [r3, #FLASH_CR_REG]
	bics	r5, r6
	str	r5, [r3, #FLASH_CR_REG]

	adds	r4, #4		/* increment 4 bytes address */

        cmp 	r4, r2		/* wrap rp at end of work area buffer */
	bcc	no_wrap
	mov	r4, r1
	adds	r4, #8          /* skip rp,wp at start of work area */
no_wrap:
	str 	r4, [r1, #4]	/* write back rp */
	subs	r0, #4          /* decrement 4 bytes count */
	bne     wait_fifo	/* loop if not done */
exit:
	cpsie	i
	bkpt    #0
