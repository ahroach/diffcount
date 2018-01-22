/*
 * Count the number of bit differences and byte differences
 * between two files.
 *
 * To compile: gcc -march=broadwell -O3 -o diffcount diffcount.c
 * or:         gcc -mpopcnt -O3 -o diffcount diffcount.c
 */

#define BUFSIZE 512*64

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <smmintrin.h>

/* Structure for diffcount control */
struct diffcount_ctl {
	char *fname_1;
	char *fname_2;
	uint64_t skip_1;   /* Seek value for file 1 */
	uint64_t skip_2;   /* Seek value for file 2 */
	uint64_t max_len;  /* Maximum number of bytes to compare.
	                      Go to first EOF if zero. */
	int const_mode;    /* 0: Compare files.
                              1: Compare file to constant byte */
	uint8_t const_val; /* Constant byte value */
};

/* Structure for diffcount result */
struct diffcount_res {
	uint64_t comp_B;   /* Total number of bytes compared */
	uint64_t diff_B;   /* Number of different bytes */
	uint64_t diff_b;   /* Number of different bits */
};

static struct diffcount_res *diffcount(struct diffcount_ctl *dc)
{
	unsigned char *buf_1;
	unsigned char *buf_2;

	FILE *stream_1 = NULL;
	FILE *stream_2 = NULL;

	struct diffcount_res *dr;

	uint8_t byte_xor;
	uint64_t quad_xor;
	uint64_t buf_cnt, ctr, buf1_cnt, buf2_cnt, read_size;

	/* Better performance using independent local variables. Assign
	 * to the struct at the end of the function */
	uint64_t comp_B = 0;
	uint64_t diff_B = 0;
	uint64_t diff_b = 0;

	if ((stream_1 = fopen(dc->fname_1, "r")) == NULL) {
		fprintf(stderr, "fopen %s: %s\n",
		        dc->fname_1, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (fseeko(stream_1, dc->skip_1, 0) == -1) {
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
		if (fseeko(stream_2, dc->skip_2, 0) == -1) {
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

		if ((buf_cnt - ctr) > 8) {
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

			// Use the SSE4 POPCNT instruction
			// Need to compile with -march=broadwell,
			// or another option supporting SSE4
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
	dr->diff_B = diff_B;
	dr->diff_b = diff_b;

	return dr;
}

static void show_help(char **argv, int verbose)
{
	printf("Usage: %s [-ch] [-n len] file file/constant [skip1] [skip2]\n",
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

	struct stat sb;
	uint64_t fsize_1, fsize_2;

	struct diffcount_ctl *dc;
	struct diffcount_res *dr;


	/* Allocate the diffcnt_ctl structure and set defaults */
	if ((dc = malloc(sizeof(struct diffcount_ctl))) == NULL) {
		perror("malloc dc");
		exit(EXIT_FAILURE);
	}
	dc->fname_1 = NULL;
	dc->fname_2 = NULL;
	dc->skip_1 = 0;
	dc->skip_2 = 0;
	dc->max_len = 0;
	dc->const_mode = 0;
	dc->const_val = 0;

	/* Get command line arguments */
	while ((opt = getopt(argc, argv, "cn:h")) != -1) {
		switch (opt) {
		case 'n':
			dc->max_len = strtoull(optarg, NULL, 0);
			break;
		case 'c':
			dc->const_mode = 1;
			break;
		case 'h':
			show_help(argv, 1);
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

	dc->skip_1 = (optind < argc) ? strtoull(argv[optind++], NULL, 0) : 0;
	dc->skip_2 = (optind < argc) ? strtoull(argv[optind++], NULL, 0) : 0;

	if (optind < argc) show_help(argv, 0); //Leftover arguments

	// Get the size of file1
	if (stat(dc->fname_1, &sb) == -1) {
		fprintf(stderr, "fstat: %s: %s\n", dc->fname_1,
		        strerror(errno));
		exit(EXIT_FAILURE);
	}
	fsize_1 = sb.st_size;

	dr = diffcount(dc);

	printf("Bytes equal: %012llu %.11g\n",
	        (dr->comp_B - dr->diff_B),
		1.0*(dr->comp_B - dr->diff_B)/dr->comp_B);
	printf("Bytes diff:  %012llu %.11g\n",
	       dr->diff_B,
	       1.0*dr->diff_B/dr->comp_B);
	printf("Bits equal:  %012llu %.11g\n",
	       (8*dr->comp_B - dr->diff_b),
	       1.0*(8*dr->comp_B - dr->diff_b)/(8*dr->comp_B));
	printf("Bits diff:   %012llu %.11g\n",
	      dr->diff_b,
	      1.0*dr->diff_b/(8*dr->comp_B));

	free(dc);
	free(dr);

	return 0;
}
