Diffcount: Count bit and byte differences between files
=======================================================

Diffcount is an application for counting the bit and byte differences between
two files, or between one file and a constant value.

Features
--------
* Arbitrary offsets can be set for each input file.
* A maximum compare length can be specified to limit the amount of compared data.
* Designed to be reasonably fast with large files.

Installation
------------
Diffcount relies on popcnt intrinsic functions, using the POPCNT instruction 
that was introduced at about the same time as SSE4. You should signal to gcc
that your processor supports the POPCNT instruction, either with an
appropriate `-march=` option, for example:

	gcc -march=broadwell -O3 -o diffcount diffcount.c

or, more generically, with `-mpopcnt`:

	gcc -mpopcnt -O3 -o diffcount diffcount.c

Compiling with optimizations is highly encourage, as this leads to significant
performance improvements.

Usage
-----
The user runs:

	diffcount [-ch] [-n len] file1 file2/const [seek1 [seek2]]

with the command line arguments:
* `-c`: compare file to constant byte value
* `-h`: print help
* `-n`: specify a maximum number of bytes to compare
* `seek1`: offset for `file1`
* `seek2`: offset for `file2`

In constant mode, a constant byte value should be specified in place of
`file2`. In constant mode, specifying `seek2` has no effect.

