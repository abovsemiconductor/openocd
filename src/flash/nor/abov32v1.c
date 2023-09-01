// SPDX-License-Identifier: BSD-3-Clause
/**
 *******************************************************************************
 * @file        abov32v1.c
 * @author      ABOVSemiconductor R&D Division
 * @brief       Nor Flash Driver for ABOV32 V1
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

#define A33G52X_FLASH_384KB				0x33A000
#define A33G52X_FLASH_256KB				0x33A001
#define A33G52X_FLASH_128KB				0x33A002

#define ABOV_CHIP_ID_MASK				0xFFFFFF

/* Code Flash Reg Offset */
#define FLASH_CODE_REG_START_ADDR		0x40000100
#define FLASH_CODE_CTRL_REG				(FLASH_CODE_REG_START_ADDR | 0x00000004)
#define FLASH_CODE_ADDR_REG				(FLASH_CODE_REG_START_ADDR | 0x00000010)
#define FLASH_CODE_EXT_REG				(FLASH_CODE_REG_START_ADDR | 0x00000014)
#define FLASH_CODE_WPROT_REG			(FLASH_CODE_REG_START_ADDR | 0x0000001C)

/* Code Flash Control Reg bit position */
#define FLASH_CODE_CTRL_PROGRAM			BIT(0)
#define FLASH_CODE_CTRL_ROM_SEL			BIT(11)
#define FLASH_CODE_CTRL_CODE_SEL		BIT(14)
#define FLASH_CODE_CTRL_ADDR_LATCH		BIT(13)
#define FLASH_CODE_CTRL_OPS				BIT(7)

/* Flash Status Reg bit position */
#define FLASH_CODE_STAT_WBUSY			BIT(8)

/* Flash Extended Mode Contrl Reg bit */
#define FLASH_CODE_EXT_CTRL_EN			BIT(0)
#define FLASH_CODE_EXT_CTRL_KEY			0x00004A00

#define FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT		2
#define FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT		4

#define FLASH_CODE_1KB_SECTOR_IDX_MASK		0x01
#define FLASH_CODE_2KB_SECTOR_IDX_MASK		0x03

#define FLASH_CODE_PROT_LOCK			0x00000000
#define FLASH_CODE_PROT_UNLOCK			0x00000001
#define FLASH_CODE_ERASE_TIMEOUT		100
#define FLASH_CODE_MAX_WPROT_CNT		4
#define FLASH_DATA_WPROT_START_INDEX	8
#define FLASH_DATA_WPROT_BIT_CNT		4

#define FLASH_ENABLE					0x00000001
#define FLASH_DISABLE					0x00000000
#define FLASH_WORD_COUNT				4
#define FLASH_WORD_ALIGN_MASK			0x00000003

/* Watchdog Reg */
#define WATCHDOG_CTRL_REG				0x40000408
#define WATCHDOG_DISABLE				0x00000000

/* System Clock Reg addr */
#define SYSTEM_CLKSRC_REG				0x40000030
#define SYSTEM_CLKSRC_HSI				0x000000A0

#define SYSTEM_CLKCR_REG				0x4000003C
#define SYSTEM_CLKSEL_HSI				0x00000003

enum erase_mode {
	erase_mode_page		=	BIT(0),
	erase_mode_1kb		=	BIT(1),
	erase_mode_2kb		=	BIT(2),
	erase_mode_4kb		=	BIT(3),
	erase_mode_chip		=	BIT(4),
	erase_mode_max
};

struct abov32v1_wprot {
	uint32_t num_wprot;
	uint32_t offset;
	uint32_t size;
};

struct abov32v1_erase {
	uint32_t support_mode;
	uint32_t chipset_bit;
	uint32_t page_bit;
	uint32_t sector_1kb_bit;
	uint32_t sector_2kb_bit;
	uint32_t sector_4kb_bit;
	uint32_t min_sector_by_page_cnt;
};

static const struct abov32v1_dev {
	uint32_t id;
	const char *device_str;
	uint32_t code_size;
	uint32_t system_size;
	uint32_t page_size;
	struct abov32v1_erase erase;
	struct abov32v1_wprot wprot[FLASH_CODE_MAX_WPROT_CNT];
} abov32v1_devices[] = {
{
	.id = A33G52X_FLASH_128KB,
	.device_str = "A33G524 : FLASH 128KB / SRAM 24KB",
	.code_size = 0x20000,
	.system_size = 0x0,
	.page_size = 0x200,
	.erase.support_mode = erase_mode_page | erase_mode_1kb | erase_mode_chip,
	.erase.chipset_bit = BIT(2),
	.erase.page_bit = BIT(18),
	.erase.sector_1kb_bit = BIT(1),
	.erase.min_sector_by_page_cnt = FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT,
	.wprot = {
		{
			.num_wprot = 4,
			.offset = 0,
			.size = 0x1000
		}, {
			.num_wprot = 1,
			.offset = 0x4000,
			.size = 0xC000
		}, {
			.num_wprot = 1,
			.offset = 0x10000,
			.size = 0x10000
		}
	}
}, {
	.id = A33G52X_FLASH_256KB,
	.device_str = "A33G526 : FLASH 256KB / SRAM 24KB",
	.code_size = 0x40000,
	.system_size = 0x0,
	.page_size = 0x200,
	.erase.support_mode = erase_mode_page | erase_mode_1kb | erase_mode_chip,
	.erase.chipset_bit = BIT(2),
	.erase.page_bit = BIT(18),
	.erase.sector_1kb_bit = BIT(1),
	.erase.min_sector_by_page_cnt = FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT,
	.wprot = {
		{
			.num_wprot = 4,
			.offset = 0,
			.size = 0x1000
		}, {
			.num_wprot = 1,
			.offset = 0x4000,
			.size = 0xC000
		}, {
			.num_wprot = 3,
			.offset = 0x10000,
			.size = 0x10000
		}
	}
}, {
	.id = A33G52X_FLASH_384KB,
	.device_str = "A33G527 : FLASH 384KB / SRAM 24KB",
	.code_size = 0x60000,
	.system_size = 0x0,
	.page_size = 0x200,
	.erase.support_mode = erase_mode_page | erase_mode_1kb | erase_mode_chip,
	.erase.chipset_bit = BIT(2),
	.erase.page_bit = BIT(18),
	.erase.sector_1kb_bit = BIT(1),
	.erase.min_sector_by_page_cnt = FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT,
	.wprot = {
		{
			.num_wprot = 4,
			.offset = 0,
			.size = 0x1000
		}, {
			.num_wprot = 1,
			.offset = 0x4000,
			.size = 0xC000
		}, {
			.num_wprot = 3,
			.offset = 0x10000,
			.size = 0x10000
		}, {
			.num_wprot = 2,
			.offset = 0x40000,
			.size = 0x10000
		}
	}
}};


struct abov32v1_flash_bank {
	bool probed;
	const struct abov32v1_dev *dev;
};


static int abov32v1_wait_busy(struct flash_bank *bank, int timeout)
{
	struct target *target = bank->target;
	uint32_t busy = 0;

	while (1) {
		int ret = target_read_u32(target, FLASH_CODE_CTRL_REG, &busy);
		if (ret != ERROR_OK)
			return ret;

		if (!(busy & FLASH_CODE_STAT_WBUSY))
			break;

		if (timeout-- <= 0)
			return ERROR_TIMEOUT_REACHED;
	}

	return ERROR_OK;
}

static int abov32v1_set_clear_reg(struct flash_bank *bank, uint32_t addr, uint32_t set, uint32_t clear)
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

static int abov32v1_get_chipset_id(struct flash_bank *bank, uint32_t *chipset_id)
{
	struct target *target = bank->target;
	int ret = target_read_u32(target, ABOV_CHIP_ID_REG, chipset_id);
	return ret;
}

const struct abov32v1_dev *abov32v1_find_device(uint32_t id)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(abov32v1_devices); i++)
		if ((id & ABOV_CHIP_ID_MASK) == abov32v1_devices[i].id)
			return &abov32v1_devices[i];
	return NULL;
}

static int abov32v1_protect_check(struct flash_bank *bank)
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
	for (unsigned int i = 0, j = 0; i < bank->num_prot_blocks; i++) {
		if (i >= FLASH_DATA_WPROT_START_INDEX)
			j = i + FLASH_DATA_WPROT_BIT_CNT;
		else
			j = i;

		bank->prot_blocks[i].is_protected = (reg & (FLASH_CODE_PROT_UNLOCK << j))
			? false : true;
#if defined DEBUG_LOG
		LOG_INFO("%d block is %s", i, (bank->prot_blocks[i].is_protected == 1) ? "lock" : "unlock");
#endif
	}
err:
	return ret;
}

static int abov32v1_set_write_protect(struct flash_bank *bank, int set, uint32_t first, uint32_t last)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t reg = 0, prot = 0;

	/* read protection bits */
	ret = target_read_u32(target, FLASH_CODE_WPROT_REG, &reg);
	if (ret != ERROR_OK)
		goto err;

	for (unsigned int i = first, j = 0; i <= last; i++) {
		if (i >= FLASH_DATA_WPROT_START_INDEX)
			j = i + FLASH_DATA_WPROT_BIT_CNT;
		else
			j = i;

		prot |= (FLASH_CODE_PROT_UNLOCK << j);
	}

#if defined DEBUG_LOG
	LOG_INFO("cur write protection:0x%08x, protection bits:0x%08x", reg, prot);
#endif

	if (set)
		reg |= prot;
	else
		reg &= ~prot;

	/* write protection bits */
	target_write_u32(target, FLASH_CODE_WPROT_REG, reg);

#if defined DEBUG_LOG
	(void)target_read_u32(target, FLASH_CODE_WPROT_REG, &reg);
	LOG_INFO("new write protection:0x%08x", reg);
#endif

	/* update protection bits in a bank */
	ret = abov32v1_protect_check(bank);
err:
	return ret;
}

static int abov32v1_set_erase(struct flash_bank *bank, uint32_t pageaddr, enum erase_mode mode)
{
	struct abov32v1_flash_bank *abov32v1_info = bank->driver_priv;
	const struct abov32v1_erase *erase_info = &abov32v1_info->dev->erase;
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t addr = 0;

	/* extended enable */
	ret = target_write_u32(target, FLASH_CODE_EXT_REG, FLASH_CODE_EXT_CTRL_KEY | FLASH_CODE_EXT_CTRL_EN);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, FLASH_CODE_CTRL_CODE_SEL, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, FLASH_CODE_CTRL_ROM_SEL, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* get erase address */
	addr = pageaddr & ~FLASH_WORD_ALIGN_MASK;
	ret = target_write_u32(target, FLASH_CODE_ADDR_REG, addr);
	if (ret != ERROR_OK)
		goto err;

	/* select erase area mode */
	switch (mode) {
		case erase_mode_page:
			ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->page_bit, FLASH_ENABLE);
			break;
		case erase_mode_chip:
			ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->chipset_bit, FLASH_ENABLE);
			break;
		case erase_mode_1kb:
			ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->sector_1kb_bit, FLASH_ENABLE);
			break;
		case erase_mode_2kb:
			ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->sector_2kb_bit, FLASH_ENABLE);
			break;
		case erase_mode_4kb:
			ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, erase_info->sector_4kb_bit, FLASH_ENABLE);
			break;
		default:
			/* page erase mode */
			break;
	}

	if (ret != ERROR_OK)
		goto err;

	/* set latch signal */
	ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, FLASH_CODE_CTRL_ADDR_LATCH, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;


	/* enable hight voltage */
	ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, FLASH_CODE_CTRL_OPS, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;


	/* wait a write is done */
	ret = abov32v1_wait_busy(bank, FLASH_CODE_ERASE_TIMEOUT);
	if (ret != ERROR_OK)
		goto err;

err:
	/* clear control reg */
	target_write_u32(target, FLASH_CODE_CTRL_REG, 0);

	/* clear extended reg */
	target_write_u32(target, FLASH_CODE_EXT_REG, 0);

	return ret;
}

FLASH_BANK_COMMAND_HANDLER(abov32v1_flash_bank_command)
{
	/* Need syntax count from flash bank command */
	if (CMD_ARGC <  6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct abov32v1_flash_bank *abov32v1_info;
	abov32v1_info = malloc(sizeof(struct abov32v1_flash_bank));
	if (!abov32v1_info) {
		LOG_ERROR("Out of memory");
		return ERROR_FAIL;
	}

	bank->driver_priv = abov32v1_info;
	abov32v1_info->probed = false;

	return ERROR_OK;
}

static int abov32v1_erase(struct flash_bank *bank, unsigned int first,
		unsigned int last)
{
	struct abov32v1_flash_bank *abov32v1_info = bank->driver_priv;
	const struct abov32v1_erase *erase_info = &abov32v1_info->dev->erase;
	int ret = ERROR_OK;
	uint32_t sec_cnt = 0;
	uint32_t sec_offset = first;
	uint32_t wprot_first = 0, wprot_last = 0;
	uint32_t start_addr = 0, end_addr = 0;

#if defined DEBUG_LOG
	LOG_INFO("erase first=%d last=%d", first, last);
#endif

	/* find fist and last protect block index */
	start_addr = first * abov32v1_info->dev->page_size;
	end_addr = last * abov32v1_info->dev->page_size;

	for (unsigned int i = 0; i < bank->num_prot_blocks; i++) {
		if (start_addr >= bank->prot_blocks[i].offset) {
			wprot_first = i;
			break;
		}
	}

	for (unsigned int i = 0; i < bank->num_prot_blocks; i++) {
		if (end_addr >= bank->prot_blocks[i].offset
			&& (end_addr < bank->prot_blocks[i].offset + bank->prot_blocks[i].size)) {
			wprot_last = i;
			break;
		}
	}

#if defined DEBUG_LOG
	LOG_INFO("start=0x%x, end=0x%x, first=%d, last=%d\n", start_addr, end_addr, wprot_first, wprot_last);
#endif

	/* unlock protection area */
	abov32v1_set_write_protect(bank, FLASH_CODE_PROT_UNLOCK, wprot_first, wprot_last);

	sec_cnt = (last - first) + 1;
	if (sec_cnt == bank->num_sectors) {
		ret = abov32v1_set_erase(bank, bank->sectors[0].offset, erase_mode_chip);
	} else {
		while (sec_cnt != 0) {
			if (erase_info->support_mode & erase_mode_page
				&& (((sec_offset & (erase_info->min_sector_by_page_cnt - 1)) != 0x0)
					 || (sec_cnt <= erase_info->min_sector_by_page_cnt - 1))) {
#if defined DEBUG_LOG
				LOG_INFO("page erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v1_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_page);
				if (ret != ERROR_OK)
					break;

				sec_offset++;
				sec_cnt--;
			} else if ((erase_info->support_mode & erase_mode_2kb)
					&& (sec_cnt >= FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT)) {
#if defined DEBUG_LOG
				LOG_INFO("2KB erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v1_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_1kb);
				if (ret != ERROR_OK)
					break;

				sec_offset += FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT;
				sec_cnt -= FLASH_CODE_2KB_SECTOR_BY_PAGE_COUNT;
			} else if ((erase_info->support_mode & erase_mode_1kb)
					&& (sec_cnt >= FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT)) {
#if defined DEBUG_LOG
				LOG_INFO("1KB erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
				ret = abov32v1_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_1kb);
				if (ret != ERROR_OK)
					break;

				sec_offset += FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT;
				sec_cnt -= FLASH_CODE_1KB_SECTOR_BY_PAGE_COUNT;
			}
		}
	}

	if (ret == ERROR_OK) {
		for (unsigned int i = wprot_first; i <= wprot_last; i++)
			bank->sectors[i].is_erased = 1;
	}

	return ret;
}

static int abov32v1_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last)
{
	return abov32v1_set_write_protect(bank, (set == 1 ? FLASH_CODE_PROT_LOCK : FLASH_CODE_PROT_UNLOCK), first, last);
}

static int abov32v1_set_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	struct armv7m_algorithm armv7m_method;
	struct working_area *algobuffer, *imgbuffer;
	struct reg_param reg_params[5];

	int ret = ERROR_OK;
	uint32_t addr = 0;
	uint32_t bufsize = 1024;

	static const uint8_t abov32v1_algo_code[] = {
#include "../../../contrib/loaders/flash/abovsemi/abov32v1.inc"
	};

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* stop watchdog */
	target_write_u32(target, WATCHDOG_CTRL_REG, WATCHDOG_DISABLE);

	LOG_INFO("Writing buffer to flash address=0x%08x count=%d bytes", offset, count);

	/* enable code flash */
	ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, FLASH_CODE_CTRL_CODE_SEL, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* enable flash rom */
	ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, FLASH_CODE_CTRL_ROM_SEL, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* enable program mode */
	ret = abov32v1_set_clear_reg(bank, FLASH_CODE_CTRL_REG, FLASH_CODE_CTRL_PROGRAM, FLASH_ENABLE);
	if (ret != ERROR_OK)
		goto err;

	/* set flash address */
	addr = (bank->base + offset) & ~(FLASH_WORD_ALIGN_MASK);

	/* alloc algo code area in MCU memory */
	ret = target_alloc_working_area(target, sizeof(abov32v1_algo_code), &algobuffer);
	if (ret != ERROR_OK)
		goto err;

	/* upload algo code into memory */
	ret = target_write_buffer(target, algobuffer->address, sizeof(abov32v1_algo_code), abov32v1_algo_code);
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
	init_reg_param(&reg_params[4], "r4", 32, PARAM_OUT);	/* destination flash address */

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

	return ret;
}

static int abov32v1_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
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

	ret = abov32v1_set_write(bank, align_buf, offset, align_cnt);
	if (ret != ERROR_OK)
		LOG_ERROR("couldn't write data to flash memory.");

	if (count & FLASH_WORD_ALIGN_MASK)
		free(align_buf);

	return ret;
}

static int abov32v1_probe(struct flash_bank *bank)
{
	struct abov32v1_flash_bank *abov32v1_info = bank->driver_priv;
	const struct abov32v1_wprot *wprot;
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t chipset_id;

	if (abov32v1_info->probed)
		return ERROR_OK;

	/* set high speed internal clock */
	target_write_u32(target, SYSTEM_CLKSRC_REG, SYSTEM_CLKSRC_HSI);
	target_write_u32(target, SYSTEM_CLKCR_REG, SYSTEM_CLKSEL_HSI);

	ret = abov32v1_get_chipset_id(bank, &chipset_id);
	if (ret != ERROR_OK)
		goto err;

	/* find and get geometry info */
	abov32v1_info->dev = abov32v1_find_device(chipset_id);
	if (!abov32v1_info->dev)
		return ERROR_FAIL;

	wprot = abov32v1_info->dev->wprot;

	/* code area size */
	bank->size = abov32v1_info->dev->code_size;

	/* start address */
	bank->base = 0;

	/* num of sectors (page == sector) */
	bank->num_sectors = abov32v1_info->dev->code_size / abov32v1_info->dev->page_size;
	/* alloc sector blocks */
	free(bank->sectors);
	bank->sectors = NULL;
	bank->sectors = alloc_block_array(0, abov32v1_info->dev->page_size, bank->num_sectors);
	if (!bank->sectors) {
		ret = ERROR_FAIL;
		goto err;
	}

	/* num of protection segments (blocks) */
	for (unsigned int i = 0; i < FLASH_CODE_MAX_WPROT_CNT; i++)
		bank->num_prot_blocks += wprot[i].num_wprot;

	if (!bank->num_prot_blocks) {
		ret = ERROR_FAIL;
		goto err;
	}

	/* alloc protection blocks */
	free(bank->prot_blocks);
	bank->prot_blocks = NULL;
	bank->prot_blocks = malloc(sizeof(struct flash_sector) * bank->num_prot_blocks);
	if (!bank->prot_blocks) {
		ret = ERROR_FAIL;
		free(bank->sectors);
		goto err;
	}

	for (unsigned int i = 0, j = 0; i < FLASH_CODE_MAX_WPROT_CNT; i++) {
		for (unsigned int k = 0; k < wprot[i].num_wprot; k++) {
			bank->prot_blocks[j].offset = wprot[i].offset + ((wprot[i].size) * k);
			bank->prot_blocks[j++].size = wprot[i].size;
#if defined DEBUG_LOG
			LOG_INFO("%d, offset=0x%x, size=0x%x\n", j - 1, bank->prot_blocks[j - 1].offset,
													bank->prot_blocks[j - 1].size);
#endif
		}
	}

	/* update protection bits */
	ret = abov32v1_protect_check(bank);
	if (ret != ERROR_OK)
		goto err;

	abov32v1_info->probed = true;
#if defined DEBUG_LOG
	LOG_INFO("ID:0x%08x, size=0x%08x, page=0x%08x, sectors=%d, prot=%d",
			chipset_id, bank->size, abov32v1_info->dev->page_size, bank->num_sectors, bank->num_prot_blocks);
#endif

err:
	return ret;
}

static int abov32v1_auto_probe(struct flash_bank *bank)
{
	struct abov32v1_flash_bank *abov32v1_info = bank->driver_priv;
	if (abov32v1_info->probed)
		return ERROR_OK;
	return abov32v1_probe(bank);
}

static int abov32v1_info(struct flash_bank *bank, struct command_invocation *cmd)
{
	struct abov32v1_flash_bank *abov32v1_info = bank->driver_priv;

	if (abov32v1_info->probed)
		command_print_sameline(cmd, "\nABOV 32Bit MCU - %s", abov32v1_info->dev->device_str);
	else
		command_print_sameline(cmd, "\nNo chipset detected");

	return ERROR_OK;
}

COMMAND_HANDLER(abov32v1_handle_lock_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v1_set_write_protect(bank, FLASH_CODE_PROT_LOCK, 0, (bank->num_prot_blocks - 1));
err:
	return ret;
}

COMMAND_HANDLER(abov32v1_handle_unlock_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v1_set_write_protect(bank, FLASH_CODE_PROT_UNLOCK, 0, (bank->num_prot_blocks - 1));
err:
	return ret;
}

COMMAND_HANDLER(abov32v1_handle_chip_erase_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	ret = abov32v1_set_erase(bank, bank->sectors[0].offset, erase_mode_chip);
err:
	return ret;
}

static const struct command_registration abov32v1_exec_command_handlers[] = {
	{
		.name = "lock",
		.handler = abov32v1_handle_lock_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Lock entire flash device.",
	},
	{
		.name = "unlock",
		.handler = abov32v1_handle_unlock_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Unlock entire protected flash device.",
	},
	{
		.name = "chip_erase",
		.handler = abov32v1_handle_chip_erase_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Erase entire flash device.",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration abov32v1_command_handlers[] = {
	{
		.name = "abov32v1",
		.mode = COMMAND_ANY,
		.help = "abov32v1 flash command group",
		.usage = "",
		.chain = abov32v1_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

const struct flash_driver abov32v1_flash = {
	.name = "abov32v1",
	.commands = abov32v1_command_handlers,
	.flash_bank_command = abov32v1_flash_bank_command,
	.erase = abov32v1_erase,
	.protect = abov32v1_protect,
	.write = abov32v1_write,
	.read = default_flash_read,
	.probe = abov32v1_probe,
	.auto_probe = abov32v1_auto_probe,
	.erase_check = default_flash_blank_check,
	.protect_check = abov32v1_protect_check,
	.info = abov32v1_info,
	.free_driver_priv = default_flash_free_driver_priv,
};
