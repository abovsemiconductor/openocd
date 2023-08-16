// SPDX-License-Identifier: BSD-3-Clause
/**
 *******************************************************************************
 * @file        abov32v3.c
 * @author      ABOVSemiconductor R&D Division
 * @brief       Nor Flash Driver for ABOV32 V3
 *
 * Copyright 2023 ABOVSemiconductor Co.,Ltd. All rights reserved.
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/binarybuffer.h>
#include <helper/bits.h>
#include <target/algorithm.h>
#include <target/armv7m.h>

//#define DEBUG_LOG

#define ABOV_CHIP_ID_REG				0x4000F004

#define A34M41X_FLASH_512KB_SRAM_16KB	0x34A020
#define A34M41X_FLASH_512KB_SRAM_32KB	0x34A010
#define A34M41X_FLASH_512KB_SRAM_64KB	0x34A000

#define A34M41X_FLASH_256KB_SRAM_16KB	0x34A021
#define A34M41X_FLASH_256KB_SRAM_32KB	0x34A011
#define A34M41X_FLASH_256KB_SRAM_64KB	0x34A001

#define A34M41X_FLASH_128KB_SRAM_16KB	0x34A022
#define A34M41X_FLASH_128KB_SRAM_32KB	0x34A012
#define A34M41X_FLASH_128KB_SRAM_64KB	0x34A002

#define A33M11X_FLASH_256KB				0x33A004
#define A33M11X_FLASH_128KB				0x33A005

#define ABOV_CHIP_ID_MASK				0xFFFFFF

/* Code Flash Access Key */
#define FLASH_CODE_ACCESS_KEY_1			0x01234567
#define FLASH_CODE_ACCESS_KEY_2			0x12345678
#define FLASH_CODE_ACCESS_KEY_3			0x23456789

/* Code Flash Reg Offset */
#define FLASH_CODE_REG_START_ADDR		0x41000000
#define FLASH_CODE_CFG_REG				(FLASH_CODE_REG_START_ADDR | 0x00000000)
#define FLASH_CODE_ACCESS_REG			(FLASH_CODE_REG_START_ADDR | 0x00000004)
#define FLASH_CODE_WPROT_REG			(FLASH_CODE_REG_START_ADDR | 0x0000000C)
#define FLASH_CODE_CTRL_REG				(FLASH_CODE_REG_START_ADDR | 0x00000014)
#define FLASH_CODE_STAT_REG				(FLASH_CODE_REG_START_ADDR | 0x00000018)

/* Code Flash Control Reg bit position */
#define FLASH_CODE_CTRL_PROGRAM			BIT(0)

/* Flash Status Reg bit position */
#define FLASH_CODE_STAT_WBUSY			BIT(0)

#define FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT		2
#define FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT		4
#define FLASH_CODE_4KB_SECTOR_BY_PAGE_COUNT		8

#define FLASH_CODE_1KB_SECTOR_IDX_MASK	0x01
#define FLASH_CODE_4KB_SECTOR_IDX_MASK	0x07

#define FLASH_CODE_PROT_LOCK			0x00000001
#define FLASH_CODE_PROT_UNLOCK			0x00000000
#define FLASH_CODE_ERASE_TIMEOUT		100

#define FLASH_ENABLE					0x00000001
#define FLASH_DISABLE					0x00000000
#define FLASH_WORD_COUNT				4
#define FLASH_WORD_ALIGN_MASK			0x00000003

#define FLASH_CODE_PROT_LAST_4K_MASK	0xFF000000

/* Watchdog Reg */
#define WATCHDOG_ACCESS_REG				0x400002F0
#define WATCHDOG_ACCESS_KEY				0x0000A55A
#define WATCHDOG_CTRL_REG				0x40000208
#define WATCHDOG_DISABLE				0x00000000

/* System Clock Reg addr */
#define SYSTEM_CLKSRC_REG				0x40000040
#define SYSTEM_CLKSRC_HSI				0x00000028

#define SYSTEM_CLKCR_REG				0x40000044
#define SYSTEM_CLKSEL_HSI				0x00000002

#define SYSTEM_ACCESS_REG				0x400000F0
#define SYSTEM_ACCESS_KEY_1				0x00000057
#define SYSTEM_ACCESS_KEY_2				0x00000075

enum erase_mode {
	erase_mode_page		=	BIT(0),
	erase_mode_1kb		=	BIT(1),
	erase_mode_2kb		=	BIT(2),
	erase_mode_4kb		=	BIT(3),
	erase_mode_chip		=	BIT(4),
	erase_mode_max
};

struct abov32v3_erase {
	uint32_t support_mode;
	uint32_t chipset_bit;
	uint32_t page_bit;
	uint32_t sector_1kb_bit;
	uint32_t sector_2kb_bit;
	uint32_t sector_4kb_bit;
	uint32_t min_sector_by_page_cnt;
};

static const struct abov32v3_dev {
	uint32_t id;
	const char *device_str;
	uint32_t enable_bit;
	uint32_t code_size;
	uint32_t system_size;
	uint32_t page_size;
	uint32_t wprot_seg_size;
	struct abov32v3_erase erase;
} abov32v3_devices[] = {
{
	.id = A34M41X_FLASH_128KB_SRAM_32KB,
	.device_str = "A34M414 : FLASH 128KB / SRAM 32KB ",
	.enable_bit = 0,
	.code_size = 0x20000,
	.system_size = 0x0,
	.page_size = 0x200,
	.wprot_seg_size = 0x8000,
	.erase.support_mode = erase_mode_page | erase_mode_1kb | erase_mode_4kb | erase_mode_chip,
	.erase.chipset_bit = BIT(4),
	.erase.page_bit = BIT(1),
	.erase.sector_1kb_bit = BIT(2),
	.erase.sector_4kb_bit = BIT(3),
	.erase.min_sector_by_page_cnt = FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT
}, {
	.id = A34M41X_FLASH_256KB_SRAM_64KB,
	.device_str = "A34M416 : FLASH 256KB / SRAM 64KB",
	.enable_bit = 0,
	.code_size = 0x40000,
	.system_size = 0x0,
	.page_size = 0x200,
	.wprot_seg_size = 0x8000,
	.erase.support_mode = erase_mode_page | erase_mode_1kb | erase_mode_4kb | erase_mode_chip,
	.erase.chipset_bit = BIT(4),
	.erase.page_bit = BIT(1),
	.erase.sector_1kb_bit = BIT(2),
	.erase.sector_4kb_bit = BIT(3),
	.erase.min_sector_by_page_cnt = FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT
}, {
	.id = A34M41X_FLASH_512KB_SRAM_64KB,
	.device_str = "A34M418 : FLASH 512KB / SRAM 64KB",
	.enable_bit = 0,
	.code_size = 0x80000,
	.system_size = 0x0,
	.page_size = 0x200,
	.wprot_seg_size = 0x8000,
	.erase.support_mode = erase_mode_page | erase_mode_1kb | erase_mode_4kb | erase_mode_chip,
	.erase.chipset_bit = BIT(4),
	.erase.page_bit = BIT(1),
	.erase.sector_1kb_bit = BIT(2),
	.erase.sector_4kb_bit = BIT(3),
	.erase.min_sector_by_page_cnt = FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT
}, {
	.id = A33M11X_FLASH_128KB,
	.device_str = "A33M114 : FLASH 128KB / SRAM 16KB",
	.enable_bit = BIT(25),
	.code_size = 0x20000,
	.system_size = 0x0,
	.page_size = 0x200,
	.wprot_seg_size = 0x4000,
	.erase.support_mode = erase_mode_page | erase_mode_2kb | erase_mode_chip,
	.erase.chipset_bit = BIT(3),
	.erase.page_bit = BIT(1),
	.erase.sector_2kb_bit = BIT(2),
	.erase.min_sector_by_page_cnt = FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT
}, {
	.id = A33M11X_FLASH_256KB,
	.device_str = "A33M116 : FLASH 256KB / SRAM 16KB",
	.enable_bit = BIT(25),
	.code_size = 0x40000,
	.system_size = 0x0,
	.page_size = 0x200,
	.wprot_seg_size = 0x4000,
	.erase.support_mode = erase_mode_page | erase_mode_2kb | erase_mode_chip,
	.erase.chipset_bit = BIT(3),
	.erase.page_bit = BIT(1),
	.erase.sector_2kb_bit = BIT(2),
	.erase.min_sector_by_page_cnt = FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT
}};

struct abov32v3_flash_bank {
	bool probed;
	const struct abov32v3_dev *dev;
};


static int abov32v3_wait_busy(struct flash_bank *bank, int timeout)
{
	struct target *target = bank->target;
	uint32_t busy = 0;

	while (1) {
		int ret = target_read_u32(target, FLASH_CODE_STAT_REG, &busy);
		if (ret != ERROR_OK)
			return ret;

		if (!(busy & FLASH_CODE_STAT_WBUSY))
			break;

		if (timeout-- <= 0)
			return ERROR_TIMEOUT_REACHED;
	}

	return ERROR_OK;
}

static int abov32v3_set_clear_reg(struct flash_bank *bank, uint32_t addr, uint32_t set, uint32_t clear)
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

	reg = (reg & ~clear) | set;

#if defined DEBUG_LOG
	LOG_INFO("new reg:0x%08x, val:0x%08x", addr, reg);
#endif
	ret = target_write_u32(target, addr, reg);
err:
	return ret;
}

static int abov32v3_get_chipset_id(struct flash_bank *bank, uint32_t *chipset_id)
{
	struct target *target = bank->target;
	int ret = target_read_u32(target, ABOV_CHIP_ID_REG, chipset_id);
	return ret;
}

const struct abov32v3_dev *abov32v3_find_device(uint32_t id)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(abov32v3_devices); i++)
		if ((id & ABOV_CHIP_ID_MASK) == abov32v3_devices[i].id)
			return &abov32v3_devices[i];
	return NULL;
}

static int abov32v3_protect_check(struct flash_bank *bank)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t reg = 0;

	/* read protection bits */
	ret = target_read_u32(target, FLASH_CODE_WPROT_REG, &reg);
	if (ret != ERROR_OK)
		goto err;

#if defined DEBUG_LOG
	LOG_INFO("cur wprotect:0x%08x, wprotect blocks:%d\n", reg, bank->num_prot_blocks);
#endif
	for (unsigned int i = 0; i < bank->num_prot_blocks; i++) {
		bank->prot_blocks[i].is_protected = (reg & (FLASH_CODE_PROT_LOCK << i))
			? FLASH_CODE_PROT_LOCK : FLASH_CODE_PROT_UNLOCK;
#if defined DEBUG_LOG
		LOG_INFO("%d block is %s", i, (bank->prot_blocks[i].is_protected == 1) ? "lock" : "unlock");
#endif
	}
err:
	return ret;
}

static int abov32v3_set_write_protect(struct flash_bank *bank, int set, uint32_t first, uint32_t last)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t reg = 0, prot = 0;

	/* read protection bits */
	ret = target_read_u32(target, FLASH_CODE_WPROT_REG, &reg);
	if (ret != ERROR_OK)
		goto err;

	for (unsigned int i = first; i <= last; i++)
		prot |= (FLASH_CODE_PROT_LOCK << i);

#if defined DEBUG_LOG
	LOG_INFO("cur write protection:0x%08x, protection bits:0x%08x", reg, prot);
#endif

	if (set)
		reg |= prot;
	else
		reg &= ~prot;

	/* last 4K area unprotection bit */
	if ((last > (bank->num_sectors - 5)) && ((bank->num_sectors - 1) <= last)) {
		if (set)
			reg &= ~FLASH_CODE_PROT_LAST_4K_MASK;
		else
			reg |= FLASH_CODE_PROT_LAST_4K_MASK;
	}

	/* write protection bits */
	target_write_u32(target, FLASH_CODE_WPROT_REG, reg);

#if defined DEBUG_LOG
	(void)target_read_u32(target, FLASH_CODE_WPROT_REG, &reg);
	LOG_INFO("new write protection:0x%08x", reg);
#endif

	/* update protection bits in a bank */
	ret = abov32v3_protect_check(bank);
err:
	return ret;
}

static int abov32v3_set_erase(struct flash_bank *bank, uint32_t pageaddr, enum erase_mode mode)
{
	struct abov32v3_flash_bank *abov32v3_info = bank->driver_priv;
	const struct abov32v3_erase *erase_info = &abov32v3_info->dev->erase;
	struct target *target = bank->target;
	int ret = ERROR_OK;

	/* erase mode */
	target_write_u32(target, FLASH_CODE_ACCESS_REG, FLASH_CODE_ACCESS_KEY_1);
	target_write_u32(target, FLASH_CODE_ACCESS_REG, FLASH_CODE_ACCESS_KEY_2);
	target_write_u32(target, FLASH_CODE_ACCESS_REG, FLASH_CODE_ACCESS_KEY_3);

	/* select erase area mode */
	switch (mode) {
		case erase_mode_page:
			ret = abov32v3_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->page_bit, FLASH_ENABLE);
			break;
		case erase_mode_chip:
			ret = abov32v3_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->chipset_bit, FLASH_ENABLE);
			break;
		case erase_mode_1kb:
			ret = abov32v3_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->sector_1kb_bit, FLASH_ENABLE);
			break;
		case erase_mode_2kb:
			ret = abov32v3_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->sector_2kb_bit, FLASH_ENABLE);
			break;
		case erase_mode_4kb:
			ret = abov32v3_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->sector_4kb_bit, FLASH_ENABLE);
			break;
		default:
			/* page erase mode */
			break;
	}

	if (ret != ERROR_OK)
		goto err;

	/* signaling erase flash commands to bus */
	ret = target_write_u32(target, pageaddr, 0xFFFFFFFF);
	if (ret != ERROR_OK)
		goto err;

	/* wait a write is done */
	ret = abov32v3_wait_busy(bank, FLASH_CODE_ERASE_TIMEOUT);
	if (ret != ERROR_OK)
		goto err;

err:
	/* clear control reg */
	target_write_u32(target, FLASH_CODE_CTRL_REG, 0);

	/* clear mode  */
	target_write_u32(target, FLASH_CODE_ACCESS_REG, 0);

	return ret;
}

FLASH_BANK_COMMAND_HANDLER(abov32v3_flash_bank_command)
{
	/* Need syntax count from flash bank command */
	if (CMD_ARGC <  6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct abov32v3_flash_bank *abov32v3_info;
	abov32v3_info = malloc(sizeof(struct abov32v3_flash_bank));
	if (!abov32v3_info) {
		LOG_ERROR("Out of memory");
		return ERROR_FAIL;
	}

	bank->driver_priv = abov32v3_info;
	abov32v3_info->probed = false;

	return ERROR_OK;
}

static int abov32v3_erase(struct flash_bank *bank, unsigned int first,
		unsigned int last)
{
	struct abov32v3_flash_bank *abov32v3_info = bank->driver_priv;
	const struct abov32v3_erase *erase_info = &abov32v3_info->dev->erase;
	int ret = ERROR_OK;
	uint32_t sec_cnt = 0;
	uint32_t sec_offset = first;

#if defined DEBUG_LOG
	LOG_INFO("erase first=%d last=%d", first, last);
#endif

	abov32v3_set_write_protect(bank, FLASH_CODE_PROT_UNLOCK, first, last);

	sec_cnt = (last - first) + 1;
	if (sec_cnt == bank->num_sectors) {
		ret = abov32v3_set_erase(bank, bank->sectors[0].offset, erase_mode_chip);
	} else {
		while (sec_cnt != 0) {
			if (erase_info->support_mode & erase_mode_page
				&& (((sec_offset & (erase_info->min_sector_by_page_cnt - 1)) != 0x0)
					 || (sec_cnt <= erase_info->min_sector_by_page_cnt - 1))) {
#if defined DEBUG_LOG
				LOG_INFO("page erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v3_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_page);
				if (ret != ERROR_OK)
					break;

				sec_offset++;
				sec_cnt--;
			} else if ((erase_info->support_mode & erase_mode_1kb)
					&& ((sec_offset & FLASH_CODE_4KB_SECTOR_IDX_MASK) != 0x0
					|| sec_cnt < FLASH_CODE_4KB_SECTOR_BY_PAGE_COUNT)) {
#if defined DEBUG_LOG
				LOG_INFO("1KB erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v3_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_1kb);
				if (ret != ERROR_OK)
					break;

				sec_offset += FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT;
				sec_cnt -= FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT;
			} else if ((erase_info->support_mode & erase_mode_2kb)
					&& (sec_cnt >= FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT)) {
#if defined DEBUG_LOG
				LOG_INFO("2KB erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v3_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_2kb);
				if (ret != ERROR_OK)
					break;

				sec_offset += FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT;
				sec_cnt -= FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT;
			} else if ((erase_info->support_mode & erase_mode_4kb)
					&& (sec_cnt >= FLASH_CODE_4KB_SECTOR_BY_PAGE_COUNT)) {
#if defined DEBUG_LOG
				LOG_INFO("4KB erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v3_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_4kb);
				if (ret != ERROR_OK)
					break;

				sec_offset += FLASH_CODE_4KB_SECTOR_BY_PAGE_COUNT;
				sec_cnt -= FLASH_CODE_4KB_SECTOR_BY_PAGE_COUNT;
			}
		}
	}

	if (ret == ERROR_OK) {
		for (unsigned int i = first; i <= last; i++)
			bank->sectors[i].is_erased = 1;
	}

	return ret;
}

static int abov32v3_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last)
{
	return abov32v3_set_write_protect(bank, set, first, last);
}

static int abov32v3_set_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	struct armv7m_algorithm armv7m_method;
	struct working_area *algobuffer, *imgbuffer;
	struct reg_param reg_params[5];

	int ret = ERROR_OK;
	uint32_t addr = 0;
	uint32_t bufsize = 1024;

	static const uint8_t abov32v3_algo_code[] = {
#include "../../../contrib/loaders/flash/abovsemi/abov32v3.inc"
	};

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* stop watchdog */
	target_write_u32(target, WATCHDOG_ACCESS_REG, WATCHDOG_ACCESS_KEY);
	target_write_u32(target, WATCHDOG_CTRL_REG, WATCHDOG_DISABLE);

	LOG_INFO("Writing buffer to flash address=0x%08x count=%d bytes", offset, count);

	/* program mode */
	target_write_u32(target, FLASH_CODE_ACCESS_REG, FLASH_CODE_ACCESS_KEY_1);
	target_write_u32(target, FLASH_CODE_ACCESS_REG, FLASH_CODE_ACCESS_KEY_2);
	target_write_u32(target, FLASH_CODE_ACCESS_REG, FLASH_CODE_ACCESS_KEY_3);

	/* set flash address */
	addr = (bank->base + offset) & ~(FLASH_WORD_ALIGN_MASK);

	/* set pmode and signaling write command to bus */
	ret = abov32v3_set_clear_reg(bank, FLASH_CODE_CTRL_REG, FLASH_CODE_CTRL_PROGRAM, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* alloc algo code area in MCU memory */
	ret = target_alloc_working_area(target, sizeof(abov32v3_algo_code), &algobuffer);
	if (ret != ERROR_OK)
		goto err;

	/* upload algo code into memory */
	ret = target_write_buffer(target, algobuffer->address, sizeof(abov32v3_algo_code), abov32v3_algo_code);
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
	init_reg_param(&reg_params[4], "r4", 32, PARAM_IN_OUT);	/* destination flash address */

	buf_set_u32(reg_params[0].value, 0, 32, count);		/* algo code decrement 4 count */
	buf_set_u32(reg_params[1].value, 0, 32, imgbuffer->address);
	buf_set_u32(reg_params[2].value, 0, 32, imgbuffer->address + imgbuffer->size);
	buf_set_u32(reg_params[3].value, 0, 32, FLASH_CODE_REG_START_ADDR);
	buf_set_u32(reg_params[4].value, 0, 32, addr);

	ret = target_run_flash_async_algorithm(target, buffer, (count / FLASH_WORD_COUNT), FLASH_WORD_COUNT, 0, NULL,
			5, reg_params, imgbuffer->address, imgbuffer->size, algobuffer->address, 0, &armv7m_method);

	target_free_working_area(target, imgbuffer);
	target_free_working_area(target, algobuffer);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);
	destroy_reg_param(&reg_params[2]);
	destroy_reg_param(&reg_params[3]);
	destroy_reg_param(&reg_params[4]);

err:
	/* clear control reg */
	target_write_u32(target, FLASH_CODE_CTRL_REG, 0);

	/* clear mode  */
	target_write_u32(target, FLASH_CODE_ACCESS_REG, 0);

	return ret;
}

static int abov32v3_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint8_t *align_buf = NULL;
	uint32_t align_cnt = 0;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	if (offset & FLASH_WORD_ALIGN_MASK) {
		LOG_ERROR("Must be 4-byte alignment offset = 0x%08x", offset);
		return ERROR_FLASH_DST_BREAKS_ALIGNMENT;
	}

	if (count & FLASH_WORD_ALIGN_MASK) {
		align_cnt = ((count / FLASH_WORD_COUNT) * FLASH_WORD_COUNT) + FLASH_WORD_COUNT;
		align_buf = malloc(align_cnt);
		if (!align_buf) {
			LOG_ERROR("there is no allocation memory available.");
			return ERROR_FAIL;
		}
		memset(&align_buf[align_cnt - FLASH_WORD_COUNT], 0xff, FLASH_WORD_COUNT);
		align_buf = memcpy(align_buf, buffer, count);
	} else {
		align_buf = (uint8_t *)buffer;
		align_cnt = count;
	}

	ret = abov32v3_set_write(bank, align_buf, offset, align_cnt);
	if (ret != ERROR_OK)
		LOG_ERROR("couldn't write data to flash memory.");

	if (count & FLASH_WORD_ALIGN_MASK)
		free(align_buf);

	return ret;
}

static int abov32v3_probe(struct flash_bank *bank)
{
	struct abov32v3_flash_bank *abov32v3_info = bank->driver_priv;
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t chipset_id;

	/* set high speed internal clock */
	target_write_u32(target, SYSTEM_ACCESS_REG, SYSTEM_ACCESS_KEY_1);
	target_write_u32(target, SYSTEM_ACCESS_REG, SYSTEM_ACCESS_KEY_2);
	target_write_u32(target, SYSTEM_CLKSRC_REG, SYSTEM_CLKSRC_HSI);
	target_write_u32(target, SYSTEM_CLKCR_REG, SYSTEM_CLKSEL_HSI);

	/* some chipsets need to enable register write function */
	if (abov32v3_info->dev->enable_bit) {
		ret = abov32v3_set_clear_reg(bank, FLASH_CODE_CFG_REG, abov32v3_info->dev->enable_bit, FLASH_ENABLE);
		if (ret != ERROR_OK)
			goto err;
	}

	ret = abov32v3_get_chipset_id(bank, &chipset_id);
	if (ret != ERROR_OK)
		goto err;

	/* find and get geometry info */
	abov32v3_info->dev = abov32v3_find_device(chipset_id);
	if (!abov32v3_info->dev)
		return ERROR_FAIL;

	/* code area size */
	bank->size = abov32v3_info->dev->code_size;

	/* start address */
	bank->base = 0;

	/* num of sectors (page == sector) */
	bank->num_sectors = abov32v3_info->dev->code_size / abov32v3_info->dev->page_size;
	/* alloc sector blocks */
	free(bank->sectors);
	bank->sectors = NULL;
	bank->sectors = alloc_block_array(0, abov32v3_info->dev->page_size, bank->num_sectors);
	if (!bank->sectors) {
		ret = ERROR_FAIL;
		goto err;
	}

	/* num of protection segments (blocks) */
	bank->num_prot_blocks = abov32v3_info->dev->code_size / abov32v3_info->dev->wprot_seg_size;
	/* alloc protection blocks */
	free(bank->prot_blocks);
	bank->prot_blocks = NULL;
	bank->prot_blocks = alloc_block_array(0, abov32v3_info->dev->wprot_seg_size, bank->num_prot_blocks);
	if (!bank->prot_blocks) {
		ret = ERROR_FAIL;
		free(bank->sectors);
		goto err;
	}

	/* update protection bits */
	ret = abov32v3_protect_check(bank);
	if (ret != ERROR_OK)
		goto err;

	abov32v3_info->probed = true;
#if defined DEBUG_LOG
	LOG_INFO("ID:0x%08x, size=0x%08x, page=0x%08x, sectors=%d, prot=%d",
			chipset_id, bank->size, abov32v3_info->dev->page_size, bank->num_sectors, bank->num_prot_blocks);
#endif

err:
	return ret;
}

static int abov32v3_auto_probe(struct flash_bank *bank)
{
	struct abov32v3_flash_bank *abov32v3_info = bank->driver_priv;
	if (abov32v3_info->probed)
		return ERROR_OK;
	return abov32v3_probe(bank);
}

static int abov32v3_info(struct flash_bank *bank, struct command_invocation *cmd)
{
	struct abov32v3_flash_bank *abov32v3_info = bank->driver_priv;

	if (abov32v3_info->probed)
		command_print_sameline(cmd, "\nABOV 32Bit MCU - %s", abov32v3_info->dev->device_str);
	else
		command_print_sameline(cmd, "\nNo chipset detected");

	return ERROR_OK;
}

COMMAND_HANDLER(abov32v3_handle_lock_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v3_set_write_protect(bank, FLASH_CODE_PROT_LOCK, 0, (bank->num_prot_blocks - 1));
err:
	return ret;
}

COMMAND_HANDLER(abov32v3_handle_unlock_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v3_set_write_protect(bank, FLASH_CODE_PROT_UNLOCK, 0, (bank->num_prot_blocks - 1));
err:
	return ret;
}

COMMAND_HANDLER(abov32v3_handle_chip_erase_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v3_set_erase(bank, bank->sectors[0].offset, erase_mode_chip);
err:
	return ret;
}

static const struct command_registration abov32v3_exec_command_handlers[] = {
	{
		.name = "lock",
		.handler = abov32v3_handle_lock_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Lock entire flash device.",
	},
	{
		.name = "unlock",
		.handler = abov32v3_handle_unlock_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Unlock entire protected flash device.",
	},
	{
		.name = "chip_erase",
		.handler = abov32v3_handle_chip_erase_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Erase entire flash device.",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration abov32v3_command_handlers[] = {
	{
		.name = "abov32v3",
		.mode = COMMAND_ANY,
		.help = "abov32v3 flash command group",
		.usage = "",
		.chain = abov32v3_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

const struct flash_driver abov32v3_flash = {
	.name = "abov32v3",
	.commands = abov32v3_command_handlers,
	.flash_bank_command = abov32v3_flash_bank_command,
	.erase = abov32v3_erase,
	.protect = abov32v3_protect,
	.write = abov32v3_write,
	.read = default_flash_read,
	.probe = abov32v3_probe,
	.auto_probe = abov32v3_auto_probe,
	.erase_check = default_flash_blank_check,
	.protect_check = abov32v3_protect_check,
	.info = abov32v3_info,
	.free_driver_priv = default_flash_free_driver_priv,
};
