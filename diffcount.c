/*
 * Count the number of bit differences and byte differences
 * between two files.
 *
 * To compile: gcc -march=broadwell -O3 -o diffcount diffcount.c
 * or:         gcc -mpopcnt -O3 -o diffcount diffcount.c
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
	uint64_t seek_1;   /* Seek value for file 1 */
	uint64_t seek_2;   /* Seek value for file 2 */
	uint64_t max_len;  /* Maximum number of bytes to compare.
	                      Go to first EOF if zero. */
	int const_mode;    /* 0: Compare two files.
                              1: Compare file to constant byte */
	uint8_t const_val; /* Constant byte value */
};

/* Diffcount result */
struct diffcount_res {
	uint64_t comp_B;   /* Total number of bytes compared */
	uint64_t comp_b;   /* Total number of bits compared */
	uint64_t diff_B;   /* Number of different bytes */
	uint64_t diff_b;   /* Number of different bits */
};

static struct diffcount_res *diffcount(struct diffcount_ctl *dc)
{
	FILE *stream_1 = NULL;
	FILE *stream_2 = NULL;

	unsigned char *buf_1;
	unsigned char *buf_2;

	uint8_t byte_xor;
	uint64_t quad_xor;
	uint64_t buf_cnt, buf1_cnt, buf2_cnt, read_size, ctr;

	/* Better performance using independent local variables. Assigned
	   to the struct before returning */
	uint64_t comp_B = 0;
	uint64_t diff_B = 0;
	uint64_t diff_b = 0;
	struct diffcount_res *dr;

	if ((stream_1 = fopen(dc->fname_1, "r")) == NULL) {
		fprintf(stderr, "fopen %s: %s\n",
		        dc->fname_1, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (fseeko(stream_1, dc->seek_1, 0) == -1) {
		fprintf(stderr, "fseeko %s: %s", dc->fname_1,
		        strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (dc->const_mode == 0) {
		if ((stream_2 = fopen(dc->fname_2, "r")) == NULL) {
			fprintf(stderr, "fopen %s: %s",
			        dc->fname_2, strerror(errno));
			exit(EXIT_FAILURE);
		}
		if (fseeko(stream_2, dc->seek_2, 0) == -1) {
			fprintf(stderr, "fseeko %s: %s", dc->fname_2,
			        strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	if ((buf_1 = malloc(BUFSIZE)) == NULL) {
		perror("malloc buf_1");
		exit(EXIT_FAILURE);
	}
	if ((buf_2 = malloc(BUFSIZE)) == NULL) {
		perror("malloc buf_2");
		exit(EXIT_FAILURE);
	}

	/* Fill buffer 2 with the constant value in constant mode */
	if (dc->const_mode == 1) {
		memset(buf_2, dc->const_val, BUFSIZE);
	}

	buf_cnt = 0;
	ctr = 0;
	while(1) {
		/* Fill up the buffer if empty */
		/* TODO: threads for better performance? */
		if (ctr == buf_cnt) {
			if ((dc->max_len != 0) &&
			    (dc->max_len - comp_B) < BUFSIZE) {
				read_size = dc->max_len - comp_B;
			} else {
				read_size = BUFSIZE;
			}

			buf1_cnt = fread(buf_1, 1, read_size, stream_1);

			if (dc->const_mode == 1) {
				buf_cnt = buf1_cnt;
			} else {
				buf2_cnt = fread(buf_2, 1, read_size,
				                 stream_2);
				buf_cnt = buf1_cnt < buf2_cnt ?
				            buf1_cnt : buf2_cnt;
			}

			/* Nothing was read in from at least one stream,
			   either because dc->max-len - comp_B is zero, or
			   because we encountered an EOF. Either way, we're
			   done. */
			if (buf_cnt == 0) break;
			ctr = 0;
		}

		if ((buf_cnt - ctr) >= 8) {
			quad_xor = *(uint64_t *)(buf_1 + ctr) ^
			           *(uint64_t *)(buf_2 + ctr);
			for (int i = 0; i < 8; i++) {
				diff_B += (((quad_xor >> (8*i)) & 0xff) != 0);
			}
			diff_b += _mm_popcnt_u64(quad_xor);
			ctr += 8;
			comp_B += 8;
		} else {
			byte_xor = buf_1[ctr]^buf_2[ctr];
			diff_B += byte_xor != 0;
			diff_b += _mm_popcnt_u32(byte_xor);
			ctr++;
			comp_B++;
		}
	}

	fclose(stream_1);
	if (stream_2 != NULL) fclose(stream_2);
	free(buf_1);
	free(buf_2);

	if ((dr = malloc(sizeof(struct diffcount_res))) == NULL) {
		perror("malloc dr");
		exit(EXIT_FAILURE);
	}
	dr->comp_B = comp_B;
	dr->comp_b = comp_B*8;
	dr->diff_B = diff_B;
	dr->diff_b = diff_b;

	return dr;
}

static void show_help(char **argv, int verbose)
{
	printf("Usage: %s [-ch] [-n len] file file/const [seek1] [seek2]\n",
	       argv[0]);
	if (verbose) {
		printf(" -c           compare to constant byte value\n"
		       " -h           print help\n"
		       " -n max_len   maximum number of bytes to compare\n");
	}
	exit(EXIT_FAILURE);
}


int main(int argc, char **argv) 
{
	int opt;

	struct diffcount_ctl *dc;
	struct diffcount_res *dr;

	struct stat sb;
	uint64_t fsize_1, fsize_2;

	/* Allocate diffcount_ctl structure and set defaults */
	if ((dc = malloc(sizeof(struct diffcount_ctl))) == NULL) {
		perror("malloc dc");
		exit(EXIT_FAILURE);
	}
	dc->fname_1 = NULL;
	dc->fname_2 = NULL;
	dc->seek_1 = 0;
	dc->seek_2 = 0;
	dc->max_len = 0;
	dc->const_mode = 0;
	dc->const_val = 0;

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

	/* Count differences */
	dr = diffcount(dc);

	/* Output statistics */
	if (stat(dc->fname_1, &sb) == -1) {
		fprintf(stderr, "fstat: %s: %s\n", dc->fname_1,
		        strerror(errno));
		exit(EXIT_FAILURE);
	}
	fsize_1 = sb.st_size;

	if (dc->const_mode == 0) {
		if (stat(dc->fname_2, &sb) == -1) {
			fprintf(stderr, "fstat: %s: %s\n", dc->fname_2,
			        strerror(errno));
			exit(EXIT_FAILURE);
		}
		fsize_2 = sb.st_size;
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

	free(dc);
	free(dr);

	return 0;
}
