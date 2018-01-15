/**************************************************************
 *                                                            *
 * Count the number of bit differences or byte differences    *
 * between two files. Computes byte differences by default.   *
 *                                                            *
 * Usage: count_byte_diff [-b/-B] filename1 filename2         *
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
 * To compile: gcc -march=broadwell -O3 -o count_byte_diff /  *
 *             count_byte_diff.c                              *
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
	unsigned char bit_mode = 0;
	unsigned char byte_mode = 1;
	unsigned char constant_mode = 0;
	unsigned char equal_mode = 0;
	unsigned char fraction_mode = 0;

	char constant_value = 0x00;

	char *filename1;
	char *filename2;

	FILE *stream1;
	FILE *stream2;

	off_t file_size_1;
	off_t file_size_2;

	off_t min_file_size;
	off_t diff_count = 0;

	off_t buf_cnt = 0;

	unsigned char *data_buf_1;
	unsigned char *data_buf_2;

	unsigned char byte_xor;

	data_buf_1 = malloc(BUFSIZE);
	data_buf_2 = malloc(BUFSIZE);


	// Parse the command line arguments
	if (argc < 3) {
		fprintf(stderr,
		        "usage: %s [-b/-B/-c/-f/-e] filename1 [filename2/const_byte_value]\n",
		        argv[0]);
		return 1;
	}

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

	filename1 = argv[argc-2];

	// Open file1
	stream1 = fopen(filename1, "r");
	if (errno != 0) {
		fprintf(stderr,
		        "Error opening file %s\n",
		        filename1);
		return 1;
	}

	// Get the size of file1
	if(fseeko(stream1, 0, SEEK_END) != 0) {
		fprintf(stderr,
		        "Error seeking to SEEK_END of %s\n",
			filename1);
	}
	file_size_1 = ftello(stream1);
	if(fseeko(stream1, 0, 0) != 0) {
		fprintf(stderr,
		        "Error seeking to beginning of %s\n",
		        filename1);
	}

	// If constant mode, get the value of the byte
	if (constant_mode == 1) {
		sscanf(argv[argc-1], "%hhx", &constant_value);
		min_file_size = file_size_1;
	} else {
		//Otherwise, open file2 and get its size
		filename2 = argv[argc-1];

		stream2 = fopen(filename2, "r");
		if (errno != 0) {
			fprintf(stderr,
			        "Error opening file %s\n",
			        filename2);
			return 1;
		}

		if(fseeko(stream2, 0, SEEK_END) != 0) {
			fprintf(stderr,
			        "Error seeking to SEEK_END of %s\n",
			        filename2);
		}

		file_size_2 = ftello(stream2);
		if(fseeko(stream2, 0, 0) != 0) {
			fprintf(stderr,
			        "Error seeking to beginning of %s\n",
			        filename2);
		}

		if (file_size_1 != file_size_2) {
			fprintf(stderr,
			        "File sizes differ. File 1: %llu bytes; File 2: %llu bytes\n",
			        file_size_1, file_size_2);
			fprintf(stderr,
			        "Only comparing across the smaller file size.\n");
		}

		if (file_size_1 < file_size_2) {
			min_file_size = file_size_1;
		} else {
			min_file_size = file_size_2;
		}
	}

	//Fill the buffer with the constant value if we're using that.
	if (constant_mode==1) {
		memset(data_buf_2, constant_value, BUFSIZE);
	}

	for(off_t i=0; i < min_file_size; i++) {
		// Fill up the buffer if we're at the beginning.
		if (buf_cnt == 0) {
			fread(data_buf_1, BUFSIZE, 1, stream1);
			if (constant_mode == 0) {
				fread(data_buf_2, BUFSIZE, 1, stream2);
			}
		}

		// Compare the bytes
		if (byte_mode) {
			if(data_buf_1[buf_cnt] != data_buf_2[buf_cnt]) {
				diff_count++;
			}
		} else if (bit_mode) {
			byte_xor = data_buf_1[buf_cnt]^data_buf_2[buf_cnt];
			/* 
			// Slow way of counting bits, but should work
			// on all architectures
			while (byte_xor > 0) {
				if ((byte_xor & 1) == 1) {
					diff_count++;
				}
				byte_xor = byte_xor >> 1;
			}
			*/

			// Use the SSE4 instruction
			// Need to compile with -march=broadwell,
			// or another option supporting SSE4
			diff_count += _mm_popcnt_u32(byte_xor);
		}

		// Increment buf_cnt; Wrap around if we're at the end.
		buf_cnt++;
		if (buf_cnt > BUFSIZE) {
			buf_cnt = 0;
		}
	}

	if (equal_mode) {
		if (byte_mode) {
			if (fraction_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*(min_file_size-diff_count) /
				        min_file_size);
			} else {
				fprintf(stdout,
				        "%llu\n",
				        min_file_size-diff_count);
			}
		} else if (bit_mode) {
			if (fraction_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*(min_file_size*8-diff_count) /
				        (min_file_size*8));
			} else {
				fprintf(stdout,
				        "%llu\n",
				        min_file_size*8-diff_count);
			}
		} 
	} else {
		if (fraction_mode) {
			if (byte_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*diff_count/min_file_size);
			} else if (bit_mode) {
				fprintf(stdout,
				        "%.11g\n",
				        1.0*diff_count/(8*min_file_size));
			}
		} else {
			fprintf(stdout, "%llu\n", diff_count);
		}
	}
	// Close the files
	fclose(stream1);
	if (constant_mode == 0) {
		fclose(stream2);
	}

	// Free the data buffers
	free(data_buf_1);
	free(data_buf_2);

	return 0;
}
