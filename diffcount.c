/*
 * diffcount - count bit and byte differences between files
 *
 * Copyright 2018 Austin Roach <ahroach@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BUFSIZE
#define BUFSIZE 512*64
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <smmintrin.h>

/* Diffcount control */
struct diffcount_ctl {
	char *fname_1;
	char *fname_2;
	unsigned long long seek_1;   /* Seek value for file 1 */
	unsigned long long seek_2;   /* Seek value for file 2 */
	unsigned long long max_len;  /* Maximum number of bytes to compare.
	                      Go to first EOF if zero. */
	int const_mode;    /* 0: Compare two files.
                              1: Compare file to constant byte */
	uint8_t const_val; /* Constant byte value */
};

/* Diffcount result */
struct diffcount_res {
	unsigned long long comp_B;   /* Total number of bytes compared */
	unsigned long long comp_b;   /* Total number of bits compared */
	unsigned long long diff_B;   /* Number of different bytes */
	unsigned long long diff_b;   /* Number of different bits */
};

static void *malloc_or_die(size_t size)
{
	void *buf;

	buf = malloc(size);
	if (buf == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	return buf;
}

static struct diffcount_ctl *diffcount_ctl_init(void)
{
	struct diffcount_ctl *dc;

	dc = malloc_or_die(sizeof(struct diffcount_ctl));
	dc->fname_1 = NULL;
	dc->fname_2 = NULL;
	dc->seek_1 = 0;
	dc->seek_2 = 0;
	dc->max_len = 0;
	dc->const_mode = 0;
	dc->const_val = 0;

	return dc;
}

static FILE *fopen_and_seek(const char *filename, off_t seek)
{
	FILE *stream;

	if ((stream = fopen(filename, "r")) == NULL) {
		fprintf(stderr, "fopen %s: %s\n",
		        filename, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (fseeko(stream, seek, 0) == -1) {
		fprintf(stderr, "fseeko %s: %s", filename,
		        strerror(errno));
		exit(EXIT_FAILURE);
	}
	return stream;
}

static off_t get_filesize(const char *filename)
{
	struct stat sb;

	if (stat(filename, &sb) == -1) {
		fprintf(stderr, "fstat: %s: %s\n", filename,
		        strerror(errno));
		exit(EXIT_FAILURE);
	}
	return sb.st_size;
}

/* Fill buffers. Returns the number of bytes that are ready to be compared
   in the two buffers. */
static size_t fill_buffers(const struct diffcount_ctl *dc,
                           FILE *stream_1, FILE *stream_2,
			   uint8_t *buf_1, uint8_t *buf_2,
                           unsigned long long bytes_compared)
{
	size_t buf_fill, buf1_fill, buf2_fill, read_size;

	if ((dc->max_len != 0) &&
	    (dc->max_len - bytes_compared) < BUFSIZE) {
		/* Read size limited by user-specified max_len */
		read_size = (size_t)(dc->max_len - bytes_compared);
	} else {
		/* Try to read the full BUFSIZE */
		read_size = BUFSIZE;
	}

	buf1_fill = fread(buf_1, 1, read_size, stream_1);

	if (dc->const_mode == 1) {
		/* In const mode, buffer count is whatever we managed to read
		   from the first stream */
		buf_fill = buf1_fill;
	} else {
		buf2_fill = fread(buf_2, 1, read_size, stream_2);
		/* Return the lesser of the number of bytes that we
		   successfuly read from each of the two streams. */
		buf_fill = buf1_fill < buf2_fill ? buf1_fill : buf2_fill;
	}

	return buf_fill;
}

static struct diffcount_res *diffcount(const struct diffcount_ctl *dc)
{
	FILE *stream_1 = NULL, *stream_2 = NULL;
	uint8_t *buf_1, *buf_2, byte_xor;
	uint64_t quad_xor;
	unsigned long long buf_idx, buf_fill;
	struct diffcount_res *dr;
	/* Better performance using independent local variables. Assigned
	   to the struct before returning */
	unsigned long long comp_B = 0, diff_B = 0, diff_b = 0;

	stream_1 = fopen_and_seek(dc->fname_1, dc->seek_1);
	if (dc->const_mode == 0) {
		stream_2 = fopen_and_seek(dc->fname_2, dc->seek_2);
	}

	buf_1 = malloc_or_die(BUFSIZE);
	buf_2 = malloc_or_die(BUFSIZE);

	/* Fill buffer 2 with the constant value in constant mode */
	if (dc->const_mode == 1) {
		memset(buf_2, dc->const_val, BUFSIZE);
	}

	buf_fill = 0;
	buf_idx = 0;
	while(1) {
		/* Fill up the buffer if we're at the end */
		if (buf_idx == buf_fill) {
			/* TODO: Threads for better performance? */
			buf_fill = fill_buffers(dc, stream_1, stream_2, buf_1,
			                        buf_2, comp_B);

			/* If buf_fill is zero, we have no new data to compare,
			   either because we already read up to max_len, or
			   because we encountered EOF in either or both of
			   the streams. Either way, we're done.*/
			if (buf_fill == 0) break;
			buf_idx = 0;
		}

		if ((buf_fill - buf_idx) >= 8) {
			/* Process 8 bytes at a time */
			quad_xor = *(uint64_t *)(buf_1 + buf_idx) ^
			           *(uint64_t *)(buf_2 + buf_idx);
			for (int i = 0; i < 8; i++) {
				diff_B += (((quad_xor >> (8*i)) & 0xff) != 0);
			}
			diff_b += _mm_popcnt_u64(quad_xor);
			buf_idx += 8;
			comp_B += 8;
		} else {
			/* Clean up any remaining bytes */
			byte_xor = buf_1[buf_idx] ^ buf_2[buf_idx];
			diff_B += byte_xor != 0;
			diff_b += _mm_popcnt_u32(byte_xor);
			buf_idx++;
			comp_B++;
		}
	}

	fclose(stream_1);
	if (stream_2 != NULL) fclose(stream_2);
	free(buf_1);
	free(buf_2);

	dr = malloc_or_die(sizeof(struct diffcount_res));
	dr->comp_B = comp_B;
	dr->comp_b = 8*comp_B;
	dr->diff_B = diff_B;
	dr->diff_b = diff_b;

	return dr;
}

static void print_results(const struct diffcount_ctl *dc,
                          const struct diffcount_res *dr)
{
	unsigned long long fsize_1, fsize_2;

	fsize_1 = get_filesize(dc->fname_1);

	if (dc->const_mode == 0) {
		fsize_2 = get_filesize(dc->fname_2);
	}

	printf("File 1: %s\n"
	       "  Size: %llu (0x%llx) bytes\n"
	       "  Offset: %llu (0x%llx) bytes\n",
	       dc->fname_1, fsize_1, fsize_1, dc->seek_1, dc->seek_1);
	if (dc->const_mode == 0) {
		printf("File 2: %s\n"
		       "  Size: %llu (0x%llx) bytes\n"
		       "  Offset: %llu (0x%llx) bytes\n",
		       dc->fname_2, fsize_2, fsize_2,
		       dc->seek_2, dc->seek_2);
	} else {
		printf("Compared to constant value 0x%02hhx\n",
		       dc->const_val);
	}
	printf("Compared %llu (0x%llx) bytes, %llu (0x%llx) bits\n\n",
	       dr->comp_B, dr->comp_B, dr->comp_b, dr->comp_b);

	printf("            Byte count    Byte fraction       "
	       "Bit count     Bit fraction\n");

	printf("Differ: %14llu  %14.13f  %14llu  %14.13f\n",
	       dr->diff_B, 1.0*dr->diff_B/dr->comp_B,
	       dr->diff_b, 1.0*dr->diff_b/dr->comp_b);
	printf("Equal:  %14llu  %14.13f  %14llu  %14.13f\n",
	       dr->comp_B - dr->diff_B,
	       (1.0*dr->comp_B - dr->diff_B)/dr->comp_B,
	       dr->comp_b - dr->diff_b,
	       (1.0*dr->comp_b - dr->diff_b)/dr->comp_b);
}

static void show_help(char **argv, int verbose)
{
	printf("Usage: %s [-ch] [-n len] file1 file2/const [seek1 [seek2]]\n",
	       argv[0]);
	if (verbose) {
		printf(" -c       compare file to constant byte value\n"
		       " -h       print help\n"
		       " -n len   maximum number of bytes to compare\n");
	}
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv) 
{
	int opt;

	struct diffcount_ctl *dc;
	struct diffcount_res *dr;

	dc = diffcount_ctl_init();

	/* Get command line arguments */
	while ((opt = getopt(argc, argv, "chn:")) != -1) {
		switch (opt) {
		case 'c':
			dc->const_mode = 1;
			break;
		case 'h':
			show_help(argv, 1);
			break;
		case 'n':
			dc->max_len = strtoull(optarg, NULL, 0);
			break;
		default:
			show_help(argv, 0);
		}
	}

	if ((argc - optind) < 2) show_help(argv, 0);
	dc->fname_1 = argv[optind++];

	if (dc->const_mode)
		dc->const_val = strtoul(argv[optind++], NULL, 0);
	else
		dc->fname_2 = argv[optind++];

	dc->seek_1 = (optind < argc) ? strtoull(argv[optind++], NULL, 0) : 0;
	dc->seek_2 = (optind < argc) ? strtoull(argv[optind++], NULL, 0) : 0;

	if (optind < argc) show_help(argv, 0); //Leftover arguments

	/* Perform calculations and print results */
	dr = diffcount(dc);
	print_results(dc, dr);

	free(dc);
	free(dr);

	return 0;
}

