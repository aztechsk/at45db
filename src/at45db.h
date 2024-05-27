/*
 * at45db.h
 *
 * Copyright (c) 2024 Jan Rusnak <jan@rusnak.sk>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef AT45DB_H
#define AT45DB_H

#ifndef AT45DB_USE_EXT_STAT
  #define AT45DB_USE_EXT_STAT 0
#endif

// AT45DB flash descriptor.
typedef struct at45db_dsc *at45db;

struct at45db_dsc {
        int pg_count;  // <SetIt>
        int pg_size;   // <SetIt>
        int bl_count;  // <SetIt>
        spim spi;      // <SetIt>
        struct spim_csel_dcs csel;  // <SetIt>
        char *id;          // <SetIt>
	boolean_t use_dma; // <SetIt>
        boolean_t buf2_ff; // <SetIt> FALSE
};

// Status Register Format - byte 1.
#define AT45DB_FLASH_READY            (0x1 << 7)
#define AT45DB_COMPARE_NOT_MATCH      (0x1 << 6)
#define AT45DB_PROTECT_ENABLED        (0x1 << 1)
#define AT45DB_PAGE_SIZE_1024B        (0x1 << 0)
#define AT45DB_PAGE_SIZE_PO2          (0x1 << 0)
#define at45db_device_density(status) (((status) & 0x3C) >> 2)

#if AT45DB_USE_EXT_STAT == 1
// Status Register Format - byte 2.
#define AT45DB_FLASH_READY2    (0x1 << (7 + 8))
#define AT45DB_PROG_ERR        (0x1 << (5 + 8))
#define AT45DB_SECLOCK_ENABLED (0x1 << (3 + 8))
#define AT45DB_PROG_SUSP_BUF2  (0x1 << (2 + 8))
#define AT45DB_PROG_SUSP_BUF1  (0x1 << (1 + 8))
#define AT45DB_ERASE_SUSP      (0x1 << (0 + 8))
#endif

/**
 * at45db_stat - status command.
 *
 * @fi: Flash instance.
 * @stat: Points to unsigned int storage for status (1st byte).
 *
 * Returns: 0 - success; -EHW - hardware error.
 */
int at45db_stat(at45db fi, unsigned int *stat);

#if AT45DB_USE_EXT_STAT == 1
/**
 * at45db_ext_stat - status command.
 *
 * @fi: Flash instance.
 * @stat: Points to unsigned int storage for status (2 bytes).
 *
 * Returns: 0 - success; -EHW - hardware error.
 */
int at45db_ext_stat(at45db fi, unsigned int *stat);
#endif

/**
 * at45db_read_mem - main memory page read.
 *
 * Main Memory Page Read allows the reading of data directly from a single
 * page in the main memory, bypassing both of the data buffers and leaving
 * the contents of the buffers unchanged.
 *
 * @fi: Flash instance.
 * @buf: Buffer for data.
 * @page: Page number.
 * @offs: Data offset in page.
 * @num: Count of bytes to read.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error.
 */
int at45db_read_mem(at45db fi, unsigned char *buf, int page, int offs, int num);

/**
 * at45db_write_mem - main memory page write through flash buffer with page erase.
 *
 * Main Memory Page Program through Buffer with Built-In Erase command combines
 * the Buffer Write and Buffer to Main Memory Page Program with Built-In Erase
 * operations into a single operation.
 *
 * @fi: Flash instance.
 * @buf: Buffer for data.
 * @bfn: Select flash buffer for write data (1 or 2).
 * @page: Page number.
 * @offs: Data offset in page.
 * @num: Count of bytes to write.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error; -EDATA - write error.
 */
int at45db_write_mem(at45db fi, unsigned char *buf, int bfn, int page, int offs, int num);

/**
 * at45db_read_buf - read from flash buffer.
 *
 * @fi: Flash instance.
 * @buf: Buffer for data.
 * @bfn: Select flash buffer (1 or 2).
 * @offs: Data offset in buffer.
 * @num: Count of bytes to read.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error.
 */
int at45db_read_buf(at45db fi, unsigned char *buf, int bfn, int offs, int num);

/**
 * at45db_write_buf - write to flash buffer.
 *
 * @fi: Flash instance.
 * @buf: Data buffer.
 * @bfn: Select flash buffer (1 or 2).
 * @offs: Data offset in buffer.
 * @num: Count of bytes to write.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error.
 */
int at45db_write_buf(at45db fi, unsigned char *buf, int bfn, int offs, int num);

/**
 * at45db_store_buf - store flash buffer to main memory page.
 *
 * @fi: Flash instance.
 * @bfn: Select flash buffer (1 or 2).
 * @page: Page number.
 * @erase: TRUE - enable page erase before write.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error; -EDATA - write error.
 */
int at45db_store_buf(at45db fi, int bfn, int page, boolean_t erase);

/**
 * at45db_load_buf - load page from main memory to flash buffer.
 *
 * @fi: Flash instance.
 * @bfn: Select flash buffer (1 or 2).
 * @page: Page number.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error.
 */
int at45db_load_buf(at45db fi, int bfn, int page);

/**
 * at45db_page_erase - erase page.
 *
 * @fi: Flash instance.
 * @page: Page number.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error;
 *          -EDATA - erase error.
 */
int at45db_page_erase(at45db fi, int page);

/**
 * at45db_check_page_erased - check erased page, 0xFF pattern.
 *
 * @fi: Flash instance.
 * @page: Page number.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error;
            -EDATA - erase error.
 */
int at45db_check_page_erased(at45db fi, int page);

/**
 * at45db_block_erase - erase block.
 *
 * @fi: Flash instance.
 * @block: Block number.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error;
 *          -EDATA - erase error.
 */
int at45db_block_erase(at45db fi, int block);

/**
 * at45db_chip_erase - erase chip.
 *
 * @fi: Flash instance.
 *
 * Returns: 0 - success; -EHW - hardware error; -EDATA - erase error.
 */
int at45db_chip_erase(at45db fi);

/**
 * at45db_section_erase - erase flash section.
 *
 * @fi: Flash instance.
 * @start: Start page.
 * @end: End page.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error;
 *          -EDATA - erase error.
 */
int at45db_section_erase(at45db fi, int start, int end);

enum at45db_read_cont_type {
	AT45DB_READ_CONT_HF0 = 0x0B,
	AT45DB_READ_CONT_HF1 = 0x1B,
	AT45DB_READ_CONT_LF  = 0x03,
	AT45DB_READ_CONT_LP  = 0x01
};

/**
 * at45db_read_cont - main memory continuous read.
 *
 * Main Memory Page Continuous Read allows the reading of data directly from
 * main memory continuous across pages, bypassing both of the data buffers and leaving
 * the contents of the buffers unchanged.
 *
 * @fi: Flash instance.
 * @type: read_mem_cont type according to power mode and device frequency.
 * @buf: Buffer for data.
 * @page: Page number.
 * @offs: Data offset in page.
 * @num: Count of bytes to read.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error.
 */
int at45db_read_cont(at45db fi, enum at45db_read_cont_type type, unsigned char *buf, int page, int offs, int num);

/**
 * at45db_read_mod_write - Read-Modify-Write main memory.
 *
 * This command allows the device to easily emulate an EEPROM. The Read-Modify-Write command is
 * essentially a combination of the Main Memory Page to Buffer Transfer, Buffer Write, and Buffer
 * to Main Memory Page Program with Built-in Erase commands.
 *
 * @fi: Flash instance.
 * @buf: Buffer for data.
 * @bfn: Select flash buffer for internal use (1 or 2).
 * @page: Page number.
 * @offs: Data offset in page.
 * @num: Count of bytes to read.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error; -EDATA - erase or write error.
 */
int at45db_read_mod_write(at45db fi, unsigned char *buf, int bfn, int page, int offs, int num);

enum at45db_pwr_down_type {
	AT45DB_DEEP_PWR_DOWN       = 0xB9,
	AT45DB_ULTRA_DEEP_PWR_DOWN = 0x79
};

/**
 * at45db_pwr_down - power down device.
 *
 * @fi: Flash instance.
 * @type: Device power down type (deep or ultradeep).
 *
 * Returns: 0 - success; -EHW - hardware error.
 */
int at45db_pwr_down(at45db fi, enum at45db_pwr_down_type type);

/**
 * at45db_wake - wake up device.
 *
 * @fi: Flash instance.
 *
 * Returns: 0 - success; -EHW - hardware error.
 */
int at45db_wake(at45db fi);

enum at45db_page_size {
	AT45DB_SET_PAGE_SIZE_PO2 = 0xA6,
	AT45DB_SET_PAGE_SIZE_STD = 0xA7
};

/**
 * at45db_set_page_size - page size configuration.
 *
 * @fi: Flash instance.
 * @sz: Flash page size (enum at45db_page_size).
 *
 * Returns: 0 - success; -EHW - hardware error.
 */
int at45db_set_page_size(at45db fi, enum at45db_page_size sz);

#if AT45DB_TEST_CODE == 1
/**
 * at45db_rw_test - test flash RW operations by data integrity.
 *
 * @fi: Flash instance.
 * @num: Count of test cycles.
 * @verb: Verbose messages.
 *
 * Returns: TRUE - success; FALSE - error.
 */
boolean_t at45db_rw_test(at45db fi, int num, boolean_t verb);

/**
 * at45db_ro_test - test flash RO operations by data integrity.
 *
 * Data in flash must be prepared by previous call to at45db_test_rw().
 *
 * @fi: Flash instance.
 * @num: Count of test cycles.
 * @verb: Verbose messages.
 *
 * Returns: TRUE - success; FALSE - error.
 */
boolean_t at45db_ro_test(at45db fi, int num, boolean_t verb);
#endif

#endif
