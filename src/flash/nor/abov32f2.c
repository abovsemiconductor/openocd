// SPDX-License-Identifier: BSD-3-Clause
/**
 *******************************************************************************
 * @file        abov32f2.c
 * @author      ABOVSemiconductor R&D Division
 * @brief       Nor Flash Driver for ABOV32 F2
 *
 * Copyright 2025 ABOVSemiconductor Co.,Ltd. All rights reserved.
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

#define A31L21X_FLASH_64KB				0x1F011
#define A31L21X_FLASH_128KB				0x1F010
#define A31L21X_FLASH_CODE_BASE_ADDR	0x10000000
#define A31L21X_FLASH_REG_ADDR			0x40000100

#define A31S13X_FLASH_128KB				0x11340
#define A31S13X_FLASH_CODE_BASE_ADDR	0x10000000
#define A31S13X_FLASH_REG_ADDR			0x40001B00

#define ABOV_CHIP_ID_MASK				0xFFFFF

/* Flash Base Address */
#define FLASH_BASE_ADDR                 0x10000000
#define DFLASH_BASE_ADDR                0x18800000

/* Flash Mode */
#define FLASH_CODE_ADR_RESET_CMD        0x5FFFFF80
#define FLASH_CODE_ADR_CMD              0x5FFFFFFF
#define FLASH_CODE_IDR1_CMD             0x08192A3B
#define FLASH_CODE_IDR2_CMD             0x4C5D6E7F

/* DFlash Mode */
#define DFLASH_CODE_ADR_RESET_CMD       0x5FFF9990
#define DFLASH_CODE_ADR_CMD             0x59999990
#define DFLASH_CODE_IDR1_CMD            0xA9C46E91
#define DFLASH_CODE_IDR2_CMD            0x78F5B3D2

/* Flash Chip Erase Value */
#define FLASH_CODE_ERASE_CHIP           0x5F9A30D7
#define DFLASH_CODE_ERASE_CHIP          0x4BC27F54

/* Flash Read Protection Level 0 */
#define FLASH_RPROT_LEVEL_0             0x69C8A273

/* Flash Watch-Dog Timer */
#define FLASH_WDT_DISABLE               0xFFFF96D3

/* Flash Reg Offset */
#define FLASH_CODE_REG_START_ADDR		0x40001B00
#define FLASH_CODE_ADDR_REG             (FLASH_CODE_REG_START_ADDR | 0x00000000)
#define FLASH_CODE_IDR1_REG             (FLASH_CODE_REG_START_ADDR | 0x00000004)
#define FLASH_CODE_IDR2_REG             (FLASH_CODE_REG_START_ADDR | 0x00000008)
#define FLASH_CODE_CR_REG               (FLASH_CODE_REG_START_ADDR | 0x0000000C)
#define FLASH_CODE_COA_BULK_REG         (FLASH_CODE_REG_START_ADDR | 0x00000010)
#define FLASH_CODE_ERR_REG              (FLASH_CODE_REG_START_ADDR | 0x00000014)

/* DFlash Reg Offset */
#define DFLASH_CODE_REG_START_ADDR      0x40001D00
#define DFLASH_CODE_ADDR_REG            (DFLASH_CODE_REG_START_ADDR | 0x00000000)
#define DFLASH_CODE_IDR1_REG            (DFLASH_CODE_REG_START_ADDR | 0x00000004)
#define DFLASH_CODE_IDR2_REG            (DFLASH_CODE_REG_START_ADDR | 0x00000008)
#define DFLASH_CODE_CR_REG              (DFLASH_CODE_REG_START_ADDR | 0x0000000C)
#define DFLASH_CODE_ERR_REG             (DFLASH_CODE_REG_START_ADDR | 0x00000014)

/* Flash Page Buffer */
#define FLASH_CODE_PAGE_BUFFER_ADDR     0x40001C00
#define FLASH_CODE_PAGE_BUFFER_SIZE     256

/* DFlash Page Buffer */
#define DFLASH_CODE_PAGE_BUFFER_ADDR    0x40001E00
#define DFLASH_CODE_PAGE_BUFFER_SIZE    32

/* Flash Configration Option Area */
#define FLASH_COA_START_ADDR            0x1FFFF200
#define FLASH_COA_RPROT_FMA             (FLASH_COA_START_ADDR | 0x00000000)
#define FLASH_COA_WPROT_COA             (FLASH_COA_START_ADDR | 0x00000014)
#define FLASH_COA_WPROT_FMA             (FLASH_COA_START_ADDR | 0x00000040)
#define FLASH_COA_WPROT_FMA1            (FLASH_COA_START_ADDR | 0x00000044)
#define FLASH_COA_WPROT_DFMA            (FLASH_COA_START_ADDR | 0x00000070)

/* Flash Read Protection Level */
#define FLASH_RPROT_LEVEL_0             0x69C8A273
#define FLASH_RPROT_LEVEL_1             0x69C8A272
#define FLASH_RPROT_LEVEL_2             0x69C8A271

/* FLASH COA Data Index */
#define FLASH_COA_RPROT_LEVEL_IDX       0
#define FLASH_COA_WDT_IDX               3
#define FLASH_COA_WPROT_IDX             16
#define FLASH_COA_WPROT_IDX1            17

/* Flash Control Reg bit position */
#define FLASH_CODE_CR_PAGE_BUF_RESET         BIT(0)
#define FLASH_CODE_CR_ERASE_PAGE             BIT(1)
#define FLASH_CODE_CR_WRITE_PAGE             BIT(2)
#define FLASH_CODE_CR_ERASE_CHIP             BIT(3)
#define FLASH_CODE_CR_OPS_PAGE_BUF_RESET     (0x00 << 8)
#define FLASH_CODE_CR_OPS_COA_MODE           (0x38 << 8)
#define FLASH_CODE_CR_OPS_FMA_MODE           (0xA4 << 8)
#define FLASH_CODE_CR_WTIDKY_VAL             (0x6C93 << 16)
#define DFLASH_CODE_CR_WTIDKY_VAL            (0xB69CU << 16)

/* Flash Bulk Erase Control Reg bit position */
#define FLASH_CODE_BCR_OPS_COA_BULK          (0x05)
#define FLASH_CODE_BCR_OPS_COA_PAGE1         (FLASH_CODE_BCR_OPS_COA_BULK << 0)
#define FLASH_CODE_BCR_OPS_COA_PAGE2         (FLASH_CODE_BCR_OPS_COA_BULK << 4)
#define FLASH_CODE_BCR_OPS_COA_PAGE3         (FLASH_CODE_BCR_OPS_COA_BULK << 8)
#define FLASH_CODE_BCR_WTIDKY_VAL            (0xC1BEU << 16)

/* Flash Status Reg bit position */
#define FLASH_CODE_WBUSY_REG_BUSY			 BIT(7)

/* Flash Error Reg bit position */
#define FLASH_CODE_ERR_FLAG                  BIT(0)

#define FLASH_PROT_LOCK					0x00000001
#define FLASH_PROT_UNLOCK				0x00000000
#define FLASH_ERASE_TIMEOUT				100
#define FLASH_WORD_ALIGN				4
#define FLASH_WPROT_EXT_MAX_COUNT		32
#define FLASH_RPROT_MASK				0x03

enum erase_mode {
	erase_mode_page,
	erase_mode_1kb,
	erase_mode_4kb,
	erase_mode_chip,
	erase_mode_max
};

static const struct abov32f2_dev {
	uint32_t id;
	const char *device_str;
	uint32_t code_size;
	uint32_t system_size;
	uint32_t page_size;
	uint32_t wprot_seg_size;
	bool abwprot;				/* active bootloader write protection */
} abov32f2_devices[] = {
{
	.id = A31L21X_FLASH_64KB,
	.device_str = "A31L213 : FLASH 64KB / SRAM 20KB ",
	.code_size = 0x10000,
	.system_size = 0x0,
	.page_size = 0x100,
	.wprot_seg_size = 0x800,
	.abwprot = false
}, {
	.id = A31L21X_FLASH_128KB,
	.device_str = "A31L214 : FLASH 128KB / SRAM 20KB",
	.code_size = 0x20000,
	.system_size = 0x0,
	.page_size = 0x100,
	.wprot_seg_size = 0x800,
	.abwprot = false
}, {
	.id = A31S13X_FLASH_128KB,
	.device_str = "A31S134 : FLASH 128KB / SRAM 16KB",
	.code_size = 0x20000,
	.system_size = 0x0,
	.page_size = 0x100,
	.wprot_seg_size = 0x800,
	.abwprot = false
}};

struct abov32f2_flash_bank {
	bool probed;
	const struct abov32f2_dev *dev;
};

static int abov32f2_set_erase(struct flash_bank *bank, uint32_t pageaddr, enum erase_mode mode);

static int abov32f2_wait_busy(struct flash_bank *bank, int timeout)
{
	struct target *target = bank->target;
	uint32_t busy = 0;

	while (1) {
		int ret = target_read_u32(target, FLASH_CODE_CR_REG, &busy);
		if (ret != ERROR_OK)
			return ret;

		if (!(busy & FLASH_CODE_WBUSY_REG_BUSY))
			break;

		if (timeout-- <= 0)
			return ERROR_TIMEOUT_REACHED;
	}

	return ERROR_OK;
}

static int abov32f2_set_preops(struct flash_bank *bank)
{
	struct target *target = bank->target;
	uint8_t pagebuf[FLASH_CODE_PAGE_BUFFER_SIZE];
	int ret;

	/* clear error flag */
	ret = target_write_u32(target, FLASH_CODE_ERR_REG, 0x00000001);
	if (ret != ERROR_OK)
	    goto err;

	/* operation mode */
	ret = target_write_u32(target, FLASH_CODE_ADDR_REG, FLASH_CODE_ADR_CMD);
	if (ret != ERROR_OK)
	    goto err;
	ret = target_write_u32(target, FLASH_CODE_IDR1_REG, FLASH_CODE_IDR1_CMD);
	if (ret != ERROR_OK)
	    goto err;
	ret = target_write_u32(target, FLASH_CODE_IDR2_REG, FLASH_CODE_IDR2_CMD);
	if (ret != ERROR_OK)
	    goto err;

	/* clear buffer */
	ret = target_write_u32(target, FLASH_CODE_CR_REG, FLASH_CODE_CR_WTIDKY_VAL | FLASH_CODE_CR_PAGE_BUF_RESET);
	if (ret != ERROR_OK)
	    goto err;
	
	memset(pagebuf, 0xff, FLASH_CODE_PAGE_BUFFER_SIZE);

	ret = target_write_memory(target, FLASH_CODE_PAGE_BUFFER_ADDR, 4, FLASH_CODE_PAGE_BUFFER_SIZE / 4, pagebuf);

err:
	return ret;
}

static int abov32f2_dflash_wait_busy(struct flash_bank *bank, int timeout)
{
	struct target *target = bank->target;
	uint32_t busy = 0;

	while (1) {
		int ret = target_read_u32(target, DFLASH_CODE_CR_REG, &busy);
		if (ret != ERROR_OK)
			return ret;

		if (!(busy & FLASH_CODE_WBUSY_REG_BUSY))
			break;

		if (timeout-- <= 0)
			return ERROR_TIMEOUT_REACHED;
	}

	return ERROR_OK;
}

static int abov32f2_dflash_set_preops(struct flash_bank *bank)
{
	struct target *target = bank->target;
	uint8_t pagebuf[DFLASH_CODE_PAGE_BUFFER_SIZE];
	int ret;

	/* clear error flag */
	ret = target_write_u32(target, DFLASH_CODE_ERR_REG, 0x00000001);
	if (ret != ERROR_OK)
	    goto err;

	/* operation mode */
	ret = target_write_u32(target, DFLASH_CODE_ADDR_REG, DFLASH_CODE_ADR_CMD);
	if (ret != ERROR_OK)
	    goto err;
	ret = target_write_u32(target, DFLASH_CODE_IDR1_REG, DFLASH_CODE_IDR1_CMD);
	if (ret != ERROR_OK)
	    goto err;
	ret = target_write_u32(target, DFLASH_CODE_IDR2_REG, DFLASH_CODE_IDR2_CMD);
	if (ret != ERROR_OK)
	    goto err;

	/* clear buffer */
	ret = target_write_u32(target, DFLASH_CODE_CR_REG, DFLASH_CODE_CR_WTIDKY_VAL | FLASH_CODE_CR_PAGE_BUF_RESET);
	if (ret != ERROR_OK)
	    goto err;

	memset(pagebuf, 0xff, DFLASH_CODE_PAGE_BUFFER_SIZE);

	ret = target_write_memory(target, DFLASH_CODE_PAGE_BUFFER_ADDR, 4, DFLASH_CODE_PAGE_BUFFER_SIZE / 4, pagebuf);

err:
	return ret;
}

static int abov32f2_dflash_set_erase(struct flash_bank *bank, uint32_t pageaddr, enum erase_mode mode)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;

	/* set operation mode */
    ret = abov32f2_dflash_set_preops(bank);
	if (ret != ERROR_OK)
	    goto err;

	/* select erase area mode */
	switch (mode) {
		case erase_mode_chip:
	        target_write_u32(target, DFLASH_CODE_ADDR_REG, DFLASH_CODE_ERASE_CHIP);
			target_write_u32(target, DFLASH_CODE_CR_REG, DFLASH_CODE_CR_WTIDKY_VAL | FLASH_CODE_CR_OPS_FMA_MODE | FLASH_CODE_CR_ERASE_CHIP);
			break;
		default:
			target_write_u32(target, DFLASH_CODE_ADDR_REG, pageaddr | FLASH_BASE_ADDR);
			target_write_u32(target, DFLASH_CODE_CR_REG, DFLASH_CODE_CR_WTIDKY_VAL | FLASH_CODE_CR_OPS_FMA_MODE | FLASH_CODE_CR_ERASE_PAGE);
			break;
	}

	if (ret != ERROR_OK)
		goto err;

	/* wait a write is done */
	ret = abov32f2_dflash_wait_busy(bank, FLASH_ERASE_TIMEOUT);
	if (ret != ERROR_OK)
		goto err;

err:
	return ret;
}

static int abov32f2_get_chipset_id(struct flash_bank *bank, uint32_t *chipset_id)
{
	struct target *target = bank->target;
	int ret = target_read_u32(target, ABOV_CHIP_ID_REG, chipset_id);
	return ret;
}

const struct abov32f2_dev *abov32f2_find_device(uint32_t id)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(abov32f2_devices); i++)
		if ((id & ABOV_CHIP_ID_MASK) == abov32f2_devices[i].id)
			return &abov32f2_devices[i];
	return NULL;
}

static int abov32f2_set_pagewrite(struct flash_bank *bank, uint8_t *buffer, uint32_t addr, uint32_t count, bool bcoa)
{
	struct target *target = bank->target;
	int ret;
	uint32_t cmd = 0;

	if (count > FLASH_CODE_PAGE_BUFFER_SIZE)
	{
	    ret = ERROR_FAIL;
		goto err;
	}
	
	/* set operation mode */
    ret = abov32f2_set_preops(bank);
	if (ret != ERROR_OK)
	    goto err;

	ret = target_write_memory(target, FLASH_CODE_PAGE_BUFFER_ADDR, 4, count / 4, buffer);
	if (ret != ERROR_OK)
	    goto err;


	ret = target_write_u32(target, FLASH_CODE_ADDR_REG, addr | FLASH_BASE_ADDR);
	if (ret != ERROR_OK)
	    goto err;

	cmd = FLASH_CODE_CR_WTIDKY_VAL | FLASH_CODE_CR_WRITE_PAGE;

	if (bcoa)
	    cmd |= FLASH_CODE_CR_OPS_COA_MODE;
	else
	    cmd |= FLASH_CODE_CR_OPS_FMA_MODE;

	/* flash write from page buffer to configuration option area */
	ret = target_write_u32(target, FLASH_CODE_CR_REG, cmd);
	if (ret != ERROR_OK)
	    goto err;

	ret = abov32f2_wait_busy(bank, FLASH_ERASE_TIMEOUT);

err:
	return ret;
}

static int abov32f2_set_rprotlvl(struct flash_bank *bank, uint32_t lvl)
{
	struct target *target = bank->target;
	int ret;
	unsigned char coabuf[FLASH_CODE_PAGE_BUFFER_SIZE];
	uint32_t *pcoabuf = (uint32_t *)coabuf;
	uint32_t reg;

	ret = target_read_u32(target, FLASH_COA_RPROT_FMA, &reg);
	if (ret != ERROR_OK)
	    goto err;

	if ((reg == lvl) && ((reg & FLASH_RPROT_MASK) == (FLASH_RPROT_LEVEL_0 & FLASH_RPROT_MASK)))
	{
		LOG_INFO("read protect level 0 is already set.");
		ret = ERROR_OK;
	    goto err;
	}

	memset(coabuf, 0x00, FLASH_CODE_PAGE_BUFFER_SIZE);

	ret = target_read_memory(target, FLASH_COA_START_ADDR, FLASH_WORD_ALIGN, 
					         FLASH_CODE_PAGE_BUFFER_SIZE / FLASH_WORD_ALIGN, coabuf);
	if (ret != ERROR_OK)
	    goto err;

	pcoabuf[FLASH_COA_RPROT_LEVEL_IDX] = lvl;

	/* erase bulk */
	if((reg & FLASH_RPROT_MASK) != (FLASH_RPROT_LEVEL_0 & FLASH_RPROT_MASK))
	{
		LOG_INFO("bulk erase, read protect is 0x%x", reg & FLASH_RPROT_MASK);
		ret = abov32f2_dflash_set_erase(bank, DFLASH_BASE_ADDR, erase_mode_chip);
		ret |= abov32f2_set_erase(bank, FLASH_COA_START_ADDR, erase_mode_chip);
		if (ret != ERROR_OK)
			goto err;
	}

	/* erase coa */
    ret = abov32f2_set_erase(bank, FLASH_COA_START_ADDR, erase_mode_page);
	if (ret != ERROR_OK)
		goto err;

    ret = abov32f2_set_pagewrite(bank, coabuf, FLASH_COA_START_ADDR, FLASH_CODE_PAGE_BUFFER_SIZE, true);

err:
	return ret;
}

static int abov32f2_protect_check(struct flash_bank *bank)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t reg = 0, reg1 = 0;

	/* read protection bits */
	ret = target_read_u32(target, FLASH_COA_WPROT_FMA, &reg);
	if (ret != ERROR_OK)
		goto err;

	/* read protection bits more 32 bit */
	if (bank->num_prot_blocks > FLASH_WPROT_EXT_MAX_COUNT) {
		ret = target_read_u32(target, FLASH_COA_WPROT_FMA1, &reg1);
		if (ret != ERROR_OK)
			goto err;
	}

#if defined DEBUG_LOG
	LOG_INFO("cur wprotect:0x%08x, wprotect blocks:%d\n", reg, bank->num_prot_blocks);
#endif
	for (unsigned int i = 0; i < bank->num_prot_blocks; i++) {
		if (i < FLASH_WPROT_EXT_MAX_COUNT) {
			bank->prot_blocks[i].is_protected = (reg & (FLASH_PROT_LOCK << i)) ? FLASH_PROT_UNLOCK : FLASH_PROT_LOCK;
		} else {
			bank->prot_blocks[i].is_protected = (reg1 & (FLASH_PROT_LOCK << (i-FLASH_WPROT_EXT_MAX_COUNT))) ? FLASH_PROT_UNLOCK : FLASH_PROT_LOCK;
		}
#if defined DEBUG_LOG
		LOG_INFO("%d block is %s", i, (bank->prot_blocks[i].is_protected == 1) ? "lock" : "unlock");
#endif
	}
err:
	return ret;
}

static int abov32f2_set_write_protect(struct flash_bank *bank, int set, uint32_t first, uint32_t last)
{
	int ret = ERROR_OK;
	struct target *target = bank->target;
#if defined DEBUG_LOG
    uint32_t reg = 0;
#endif
	uint32_t prot = 0, prot1 = 0;
	unsigned char coabuf[FLASH_CODE_PAGE_BUFFER_SIZE];
	uint32_t *pcoabuf = (uint32_t *)coabuf;

	memset(coabuf, 0x00, FLASH_CODE_PAGE_BUFFER_SIZE);

	ret = target_read_memory(target, FLASH_COA_START_ADDR, FLASH_WORD_ALIGN, 
					         FLASH_CODE_PAGE_BUFFER_SIZE / FLASH_WORD_ALIGN, coabuf);
	if (ret != ERROR_OK)
	    goto err;


	for (unsigned int i = first; i <= last; i++) {
		if (i < FLASH_WPROT_EXT_MAX_COUNT) {
	   		prot |= (FLASH_PROT_LOCK << i);
		} else {
	   		prot1 |= (FLASH_PROT_LOCK << (i-FLASH_WPROT_EXT_MAX_COUNT));
		}
	}

#if defined DEBUG_LOG
	(void)target_read_u32(target, FLASH_COA_WPROT_FMA, &reg);
	LOG_INFO("cur write protection:0x%08x, protection bits:0x%08x", reg, prot);
#endif

	if (set) {
		pcoabuf[FLASH_COA_WPROT_IDX] &= ~prot;
		pcoabuf[FLASH_COA_WPROT_IDX1] &= ~prot1;
	} else {
		pcoabuf[FLASH_COA_WPROT_IDX] |= prot;
		pcoabuf[FLASH_COA_WPROT_IDX1] |= prot1;
	}

	/* erase coa */
    ret = abov32f2_set_erase(bank, FLASH_COA_START_ADDR, erase_mode_page);
    if (ret != ERROR_OK)
	    goto err;

    ret = abov32f2_set_pagewrite(bank, coabuf, FLASH_COA_START_ADDR, FLASH_CODE_PAGE_BUFFER_SIZE, true);
    if (ret != ERROR_OK)
	    goto err;

#if defined DEBUG_LOG
	(void)target_read_u32(target, FLASH_COA_WPROT_FMA, &reg);
	LOG_INFO("new write protection:0x%08x", reg);
#endif

	/* update protection bits in a bank */
	ret = abov32f2_protect_check(bank);

err:
	return ret;
}

static int abov32f2_set_erase(struct flash_bank *bank, uint32_t pageaddr, enum erase_mode mode)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;

	/* set operation mode */
    ret = abov32f2_set_preops(bank);
	if (ret != ERROR_OK)
	    goto err;

	/* select erase area mode */
	switch (mode) {
		case erase_mode_chip:
	        if (pageaddr == FLASH_COA_START_ADDR) {
	            LOG_INFO("set CNF1BEN bit in the BCR register");
	            target_write_u32(target, FLASH_CODE_COA_BULK_REG, FLASH_CODE_BCR_WTIDKY_VAL | FLASH_CODE_BCR_OPS_COA_PAGE1);
	        }
	        target_write_u32(target, FLASH_CODE_ADDR_REG, FLASH_CODE_ERASE_CHIP);
	        target_write_u32(target, FLASH_CODE_CR_REG, FLASH_CODE_CR_WTIDKY_VAL | FLASH_CODE_CR_OPS_FMA_MODE | FLASH_CODE_CR_ERASE_CHIP);
			break;
		default:
			target_write_u32(target, FLASH_CODE_ADDR_REG, pageaddr | FLASH_BASE_ADDR);
#if 1
			if (pageaddr == FLASH_COA_START_ADDR)
	            target_write_u32(target, FLASH_CODE_CR_REG, FLASH_CODE_CR_WTIDKY_VAL | FLASH_CODE_CR_OPS_COA_MODE | FLASH_CODE_CR_ERASE_PAGE);
			else
	            target_write_u32(target, FLASH_CODE_CR_REG, FLASH_CODE_CR_WTIDKY_VAL | FLASH_CODE_CR_OPS_FMA_MODE | FLASH_CODE_CR_ERASE_PAGE);
			/* page erase mode */
#endif
			break;
	}

	if (ret != ERROR_OK)
		goto err;

	/* wait a write is done */
	ret = abov32f2_wait_busy(bank, FLASH_ERASE_TIMEOUT);
	if (ret != ERROR_OK)
		goto err;

err:
	return ret;
}

FLASH_BANK_COMMAND_HANDLER(abov32f2_flash_bank_command)
{
	/* Need syntax count from flash bank command */
	if (CMD_ARGC <  6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct abov32f2_flash_bank *abov32f2_info;
	abov32f2_info = malloc(sizeof(struct abov32f2_flash_bank));
	if (!abov32f2_info) {
		LOG_ERROR("Out of memory");
		return ERROR_FAIL;
	}

	bank->driver_priv = abov32f2_info;
	abov32f2_info->probed = false;

	return ERROR_OK;
}

static int abov32f2_erase(struct flash_bank *bank, unsigned int first,
		unsigned int last)
{
	struct abov32f2_flash_bank *abov32f2_info = bank->driver_priv;
	int ret = ERROR_OK;
	uint32_t sec_cnt = 0;
	uint32_t sec_offset = first;
	uint32_t wprot_first = 0, wprot_last = 0;
	uint32_t start_addr = 0, end_addr = 0;

#if defined DEBUG_LOG
	LOG_INFO("erase first=%d last=%d", first, last);
#endif

	/* find fist and last protect block index */
	start_addr = first * abov32f2_info->dev->page_size;
	end_addr = last * abov32f2_info->dev->page_size;

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
	abov32f2_set_write_protect(bank, FLASH_PROT_UNLOCK, wprot_first, wprot_last);

	sec_cnt = (last - first) + 1;
	if (sec_cnt == bank->num_sectors) {
		LOG_INFO("chip/bulk erase.");
		ret = abov32f2_set_erase(bank, bank->sectors[0].offset, erase_mode_chip);
	} else {
		while (sec_cnt != 0) {
#if defined DEBUG_LOG
            LOG_INFO("page erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
			if(!(sec_offset % 8))
  			    LOG_INFO("erase : 0x%08x.", bank->sectors[sec_offset].offset);
            ret = abov32f2_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_page);
            if (ret != ERROR_OK)
                break;

			sec_offset++;
            sec_cnt--;

            keep_alive();
		}
	}

	if (ret == ERROR_OK) {
		for (unsigned int i = first; i <= last; i++)
			bank->sectors[i].is_erased = 1;
	}

	return ret;
}

static int abov32f2_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last)
{
	return abov32f2_set_write_protect(bank, set, first, last);
}

static int abov32f2_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint8_t *pbuf = (uint8_t *)buffer;

	uint32_t quot = 0, remaind = 0, idx = 0;
	bool completed = false;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	quot = count / FLASH_CODE_PAGE_BUFFER_SIZE;
    remaind = count % FLASH_CODE_PAGE_BUFFER_SIZE;
	idx = 0;

	do {
		if(!(idx % 8))
  		    LOG_INFO("write : 0x%08x.", offset + (idx * FLASH_CODE_PAGE_BUFFER_SIZE));

	    ret = abov32f2_set_pagewrite(bank, &pbuf[idx * FLASH_CODE_PAGE_BUFFER_SIZE],
			                         offset + (idx * FLASH_CODE_PAGE_BUFFER_SIZE), 
									 FLASH_CODE_PAGE_BUFFER_SIZE, false);
       	if (ret != ERROR_OK)
		{
	        LOG_ERROR("couldn't write data to flash memory.");
			goto err;
		}

		idx++;

		if (idx == quot)
		{
		    completed = true;
		}

	    keep_alive();

    } while(!completed);

	if (remaind != 0)
    {
  	    LOG_INFO("write : 0x%08x.", offset + (idx * FLASH_CODE_PAGE_BUFFER_SIZE));
	    ret = abov32f2_set_pagewrite(bank, &pbuf[idx * FLASH_CODE_PAGE_BUFFER_SIZE], 
						             offset + (idx * FLASH_CODE_PAGE_BUFFER_SIZE), 
									 remaind, false);
       	if (ret != ERROR_OK)
	        LOG_ERROR("couldn't write data to flash memory.");
    }

err:
	return ret;
}

static int abov32f2_probe(struct flash_bank *bank)
{
	struct abov32f2_flash_bank *abov32f2_info = bank->driver_priv;
	int ret = ERROR_OK;
	uint32_t chipset_id;

	ret = abov32f2_get_chipset_id(bank, &chipset_id);
	if (ret != ERROR_OK)
		goto err;

	/* find and get geometry info */
	abov32f2_info->dev = abov32f2_find_device(chipset_id);

	if (!abov32f2_info->dev)
		return ERROR_FAIL;

	/* code area size */
	bank->size = abov32f2_info->dev->code_size;

	/* start address */
	bank->base = 0;

	/* num of sectors (page == sector) */
	bank->num_sectors = abov32f2_info->dev->code_size / abov32f2_info->dev->page_size;
	/* alloc sector blocks */
	free(bank->sectors);
	bank->sectors = NULL;
	bank->sectors = alloc_block_array(0, abov32f2_info->dev->page_size, bank->num_sectors);
	if (!bank->sectors) {
		ret = ERROR_FAIL;
		goto err;
	}

	/* num of protection segments (blocks) */
	bank->num_prot_blocks = abov32f2_info->dev->code_size / abov32f2_info->dev->wprot_seg_size;
	/* alloc protection blocks */
	free(bank->prot_blocks);
	bank->prot_blocks = NULL;
	bank->prot_blocks = alloc_block_array(0, abov32f2_info->dev->wprot_seg_size, bank->num_prot_blocks);
	if (!bank->prot_blocks) {
		ret = ERROR_FAIL;
		free(bank->sectors);
		goto err;
	}

	/* update protection bits */
	ret = abov32f2_protect_check(bank);
	if (ret != ERROR_OK)
		goto err;

	abov32f2_info->probed = true;
#if defined DEBUG_LOG
	LOG_INFO("ID:0x%08x, size=0x%08x, page=0x%08x, sectors=%d, prot=%d",
			chipset_id, bank->size, abov32f2_info->dev->page_size, bank->num_sectors, bank->num_prot_blocks);
#endif

err:
	return ret;
}

static int abov32f2_auto_probe(struct flash_bank *bank)
{
	struct abov32f2_flash_bank *abov32f2_info = bank->driver_priv;
	if (abov32f2_info->probed)
		return ERROR_OK;
	return abov32f2_probe(bank);
}

static int abov32f2_info(struct flash_bank *bank, struct command_invocation *cmd)
{
	struct abov32f2_flash_bank *abov32f2_info = bank->driver_priv;

	if (abov32f2_info->probed)
		command_print_sameline(cmd, "\nABOV 32Bit MCU - %s", abov32f2_info->dev->device_str);
	else
		command_print_sameline(cmd, "\nNo chipset detected");

	return ERROR_OK;
}

COMMAND_HANDLER(abov32f2_handle_rprotect_command)
{
	int ret = ERROR_OK;
	struct flash_bank *bank;
	uint32_t lvl = 0;

	if (CMD_ARGC < 2)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ret = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ret != ERROR_OK)
		goto err;

	if (!strcmp(CMD_ARGV[1], "lvl0"))
	{
	    lvl = FLASH_RPROT_LEVEL_0;
	}
	else if (!strcmp(CMD_ARGV[1], "lvl1"))
	{
	    lvl = FLASH_RPROT_LEVEL_1;
	}
	else if (!strcmp(CMD_ARGV[1], "lvl2"))
	{
	    lvl = FLASH_RPROT_LEVEL_2;
	}
	/* set read protection level */
    ret = abov32f2_set_rprotlvl(bank, lvl);

err:
	return ret;
}

static const struct command_registration abov32f2_exec_command_handlers[] = {
	{
		.name = "rprot",
		.handler = abov32f2_handle_rprotect_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id [ lvl0 | lvl1 | lvl2 ]",
		.help = "Set read protection level for entire flash memory area",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration abov32f2_command_handlers[] = {
	{
		.name = "abov32f2",
		.mode = COMMAND_ANY,
		.help = "abov32f2 flash command group",
		.usage = "",
		.chain = abov32f2_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

const struct flash_driver abov32f2_flash = {
	.name = "abov32f2",
	.commands = abov32f2_command_handlers,
	.flash_bank_command = abov32f2_flash_bank_command,
	.erase = abov32f2_erase,
	.protect = abov32f2_protect,
	.write = abov32f2_write,
	.read = default_flash_read,
	.probe = abov32f2_probe,
	.auto_probe = abov32f2_auto_probe,
	.erase_check = default_flash_blank_check,
	.protect_check = abov32f2_protect_check,
	.info = abov32f2_info,
	.free_driver_priv = default_flash_free_driver_priv,
};
