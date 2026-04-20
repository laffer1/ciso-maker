/*
    This file is part of Ciso.

    Ciso is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Ciso is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Foobar; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA


    Copyright 2005 BOOSTER
*/

#define _XOPEN_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>

#include <zlib.h>

#ifndef __APPLE__
#include <getopt.h>
#include <zconf.h>
#endif

#include "ciso.h"

#ifndef CISO_MAKER_VERSION
#define CISO_MAKER_VERSION "1.1.0"
#endif

static unsigned long long check_file_size(FILE *);
static unsigned long long get_stream_size(FILE *);
static int validate_cso_header(unsigned long long);
static int validate_index_entry(size_t, unsigned long long, unsigned long long);
static int compress_iso_to_cso(FILE *, FILE *, int);
static int decompress_cso_to_iso(FILE *, FILE *);
static void usage();

z_stream z;

uint32_t *index_buf = NULL;
uint32_t *crc_buf = NULL;
unsigned char *block_buf1 = NULL;
unsigned char *block_buf2 = NULL;

CISO_H ciso;
size_t ciso_total_block;

/* returns ULLONG_MAX on error */
static unsigned long long
check_file_size(FILE *fp)
{
	unsigned long long pos;

	pos = get_stream_size(fp);
	if (pos == ULLONG_MAX)
		return ULLONG_MAX;

	/* init ciso header */
	memset(&ciso, 0, sizeof(ciso));

	ciso.magic[0] = 'C';
	ciso.magic[1] = 'I';
	ciso.magic[2] = 'S';
	ciso.magic[3] = 'O';
	ciso.header_size = sizeof(ciso);
	ciso.ver      = 0x01;

	ciso.block_size  = 0x800; /* ISO9660 one of sector */
	ciso.total_bytes = pos;

	ciso_total_block = pos / ciso.block_size ;

	fseek(fp, 0, SEEK_SET);

	return pos;
}

/* returns ULLONG_MAX on error and preserves the current stream position */
static unsigned long long
get_stream_size(FILE *fp)
{
	long cur_pos;
	long end_pos;

	cur_pos = ftell(fp);
	if (cur_pos < 0)
		return ULLONG_MAX;

	if (fseek(fp, 0, SEEK_END) < 0)
		return ULLONG_MAX;

	end_pos = ftell(fp);
	if (end_pos < 0)
		return ULLONG_MAX;

	if (fseek(fp, cur_pos, SEEK_SET) < 0)
		return ULLONG_MAX;

	return (unsigned long long) end_pos;
}

static int
validate_cso_header(unsigned long long input_size)
{
	unsigned long long index_bytes;

	if (
		ciso.magic[0] != 'C' ||
		ciso.magic[1] != 'I' ||
		ciso.magic[2] != 'S' ||
		ciso.magic[3] != 'O' ||
		ciso.header_size != sizeof(ciso) ||
		ciso.ver != 0x01 ||
		ciso.block_size == 0 ||
		ciso.total_bytes == 0 ||
		ciso.align > 31
	)
	{
		fprintf(stderr, "ciso file format error\n");
		return 1;
	}

	if (ciso.total_bytes % ciso.block_size != 0)
	{
		fprintf(stderr, "ciso file format error\n");
		return 1;
	}

	ciso_total_block = (size_t) (ciso.total_bytes / ciso.block_size);
	if (ciso_total_block < 1)
	{
		fprintf(stderr, "total block less than 1.\n");
		return 1;
	}

	if (ciso_total_block > (SIZE_MAX / sizeof(*index_buf)) - 1)
	{
		fprintf(stderr, "ciso index too large\n");
		return 1;
	}

	index_bytes = (unsigned long long) (ciso_total_block + 1) * sizeof(*index_buf);
	if (input_size < sizeof(ciso) + index_bytes)
	{
		fprintf(stderr, "file read error\n");
		return 1;
	}

	return 0;
}

static int
validate_index_entry(size_t block, unsigned long long start_pos, unsigned long long end_pos)
{
	unsigned long long max_read_size;

	if (start_pos > end_pos)
	{
		fprintf(stderr, "block %zu : invalid index order\n", block);
		return 1;
	}

	if (end_pos - start_pos > UINT_MAX)
	{
		fprintf(stderr, "block %zu : block size too large\n", block);
		return 1;
	}

	max_read_size = (unsigned long long) ciso.block_size * 2;
	if (end_pos - start_pos > max_read_size)
	{
		fprintf(stderr, "block %zu : compressed block too large\n", block);
		return 1;
	}

	return 0;
}

static int
decompress_cso_to_iso(FILE *fin, FILE *fout)
{
	unsigned long long input_size;
	uint32_t index;
	uint32_t index2;
	unsigned long long read_pos;
	size_t read_size;
	size_t index_size;
	size_t block;
	size_t cmp_size;
	int status;
	size_t percent_period;
	size_t percent_cnt;
	bool plain;

	/* read header */
	if (fread(&ciso, 1, sizeof(ciso), fin) != sizeof(ciso))
	{
		fprintf(stderr, "file read error\n");
		return 1;
	}

	/* check header */
	input_size = get_stream_size(fin);
	if (input_size == ULLONG_MAX)
	{
		fprintf(stderr, "Can't get file size or size too large\n");
		return 1;
	}

	if (validate_cso_header(input_size) != 0)
		return 1;

	/* allocate index block */
	index_size = (ciso_total_block + 1) * sizeof(*index_buf);
	index_buf  = calloc(1, index_size);
	block_buf1 = calloc(1, ciso.block_size);
	block_buf2 = calloc(2, ciso.block_size);

	if (!index_buf || !block_buf1 || !block_buf2)
	{
		fprintf(stderr, "Can't allocate memory\n");
		return (1);
	}

	/* read index block */
	if (fread(index_buf, 1, index_size, fin) != index_size)
	{
		fprintf(stderr, "file read error\n");
		return (1);
	}

	/* show info */
	printf("Total File Size %llu bytes\n", ciso.total_bytes);
	printf("block size      %d  bytes\n", ciso.block_size);
	printf("total blocks    %zu  blocks\n", ciso_total_block);
	printf("index align     %u\n", 1U << ciso.align);

	/* init zlib */
	z.zalloc = Z_NULL;
	z.zfree  = Z_NULL;
	z.opaque = Z_NULL;

	/* decompress data */
	percent_period = ciso_total_block / 100;
	if (percent_period == 0)
		percent_period = 1;
	percent_cnt = 0;

	for (block = 0; block < ciso_total_block; block++)
	{
		if (--percent_cnt <= 0)
		{
			percent_cnt = percent_period;
			printf("decompress %zu%%\r", block / percent_period);
		}

		if (inflateInit2(&z,-15) != Z_OK)
		{
			fprintf(stderr, "inflateInit : %s\n", (z.msg) ? z.msg : "???");
			return (1);
		}

		/* check index */
		index  = index_buf[block];
		plain  = (index & 0x80000000U) != 0;
		index  &= 0x7fffffff;
		read_pos = index << (ciso.align);
		if (plain)
		{
			read_size = ciso.block_size;
		}
		else
		{
			index2 = index_buf[block + 1] & 0x7fffffffU;
			if (validate_index_entry(block, read_pos, (unsigned long long) index2 << ciso.align) != 0)
			{
				inflateEnd(&z);
				return 1;
			}
			read_size = ((size_t) (index2 - index)) << ciso.align;
		}
		if (plain && read_pos + read_size > input_size)
		{
			fprintf(stderr, "block %zu : read error\n", block);
			inflateEnd(&z);
			return 1;
		}

		if (fseek(fin, (long) read_pos, SEEK_SET) != 0)
		{
			fprintf(stderr, "block %zu : seek error\n", block);
			inflateEnd(&z);
			return 1;
		}

		z.avail_in  = (uInt) fread(block_buf2, 1, read_size , fin);
		if (z.avail_in != read_size)
		{
			fprintf(stderr, "block=%zu : read error\n", block);
			inflateEnd(&z);
			return (1);
		}

		if (plain)
		{
			memcpy(block_buf1,block_buf2, read_size);
			cmp_size = read_size;
		}
		else
		{
			z.next_out  = block_buf1;
			z.avail_out = ciso.block_size;
			z.next_in   = block_buf2;
			status = inflate(&z, Z_FULL_FLUSH);
			
			if (status != Z_STREAM_END)
			{
				fprintf(stderr, "block %zu:inflate : %s[%d]\n", block,(z.msg) ? z.msg : "error",status);
				inflateEnd(&z);
				return (1);
			}
			
			cmp_size = ciso.block_size - z.avail_out;
			
			if (cmp_size != ciso.block_size)
			{
				fprintf(stderr, "block %zu : block size error %zu != %u\n",block,cmp_size , ciso.block_size);
				inflateEnd(&z);
				return (1);
			}
		}
		
		/* write decompressed block */
		if (fwrite(block_buf1, 1, (size_t) cmp_size, fout) != cmp_size)
		{
			fprintf(stderr, "block %zu : Write error\n",block);
			inflateEnd(&z);
			return (1);
		}

		/* term zlib */
		if (inflateEnd(&z) != Z_OK)
		{
			fprintf(stderr, "inflateEnd : %s\n", (z.msg) ? z.msg : "error");
			return (1);
		}
	}

	printf("ciso decompress completed\n");
	
	return (0);
}


static int
compress_iso_to_cso(FILE *fin, FILE *fout, int level)
{
	unsigned long long file_size;
	unsigned long long write_pos;
	size_t index_size;
	size_t block;
	unsigned char buf4[64];
	size_t cmp_size;
	int status;
	size_t percent_period;
	size_t percent_cnt;
	size_t align;
	size_t align_b;
	size_t align_m;

	file_size = check_file_size(fin);
	if (file_size == ULLONG_MAX)
	{
		fprintf(stderr, "Can't get file size or size too large\n");
		return (1);
	}

	/* allocate index block */
	if (ciso_total_block > (SIZE_MAX / sizeof(*index_buf)) - 1)
	{
		fprintf(stderr, "ciso index too large\n");
		return 1;
	}

	index_size = (ciso_total_block + 1) * sizeof(*index_buf);
	index_buf  = calloc(1, index_size);
	crc_buf    = calloc(1, index_size);
	block_buf1 = calloc(1, ciso.block_size);
	block_buf2 = calloc(2, ciso.block_size);

	if (!index_buf || !crc_buf || !block_buf1 || !block_buf2)
	{
		fprintf(stderr, "Can't allocate memory\n");
		return (1);
	}
	memset(buf4, 0, sizeof(buf4));

	/* init zlib */
	z.zalloc = Z_NULL;
	z.zfree  = Z_NULL;
	z.opaque = Z_NULL;

	/* show info */
	printf("Total File Size %llu bytes\n", ciso.total_bytes);
	printf("block size      %d  bytes\n", ciso.block_size);
	printf("index align     %u\n", 1U << ciso.align);
	printf("compress level  %d\n", level);

	/* write header block */
	fwrite(&ciso,1, sizeof(ciso), fout);

	/* dummy write index block */
	fwrite(index_buf, 1, index_size, fout);

	write_pos = sizeof(ciso) + index_size;

	/* compress data */
	percent_period = ciso_total_block / 100;
	if (percent_period == 0)
		percent_period = 1;
	percent_cnt    = percent_period;

	align_b = (size_t) (1 << (ciso.align));
	align_m = align_b - 1;

	for (block = 0; block < ciso_total_block; block++)
	{
		if (--percent_cnt <= 0)
		{
			percent_cnt = percent_period;
			printf("compress %3zu%% avarage rate %3llu%%\r"
				, block / percent_period
				, block==0 ? 0 : 100 * write_pos / (block * 0x800));
		}

		if (deflateInit2(&z, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		{
			printf("deflateInit : %s\n", (z.msg) ? z.msg : "???");
			return 1;
		}

		/* write align */
		align = (size_t) write_pos & align_m;
		if (align)
		{
			align = align_b - align;
			if (fwrite(buf4, 1, align, fout) != align)
			{
				printf("block %zu : Write error\n",block);
				return 1;
			}
			write_pos += align;
		}

		/* mark offset index */
		if ((write_pos >> ciso.align) > 0x7fffffffU)
		{
			fprintf(stderr, "compressed file too large for ciso index\n");
			return 1;
		}
		index_buf[block] = (uint32_t) (write_pos >> ciso.align);

		/* read buffer */
		z.next_out  = block_buf2;
		z.avail_out = ciso.block_size * 2;
		z.next_in   = block_buf1;
		z.avail_in  = (uInt) fread(block_buf1, 1, ciso.block_size , fin);
		
		if (z.avail_in != ciso.block_size)
		{
			printf("block=%zu : read error\n",block);
			return 1;
		}

		status = deflate(&z, Z_FINISH);
		if (status != Z_STREAM_END)
		{
			printf("block %zu:deflate : %s[%d]\n", block,(z.msg) ? z.msg : "error",status);
			return 1;
		}

		cmp_size = ciso.block_size * 2 - z.avail_out;

		/* choise plain / compress */
		if (cmp_size >= ciso.block_size)
		{
			cmp_size = ciso.block_size;
			memcpy(block_buf2, block_buf1, cmp_size);
			/* plain block mark */
			index_buf[block] |= 0x80000000U;
		}

		/* write compressed block */
		if (fwrite(block_buf2, 1, cmp_size , fout) != cmp_size)
		{
			printf("block %zu : Write error\n",block);
			return 1;
		}

		/* mark next index */
		write_pos += cmp_size;

		/* term zlib */
		if (deflateEnd(&z) != Z_OK)
		{
			printf("deflateEnd : %s\n", (z.msg) ? z.msg : "error");
			return 1;
		}
	}

	/* last position (total size)*/
	if ((write_pos >> ciso.align) > 0x7fffffffU)
	{
		fprintf(stderr, "compressed file too large for ciso index\n");
		return 1;
	}
	index_buf[block] = (uint32_t) (write_pos >> ciso.align);

	/* write header & index block */
	fseek(fout, sizeof(ciso), SEEK_SET);
	fwrite(index_buf, 1, index_size, fout);

	printf("ciso compress completed , total size = %8d bytes, rate %d%%\n"
		, (int)write_pos, (int)(write_pos * 100 / ciso.total_bytes));
	
	return (0);
}

static void
usage()
{
	fprintf(stderr, "usage: ciso-maker [-c] [-x] -l level infile outfile\n");
	fprintf(stderr, "-c compresses\n");
	fprintf(stderr, "-x extracts e.g level 0\n");
	fprintf(stderr, "  level: 1-9 compress ISO to CSO (1=fast/large - 9=small/slow\n");
	fprintf(stderr, "         0   decompress CSO to ISO\n");
	exit(0);
}

int
main(int argc, char *argv[])
{
	int level = 1;
	int result;
	int ch;
	bool xFlag = false;
	FILE *fin, *fout;
	char *fname_in = NULL;
	char *fname_out = NULL;

	fprintf(stderr, "Compressed ISO9660 converter Ver.%s by BOOSTER, froom, and Lucas Holt\n",
		CISO_MAKER_VERSION);

	while ((ch = getopt(argc, argv, "cxl:")) != -1)
	{
		switch(ch) {
			case 'c':
				break;
			case 'x': 
				xFlag = true;
				break;
			case 'l':
				level = atoi(optarg);
				break;
			case '?':
			default:
				usage();
		}
	}
	argc -= optind;
	argv += optind;

	/* backward compatibility */
	if (argc == 3)
	{
		level = atoi(argv[0]);
		fname_in = argv[1];
		fname_out = argv[2];
	}
	else if (argc == 2)
	{
		fname_in = argv[0];
		fname_out = argv[1];
	}
	else
	{
		usage();
	}

	if (level < 0 || level > 9)
	{
		fprintf(stderr, "Unknown mode: %c\n", level);
		usage();
		return 1;
	}

	if ((fin = fopen(fname_in, "rb")) == NULL)
	{
		fprintf(stderr, "Can't open %s\n", fname_in);
		return 1;
	}

	if ((fout = fopen(fname_out, "wb")) == NULL)
	{
		fprintf(stderr, "Can't create %s\n", fname_out);
		return 1;
	}

	if (xFlag || level == 0)
	{
		printf("Decompress '%s' to '%s'\n", fname_in,fname_out);
		result = decompress_cso_to_iso(fin, fout);
	}
	else
	{
		printf("Compress '%s' to '%s'\n", fname_in, fname_out);
		result = compress_iso_to_cso(fin, fout, level);
	}

	free(index_buf);
	free(crc_buf);
	free(block_buf1);
	free(block_buf2);

	fclose(fin);
	fclose(fout);
	
	return (result);
}
