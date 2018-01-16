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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <smmintrin.h>


int main(int argc, char **argv) 
{
	int bit_mode = 0;
	int byte_mode = 1;
	int constant_mode = 0;
	int equal_mode = 0;
	int fraction_mode = 0;
	int verbose = 0;

	char *fname_1;
	char *fname_2;

	FILE *stream1 = NULL;
	FILE *stream2 = NULL;

	off_t fsize_1;
	off_t fsize_2;

	off_t cmp_size = 0;
	off_t diff_cnt = 0;

	off_t buf_cnt = 0;

	unsigned char *buf_1;
	unsigned char *buf_2;

	unsigned char byte_xor, constant_value = 0;

	buf_1 = malloc(BUFSIZE);
	buf_2 = malloc(BUFSIZE);


	// Parse the command line arguments
	if (argc < 3) {
		fprintf(stderr,
		        "usage: %s [-b/-B/-c/-f/-e] fname_1 [fname_2/const_byte_value]\n",
		        argv[0]);
		return 1;
	}

	// TODO: Move to using getopt_long for argument passing
	for (int i = 1; i <= argc-3; i++) {
		if((strcmp(argv[i], "-b") == 0) || 
		   (strcmp(argv[i], "--bit") == 0)) {
			bit_mode = 1;
			byte_mode = 0;
		} else if((strcmp(argv[i], "-B") == 0) ||
		          (strcmp(argv[i], "--byte") == 0)) {
			bit_mode = 0;
			byte_mode=1;
		} else if((strcmp(argv[i], "-c") == 0) ||
		          (strcmp(argv[i], "--constant") == 0)) {
			constant_mode=1;
		} else if((strcmp(argv[i], "-e") == 0) ||
		          (strcmp(argv[i], "--equal") == 0)) {
			equal_mode=1;
		} else if((strcmp(argv[i], "-f") == 0) ||
		          (strcmp(argv[i], "--fraction") == 0)) {
			fraction_mode=1;
		} else {
			fprintf(stderr,
			        "Unsupported option %s in call to %s\n",
			        argv[i], argv[0]);
			return 1;
		}
	}

	fname_1 = argv[argc-2];

	// Open file1
	stream1 = fopen(fname_1, "r");
	if (errno != 0) {
		fprintf(stderr,
		        "Error opening file %s\n",
		        fname_1);
		exit(EXIT_FAILURE);
	}

	// Get the size of file1
	if(fseeko(stream1, 0, SEEK_END) != 0) {
		fprintf(stderr,
		        "Error seeking to SEEK_END of %s\n",
			fname_1);
		fclose(stream1);
		exit(EXIT_FAILURE);
	}
	fsize_1 = ftello(stream1);
	if(fseeko(stream1, 0, 0) != 0) {
		fprintf(stderr,
		        "Error seeking to beginning of %s\n",
		        fname_1);
		fclose(stream1);
		exit(EXIT_FAILURE);
	}

	// If constant mode, get the value of the byte
	if (constant_mode == 1) {
		constant_value = (unsigned char)strtol(argv[argc-1], NULL, 0);
		cmp_size = cmp_size == 0 ? fsize_1 : cmp_size;
		}
	} else {
		//Otherwise, open file2 and get its size
		fname_2 = argv[argc-1];

		stream2 = fopen(fname_2, "r");
		if (errno != 0) {
			fprintf(stderr,
			        "Error opening file %s\n",
			        fname_2);
			fclose(stream1);
			exit(EXIT_FAILURE);
		}

		if(fseeko(stream2, 0, SEEK_END) != 0) {
			fprintf(stderr,
			        "Error seeking to SEEK_END of %s\n",
			        fname_2);
			fclose(stream1);
			fclose(stream2);
			exit(EXIT_FAILURE);
		}

		fsize_2 = ftello(stream2);
		if(fseeko(stream2, 0, 0) != 0) {
			fprintf(stderr,
			        "Error seeking to beginning of %s\n",
			        fname_2);
			fclose(stream1);
			fclose(stream2);
			exit(EXIT_FAILURE);
		}

		if (cmp_size == 0) {
			cmp_size = fsize_1 < fsize_2 ? fsize_1 : fsize_2;
			if ((fsize_1 != fsize_2) && verbose) {
				fprintf(stderr,
				        "File sizes differ.\n"
				        "File 1: %llu bytes\n"
				        "File 2: %llu bytes\n"
				        "Comparing only %llu bytes\n",
				        fsize_1, fsize_2, cmp_size);
			}
		}
	}

	//Fill the buffer with the constant value if we're using that.
	if (constant_mode==1) {
		memset(buf_2, constant_value, BUFSIZE);
	}

	// TODO: Take advantage of 64-bit registers to process 8 bytes
	// at a time when possible
	for(off_t i=0; i < cmp_size; i++) {
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
				        1.0*(cmp_size-diff_cnt) /
				        cmp_size);
			} else {
				fprintf(stdout,
				        "%llu\n",
				        cmp_size-diff_cnt);
			}
		} else if (bit_mode) {
			if (fraction_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*(cmp_size*8-diff_cnt) /
				        (cmp_size*8));
			} else {
				fprintf(stdout,
				        "%llu\n",
				        cmp_size*8-diff_cnt);
			}
		} 
	} else {
		if (fraction_mode) {
			if (byte_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*diff_cnt/cmp_size);
			} else if (bit_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*diff_cnt/(8*cmp_size));
			}
		} else {
			fprintf(stdout, "%llu\n", diff_cnt);
		}
	}

	return 0;
}
