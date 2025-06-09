// SPDX-License-Identifier: BSD-3-Clause
/**
 *******************************************************************************
 * @file        abov32f1.c
 * @author      ABOVSemiconductor R&D Division
 * @brief       Nor Flash Driver for ABOV32 F1
 *
 * Copyright 2024 ABOVSemiconductor Co.,Ltd. All rights reserved.
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

#define A31G11X_FLASH_16KB				0x1F001
#define A31G11X_FLASH_32KB				0x1F000
#define A31G11X_FLASH_CODE_BASE_ADDR	0x10000000
#define A31G11X_FLASH_REG_ADDR			0x40000100

#define A31G12X_FLASH_32KB				0x1F003
#define A31G12X_FLASH_64KB				0x1F002
#define A31G12X_FLASH_CODE_BASE_ADDR	0x10000000
#define A31G12X_FLASH_REG_ADDR			0x40000100

#define A31L12X_FLASH_32KB				0x1F005
#define A31L12X_FLASH_64KB				0x1F004
#define A31L12X_FLASH_CODE_BASE_ADDR	0x10000000
#define A31L12X_FLASH_REG_ADDR			0x40000100

#define A31L22X_FLASH_16KB				0x1F015
#define A31L22X_FLASH_32KB				0x1F014
#define A31L22X_FLASH_CODE_BASE_ADDR	0x10000000
#define A31L22X_FLASH_REG_ADDR			0x40000100

#define ABOV_CHIP_ID_MASK				0xFFFFF

/* Flash Base Address */
#define FLASH_BASE_ADDR                 0x10000000

/* Flash Mode */
#define FLASH_ADR_RESET_CMD             0x5FFFFF80
#define FLASH_ADR_CMD                   0x5FFFFFFF
#define FLASH_IDR1_CMD                  0x08192A3B
#define FLASH_IDR2_CMD                  0x4C5D6E7F

/* Flash Chip Erase Value */
#define FLASH_ERASE_CHIP                0x5F9A30D7

/* Flash Read Protection Level 0 */
#define FLASH_RPROT_LEVEL_0             0x69C8A273

/* Flash Watch-Dog Timer */
#define FLASH_WDT_DISABLE               0xFFFF96D3

/* Flash Reg Offset */
#define FLASH_REG_START_ADDR			0x40001B00
#define FLASH_ADDR_REG                  (FLASH_REG_START_ADDR | 0x00000000)
#define FLASH_IDR1_REG                  (FLASH_REG_START_ADDR | 0x00000004)
#define FLASH_IDR2_REG                  (FLASH_REG_START_ADDR | 0x00000008)
#define FLASH_CR_REG                    (FLASH_REG_START_ADDR | 0x0000000C)
#define FLASH_COA_BULK_REG              (FLASH_REG_START_ADDR | 0x00000010)
#define FLASH_ERR_REG                   (FLASH_REG_START_ADDR | 0x00000014)

/* Flash Page Buffer */
#define FLASH_PAGE_BUFFER_ADDR          0x40001C00
#define FLASH_PAGE_BUFFER_SIZE          128

/* Flash Configration Option Area */
#define FLASH_COA_START_ADDR            0x1FFFF200
#define FLASH_COA_RPROT_FMA             (FLASH_COA_START_ADDR | 0x00000000)
#define FLASH_COA_WPROT_COA             (FLASH_COA_START_ADDR | 0x00000014)
#define FLASH_COA_WPROT_FMA             (FLASH_COA_START_ADDR | 0x00000040)

/* Flash Read Protection Level */
#define FLASH_RPROT_LEVEL_0             0x69C8A273
#define FLASH_RPROT_LEVEL_1             0x69C8A272
#define FLASH_RPROT_LEVEL_2             0x69C8A271

/* FLASH COA Data Index */
#define FLASH_COA_RPROT_LEVEL_IDX       0
#define FLASH_COA_WDT_IDX               3
#define FLASH_COA_WPROT_IDX             16

/* Flash Control Reg bit position */
#define FLASH_CR_PAGE_BUF_RESET         BIT(0)
#define FLASH_CR_ERASE_PAGE             BIT(1)
#define FLASH_CR_WRITE_PAGE             BIT(2)
#define FLASH_CR_ERASE_CHIP             BIT(3)
#define FLASH_CR_OPS_PAGE_BUF_RESET     (0x00 << 8)
#define FLASH_CR_OPS_COA_MODE           (0x38 << 8)
#define FLASH_CR_OPS_FMA_MODE           (0xA4 << 8)
#define FLASH_CR_WTIDKY_VAL             (0x6C93 << 16)

/* Flash Status Reg bit position */
#define FLASH_WBUSY_REG_BUSY			BIT(7)

/* Flash Error Reg bit position */
#define FLASH_ERR_FLAG                  BIT(0)

#define FLASH_PROT_LOCK					0x00000001
#define FLASH_PROT_UNLOCK				0x00000000
#define FLASH_ERASE_TIMEOUT				100
#define FLASH_WORD_ALIGN				4

enum erase_mode {
	erase_mode_page,
	erase_mode_1kb,
	erase_mode_4kb,
	erase_mode_chip,
	erase_mode_max
};

static const struct abov32f1_dev {
	uint32_t id;
	const char *device_str;
	uint32_t code_size;
	uint32_t system_size;
	uint32_t page_size;
	uint32_t wprot_seg_size;
	bool abwprot;				/* active bootloader write protection */
} abov32f1_devices[] = {
{
	.id = A31G12X_FLASH_32KB,
	.device_str = "A31G122 : FLASH 32KB / SRAM 6KB ",
	.code_size = 0x8000,
	.system_size = 0x600,
	.page_size = 0x80,
	.wprot_seg_size = 0x800,
	.abwprot = false
}, {
	.id = A31G12X_FLASH_64KB,
	.device_str = "A31G123 : FLASH 64KB / SRAM 6KB",
	.code_size = 0x10000,
	.system_size = 0x600,
	.page_size = 0x80,
	.wprot_seg_size = 0x800,
	.abwprot = false
}, {
	.id = A31G11X_FLASH_16KB,
	.device_str = "A31G111 : FLASH 16KB / SRAM 4KB",
	.code_size = 0x4000,
	.system_size = 0x600,
	.page_size = 0x80,
	.wprot_seg_size = 0x800,
	.abwprot = false
}, {
	.id = A31G11X_FLASH_32KB,
	.device_str = "A31G112 : FLASH 32KB / SRAM 4KB",
	.code_size = 0x8000,
	.system_size = 0x600,
	.page_size = 0x80,
	.wprot_seg_size = 0x800,
	.abwprot = false
}, {
	.id = A31L12X_FLASH_64KB,
	.device_str = "A31L123 : FLASH 64KB / SRAM 8KB",
	.code_size = 0x10000,
	.system_size = 0x600,
	.page_size = 0x80,
	.wprot_seg_size = 0x800,
	.abwprot = false
}, {
	.id = A31L12X_FLASH_32KB,
	.device_str = "A31L122 : FLASH 32KB / SRAM 8KB",
	.code_size = 0x8000,
	.system_size = 0x600,
	.page_size = 0x80,
	.wprot_seg_size = 0x800,
	.abwprot = false
}, {
	.id = A31L22X_FLASH_32KB,
	.device_str = "A31L222 : FLASH 32KB / SRAM 8KB",
	.code_size = 0x8000,
	.system_size = 0x600,
	.page_size = 0x80,
	.wprot_seg_size = 0x800,
	.abwprot = false
}, {
	.id = A31L22X_FLASH_16KB,
	.device_str = "A31L121 : FLASH 16KB / SRAM 8KB",
	.code_size = 0x4000,
	.system_size = 0x600,
	.page_size = 0x80,
	.wprot_seg_size = 0x800,
	.abwprot = false
}};

struct abov32f1_flash_bank {
	bool probed;
	const struct abov32f1_dev *dev;
};

static int abov32f1_set_erase(struct flash_bank *bank, uint32_t pageaddr, enum erase_mode mode);

static int abov32f1_wait_busy(struct flash_bank *bank, int timeout)
{
	struct target *target = bank->target;
	uint32_t busy = 0;

	while (1) {
		int ret = target_read_u32(target, FLASH_CR_REG, &busy);
		if (ret != ERROR_OK)
			return ret;

		if (!(busy & FLASH_WBUSY_REG_BUSY))
			break;

		if (timeout-- <= 0)
			return ERROR_TIMEOUT_REACHED;
	}

	return ERROR_OK;
}

static int abov32f1_set_preops(struct flash_bank *bank)
{
	struct target *target = bank->target;
	uint8_t pagebuf[FLASH_PAGE_BUFFER_SIZE];
	int ret;

	/* clear error flag */
	ret = target_write_u32(target, FLASH_ERR_REG, 0x00000001);
	if (ret != ERROR_OK)
	    goto err;

	/* operation mode */
	ret = target_write_u32(target, FLASH_ADDR_REG, FLASH_ADR_CMD);
	if (ret != ERROR_OK)
	    goto err;
	ret = target_write_u32(target, FLASH_IDR1_REG, FLASH_IDR1_CMD);
	if (ret != ERROR_OK)
	    goto err;
	ret = target_write_u32(target, FLASH_IDR2_REG, FLASH_IDR2_CMD);
	if (ret != ERROR_OK)
	    goto err;

	/* clear buffer */
	ret = target_write_u32(target, FLASH_CR_REG, FLASH_CR_WTIDKY_VAL | FLASH_CR_PAGE_BUF_RESET);
	if (ret != ERROR_OK)
	    goto err;
	
	memset(pagebuf, 0xff, FLASH_PAGE_BUFFER_SIZE);

	ret = target_write_memory(target, FLASH_PAGE_BUFFER_ADDR, 4, FLASH_PAGE_BUFFER_SIZE / 4, pagebuf);

err:
	return ret;
}

static int abov32f1_get_chipset_id(struct flash_bank *bank, uint32_t *chipset_id)
{
	struct target *target = bank->target;
	int ret = target_read_u32(target, ABOV_CHIP_ID_REG, chipset_id);
	return ret;
}

const struct abov32f1_dev *abov32f1_find_device(uint32_t id)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(abov32f1_devices); i++)
		if ((id & ABOV_CHIP_ID_MASK) == abov32f1_devices[i].id)
			return &abov32f1_devices[i];
	return NULL;
}

static int abov32f1_set_pagewrite(struct flash_bank *bank, uint8_t *buffer, uint32_t addr, uint32_t count, bool bcoa)
{
	struct target *target = bank->target;
	int ret;
	uint32_t cmd = 0;

	if (count > FLASH_PAGE_BUFFER_SIZE)
	{
	    ret = ERROR_FAIL;
		goto err;
	}
	
	/* set operation mode */
    ret = abov32f1_set_preops(bank);
	if (ret != ERROR_OK)
	    goto err;

	ret = target_write_memory(target, FLASH_PAGE_BUFFER_ADDR, 4, count / 4, buffer);
	if (ret != ERROR_OK)
	    goto err;


	ret = target_write_u32(target, FLASH_ADDR_REG, addr | FLASH_BASE_ADDR);
	if (ret != ERROR_OK)
	    goto err;

	cmd = FLASH_CR_WTIDKY_VAL | FLASH_CR_WRITE_PAGE;

	if (bcoa)
	    cmd |= FLASH_CR_OPS_COA_MODE;
	else
	    cmd |= FLASH_CR_OPS_FMA_MODE;

	/* flash write from page buffer to configuration option area */
	ret = target_write_u32(target, FLASH_CR_REG, cmd);
	if (ret != ERROR_OK)
	    goto err;

	ret = abov32f1_wait_busy(bank, FLASH_ERASE_TIMEOUT);

err:
	return ret;
}

static int abov32f1_set_rprotlvl(struct flash_bank *bank, uint32_t lvl)
{
	struct target *target = bank->target;
	int ret;
	unsigned char coabuf[FLASH_PAGE_BUFFER_SIZE];
	uint32_t *pcoabuf = (uint32_t *)coabuf;
	uint32_t reg;

	ret = target_read_u32(target, FLASH_COA_RPROT_FMA, &reg);
	if (ret != ERROR_OK)
	    goto err;

	if (reg == lvl)
	{
		LOG_INFO("read protect level is already set.");
		ret = ERROR_OK;
	    goto err;
	}

	memset(coabuf, 0x00, FLASH_PAGE_BUFFER_SIZE);

	ret = target_read_memory(target, FLASH_COA_START_ADDR, FLASH_WORD_ALIGN, 
					         FLASH_PAGE_BUFFER_SIZE / FLASH_WORD_ALIGN, coabuf);
	if (ret != ERROR_OK)
	    goto err;

	pcoabuf[FLASH_COA_RPROT_LEVEL_IDX] = lvl;

	/* erase coa */
    ret = abov32f1_set_erase(bank, FLASH_COA_START_ADDR, erase_mode_page);
	if (ret != ERROR_OK)
		goto err;

    ret = abov32f1_set_pagewrite(bank, coabuf, FLASH_COA_START_ADDR, FLASH_PAGE_BUFFER_SIZE, true);

err:
	return ret;
}

static int abov32f1_protect_check(struct flash_bank *bank)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;
	uint32_t reg = 0;

	/* read protection bits */
	ret = target_read_u32(target, FLASH_COA_WPROT_FMA, &reg);
	if (ret != ERROR_OK)
		goto err;

#if defined DEBUG_LOG
	LOG_INFO("cur wprotect:0x%08x, wprotect blocks:%d\n", reg, bank->num_prot_blocks);
#endif
	for (unsigned int i = 0; i < bank->num_prot_blocks; i++) {
		bank->prot_blocks[i].is_protected = (reg & (FLASH_PROT_LOCK << i)) ? FLASH_PROT_UNLOCK : FLASH_PROT_LOCK;
#if defined DEBUG_LOG
		LOG_INFO("%d block is %s", i, (bank->prot_blocks[i].is_protected == 1) ? "lock" : "unlock");
#endif
	}
err:
	return ret;
}

static int abov32f1_set_write_protect(struct flash_bank *bank, int set, uint32_t first, uint32_t last)
{
	int ret = ERROR_OK;
	struct target *target = bank->target;
#if defined DEBUG_LOG
    uint32_t reg = 0;
#endif
	uint32_t prot = 0;
	unsigned char coabuf[FLASH_PAGE_BUFFER_SIZE];
	uint32_t *pcoabuf = (uint32_t *)coabuf;

	memset(coabuf, 0x00, FLASH_PAGE_BUFFER_SIZE);

	ret = target_read_memory(target, FLASH_COA_START_ADDR, FLASH_WORD_ALIGN, 
					         FLASH_PAGE_BUFFER_SIZE / FLASH_WORD_ALIGN, coabuf);
	if (ret != ERROR_OK)
	    goto err;

	for (unsigned int i = first; i <= last; i++)
		prot |= (FLASH_PROT_LOCK << i);

#if defined DEBUG_LOG
	(void)target_read_u32(target, FLASH_COA_WPROT_FMA, &reg);
	LOG_INFO("cur write protection:0x%08x, protection bits:0x%08x", reg, prot);
#endif

	if (set)
		pcoabuf[FLASH_COA_WPROT_IDX] &= ~prot;
	else
		pcoabuf[FLASH_COA_WPROT_IDX] |= prot;

	/* erase coa */
    ret = abov32f1_set_erase(bank, FLASH_COA_START_ADDR, erase_mode_page);
    if (ret != ERROR_OK)
	    goto err;

    ret = abov32f1_set_pagewrite(bank, coabuf, FLASH_COA_START_ADDR, FLASH_PAGE_BUFFER_SIZE, true);
    if (ret != ERROR_OK)
	    goto err;

#if defined DEBUG_LOG
	(void)target_read_u32(target, FLASH_COA_WPROT_FMA, &reg);
	LOG_INFO("new write protection:0x%08x", reg);
#endif

	/* update protection bits in a bank */
	ret = abov32f1_protect_check(bank);

err:
	return ret;
}

static int abov32f1_set_erase(struct flash_bank *bank, uint32_t pageaddr, enum erase_mode mode)
{
	struct target *target = bank->target;
	int ret = ERROR_OK;

	/* set operation mode */
    ret = abov32f1_set_preops(bank);
	if (ret != ERROR_OK)
	    goto err;

	/* select erase area mode */
	switch (mode) {
		case erase_mode_chip:
	        target_write_u32(target, FLASH_ADDR_REG, FLASH_ERASE_CHIP);
	        target_write_u32(target, FLASH_CR_REG, FLASH_CR_WTIDKY_VAL | FLASH_CR_OPS_FMA_MODE | FLASH_CR_ERASE_CHIP);
			break;
		default:
			target_write_u32(target, FLASH_ADDR_REG, pageaddr | FLASH_BASE_ADDR);
#if 1
			if (pageaddr == FLASH_COA_START_ADDR)
	            target_write_u32(target, FLASH_CR_REG, FLASH_CR_WTIDKY_VAL | FLASH_CR_OPS_COA_MODE | FLASH_CR_ERASE_PAGE);
			else
	            target_write_u32(target, FLASH_CR_REG, FLASH_CR_WTIDKY_VAL | FLASH_CR_OPS_FMA_MODE | FLASH_CR_ERASE_PAGE);
			/* page erase mode */
#endif
			break;
	}

	if (ret != ERROR_OK)
		goto err;

	/* wait a write is done */
	ret = abov32f1_wait_busy(bank, FLASH_ERASE_TIMEOUT);
	if (ret != ERROR_OK)
		goto err;

err:
	return ret;
}

FLASH_BANK_COMMAND_HANDLER(abov32f1_flash_bank_command)
{
	/* Need syntax count from flash bank command */
	if (CMD_ARGC <  6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct abov32f1_flash_bank *abov32f1_info;
	abov32f1_info = malloc(sizeof(struct abov32f1_flash_bank));
	if (!abov32f1_info) {
		LOG_ERROR("Out of memory");
		return ERROR_FAIL;
	}

	bank->driver_priv = abov32f1_info;
	abov32f1_info->probed = false;

	return ERROR_OK;
}

static int abov32f1_erase(struct flash_bank *bank, unsigned int first,
		unsigned int last)
{
	struct abov32f1_flash_bank *abov32f1_info = bank->driver_priv;
	int ret = ERROR_OK;
	uint32_t sec_cnt = 0;
	uint32_t sec_offset = first;
	uint32_t wprot_first = 0, wprot_last = 0;
	uint32_t start_addr = 0, end_addr = 0;

#if defined DEBUG_LOG
	LOG_INFO("erase first=%d last=%d", first, last);
#endif

	/* find fist and last protect block index */
	start_addr = first * abov32f1_info->dev->page_size;
	end_addr = last * abov32f1_info->dev->page_size;

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
	abov32f1_set_write_protect(bank, FLASH_PROT_UNLOCK, wprot_first, wprot_last);

	sec_cnt = (last - first) + 1;
	if (sec_cnt == bank->num_sectors) {
		LOG_INFO("chip/bulk erase.");
		ret = abov32f1_set_erase(bank, bank->sectors[0].offset, erase_mode_chip);
	} else {
		while (sec_cnt != 0) {
#if defined DEBUG_LOG
            LOG_INFO("page erase : sec_cnt remain=%d, sector num=%d", sec_cnt, sec_offset);
#endif
			if(!(sec_offset % 8))
  			    LOG_INFO("erase : 0x%08x.", bank->sectors[sec_offset].offset);
            ret = abov32f1_set_erase(bank, bank->sectors[sec_offset].offset, erase_mode_page);
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

static int abov32f1_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last)
{
	return abov32f1_set_write_protect(bank, set, first, last);
}

static int abov32f1_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
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

	quot = count / FLASH_PAGE_BUFFER_SIZE;
    remaind = count % FLASH_PAGE_BUFFER_SIZE;
	idx = 0;

	do {
		if(!(idx % 8))
  		    LOG_INFO("write : 0x%08x.", offset + (idx * FLASH_PAGE_BUFFER_SIZE));

	    ret = abov32f1_set_pagewrite(bank, &pbuf[idx * FLASH_PAGE_BUFFER_SIZE],
			                         offset + (idx * FLASH_PAGE_BUFFER_SIZE), 
									 FLASH_PAGE_BUFFER_SIZE, false);
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
  	    LOG_INFO("write : 0x%08x.", offset + (idx * FLASH_PAGE_BUFFER_SIZE));
	    ret = abov32f1_set_pagewrite(bank, &pbuf[idx * FLASH_PAGE_BUFFER_SIZE], 
						             offset + (idx * FLASH_PAGE_BUFFER_SIZE), 
									 remaind, false);
       	if (ret != ERROR_OK)
	        LOG_ERROR("couldn't write data to flash memory.");
    }

err:
	return ret;
}

static int abov32f1_probe(struct flash_bank *bank)
{
	struct abov32f1_flash_bank *abov32f1_info = bank->driver_priv;
	int ret = ERROR_OK;
	uint32_t chipset_id;

	ret = abov32f1_get_chipset_id(bank, &chipset_id);
	if (ret != ERROR_OK)
		goto err;

	/* find and get geometry info */
	abov32f1_info->dev = abov32f1_find_device(chipset_id);

	if (!abov32f1_info->dev)
		return ERROR_FAIL;

	/* code area size */
	bank->size = abov32f1_info->dev->code_size;

	/* start address */
	bank->base = 0;

	/* num of sectors (page == sector) */
	bank->num_sectors = abov32f1_info->dev->code_size / abov32f1_info->dev->page_size;
	/* alloc sector blocks */
	free(bank->sectors);
	bank->sectors = NULL;
	bank->sectors = alloc_block_array(0, abov32f1_info->dev->page_size, bank->num_sectors);
	if (!bank->sectors) {
		ret = ERROR_FAIL;
		goto err;
	}

	/* num of protection segments (blocks) */
	bank->num_prot_blocks = abov32f1_info->dev->code_size / abov32f1_info->dev->wprot_seg_size;
	/* alloc protection blocks */
	free(bank->prot_blocks);
	bank->prot_blocks = NULL;
	bank->prot_blocks = alloc_block_array(0, abov32f1_info->dev->wprot_seg_size, bank->num_prot_blocks);
	if (!bank->prot_blocks) {
		ret = ERROR_FAIL;
		free(bank->sectors);
		goto err;
	}

	/* update protection bits */
	ret = abov32f1_protect_check(bank);
	if (ret != ERROR_OK)
		goto err;

	abov32f1_info->probed = true;
#if defined DEBUG_LOG
	LOG_INFO("ID:0x%08x, size=0x%08x, page=0x%08x, sectors=%d, prot=%d",
			chipset_id, bank->size, abov32f1_info->dev->page_size, bank->num_sectors, bank->num_prot_blocks);
#endif

err:
	return ret;
}

static int abov32f1_auto_probe(struct flash_bank *bank)
{
	struct abov32f1_flash_bank *abov32f1_info = bank->driver_priv;
	if (abov32f1_info->probed)
		return ERROR_OK;
	return abov32f1_probe(bank);
}

static int abov32f1_info(struct flash_bank *bank, struct command_invocation *cmd)
{
	struct abov32f1_flash_bank *abov32f1_info = bank->driver_priv;

	if (abov32f1_info->probed)
		command_print_sameline(cmd, "\nABOV 32Bit MCU - %s", abov32f1_info->dev->device_str);
	else
		command_print_sameline(cmd, "\nNo chipset detected");

	return ERROR_OK;
}

COMMAND_HANDLER(abov32f1_handle_rprotect_command)
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
    ret = abov32f1_set_rprotlvl(bank, lvl);

err:
	return ret;
}

static const struct command_registration abov32f1_exec_command_handlers[] = {
	{
		.name = "rprot",
		.handler = abov32f1_handle_rprotect_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id [ lvl0 | lvl1 | lvl2 ]",
		.help = "Set read protection level for entire flash memory area",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration abov32f1_command_handlers[] = {
	{
		.name = "abov32f1",
		.mode = COMMAND_ANY,
		.help = "abov32f1 flash command group",
		.usage = "",
		.chain = abov32f1_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

const struct flash_driver abov32f1_flash = {
	.name = "abov32f1",
	.commands = abov32f1_command_handlers,
	.flash_bank_command = abov32f1_flash_bank_command,
	.erase = abov32f1_erase,
	.protect = abov32f1_protect,
	.write = abov32f1_write,
	.read = default_flash_read,
	.probe = abov32f1_probe,
	.auto_probe = abov32f1_auto_probe,
	.erase_check = default_flash_blank_check,
	.protect_check = abov32f1_protect_check,
	.info = abov32f1_info,
	.free_driver_priv = default_flash_free_driver_priv,
};
