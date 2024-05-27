/*
 * at45db.c
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

#define CHIP_ERASE_CHECK_RATE (500 / portTICK_PERIOD_MS)

static int wait_ready(at45db fi);
static boolean_t create_address(at45db fi, unsigned char *cmd, int page, int offs);
static void adrbits(at45db fi, int page, int offs, unsigned char *p);
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

#if AT45DB_USE_EXT_STAT == 1
/**
 * at45db_ext_stat
 */
int at45db_ext_stat(at45db fi, unsigned int *stat)
{
        unsigned char cmd[2] = {0xD7};

        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd, 2, DMA_OFF)) {
		return (-EHW);
	}
        *stat = cmd[0];
	*stat |= cmd[1] << 8;
	return (0);
}
#endif

/**
 * at45db_read_mem
 */
int at45db_read_mem(at45db fi, unsigned char *buf, int page, int offs, int num)
{
        unsigned char cmd[] = {0xD2, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};

	if (!create_address(fi, cmd, page, offs)) {
		return (-EADDR);
	}
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
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};

        if (bfn == 1) {
                cmd[0] = 0x82;
        } else if (bfn == 2) {
                cmd[0] = 0x85;
        } else {
		crit_err_exit(BAD_PARAMETER);
        }
	if (!create_address(fi, cmd, page, offs)) {
		return (-EADDR);
	}
        if (bfn == 2) {
                fi->buf2_ff = FALSE;
        }
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, sizeof(cmd), buf, num,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        vTaskDelay(AT45DB_PAGE_ERASE_TIME);
	return (wait_ready(fi));
}

/**
 * at45db_read_buf
 */
int at45db_read_buf(at45db fi, unsigned char *buf, int bfn, int offs, int num)
{
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00, 0xFF};

        if (bfn == 1) {
                cmd[0] = 0xD4;
        } else if (bfn == 2) {
                cmd[0] = 0xD6;
        } else {
                crit_err_exit(BAD_PARAMETER);
        }
	if (!create_address(fi, cmd, 0, offs)) {
		return (-EADDR);
	}
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, sizeof(cmd), buf, num,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        return (0);
}

/**
 * at45db_write_buf
 */
int at45db_write_buf(at45db fi, unsigned char *buf, int bfn, int offs, int num)
{
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};

        if (bfn == 1) {
                cmd[0] = 0x84;
        } else if (bfn == 2) {
                cmd[0] = 0x87;
        } else {
                crit_err_exit(BAD_PARAMETER);
        }
	if (!create_address(fi, cmd, 0, offs)) {
		return (-EADDR);
	}
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
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};

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
                crit_err_exit(BAD_PARAMETER);
        }
	if (!create_address(fi, cmd, page, 0)) {
		return (-EADDR);
	}
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
			   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        if (erase) {
                vTaskDelay(AT45DB_PAGE_ERASE_TIME);
        }
	return (wait_ready(fi));
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
                crit_err_exit(BAD_PARAMETER);
        }
	if (!create_address(fi, cmd, page, 0)) {
		return (-EADDR);
	}
        if (bfn == 2) {
                fi->buf2_ff = FALSE;
        }
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
			   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        do {
		taskYIELD();
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
        unsigned char cmd[] = {0x81, 0x00, 0x00, 0x00};

	if (!create_address(fi, cmd, page, 0)) {
		return (-EADDR);
	}
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
			   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        vTaskDelay(AT45DB_PAGE_ERASE_TIME);
	return (wait_ready(fi));
}

/**
 * at45db_check_page_erased
 */
int at45db_check_page_erased(at45db fi, int page)
{
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};
	unsigned char buf[8];
        unsigned char stat;
	int ret = 0;

        if (page < 0 || page >= fi->pg_count) {
                return (-EADDR);
        }
        // Fill buffer2 with 0xFF pattern.
	if (!fi->buf2_ff) {
		for (int i = 0; i < fi->pg_size / 8; i++) {
			memset(buf, 0xFF, 8);
			cmd[0] = 0x87;
			adrbits(fi, 0, i * 8, cmd + 1);
	                if (0 != spi_trans(fi->spi, &fi->csel, cmd, sizeof(cmd),
				           buf, 8, (fi->use_dma) ? DMA_ON : DMA_OFF)) {
				return (-EHW);
			}
			fi->buf2_ff = TRUE;
		}
	}
        // Cmp buffer2 with page.
        cmd[0] = 0x61;
        adrbits(fi, page, 0, cmd + 1);
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
		           (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        do {
                stat = 0xD7;
                if (0 != spi_trans(fi->spi, &fi->csel, &stat, 1, &stat, 1, DMA_OFF)) {
			return (-EHW);
		}
		if (stat & AT45DB_COMPARE_NOT_MATCH) {
	                ret = -EDATA;
	        }
        } while (!(stat & AT45DB_FLASH_READY));
	return (ret);
}

/**
 * at45db_block_erase
 */
int at45db_block_erase(at45db fi, int block)
{
        unsigned char cmd[] = {0x50, 0x00, 0x00, 0x00};

        if (block < 0 || block >= fi->bl_count) {
                return (-EADDR);
        }
	switch (fi->pg_size) {
	case 264 :
	        cmd[1] = block >> 4;
		cmd[2] = block << 4;
		break;
	case 1056 :
	        cmd[1] = block >> 2;
		cmd[2] = block << 6;
		break;
	default :
		crit_err_exit(BAD_PARAMETER);
		break;
	}
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        vTaskDelay(AT45DB_BLOCK_ERASE_TIME);
	return (wait_ready(fi));
}

/**
 * at45db_chip_erase
 */
int at45db_chip_erase(at45db fi)
{
        unsigned char cmd[] = {0xC7, 0x94, 0x80, 0x9A};
	int ret = 0;

        if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
#if AT45DB_USE_EXT_STAT == 1
	unsigned int stat;
        do {
		vTaskDelay(CHIP_ERASE_CHECK_RATE);
		if (at45db_ext_stat(fi, &stat) != 0) {
			return (-EHW);
		}
		if (stat & AT45DB_PROG_ERR) {
			ret = -EDATA;
		}
        } while (!(stat & AT45DB_FLASH_READY2));
#else
	unsigned char stat;
        do {
		vTaskDelay(CHIP_ERASE_CHECK_RATE);
		stat = 0xD7;
                if (0 != spi_trans(fi->spi, &fi->csel, &stat, 1, &stat, 1, DMA_OFF)) {
			return (-EHW);
		}
        } while (!(stat & AT45DB_FLASH_READY));
#endif
	return (ret);
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
 * at45db_read_cont
 */
int at45db_read_cont(at45db fi, enum at45db_read_cont_type type, unsigned char *buf, int page, int offs, int num)
{
        unsigned char cmd[] = {type, 0x00, 0x00, 0x00, 0xFF, 0xFF};
	int cmd_sz = 0;

	if (!create_address(fi, cmd, page, offs)) {
		return (-EADDR);
	}
	switch (type) {
	case AT45DB_READ_CONT_HF0 :
		cmd_sz = 5;
		break;
	case AT45DB_READ_CONT_HF1 :
		cmd_sz = 6;
		break;
	case AT45DB_READ_CONT_LF :
		/* FALLTHRU */
	case AT45DB_READ_CONT_LP :
		cmd_sz = 4;
		break;
	default :
		crit_err_exit(BAD_PARAMETER);
		break;
	}
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, cmd_sz, buf, num,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
	return (0);
}

/**
 * at45db_read_mod_write
 */
int at45db_read_mod_write(at45db fi, unsigned char *buf, int bfn, int page, int offs, int num)
{
        unsigned char cmd[] = {0x00, 0x00, 0x00, 0x00};

        if (bfn == 1) {
                cmd[0] = 0x58;
        } else if (bfn == 2) {
                cmd[0] = 0x59;
        } else {
                crit_err_exit(BAD_PARAMETER);
        }
	if (!create_address(fi, cmd, page, offs)) {
		return (-EADDR);
	}
        if (bfn == 2) {
                fi->buf2_ff = FALSE;
        }
        if (0 != spi_trans(fi->spi, &fi->csel, cmd, sizeof(cmd), buf, num,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
        vTaskDelay(AT45DB_PAGE_ERASE_PROG_TIME);
	return (wait_ready(fi));
}

/**
 * at45db_pwr_down
 */
int at45db_pwr_down(at45db fi, enum at45db_pwr_down_type type)
{
	unsigned char cmd = type;

	switch (cmd) {
	case AT45DB_ULTRA_DEEP_PWR_DOWN :
		fi->buf2_ff = FALSE;
		/* FALLTHRU */
	case AT45DB_DEEP_PWR_DOWN :
		break;
	default :
		crit_err_exit(BAD_PARAMETER);
		break;
	}
        if (0 != spi_trans(fi->spi, &fi->csel, &cmd, 1, &cmd, 0, DMA_OFF)) {
		return (-EHW);
	}
	return (0);
}

/**
 * at45db_wake
 */
int at45db_wake(at45db fi)
{
	unsigned char cmd = 0xAB;

        if (0 != spi_trans(fi->spi, &fi->csel, &cmd, 1, &cmd, 0, DMA_OFF)) {
		return (-EHW);
	}
	return (0);
}

/**
 * at45db_set_page_size
 */
int at45db_set_page_size(at45db fi, enum at45db_page_size sz)
{
	unsigned char cmd[] = {0x3D, 0x2A, 0x80, 0x00};

	if (sz != AT45DB_SET_PAGE_SIZE_PO2 && sz != AT45DB_SET_PAGE_SIZE_STD) {
		crit_err_exit(BAD_PARAMETER);
	}
	cmd[3] = sz;
	if (0 != spi_trans(fi->spi, &fi->csel, cmd, 1, cmd + 1, 3,
	                   (fi->use_dma) ? DMA_ON : DMA_OFF)) {
		return (-EHW);
	}
	return (0);
}

/**
 * wait_ready
 */
static int wait_ready(at45db fi)
{
	boolean_t first = TRUE;
	int ret = 0;

#if AT45DB_USE_EXT_STAT == 1
	unsigned int stat;
        do {
                if (!first) {
                        taskYIELD();
                } else {
                        first = FALSE;
                }
		if (at45db_ext_stat(fi, &stat) != 0) {
			return (-EHW);
		}
		if (stat & AT45DB_PROG_ERR) {
			ret = -EDATA;
		}
        } while (!(stat & AT45DB_FLASH_READY2));
#else
	unsigned char stat;
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
#endif
        return (ret);
}

/**
 * create_address
 */
static boolean_t create_address(at45db fi, unsigned char *cmd, int page, int offs)
{
        if (page < 0 || page >= fi->pg_count) {
                return (FALSE);
        }
        if (offs < 0 || offs >= fi->pg_size) {
                return (FALSE);
        }
        adrbits(fi, page, offs, cmd + 1);
	return (TRUE);
}

/**
 * adrbits
 */
static void adrbits(at45db fi, int page, int offs, unsigned char *p)
{
	switch (fi->pg_size) {
	case 264 :
	        *p = page >> 7;
	        *(p + 1) = page << 1;
	        *(p + 1) |= (offs >> 8) & 0x1;
	        *(p + 2) = offs;
		break;
	case 1056 :
	        *p = page >> 5;
	        *(p + 1) = page << 3;
	        *(p + 1) |= (offs >> 8) & 0x7;
	        *(p + 2) = offs;
		break;
	default :
		crit_err_exit(BAD_PARAMETER);
		break;
	}
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
