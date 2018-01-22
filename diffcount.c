/**************************************************************
 *                                                            *
 * Count the number of bit differences or byte differences    *
 * between two files. Computes byte differences by default.   *
 *                                                            *
 * Usage: diffcount [-b/-B] filename1 filename2               *
 * -b or --bit: enables bit-differencing mode                 *
 * -B or --byte: enables byte-differencing mode (default)     *
 * -c or --constant: compare to constant hexadecimal byte     *
 *                   value given in place of filename2        *
 * -e or --equal: print the number of bytes or bits that      *
 *                are equal over the compared segment         *
 * -f or --fraction: print the number of bytes or bits as     *
 *                   a fraction over the size of the          *
 *                   compared segment
 *                                                            *
 * To compile: gcc -march=broadwell -O3 -o diffcount /        *
 *             diffcount.c                                    *
 *                                                            *
 **************************************************************/

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
	int bit_mode = 0;
	int byte_mode = 1;
	int constant_mode = 0;
	int equal_mode = 0;
	int fraction_mode = 0;

	char *fname1;
	char *fname2;

	FILE *stream1 = NULL;
	FILE *stream2 = NULL;

	struct stat sb;

	uint64_t fsize_1;
	uint64_t fsize_2;

	uint64_t max_len = 0;

	uint64_t diff_cnt = 0;

	uint64_t skip1, skip2;

	uint64_t buf_cnt = 0;

	unsigned char *buf_1;
	unsigned char *buf_2;

	unsigned char byte_xor, comp_val = 0;

	while ((opt = getopt(argc, argv, "cn:h")) != -1) {
		switch (opt) {
		case 'n':
			max_len = strtoull(optarg, NULL, 0);
			break;
		case 'c':
			constant_mode = 1;
			break;
		case 'h':
			show_help(argv, 1);
			break;
		default:
			show_help(argv, 0);
		}
	}

	// Get remaining arguments
	if ((argc - optind) < 2) show_help(argv, 0);
	fname1 = argv[optind++];

	if (constant_mode)
		comp_val = strtoul(argv[optind++], NULL, 0);
	else
		fname2 = argv[optind++];

	skip1 = (optind < argc) ? strtoull(argv[optind++], NULL, 0) : 0;
	skip2 = (optind < argc) ? strtoull(argv[optind++], NULL, 0) : 0;

	if (optind < argc) show_help(argv, 0); //Leftover arguments

	// Get the size of file1
	if (stat(fname1, &sb) == -1) {
		fprintf(stderr, "fstat: %s: %s\n", fname1, strerror(errno));
		exit(EXIT_FAILURE);
	}
	fsize_1 = sb.st_size;

	// Open file1
	if ((stream1 = fopen(fname1, "r")) == NULL) {
		fprintf(stderr, "fopen: %s: %s\n", fname1, strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (constant_mode == 0) {
		//Open file2 and get its size
		if (stat(fname2, &sb) == -1) {
			fprintf(stderr, "fstat: %s: %s\n", fname2,
			        strerror(errno));
			exit(EXIT_FAILURE);
		}
		fsize_2 = sb.st_size;

		if ((stream2 = fopen(fname2, "r")) == NULL) {
			fprintf(stderr, "fopen: %s: %s", fname2,
			        strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (max_len == 0) {
			max_len = fsize_1 < fsize_2 ? fsize_1 : fsize_2;
		}
	} else {
		if (max_len == 0) {
			max_len = fsize_1;
		}
	}

	if ((buf_1 = malloc(BUFSIZE)) == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	if ((buf_2 = malloc(BUFSIZE)) == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	//Fill the buffer with the constant value if we're using that.
	if (constant_mode==1) {
		memset(buf_2, comp_val, BUFSIZE);
	}

	// TODO: Take advantage of 64-bit registers to process 8 bytes
	// at a time when possible
	for(off_t i=0; i < max_len; i++) {
		// Fill up the buffer if we're at the beginning.
		// TODO: threads for keeping the buffer full?
		if (buf_cnt == 0) {
			fread(buf_1, BUFSIZE, 1, stream1);
			if (constant_mode == 0) {
				fread(buf_2, BUFSIZE, 1, stream2);
			}
		}

		// Compare the bytes
		if (byte_mode) {
			if(buf_1[buf_cnt] != buf_2[buf_cnt]) {
				diff_cnt++;
			}
		} else if (bit_mode) {
			byte_xor = buf_1[buf_cnt]^buf_2[buf_cnt];
			/* 
			// Slow way of counting bits, but should work
			// on all architectures
			while (byte_xor > 0) {
				if ((byte_xor & 1) == 1) {
					diff_cnt++;
				}
				byte_xor = byte_xor >> 1;
			}
			*/

			// Use the SSE4 instruction
			// Need to compile with -march=broadwell,
			// or another option supporting SSE4
			diff_cnt += _mm_popcnt_u32(byte_xor);
		}

		// Increment buf_cnt; Wrap around if we're at the end.
		buf_cnt++;
		if (buf_cnt > BUFSIZE) {
			buf_cnt = 0;
		}
	}

	// Clean up
	if (stream1 != NULL) {
		fclose(stream1);
	}
	if (stream2 != NULL) {
		fclose(stream2);
	}

	free(buf_1);
	free(buf_2);

	// TODO: Add sanity to the output process
	if (equal_mode) {
		if (byte_mode) {
			if (fraction_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*(max_len-diff_cnt) /
				        max_len);
			} else {
				fprintf(stdout,
				        "%llu\n",
				        max_len-diff_cnt);
			}
		} else if (bit_mode) {
			if (fraction_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*(max_len*8-diff_cnt) /
				        (max_len*8));
			} else {
				fprintf(stdout,
				        "%llu\n",
				        max_len*8-diff_cnt);
			}
		} 
	} else {
		if (fraction_mode) {
			if (byte_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*diff_cnt/max_len);
			} else if (bit_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*diff_cnt/(8*max_len));
			}
		} else {
			fprintf(stdout, "%llu\n", diff_cnt);
		}
	}

	return 0;
}
