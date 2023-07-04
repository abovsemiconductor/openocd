// SPDX-License-Identifier: BSD-3-Clause
/**
 *******************************************************************************
 * @file        abov32v2.c
 * @author      ABOVSemiconductor R&D Division
 * @brief       Nor Flash Driver for ABOV32 V2
 *
 * Copyright 2023 ABOVSemiconductor Co.,Ltd. All rights reserved.
 *
 * This file is licensed under terms that are found in the LICENSE file
 * located at Document directory.
 * If this file is delivered or shared without applicable license terms,
 * the terms of the BSD-3-Clause license shall be applied.
 * Reference: https://opensource.org/licenses/BSD-3-Clause
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/binarybuffer.h>
#include <target/algorithm.h>
#include <target/armv7m.h>

//#define DEBUG_LOG

#define ABOV_CHIP_ID_REG		0x4000F004

#define A31G31X_FLASH_128KB		0x1A003
#define A31G31X_FLASH_64KB		0x1A004
#define A31G31X_FLASH_256KB		0x1A005
#define A31G31X_FLASH_CODE_BASE_ADDR	0x00000000
#define A31G31X_FLASH_REG_ADDR		0x40000100

#define A31G32X_FLASH_128KB		0x1A006
#define A31G32X_FLASH_64KB		0x1A007
#define A31G32X_FLASH_CODE_BASE_ADDR	0x00000000
#define A31G32X_FLASH_REG_ADDR		0x40000100

#define A31G21X_FLASH_64KB		0x1A008
#define A31G21X_FLASH_32KB		0x1A009
#define A31G21X_FLASH_CODE_BASE_ADDR	0x00000000
#define A31G21X_FLASH_REG_ADDR		0x40000100

#define A31G22X_FLASH_256KB		0x1A00A
#define A31G22X_FLASH_128KB		0x1A00B

#define A34M41X_FLASH_512KB		0x4A000
#define A34M41X_FLASH_256KB		0x4A001
#define A34M41X_FLASH_128KB		0x4A002

#define A33G52X_FLASH_384KB		0x3A000
#define A33G52X_FLASH_256KB		0x3A001
#define A33G52X_FLASH_128KB		0x3A002

#define A33M11X_FLASH_256KB		0x3A004
#define A33M11X_FLASH_128KB		0x3A005

#define ABOV_CHIP_ID_MASK		0xFFFFF

/* Flash Mode */
#define FLASH_PE_MODE_CMD1		0x0000005A
#define FLASH_PE_MODE_CMD2		0x000000A5
#define FLASH_TRIM_MODE_CMD1		0x000000A5
#define FLASH_TRIM_MODE_CMD2		0x0000005A
#define FLASH_AMBA_MODE_CMD1		0x00000081
#define FLASH_AMBA_MODE_CMD2		0x00000028
#define FLASH_PROT_MODE_CMD1		0x00000066
#define FLASH_PROT_MODE_CMD2		0x00000099

/* Flash Reg Offset */
#define FLASH_REG_START_ADDR		0x40000100
#define FLASH_MODE_REG			(FLASH_REG_START_ADDR | 0x00000004)
#define FLASH_CR_REG			(FLASH_REG_START_ADDR | 0x00000008)
#define FLASH_WPROT_REG			(FLASH_REG_START_ADDR | 0x00000034)
#define FLASH_ADDR_REG			(FLASH_REG_START_ADDR | 0x0000000C)
#define FLASH_WBUSY_REG			(FLASH_REG_START_ADDR | 0x00000018)
#define FLASH_ABWPROT_REG		(FLASH_REG_START_ADDR | 0x00000050)
#define FLASH_NBWPROT_REG		(FLASH_REG_START_ADDR | 0x00000054)

/* Flash Control Reg bit position */
#define FLASH_CR_HVEN			0
#define FLASH_CR_ERASE			1
#define FLASH_CR_PROGRAM		2
#define FLASH_CR_WADCK			3
#define FLASH_CR_ADDR_LATCH		4
#define FLASH_CR_ERASE_1KB		5
#define FLASH_CR_ERASE_4KB		6
#define FLASH_CR_ERASE_CHIP		7
#define FLASH_CR_PROT_BOOT_BLOCK	8
#define FLASH_CR_SELF_PROGRAM		23

#define FLASH_1KB_SECTOR_BY_PAGE_COUNT	2
#define FLASH_4KB_SECTOR_BY_PAGE_COUNT	8
#define FLASH_1KB_SECTOR_IDX_MASK	0x1
#define FLASH_4KB_SECTOR_IDX_MASK	0x7
#define FLASH_PROT_LOCK			0x00000001
#define FLASH_PROT_UNLOCK		0x00000000
#define FLASH_ENABLE			0x00000001
#define FLASH_DISABLE			0x00000000
#define FLASH_ERASE_TIMEOUT		100
#define FLASH_WORD_ALIGN                4

/* Flash Active/Non-active write protection disable */
#define FLASH_ABWPROT_DISABLE           0xAAB00000
#define FLASH_NBWPROT_DISABLE		0x55B00000

/* Watchdog Reg */
#define WATCHDOG_CR_REG			0x40001A00
#define WATCHDOG_DISABLE                0x5A6995A0

/* System Clock Reg addr */
#define SYSTEM_CLKSRC_REG		0x40000040
#define SYSTEM_CLKCR_REG                0x40000044

#define SYSTEM_CLKSRC_HSI		0xA5070880
#define SYSTEM_CLKSEL_HSI               0x57040002

typedef enum {
	erase_mode_page,
	erase_mode_1kb,
	erase_mode_4kb,
	erase_mode_chip,
	erase_mode_max
} erase_mode;

struct abov32v2_flash_bank {
	bool probed;

	uint32_t code_size;
	uint32_t system_size;
	uint32_t page_size;
	uint32_t wprot_seg_size;
	bool abwprot;			/* active bootloader write protection */
};

static int abov32v2_wait_busy(struct flash_bank *bank, int timeout)
{
	struct target *target = bank->target;
	uint32_t busy = 0;
	int tout = timeout;
	int ret = ERROR_OK;

	while (1) {
		ret = target_read_u32(target, FLASH_WBUSY_REG, &busy);
		if (!(busy & 0x1))
			break;

		if (tout-- <= 0) {
			ret = ERROR_FAIL;
			break;
		}
	}

	return ret;
}

static int abov32v2_set_reg(struct flash_bank *bank, uint32_t addr, uint32_t bit, uint32_t val)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t reg = 0;

	ret = target_read_u32(target, addr, &reg);
	if (ret != ERROR_OK)
		goto err;
#if defined DEBUG_LOG
	LOG_INFO("old reg:0x%08x, val:0x%08x", addr, reg);
#endif

	reg &= ~(val << bit);
	reg |= (val << bit);

#if defined DEBUG_LOG
	LOG_INFO("new reg:0x%08x, val:0x%08x", addr, reg);
#endif
	ret = target_write_u32(target, addr, reg);
err:
	return ret;
}

static int abov32v2_get_chipset_id(struct flash_bank *bank, uint32_t *chipset_id)
{
	struct target *target = bank->target;
	int ret = target_read_u32(target, ABOV_CHIP_ID_REG, chipset_id);
	return ret;
}

static int abov32v2_get_geometry(struct flash_bank *bank, uint32_t chipset_id)
{
	struct abov32v2_flash_bank *abov32v2_info = bank->driver_priv;
	int ret = ERROR_OK;

	switch (chipset_id & ABOV_CHIP_ID_MASK) {
		case A31G31X_FLASH_128KB:
			abov32v2_info->code_size = 0x20000;
			abov32v2_info->system_size = 0x600;
			abov32v2_info->wprot_seg_size = 0x4000;
			abov32v2_info->page_size = 0x200;
			break;
		case A31G31X_FLASH_64KB:
			abov32v2_info->code_size = 0x10000;
			abov32v2_info->system_size = 0x600;
			abov32v2_info->wprot_seg_size = 0x800;
			abov32v2_info->page_size = 0x200;
			break;
		case A31G31X_FLASH_256KB:
			abov32v2_info->code_size = 0x40000;
			abov32v2_info->system_size = 0x600;
			abov32v2_info->wprot_seg_size = 0x4000;
			abov32v2_info->page_size = 0x200;
			break;
		case A31G32X_FLASH_128KB:
			abov32v2_info->code_size = 0x20000;
			abov32v2_info->system_size = 0x600;
			abov32v2_info->wprot_seg_size = 0x1000;
			abov32v2_info->page_size = 0x200;
			break;
		case A31G32X_FLASH_64KB:
			abov32v2_info->code_size = 0x10000;
			abov32v2_info->system_size = 0x600;
			abov32v2_info->wprot_seg_size = 0x1000;
			abov32v2_info->page_size = 0x200;
			break;
		case A31G21X_FLASH_64KB:
			abov32v2_info->code_size = 0x10000;
			abov32v2_info->system_size = 0x600;
			abov32v2_info->wprot_seg_size = 0x800;
			abov32v2_info->page_size = 0x200;
			break;
		case A31G21X_FLASH_32KB:
			abov32v2_info->code_size = 0x8000;
			abov32v2_info->system_size = 0x600;
			abov32v2_info->wprot_seg_size = 0x800;
			abov32v2_info->page_size = 0x200;
			break;
		case A31G22X_FLASH_256KB:
			abov32v2_info->code_size = 0x40000;
			abov32v2_info->system_size = 0x0;
			abov32v2_info->wprot_seg_size = 0x4000;
			abov32v2_info->page_size = 0x200;
			abov32v2_info->abwprot = true;
			break;
		case A31G22X_FLASH_128KB:
			abov32v2_info->code_size = 0x20000;
			abov32v2_info->system_size = 0x0;
			abov32v2_info->wprot_seg_size = 0x4000;
			abov32v2_info->page_size = 0x200;
			abov32v2_info->abwprot = true;
			break;
		default:
			ret = ERROR_FLASH_BANK_NOT_PROBED;
			break;
	}
	return ret;
}

static int abov32v2_protect_check(struct flash_bank *bank)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t reg = 0;

	/* read protection bits */
	ret = target_read_u32(target, FLASH_WPROT_REG, &reg);
	if (ret != ERROR_OK)
		goto err;

#if defined DEBUG_LOG
	LOG_INFO("cur wprotect:0x%08x, wprotect blocks:%d\n", reg, bank->num_prot_blocks);
#endif
	for (uint32_t i = 0; i < bank->num_prot_blocks; i++) {
		bank->prot_blocks[i].is_protected = (reg & (FLASH_PROT_LOCK << i)) ? FLASH_PROT_LOCK : FLASH_PROT_UNLOCK;
#if defined DEBUG_LOG
		LOG_INFO("%d block is %s", i, (bank->prot_blocks[i].is_protected == 1) ? "lock" : "unlock");
#endif
	}
err:
	return ret;
}

static int abov32v2_set_write_protect(struct flash_bank *bank, int set, uint32_t first, uint32_t last)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t reg = 0, prot = 0;

	/* protection mode */
	target_write_u32(target, FLASH_MODE_REG, FLASH_PROT_MODE_CMD1);
	target_write_u32(target, FLASH_MODE_REG, FLASH_PROT_MODE_CMD2);

	/* read protection bits */
	ret = target_read_u32(target, FLASH_WPROT_REG, &reg);
	if (ret != ERROR_OK)
		goto err;

	for (uint32_t i = first; i <= last; i++)
		prot |= (FLASH_PROT_LOCK << i);

#if defined DEBUG_LOG
	LOG_INFO("cur write protection:0x%08x, protection bits:0x%08x", reg, prot);
#endif

	if (set)
		reg |= prot;
	else
		reg &= ~prot;

	/* write protection bits */
	target_write_u32(target, FLASH_WPROT_REG, reg);

	/* clear mode  */
	target_write_u32(target, FLASH_MODE_REG, 0);
	target_write_u32(target, FLASH_MODE_REG, 0);

#if defined DEBUG_LOG
	(void)target_read_u32(target, FLASH_WPROT_REG, &reg);
	LOG_INFO("new write protection:0x%08x", reg);
#endif

	/* update protection bits in a bank */
	ret = abov32v2_protect_check(bank);
err:
	return ret;
}

static int abov32v2_set_erase(struct flash_bank *bank, uint32_t pageaddr, erase_mode mode)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t addr = 0;

	/* erase mode */
	target_write_u32(target, FLASH_MODE_REG, FLASH_PE_MODE_CMD1);
	target_write_u32(target, FLASH_MODE_REG, FLASH_PE_MODE_CMD2);

	/* get erase address */
	addr = pageaddr >> 2;
	ret = target_write_u32(target, FLASH_ADDR_REG, addr);
	if (ret != ERROR_OK)
		goto err;

	/* set pmode */
	ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_ADDR_LATCH, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* select erase area mode */
	switch (mode) {
		case erase_mode_chip:
			ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_ERASE_CHIP, FLASH_ENABLE);
			break;
		case erase_mode_1kb:
			ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_ERASE_1KB, FLASH_ENABLE);
			break;
		case erase_mode_4kb:
			ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_ERASE_4KB, FLASH_ENABLE);
			break;
		default:
			/* page erase mode */
			break;
	}

	if (ret != ERROR_OK)
		goto err;

	/* signaling erase to bus */
	ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_ERASE, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* latch signals */
	ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_WADCK, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* enable high voltage */
	ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_HVEN, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* wait a write is done */
	ret = abov32v2_wait_busy(bank, FLASH_ERASE_TIMEOUT);
	if (ret != ERROR_OK)
		goto err;

	/* disable high voltage */
	ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_HVEN, FLASH_DISABLE);
	if (ret != ERROR_OK)
		goto err;

	ret = target_write_u32(target, FLASH_WBUSY_REG, 0x80);

err:
	/* clear control reg */
	target_write_u32(target, FLASH_CR_REG, 0);

	/* clear mode  */
	target_write_u32(target, FLASH_MODE_REG, 0);
	target_write_u32(target, FLASH_MODE_REG, 0);

	return ret;
}

FLASH_BANK_COMMAND_HANDLER(abov32v2_flash_bank_command)
{
	/* Need syntax count from flash bank command */
	if (CMD_ARGC <  6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct abov32v2_flash_bank *abov32v2_info;
	abov32v2_info = malloc(sizeof(struct abov32v2_flash_bank));

	bank->driver_priv = abov32v2_info;
	abov32v2_info->probed = false;

	return ERROR_OK;
}

static int abov32v2_erase(struct flash_bank *bank, unsigned int first,
		unsigned int last)
{
	int ret = ERROR_OK;
	uint32_t sec_cnt = 0;
	uint32_t sec_offset = first;

#if defined DEBUG_LOG
	LOG_INFO("erase first=%d last=%d", first, last);
#endif

	abov32v2_set_write_protect(bank, FLASH_PROT_UNLOCK, first, last);

	sec_cnt = (last - first) + 1;
	if (sec_cnt == bank->num_sectors) {
		ret = abov32v2_set_erase(bank, bank->sectors[0].offset, erase_mode_chip);
	} else {
		while (sec_cnt != 0) {
			if ((sec_offset & FLASH_1KB_SECTOR_IDX_MASK) != 0x0 || sec_cnt == 1) {
#if defined DEBUG_LOG
				LOG_INFO("page erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v2_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_page);
				if (ret != ERROR_OK)
					break;

				sec_offset++;
				sec_cnt--;
			} else if ((sec_offset & FLASH_4KB_SECTOR_IDX_MASK) != 0x0
					|| sec_cnt < FLASH_4KB_SECTOR_BY_PAGE_COUNT) {
#if defined DEBUG_LOG
				LOG_INFO("1KB erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v2_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_1kb);
				if (ret != ERROR_OK)
					break;

				sec_offset += FLASH_1KB_SECTOR_BY_PAGE_COUNT;
				sec_cnt -= FLASH_1KB_SECTOR_BY_PAGE_COUNT;
			} else if (sec_cnt >= FLASH_4KB_SECTOR_BY_PAGE_COUNT) {
#if defined DEBUG_LOG
				LOG_INFO("4KB erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v2_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_4kb);
				if (ret != ERROR_OK)
					break;

				sec_offset += FLASH_4KB_SECTOR_BY_PAGE_COUNT;
				sec_cnt -= FLASH_4KB_SECTOR_BY_PAGE_COUNT;
			}
		}
	}

	if (ret == ERROR_OK) {
		for (uint32_t i = first; i <= last; i++)
			bank->sectors[i].is_erased = 1;
	}

	return ret;
}

static int abov32v2_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last)
{
	return abov32v2_set_write_protect(bank, set, first, last);
}

static int abov32v2_set_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	struct armv7m_algorithm armv7m_method;
	struct working_area *algobuffer, *imgbuffer;
	struct reg_param reg_params[4];

	int ret = ERROR_OK;
	uint32_t addr = 0;
	uint32_t bufsize = 1024;

	static const uint8_t abov32v2_algo_code[] = {
#include "../../../contrib/loaders/flash/abovsemi/abov32v2.inc"
	};

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* stop watchdog */
	target_write_u32(target, WATCHDOG_CR_REG, WATCHDOG_DISABLE);

	LOG_INFO("Writing buffer to flash address=0x%08x count=%d bytes", offset, count);

	/* program mode */
	target_write_u32(target, FLASH_MODE_REG, FLASH_PE_MODE_CMD1);
	target_write_u32(target, FLASH_MODE_REG, FLASH_PE_MODE_CMD2);

	/* set flash address */
	addr = (bank->base + offset) >> 2;
	ret = target_write_u32(target, FLASH_ADDR_REG, addr);
	if (ret != ERROR_OK)
		goto err;

	/* set pmode */
	ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_ADDR_LATCH, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* signaling erase command to bus */
	ret = abov32v2_set_reg(bank, FLASH_CR_REG, FLASH_CR_PROGRAM, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* alloc algo code area in MCU memory */
	ret = target_alloc_working_area(target, sizeof(abov32v2_algo_code), &algobuffer);
	if (ret != ERROR_OK)
		goto err;

	/* upload algo code into memory */
	ret = target_write_buffer(target, algobuffer->address, sizeof(abov32v2_algo_code), abov32v2_algo_code);
	if (ret != ERROR_OK) {
		target_free_working_area(target, algobuffer);
		goto err;
	}

	ret = target_alloc_working_area(target, bufsize, &imgbuffer);
	if (ret != ERROR_OK) {
		target_free_working_area(target, algobuffer);
		goto err;
	}

	armv7m_method.common_magic = ARMV7M_COMMON_MAGIC;
	armv7m_method.core_mode = ARM_MODE_THREAD;

	init_reg_param(&reg_params[0], "r0", 32, PARAM_OUT);	/* data length */
	init_reg_param(&reg_params[1], "r1", 32, PARAM_OUT);	/* working memory start address */
	init_reg_param(&reg_params[2], "r2", 32, PARAM_OUT);	/* working memory end address */
	init_reg_param(&reg_params[3], "r3", 32, PARAM_IN_OUT);	/* flash register start address */

	buf_set_u32(reg_params[0].value, 0, 32, count);		/* algo code decrement 4 count */
	buf_set_u32(reg_params[1].value, 0, 32, imgbuffer->address);
	buf_set_u32(reg_params[2].value, 0, 32, imgbuffer->address + imgbuffer->size);
	buf_set_u32(reg_params[3].value, 0, 32, FLASH_REG_START_ADDR);

	ret = target_run_flash_async_algorithm(target, buffer, (count / FLASH_WORD_ALIGN), FLASH_WORD_ALIGN, 0, NULL,
			4, reg_params, imgbuffer->address, imgbuffer->size, algobuffer->address, 0, &armv7m_method);

	target_free_working_area(target, imgbuffer);
	target_free_working_area(target, algobuffer);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);
	destroy_reg_param(&reg_params[2]);
	destroy_reg_param(&reg_params[3]);

err:
	/* clear control reg */
	target_write_u32(target, FLASH_CR_REG, 0);

	/* clear mode  */
	target_write_u32(target, FLASH_MODE_REG, 0);
	target_write_u32(target, FLASH_MODE_REG, 0);

	return ret;
}

static int abov32v2_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint8_t *align_buf = NULL;
	uint32_t align_cnt = 0;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	if (offset & FLASH_WORD_ALIGN) {
		LOG_ERROR("Must be 4-byte alignment offset = 0x%08x", offset);
		return ERROR_FLASH_DST_BREAKS_ALIGNMENT;
	}

	if (count & FLASH_WORD_ALIGN) {
		align_cnt = ((count / FLASH_WORD_ALIGN) * FLASH_WORD_ALIGN) + FLASH_WORD_ALIGN;
		align_buf = malloc(align_cnt);
		if (!align_buf) {
			LOG_ERROR("there is no allocation memory available.");
			return ERROR_FAIL;
		}
		memset(&align_buf[align_cnt - FLASH_WORD_ALIGN], 0xff, FLASH_WORD_ALIGN);
		align_buf = memcpy(align_buf, buffer, count);
	} else {
		align_buf = (uint8_t *)buffer;
		align_cnt = count;
	}

	ret = abov32v2_set_write(bank, align_buf, offset, align_cnt);
	if (ret != ERROR_OK)
		LOG_ERROR("couldn't write data to flash memory.");

	if (count & FLASH_WORD_ALIGN)
		free(align_buf);

	return ret;
}

static int abov32v2_probe(struct flash_bank *bank)
{
	struct abov32v2_flash_bank *abov32v2_info = bank->driver_priv;
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t chipset_id;

	/* set high speed internal clock */
	target_write_u32(target, SYSTEM_CLKSRC_REG, SYSTEM_CLKSRC_HSI);
	target_write_u32(target, SYSTEM_CLKCR_REG, SYSTEM_CLKSEL_HSI);

	ret = abov32v2_get_chipset_id(bank, &chipset_id);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v2_get_geometry(bank, chipset_id);
	if (ret != ERROR_OK)
		goto err;

	/* code area size */
	bank->size = abov32v2_info->code_size;

	/* start address */
	bank->base = 0;

	/* num of sectors (page == sector) */
	bank->num_sectors = abov32v2_info->code_size / abov32v2_info->page_size;
	/* alloc sector blocks */
	free(bank->sectors);
	bank->sectors = NULL;
	bank->sectors = alloc_block_array(0, abov32v2_info->page_size, bank->num_sectors);
	if (!bank->sectors) {
		ret = ERROR_FAIL;
		goto err;
	}

	/* num of protection segments (blocks) */
	bank->num_prot_blocks = abov32v2_info->code_size / abov32v2_info->wprot_seg_size;
	/* alloc protection blocks */
	free(bank->prot_blocks);
	bank->prot_blocks = NULL;
	bank->prot_blocks = alloc_block_array(0, abov32v2_info->wprot_seg_size, bank->num_prot_blocks);
	if (!bank->prot_blocks) {
		ret = ERROR_FAIL;
		free(bank->sectors);
		goto err;
	}

	if (abov32v2_info->abwprot) {
		/* Active flash write protection disable */
		target_write_u32(target, FLASH_MODE_REG, FLASH_PROT_MODE_CMD1);
		target_write_u32(target, FLASH_MODE_REG, FLASH_PROT_MODE_CMD2);
		target_write_u32(target, FLASH_ABWPROT_REG, FLASH_ABWPROT_DISABLE);
		target_write_u32(target, FLASH_MODE_REG, 0x0);
		target_write_u32(target, FLASH_MODE_REG, 0x0);
	}

	/* update protection bits */
	ret = abov32v2_protect_check(bank);
	if (ret != ERROR_OK)
		goto err;

	abov32v2_info->probed = true;
#if defined DEBUG_LOG
	LOG_INFO("ID:0x%08x, size=0x%08x, page=0x%08x, sectors=%d, prot=%d",
			chipset_id, bank->size, abov32v2_info->page_size, bank->num_sectors, bank->num_prot_blocks);
#endif

err:
	return ret;
}

static int abov32v2_auto_probe(struct flash_bank *bank)
{
	struct abov32v2_flash_bank *abov32v2_info = bank->driver_priv;
	if (abov32v2_info->probed)
		return ERROR_OK;
	return abov32v2_probe(bank);
}

static int abov32v2_info(struct flash_bank *bank, struct command_invocation *cmd)
{
	int ret = ERROR_OK;
	uint32_t chipset_id;
	const char *device_str = NULL;

	ret = abov32v2_get_chipset_id(bank, &chipset_id);
	if (ret != ERROR_OK)
		goto err;

	switch (chipset_id & ABOV_CHIP_ID_MASK) {
		case A31G31X_FLASH_64KB:
			device_str = "A31G313 : FLASH 64KB / SRAM 16KB ";
			break;
		case A31G31X_FLASH_128KB:
			device_str = "A31G314 : FLASH 128KB / SRAM 16KB";
			break;
		case A31G31X_FLASH_256KB:
			device_str = "A31G316 : Flash 256KB / SRAM 16KB";
			break;
		case A31G32X_FLASH_128KB:
			device_str = "A31G324 : FLASH 128KB / SRAM 16KB";
			break;
		case A31G32X_FLASH_64KB:
			device_str = "A31G323 : FLASH 64KB / SRAM 16KB";
			break;
		case A31G21X_FLASH_64KB:
			device_str = "A31G213 : FLASH 64KB / SRAM 6KB";
			break;
		case A31G21X_FLASH_32KB:
			device_str = "A31G212 : FLASH 32KB / SRAM 6KB";
			break;
		case A31G22X_FLASH_256KB:
			device_str = "A31G226 : FLASH 256KB / SRAM 20KB";
			break;
		case A31G22X_FLASH_128KB:
			device_str = "A31G224 : FLASH 128KB / SRAM 20KB";
			break;
		default:
			ret = ERROR_FAIL;
			break;
	}

	if (device_str)
		command_print_sameline(cmd, "\nABOV 32Bit MCU - %s", device_str);
	else
		command_print_sameline(cmd, "\nNo chipset detected");
err:
	return ret;
}

COMMAND_HANDLER(abov32v2_handle_lock_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v2_set_write_protect(bank, FLASH_PROT_LOCK, 0, (bank->num_prot_blocks - 1));
err:
	return ret;
}

COMMAND_HANDLER(abov32v2_handle_unlock_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v2_set_write_protect(bank, FLASH_PROT_UNLOCK, 0, (bank->num_prot_blocks - 1));
err:
	return ret;
}

COMMAND_HANDLER(abov32v2_handle_chip_erase_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v2_set_erase(bank, bank->sectors[0].offset, erase_mode_chip);
err:
	return ret;
}

static const struct command_registration abov32v2_exec_command_handlers[] = {
	{
		.name = "lock",
		.handler = abov32v2_handle_lock_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Lock entire flash device.",
	},
	{
		.name = "unlock",
		.handler = abov32v2_handle_unlock_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Unlock entire protected flash device.",
	},
	{
		.name = "chip_erase",
		.handler = abov32v2_handle_chip_erase_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Erase entire flash device.",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration abov32v2_command_handlers[] = {
	{
		.name = "abov32v2",
		.mode = COMMAND_ANY,
		.help = "abov32v2 flash command group",
		.usage = "",
		.chain = abov32v2_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

const struct flash_driver abov32v2_flash = {
	.name = "abov32v2",
	.commands = abov32v2_command_handlers,
	.flash_bank_command = abov32v2_flash_bank_command,
	.erase = abov32v2_erase,
	.protect = abov32v2_protect,
	.write = abov32v2_write,
	.read = default_flash_read,
	.probe = abov32v2_probe,
	.auto_probe = abov32v2_auto_probe,
	.erase_check = default_flash_blank_check,
	.protect_check = abov32v2_protect_check,
	.info = abov32v2_info,
	.free_driver_priv = default_flash_free_driver_priv,
};
