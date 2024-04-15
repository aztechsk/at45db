/*
 * at45db.h
 *
 * Copyright (c) 2020 Jan Rusnak <jan@rusnak.sk>
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

// Status Register Format.
#define AT45DB_FLASH_READY            (0x1 << 7)
#define AT45DB_COMPARE_NOT_MATCH      (0x1 << 6)
#define AT45DB_PROTECT_ENABLED        (0x1 << 1)
#define AT45DB_PAGE_SIZE_1024B        (0x1 << 0)
#define at45db_device_density(status) (((status) & 0x3C) >> 2)

/**
 * at45db_stat - status command.
 *
 * @fi: Flash instance.
 * @stat: Points to unsigned int storage for status.
 *
 * Returns: 0 - success; -EHW - hardware error.
 */
int at45db_stat(at45db fi, unsigned int *stat);

/**
 * at45db_read_mem - read from main memory.
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
 * at45db_write_mem - write to main memory through flash buffer with page erase.
 *
 * @fi: Flash instance.
 * @buf: Buffer for data.
 * @bfn: Select flash buffer (1 or 2).
 * @page: Page number.
 * @offs: Data offset in page.
 * @num: Count of bytes to write.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error.
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
 * at45db_store_buf - store flash buffer to main memory.
 *
 * @fi: Flash instance.
 * @bfn: Select flash buffer (1 or 2).
 * @page: Page number.
 * @erase: TRUE - enable page erase before write.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error.
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
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error.
 */
int at45db_page_erase(at45db fi, int page);

/**
 * at45db_check_page_erased - check erased page, 0xFF pattern.
 *
 * @fi: Flash instance.
 * @page: Page number.
 *
 * Returns: 0 - success; -EADDR - bad address; -EERASE - pattern error;
 *          -EHW - hardware error.
 */
int at45db_check_page_erased(at45db fi, int page);

/**
 * at45db_block_erase - erase block.
 *
 * @fi: Flash instance.
 * @block: Block number.
 *
 * Returns: 0 - success; -EADDR - bad address; -EHW - hardware error.
 */
int at45db_block_erase(at45db fi, int block);

/**
 * at45db_chip_erase - erase chip.
 *
 * @fi: Flash instance.
 *
 * Returns: 0 - success; -EHW - hardware error.
 */
int at45db_chip_erase(at45db fi);

/**
 * at45db_section_erase - erase flash section.
 *
 * @fi: Flash instance.
 * @start: Start page.
 * @end: End page.
 *
 * Returns: 0 - success; -EADDR - bad address; -EERASE - pattern error;
 *          -EHW - hardware error.
 */
int at45db_section_erase(at45db fi, int start, int end);

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
