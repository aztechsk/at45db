/*
 * at45db.c
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

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#include <gentyp.h>
#include "sysconf.h"
#include "board.h"
#include "msgconf.h"
#include "criterr.h"
#include "fmalloc.h"
#include "hwerr.h"
#include "spi.h"
#include "crc.h"
#include "at45db.h"
#include <string.h>
#include <stdlib.h>

#define PAGE_ERASE_TIME   (15 / portTICK_PERIOD_MS)
#define BLOCK_ERASE_TIME  (45 / portTICK_PERIOD_MS)
#define CHIP_ERASE_CHECK_TIME  (500 / portTICK_PERIOD_MS)

static void adrbits(int page, int offs, unsigned char *p);
#if AT45DB_TEST_CODE == 1
static int t_device(at45db fi, boolean_t verb);
static int t_readpage(at45db fi, unsigned char *buf, int page, boolean_t verb);
static int t_readpage_all(at45db fi, unsigned char *buf, boolean_t verb);
#endif

/**
 * at45db_stat
 */
int at45db_stat(at45db fi, unsigned int *stat)
{
        unsigned char cmd = 0xD7;

        if (0 != spi_trans(fi->spi, &fi->csel, &cmd, 1, &cmd, 1, DMA_OFF)) {
		return (-EHW);
	}
        *stat = cmd;
	return (0);
}

/**
 * at45db_read_mem
 */
int at45db_read_mem(at45db fi, unsigned char *buf, int page, int offs, int num)
{
        unsigned char cmd[] = {0xD2, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};

        if (page < 0 || page >= fi->pg_count) {
                return (-EADDR);
        }
        if (offs < 0 || offs >= fi->pg_size) {
                return (-EADDR);
        }
        adrbits(page, offs, cmd + 1);
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, sizeof(cmd), buf, num,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        return (0);
}

/**
 * at45db_write_mem
 */
int at45db_write_mem(at45db fi, unsigned char *buf, int bfn, int page, int offs, int num)
{
        boolean_t first = TRUE;
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};
        unsigned char stat;

        if (bfn == 1) {
                cmd[0] = 0x82;
        } else if (bfn == 2) {
                cmd[0] = 0x85;
        } else {
                return (-EADDR);
        }
        if (page < 0 || page >= fi->pg_count) {
                return (-EADDR);
        }
        if (offs < 0 || offs >= fi->pg_size) {
                return (-EADDR);
        }
        adrbits(page, offs, cmd + 1);
        if (bfn == 2) {
                fi->buf2_ff = FALSE;
        }
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, sizeof(cmd), buf, num,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        vTaskDelay(PAGE_ERASE_TIME);
        do {
                if (!first) {
                        taskYIELD();
                } else {
                        first = FALSE;
                }
                stat = 0xD7;
                if (0 != spi_trans(fi->spi, &fi->csel, &stat, 1, &stat, 1, DMA_OFF)) {
			return (-EHW);
		}
        } while (!(stat & AT45DB_FLASH_READY));
        return (0);
}

/**
 * at45db_read_buf
 */
int at45db_read_buf(at45db fi, unsigned char *buf, int bfn, int offset, int num)
{
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00, 0xFF};

        if (offset < 0 || offset >= fi->pg_size) {
                return (-EADDR);
        }
        if (bfn == 1) {
                cmd[0] = 0xD4;
        } else if (bfn == 2) {
                cmd[0] = 0xD6;
        } else {
                return (-EADDR);
        }
        adrbits(0, offset, cmd + 1);
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, sizeof(cmd), buf, num,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        return (0);
}

/**
 * at45db_write_buf
 */
int at45db_write_buf(at45db fi, unsigned char *buf, int bfn, int offset, int num)
{
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};

        if (offset < 0 || offset >= fi->pg_size) {
                return (-EADDR);
        }
        if (bfn == 1) {
                cmd[0] = 0x84;
        } else if (bfn == 2) {
                cmd[0] = 0x87;
        } else {
                return (-EADDR);
        }
        adrbits(0, offset, cmd + 1);
        if (bfn == 2) {
                fi->buf2_ff = FALSE;
        }
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, sizeof(cmd), buf, num,
			   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
                  return (-EHW);
	}
        return (0);
}

/**
 * at45db_store_buf
 */
int at45db_store_buf(at45db fi, int bfn, int page, boolean_t erase)
{
        boolean_t first = TRUE;
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};
        unsigned char stat;

        if (bfn == 1) {
                if (erase) {
                        cmd[0] = 0x83;
                } else {
                        cmd[0] = 0x88;
                }
        } else if (bfn == 2) {
                if (erase) {
                        cmd[0] = 0x86;
                } else {
                        cmd[0] = 0x89;
                }
        } else {
                return (-EADDR);
        }
        if (page < 0 || page >= fi->pg_count) {
                return (-EADDR);
        }
        adrbits(page, 0, cmd + 1);
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
			   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        if (erase) {
                vTaskDelay(PAGE_ERASE_TIME);
        }
        do {
                if (!first) {
                        taskYIELD();
                } else {
                        first = FALSE;
                }
                stat = 0xD7;
                if (0 != spi_trans(fi->spi, &fi->csel, &stat, 1, &stat, 1, DMA_OFF)) {
			return (-EHW);
		}
        } while (!(stat & AT45DB_FLASH_READY));
        return (0);
}

/**
 * at45db_load_buf
 */
int at45db_load_buf(at45db fi, int bfn, int page)
{
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};
        unsigned char stat;

        if (bfn == 1) {
                cmd[0] = 0x53;
        } else if (bfn == 2) {
                cmd[0] = 0x55;
        } else {
                return (-EADDR);
        }
        if (page < 0 || page >= fi->pg_count) {
                return (-EADDR);
        }
        adrbits(page, 0, cmd + 1);
        if (bfn == 2) {
                fi->buf2_ff = FALSE;
        }
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
			   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        do {
                stat = 0xD7;
                if (0 != spi_trans(fi->spi, &fi->csel, &stat, 1, &stat, 1, DMA_OFF)) {
			return (-EHW);
		}
        } while (!(stat & AT45DB_FLASH_READY));
        return (0);
}

/**
 * at45db_page_erase
 */
int at45db_page_erase(at45db fi, int page)
{
        boolean_t first = TRUE;
        unsigned char cmd[] = {0x81, 0x00, 0x00, 0x00};
        unsigned char stat;

        if (page < 0 || page >= fi->pg_count) {
                return (-EADDR);
        }
        adrbits(page, 0, cmd + 1);
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
			   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        vTaskDelay(PAGE_ERASE_TIME);
        do {
                if (!first) {
                        taskYIELD();
                } else {
                        first = FALSE;
                }
                stat = 0xD7;
                if (0 != spi_trans(fi->spi, &fi->csel, &stat, 1, &stat, 1, DMA_OFF)) {
			return (-EHW);
		}
        } while (!(stat & AT45DB_FLASH_READY));
        return (0);
}

/**
 * at45db_check_page_erased
 */
int at45db_check_page_erased(at45db fi, int page)
{
        void *mem;
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};
        unsigned char stat;

        if (page < 0 || page >= fi->pg_count) {
                return (-EADDR);
        }
        // Fill flash buffer2 with 0xFF pattern.
        if (!fi->buf2_ff) {
                mem = pvPortMalloc(fi->pg_size);
                if (mem == NULL) {
                        crit_err_exit(MALLOC_ERROR);
                }
                memset(mem, 0xFF, fi->pg_size);
                cmd[0] = 0x87;
                adrbits(0, 0, cmd + 1);
                if (0 != spi_trans(fi->spi, &fi->csel, cmd, sizeof(cmd),
			           mem, fi->pg_size, (fi->use_dma) ? DMA_ON : DMA_OFF)) {
			vPortFree(mem);
			return (-EHW);
		}
                vPortFree(mem);
                fi->buf2_ff = TRUE;
        }
        // Cmp buffer2 with page.
        cmd[0] = 0x61;
        adrbits(page, 0, cmd + 1);
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
		           (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        do {
                stat = 0xD7;
                if (0 != spi_trans(fi->spi, &fi->csel, &stat, 1, &stat, 1, DMA_OFF)) {
			return (-EHW);
		}
        } while (!(stat & AT45DB_FLASH_READY));
        if (stat & AT45DB_COMPARE_NOT_MATCH) {
                return (-EERASE);
        } else {
                return (0);
        }
}

/**
 * at45db_block_erase
 */
int at45db_block_erase(at45db fi, int block)
{
        boolean_t first = TRUE;
        unsigned char cmd[] = {0x50, 0x00, 0x00, 0x00};
        unsigned char stat;

        if (block < 0 || block >= fi->bl_count) {
                return (-EADDR);
        }
        cmd[1] = block >> 2;
        cmd[2] = block << 6;
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        vTaskDelay(BLOCK_ERASE_TIME);
        do {
                if (!first) {
                        taskYIELD();
                } else {
                        first = FALSE;
                }
                stat = 0xD7;
                if (0 != spi_trans(fi->spi, &fi->csel, &stat, 1, &stat, 1, DMA_OFF)) {
			return (-EHW);
		}
        } while (!(stat & AT45DB_FLASH_READY));
        return (0);
}

/**
 * at45db_chip_erase
 */
int at45db_chip_erase(at45db fi)
{
        unsigned char cmd[] = {0xC7, 0x94, 0x80, 0x9A};
        unsigned char stat;

        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        do {
		vTaskDelay(CHIP_ERASE_CHECK_TIME);
		stat = 0xD7;
                if (0 != spi_trans(fi->spi, &fi->csel, &stat, 1, &stat, 1, DMA_OFF)) {
			return (-EHW);
		}
        } while (!(stat & AT45DB_FLASH_READY));
	return (0);
}

/**
 * at45db_section_erase
 */
int at45db_section_erase(at45db fi, int start, int end)
{
        int i, err;

        if (start >= end) {
                return (-EADDR);
        }
        if (start < 0 || start > fi->pg_count - 2 || end >= fi->pg_count) {
                return (-EADDR);
        }
        for (i = start; i <= end; i++) {
                if (0 != (err = at45db_page_erase(fi, i))) {
			return (err);
		}
                if (0 != (err = at45db_check_page_erased(fi, i))) {
                        return (err);
                }
        }
        return (0);
}

/**
 * adrbits
 */
static void adrbits(int page, int offs, unsigned char *p)
{
        *p = page >> 5;
        *(p + 1) = page << 3;
        *(p + 1) |= (offs >> 8) & 0x7;
        *(p + 2) = offs;
}

#if AT45DB_TEST_CODE == 1
/**
 * at45db_rw_test
 */
boolean_t at45db_rw_test(at45db fi, int num, boolean_t verb)
{
	int cnt;

	if (verb) {
		msg(INF, "at45db.c: >>>>>> flash%s%s write test >>>>>>\n",
		    (fi->id) ? " " : "", (fi->id) ? fi->id : "");
	}
	for (cnt = 0; cnt < num; cnt++) {
		if (verb) {
			msg(INF, "at45db.c: test cycle %d ...\n", cnt + 1);
		}
		if (0 != t_device(fi, verb)) {
			break;
		}
	}
	if (cnt != num) {
		msg(INF, "at45db.c: ## write test error !!!\n");
                return (FALSE);
	} else {
		if (verb) {
			msg(INF, "at45db.c: ## write test done\n");
		}
                return (TRUE);
	}
}

/**
 * at45db_ro_test
 */
boolean_t at45db_ro_test(at45db fi, int num, boolean_t verb)
{
	unsigned char *buf;
	int cnt;

	if (NULL == (buf = pvPortMalloc(fi->pg_size))) {
		crit_err_exit(MALLOC_ERROR);
	}
	if (verb) {
		msg(INF, "at45db.c: >>>>>> flash%s%s read test >>>>>>\n",
		    (fi->id) ? " " : "", (fi->id) ? fi->id : "");
	}
	for (cnt = 0; cnt < num; cnt++) {
		if (verb) {
			msg(INF, "at45db.c: test cycle %d ...\n", cnt + 1);
			msg(INF, "at45db.c: reading pages (DMA)\n");
		}
                fi->use_dma = TRUE;
                if (0 != t_readpage_all(fi, buf, verb)) {
			break;
		}
                if (verb) {
			msg(INF, "at45db.c: reading pages (NO DMA, %s)\n",
			    (fi->csel.no_dma_intr) ? "intr. mode" : "poll mode");
		}
		fi->use_dma = FALSE;
                if (0 != t_readpage_all(fi, buf, verb)) {
			break;
		}
	}
	fi->use_dma = TRUE;
        vPortFree(buf);
	if (cnt != num) {
		msg(INF, "at45db.c: ## read test error !!!\n");
                return (FALSE);
	} else {
        	if (verb) {
			msg(INF, "at45db.c: ## read test done\n");
		}
                return (TRUE);
	}
}

/**
 * t_device
 */
static int t_device(at45db fi, boolean_t verb)
{
	unsigned char *buf;
	int err;
	int j, t = 499;

	if (NULL == (buf = pvPortMalloc(fi->pg_size))) {
		crit_err_exit(MALLOC_ERROR);
	}
        if (verb) {
		msg(INF, "at45db.c: writing pages\n");
	}
	for (j = 0; j < fi->pg_count; j++) {
		for (int i = 0; i < fi->pg_size / 2 - 2; i++) {
			*((uint16_t *) buf + i) = rand();
		}
		*((uint16_t *) buf + fi->pg_size / 2 - 2) = j;
		*((uint16_t *) buf + fi->pg_size / 2 - 1) = crc_ccit(INIT_CRC_CCITT, buf,
		                                                     fi->pg_size - 2);
		if (0 != (err = at45db_page_erase(fi, j))) {
			if (verb) {
				msg(INF, "at45db.c: page %d erase error\n", j);
			}
			goto err_exit;
		}
		if (0 != (err = at45db_check_page_erased(fi, j))) {
			if (verb) {
				msg(INF, "at45db.c: page %d verify erase error\n", j);
			}
			goto err_exit;
		}
		if (0 != (err = at45db_write_mem(fi, buf, 1, j, 0, fi->pg_size))) {
			if (verb) {
				msg(INF, "at45db.c: page %d write error\n", j);
			}
			goto err_exit;
		}
                fi->use_dma = TRUE;
                if (0 != (err = t_readpage(fi, buf, j, verb))) {
			goto err_exit;
		}
		fi->use_dma = FALSE;
                if (0 != (err = t_readpage(fi, buf, j, verb))) {
			goto err_exit;
		}
		if (verb && t == j) {
			msg(INF, "at45db.c: %d pages done\n", t + 1);
			t += 500;
		}
                vTaskDelay(AT45DB_TEST_DLY_MS / portTICK_PERIOD_MS);
	}
        if (verb) {
		msg(INF, "at45db.c: %d pages written\n", j);
	}
        if (verb) {
		msg(INF, "at45db.c: reading pages (DMA)\n");
	}
        fi->use_dma = TRUE;
        if (0 != (err = t_readpage_all(fi, buf, verb))) {
		goto err_exit;
	}
        if (verb) {
		msg(INF, "at45db.c: reading pages (NO DMA, %s)\n",
		    (fi->csel.no_dma_intr) ? "intr. mode" : "poll mode");
	}
	fi->use_dma = FALSE;
        err = t_readpage_all(fi, buf, verb);
err_exit:
	fi->use_dma = TRUE;
	vPortFree(buf);
	return (err);
}

/**
 * t_readpage
 */
static int t_readpage(at45db fi, unsigned char *buf, int page, boolean_t verb)
{
	for (int i = 0; i < fi->pg_size; i++) {
		*(buf + i) = 0;
	}
	if (0 != at45db_read_mem(fi, buf, page, 0, fi->pg_size)) {
		if (verb) {
			msg(INF, "at45db.c: page %d read error (", page);
			if (!fi->use_dma) {
				msg(INF, "NO ");
			}
			msg(INF, "DMA mode)\n");
		}
		return (-EHW);
	}
	if (*((uint16_t *) buf + fi->pg_size / 2 - 2) != page) {
		if (verb) {
			msg(INF, "at45db.c: page %d numbering error (", page);
			if (!fi->use_dma) {
				msg(INF, "NO ");
			}
			msg(INF, "DMA mode)\n");
		}
		return (-EDATA);
	}
	if (*((uint16_t *) buf + fi->pg_size / 2 - 1) !=
	    crc_ccit(INIT_CRC_CCITT, buf, fi->pg_size - 2)) {
		if (verb) {
			msg(INF, "at45db.c: page %d CRC error (", page);
			if (!fi->use_dma) {
				msg(INF, "NO ");
			}
			msg(INF, "DMA mode)\n");
		}
		return (-EDATA);
	}
	return (0);
}

/**
 * t_readpage_all
 */
static int t_readpage_all(at45db fi, unsigned char *buf, boolean_t verb)
{
	int t, i, err;

	t = 999;
	for (i = 0; i < fi->pg_count; i++) {
		if (0 != (err = t_readpage(fi, buf, i, verb))) {
			return (err);
		}
		if (verb && t == i) {
			msg(INF, "at45db.c: %d pages done\n", t + 1);
			t += 1000;
		}
		vTaskDelay(AT45DB_TEST_DLY_MS / portTICK_PERIOD_MS);
	}
	if (verb) {
		msg(INF, "at45db.c: %d pages tested\n", i);
	}
	return (0);
}
#endif
