
/* -*- indent-tabs-mode: t; tab-width: 8; c-basic-offset: 8; -*- */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

/* ts A91016 : libburn/ecma130ab.c is the replacement for old libburn/lec.c

   Copyright 2009, Thomas Schmitt <scdbackup@gmx.net>, libburnia-project.org
   Provided under GPL version 2 or later.

   This code module implements the production of RSPC parity bytes (P- and Q-
   parity) and the scrambling of raw CD-ROM sectors as specified in ECMA-130:
     http://www.ecma-international.org/publications/files/ECMA-ST/Ecma-130.pdf

   The following statements about Galois Fields have been learned mostly from
     http://www.cs.utk.edu/~plank/plank/papers/CS-96-332.pdf
   by James S. Plank after an e-mail exchange with Norbert Preining.

   The output has been compared with the output of the old code of libburn
   which was labeled "borrowed HEAVILY from cdrdao" and claimed by Joerg
   Schilling to stem from code by Heiko Eissfeldt.

   -------------------------------------------------------------------------
   Note: In this text, "^" denotes exponentiation and not the binary exor
         operation. Confusingly in the C code "^" is said exor.
   Note: This is not C1, C2 which is rather mentioned in ECMA-130 Annex C and
         always performed inside the drive.
   -------------------------------------------------------------------------


                            RSPC resp. P- and Q-Parity

   ECMA-130 Annex A prescribes to compute the parity bytes for P-columns and
   Q-diagonals by RSPC based on a Galois Field GF(2^8) with enumerating
   polynomials x^8+x^4+x^3+x^2+1 (i.e. 0x11d) and x^1 (i.e. 0x02).
   Bytes 12 to 2075 of a audio-sized sector get ordered in two byte words
   as 24 rows and 43 columns. Then this matrix is split into a LSB matrix
   and a MSB matrix of the same layout. Parity bytes are to be computed
   from these 8-bit values.
   2 P-bytes cover each column of 24 bytes. They get appended to the matrix
   as rows 24 and 25.
   2 Q-bytes cover each the 26 diagonals of the extended matrix.

   Both parity byte pairs have to be computed so that extended rows or
   diagonals match this linear equation:
      H x V = (0,0)
   H is a 2-row matrix of size n matching the length of the V ectors
     [   1        1     ...   1   1 ]
     [ x^(n-1)  x^(n-2)      x^1  1 ]
   Vp represents a P-row. It is a byte vector consisting of row bytes at
   position 0 to 23 and the two parity bytes which shall be determined
   at position 24 and 25. So Hp has 26 columns.
   Vq represents a Q-diagonal. It is a byte vector consisting of diagonal
   bytes at position 0 to 42 and the two parity bytes at position 43 and 44.
   So Hq has 45 columns. The Q-diagonals cover P-parity bytes.

   By applying some high school algebra one gets the parity bytes b0, b1 of
   vector V = (n_payload_bytes, b0 , b1) as

     b0 = ( H[n] * SUM(n_payload_bytes) - H[0..(n-1)] x n_payload_bytes )
          / (H[n+1] - H[n])
     b1 = - SUM(n_payload_bytes) - b0

   H[i] is the i-the element of the second row of matrix H. E.g. H[0] = x^(n-1)
   The result has to be computed by Galois field arithmetics. See below.

   The P-parity bytes of each column get reunited as LSB and MSB of two words.
   word1 gets written to positions 1032 to 1074, word0 to 1075 to 1117.
   The Q-parity bytes of each diagonal get reunited too. word1 goes to 1118
   to 1143, word0 to 1144 to 1169.
   >>> I do not read this swap of word1 and word0 from ECMA-130 Annex A.
   >>> But the new output matches the old output only if it is done that way.
   >>> See correctness reservation below.

   Algebra on Galois fields is the same as on Rational Numbers.
   But arithmetics is defined by operations on polynomials rather than the
   usual integer arithmetics on binary numbers.
   Addition and subtraction are identical with the binary exor operator.
   Multiplication and division would demand polynomial division, e.g. by the
   euclidian algorithm. The computing path over logarithms and powers follows
   algebra and allows to reduce the arithmetic task to table lookups, additions
   modulo 255, and exor operations. Note that the logarithms are natural
   numbers, not polynomials. They get added or subtracted by the usual addition
   (not by exor) and their polynomial power depends on their value modulo 255.

   Needed are a logarithm table and a power table (or inverse logarithm table)
   for Galois Field GF(2^8) which will serve to perform the peculiar
   multiplication and division operation of Galois fields.

   The power table is simply an enumeration of x^n accorting to
   GF-multiplication. It also serves as second line of matrix H for the parity
   equations:
     Hp[i] = gfpow[25-i] , i out of {0,..,25}
     Hq[i] = gfpow[44-i] , i out of {0,..,44}

   The logarithm table is the inverse permutation of the power table.

   Some simplifications apply to the implementation:
   In the world of Galois fields there is no difference between - and +.
   The term (H[n+1] - H[n]) is constant: 3.

   -------------------------------------------------------------------------


                                 Scrambling

   ECMA-130 Annex B prescribes to exor the byte stream of an audio-sized sector
   with a sequence of pseudo random bytes. It mentions polynomial x^15+x+1 and
   a 15-bit register.
   It shows a diagram of a Feedback Shift Register with 16 bit boxes, though.
 
   Comparing this with explanations in 
     http://www.newwaveinstruments.com/resources/articles/m_sequence_linear_feedback_shift_register_lfsr.htm
   one can recognize the diagram as a Fibonacci Implementation. But there seems
   really to be one bit box too many.

   The difference of both lengths is expressed in function next_bit() by
   the constants 0x3fff,0x4000 for 15 bit versus 0x7fff,0x8000 for 16 bits.
   Comparing the output of both alternatives with the old scrambler output
   lets 15 bit win for now.

   So the prescription is to start with 15 bit value 1, to use the lowest bit
   as output, to shift the bits down by one, to exor the output bit with the
   next lowest bit, and to put that exor result into bit 14 of the register.

   -------------------------------------------------------------------------


                             Correctness Reservation

   In both cases, parity and scrambling, the goal for now is to replicate the
   output of the dismissed old lec.c by output which is based on published
   specs and own implementation code. Whether they comply to ECMA-130 is a
   different question which can only be answered by real test cases for
   raw CD recording.

   Of course this implementation will be corrected so that it really complies
   to ECMA-130 as soon as evidence emerges that it does not yet.

*/


/* ------------------------------------------------------------------------- */


/* Power and logarithm tables for GF(2^8), parity matrices for ECMA-130.
   Generated by burn_rspc_setup_tables() and burn_rspc_print_tables().

   The highest possible sum of gflog[] values is is 508. So the table gfpow[]
   with period 255 was manually unrolled to 509 elements to avoid one modulo
   255 operation in burn_rspc_mult().
   Proposed by D. Hugh Redelmeier.
   
*/

static unsigned char gfpow[509] = {
	  1,   2,   4,   8,  16,  32,  64, 128,  29,  58, 
	116, 232, 205, 135,  19,  38,  76, 152,  45,  90, 
	180, 117, 234, 201, 143,   3,   6,  12,  24,  48, 
	 96, 192, 157,  39,  78, 156,  37,  74, 148,  53, 
	106, 212, 181, 119, 238, 193, 159,  35,  70, 140, 
	  5,  10,  20,  40,  80, 160,  93, 186, 105, 210, 
	185, 111, 222, 161,  95, 190,  97, 194, 153,  47, 
	 94, 188, 101, 202, 137,  15,  30,  60, 120, 240, 
	253, 231, 211, 187, 107, 214, 177, 127, 254, 225, 
	223, 163,  91, 182, 113, 226, 217, 175,  67, 134, 
	 17,  34,  68, 136,  13,  26,  52, 104, 208, 189, 
	103, 206, 129,  31,  62, 124, 248, 237, 199, 147, 
	 59, 118, 236, 197, 151,  51, 102, 204, 133,  23, 
	 46,  92, 184, 109, 218, 169,  79, 158,  33,  66, 
	132,  21,  42,  84, 168,  77, 154,  41,  82, 164, 
	 85, 170,  73, 146,  57, 114, 228, 213, 183, 115, 
	230, 209, 191,  99, 198, 145,  63, 126, 252, 229, 
	215, 179, 123, 246, 241, 255, 227, 219, 171,  75, 
	150,  49,  98, 196, 149,  55, 110, 220, 165,  87, 
	174,  65, 130,  25,  50, 100, 200, 141,   7,  14, 
	 28,  56, 112, 224, 221, 167,  83, 166,  81, 162, 
	 89, 178, 121, 242, 249, 239, 195, 155,  43,  86, 
	172,  69, 138,   9,  18,  36,  72, 144,  61, 122, 
	244, 245, 247, 243, 251, 235, 203, 139,  11,  22, 
	 44,  88, 176, 125, 250, 233, 207, 131,  27,  54, 
	108, 216, 173,  71, 142,
	  1,   2,   4,   8,  16,  32,  64, 128,  29,  58, 
	116, 232, 205, 135,  19,  38,  76, 152,  45,  90, 
	180, 117, 234, 201, 143,   3,   6,  12,  24,  48, 
	 96, 192, 157,  39,  78, 156,  37,  74, 148,  53, 
	106, 212, 181, 119, 238, 193, 159,  35,  70, 140, 
	  5,  10,  20,  40,  80, 160,  93, 186, 105, 210, 
	185, 111, 222, 161,  95, 190,  97, 194, 153,  47, 
	 94, 188, 101, 202, 137,  15,  30,  60, 120, 240, 
	253, 231, 211, 187, 107, 214, 177, 127, 254, 225, 
	223, 163,  91, 182, 113, 226, 217, 175,  67, 134, 
	 17,  34,  68, 136,  13,  26,  52, 104, 208, 189, 
	103, 206, 129,  31,  62, 124, 248, 237, 199, 147, 
	 59, 118, 236, 197, 151,  51, 102, 204, 133,  23, 
	 46,  92, 184, 109, 218, 169,  79, 158,  33,  66, 
	132,  21,  42,  84, 168,  77, 154,  41,  82, 164, 
	 85, 170,  73, 146,  57, 114, 228, 213, 183, 115, 
	230, 209, 191,  99, 198, 145,  63, 126, 252, 229, 
	215, 179, 123, 246, 241, 255, 227, 219, 171,  75, 
	150,  49,  98, 196, 149,  55, 110, 220, 165,  87, 
	174,  65, 130,  25,  50, 100, 200, 141,   7,  14, 
	 28,  56, 112, 224, 221, 167,  83, 166,  81, 162, 
	 89, 178, 121, 242, 249, 239, 195, 155,  43,  86, 
	172,  69, 138,   9,  18,  36,  72, 144,  61, 122, 
	244, 245, 247, 243, 251, 235, 203, 139,  11,  22, 
	 44,  88, 176, 125, 250, 233, 207, 131,  27,  54, 
	108, 216, 173,  71,
};

static unsigned char gflog[256] = {
	   0,   0,   1,  25,   2,  50,  26, 198,   3, 223,
	  51, 238,  27, 104, 199,  75,   4, 100, 224,  14,
	  52, 141, 239, 129,  28, 193, 105, 248, 200,   8,
	  76, 113,   5, 138, 101,  47, 225,  36,  15,  33,
	  53, 147, 142, 218, 240,  18, 130,  69,  29, 181,
	 194, 125, 106,  39, 249, 185, 201, 154,   9, 120,
	  77, 228, 114, 166,   6, 191, 139,  98, 102, 221,
	  48, 253, 226, 152,  37, 179,  16, 145,  34, 136,
	  54, 208, 148, 206, 143, 150, 219, 189, 241, 210,
	  19,  92, 131,  56,  70,  64,  30,  66, 182, 163,
	 195,  72, 126, 110, 107,  58,  40,  84, 250, 133,
	 186,  61, 202,  94, 155, 159,  10,  21, 121,  43,
	  78, 212, 229, 172, 115, 243, 167,  87,   7, 112,
	 192, 247, 140, 128,  99,  13, 103,  74, 222, 237,
	  49, 197, 254,  24, 227, 165, 153, 119,  38, 184,
	 180, 124,  17,  68, 146, 217,  35,  32, 137,  46,
	  55,  63, 209,  91, 149, 188, 207, 205, 144, 135,
	 151, 178, 220, 252, 190,  97, 242,  86, 211, 171,
	  20,  42,  93, 158, 132,  60,  57,  83,  71, 109,
	  65, 162,  31,  45,  67, 216, 183, 123, 164, 118,
	 196,  23,  73, 236, 127,  12, 111, 246, 108, 161,
	  59,  82,  41, 157,  85, 170, 251,  96, 134, 177,
	 187, 204,  62,  90, 203,  89,  95, 176, 156, 169,
	 160,  81,  11, 245,  22, 235, 122, 117,  44, 215,
	  79, 174, 213, 233, 230, 231, 173, 232, 116, 214,
	 244, 234, 168,  80,  88, 175
};


#define Libburn_use_h_matriceS 1

#ifdef Libburn_use_h_matriceS

/* On my AMD 2x64 bit 3000 MHz processor h[i] costs about 7 % more time
   than using gfpow[25-i] resp. gfpow[44-1]. I blame this on the more
   condensed data representation which slightly increases the rate of cache
   hits.
   Nevertheless this effect is very likely depending on the exact cache
   size and architecture. In general, using h[] saves more than 8000
   subtractions per sector.
*/

/* Parity matrices H as prescribed by ECMA-130 Annex A.
   Actually just reverted order start pieces of gfpow[].
*/
static unsigned char h26[26] = {
           3, 143, 201, 234, 117, 180,  90,  45, 152,  76,
          38,  19, 135, 205, 232, 116,  58,  29, 128,  64,
          32,  16,   8,   4,   2,   1,
};

static unsigned char h45[45] = {
         238, 119, 181, 212, 106,  53, 148,  74,  37, 156,
          78,  39, 157, 192,  96,  48,  24,  12,   6,   3,
         143, 201, 234, 117, 180,  90,  45, 152,  76,  38,
          19, 135, 205, 232, 116,  58,  29, 128,  64,  32,
          16,   8,   4,   2,   1,
};

#endif /* Libburn_use_h_matriceS */


/* Pseudo-random bytes which of course are exactly the same as with the
   previously used code.
   Generated by function print_ecma_130_scrambler().
*/
static unsigned char ecma_130_annex_b[2340] = {
	  1, 128,   0,  96,   0,  40,   0,  30, 128,   8, 
	 96,   6, 168,   2, 254, 129, 128,  96,  96,  40, 
	 40,  30, 158, 136, 104, 102, 174, 170, 252, 127, 
	  1, 224,   0,  72,   0,  54, 128,  22, 224,  14, 
	200,   4,  86, 131, 126, 225, 224,  72,  72,  54, 
	182, 150, 246, 238, 198, 204,  82, 213, 253, 159, 
	  1, 168,   0, 126, 128,  32,  96,  24,  40,  10, 
	158, 135,  40,  98, 158, 169, 168, 126, 254, 160, 
	 64, 120,  48,  34, 148,  25, 175,  74, 252,  55, 
	  1, 214, 128,  94, 224,  56,  72,  18, 182, 141, 
	182, 229, 182, 203,  54, 215,  86, 222, 190, 216, 
	112,  90, 164,  59,  59,  83,  83, 125, 253, 225, 
	129, 136,  96, 102, 168,  42, 254, 159,   0, 104, 
	  0,  46, 128,  28,  96,   9, 232,   6, 206, 130, 
	212,  97, 159, 104, 104,  46, 174, 156, 124, 105, 
	225, 238, 200,  76,  86, 181, 254, 247,   0,  70, 
	128,  50, 224,  21, 136,  15,  38, 132,  26, 227, 
	 75,   9, 247,  70, 198, 178, 210, 245, 157, 135, 
	 41, 162, 158, 249, 168,  66, 254, 177, 128, 116, 
	 96,  39, 104,  26, 174, 139,  60, 103,  81, 234, 
	188,  79,  49, 244,  20,  71,  79, 114, 180,  37, 
	183,  91,  54, 187,  86, 243, 126, 197, 224,  83, 
	  8,  61, 198, 145, 146, 236, 109, 141, 237, 165, 
	141, 187,  37, 179,  91,  53, 251,  87,   3, 126, 
	129, 224,  96,  72,  40,  54, 158, 150, 232, 110, 
	206, 172,  84, 125, 255,  97, 128,  40,  96,  30, 
	168,   8, 126, 134, 160,  98, 248,  41, 130, 158, 
	225, 168,  72, 126, 182, 160, 118, 248,  38, 194, 
	154, 209, 171,  28, 127,  73, 224,  54, 200,  22, 
	214, 142, 222, 228,  88,  75, 122, 183,  99,  54, 
	169, 214, 254, 222, 192,  88,  80,  58, 188,  19, 
	 49, 205, 212,  85, 159, 127,  40,  32,  30, 152, 
	  8, 106, 134, 175,  34, 252,  25, 129, 202, 224, 
	 87,   8,  62, 134, 144,  98, 236,  41, 141, 222, 
	229, 152,  75,  42, 183,  95,  54, 184,  22, 242, 
	142, 197, 164,  83,  59, 125, 211,  97, 157, 232, 
	105, 142, 174, 228, 124,  75,  97, 247, 104,  70, 
	174, 178, 252, 117, 129, 231,  32,  74, 152,  55, 
	 42, 150, 159,  46, 232,  28,  78, 137, 244, 102, 
	199, 106, 210, 175,  29, 188,   9, 177, 198, 244, 
	 82, 199, 125, 146, 161, 173, 184, 125, 178, 161, 
	181, 184, 119,  50, 166, 149, 186, 239,  51,  12, 
	 21, 197, 207,  19,  20,  13, 207,  69, 148,  51, 
	 47,  85, 220,  63,  25, 208,  10, 220,   7,  25, 
	194, 138, 209, 167,  28, 122, 137, 227,  38, 201, 
	218, 214, 219,  30, 219,  72,  91, 118, 187, 102, 
	243, 106, 197, 239,  19,  12,  13, 197, 197, 147, 
	 19,  45, 205, 221, 149, 153, 175,  42, 252,  31, 
	  1, 200,   0,  86, 128,  62, 224,  16,  72,  12, 
	 54, 133, 214, 227,  30, 201, 200,  86, 214, 190, 
	222, 240,  88,  68,  58, 179,  83,  53, 253, 215, 
	  1, 158, 128, 104,  96,  46, 168,  28, 126, 137, 
	224, 102, 200,  42, 214, 159,  30, 232,   8,  78, 
	134, 180,  98, 247, 105, 134, 174, 226, 252,  73, 
	129, 246, 224,  70, 200,  50, 214, 149, 158, 239, 
	 40,  76,  30, 181, 200, 119,  22, 166, 142, 250, 
	228,  67,  11, 113, 199, 100,  82, 171, 125, 191, 
	 97, 176,  40, 116,  30, 167,  72, 122, 182, 163, 
	 54, 249, 214, 194, 222, 209, 152,  92, 106, 185, 
	239,  50, 204,  21, 149, 207,  47,  20,  28,  15, 
	 73, 196,  54, 211,  86, 221, 254, 217, 128,  90, 
	224,  59,   8,  19,  70, 141, 242, 229, 133, 139, 
	 35,  39,  89, 218, 186, 219,  51,  27,  85, 203, 
	127,  23,  96,  14, 168,   4, 126, 131,  96,  97, 
	232,  40,  78, 158, 180, 104, 119, 110, 166, 172, 
	122, 253, 227,   1, 137, 192, 102, 208,  42, 220, 
	 31,  25, 200,  10, 214, 135,  30, 226, 136,  73, 
	166, 182, 250, 246, 195,   6, 209, 194, 220,  81, 
	153, 252, 106, 193, 239,  16,  76,  12,  53, 197, 
	215,  19,  30, 141, 200, 101, 150, 171,  46, 255, 
	 92,  64,  57, 240,  18, 196,  13, 147,  69, 173, 
	243,  61, 133, 209, 163,  28, 121, 201, 226, 214, 
	201, 158, 214, 232,  94, 206, 184,  84, 114, 191, 
	101, 176,  43,  52,  31,  87,  72,  62, 182, 144, 
	118, 236,  38, 205, 218, 213, 155,  31,  43,  72, 
	 31, 118, 136,  38, 230, 154, 202, 235,  23,  15, 
	 78, 132,  52,  99,  87, 105, 254, 174, 192, 124, 
	 80,  33, 252,  24,  65, 202, 176,  87,  52,  62, 
	151,  80, 110, 188,  44, 113, 221, 228,  89, 139, 
	122, 231,  99,  10, 169, 199,  62, 210, 144,  93, 
	172,  57, 189, 210, 241, 157, 132, 105, 163, 110, 
	249, 236,  66, 205, 241, 149, 132, 111,  35, 108, 
	 25, 237, 202, 205, 151,  21, 174, 143,  60, 100, 
	 17, 235,  76,  79, 117, 244,  39,   7,  90, 130, 
	187,  33, 179,  88, 117, 250, 167,   3,  58, 129, 
	211,  32,  93, 216,  57, 154, 146, 235,  45, 143, 
	 93, 164,  57, 187,  82, 243, 125, 133, 225, 163, 
	  8, 121, 198, 162, 210, 249, 157, 130, 233, 161, 
	142, 248, 100,  66, 171, 113, 191, 100, 112,  43, 
	100,  31, 107,  72,  47, 118, 156,  38, 233, 218, 
	206, 219,  20,  91,  79, 123, 116,  35, 103,  89, 
	234, 186, 207,  51,  20,  21, 207,  79,  20,  52, 
	 15,  87,  68,  62, 179,  80, 117, 252,  39,   1, 
	218, 128,  91,  32,  59,  88,  19, 122, 141, 227, 
	 37, 137, 219,  38, 219,  90, 219, 123,  27,  99, 
	 75, 105, 247, 110, 198, 172,  82, 253, 253, 129, 
	129, 160,  96, 120,  40,  34, 158, 153, 168, 106, 
	254, 175,   0, 124,   0,  33, 192,  24,  80,  10, 
	188,   7,  49, 194, 148,  81, 175, 124, 124,  33, 
	225, 216,  72,  90, 182, 187,  54, 243,  86, 197, 
	254, 211,   0,  93, 192,  57, 144,  18, 236,  13, 
	141, 197, 165, 147,  59,  45, 211,  93, 157, 249, 
	169, 130, 254, 225, 128,  72,  96,  54, 168,  22, 
	254, 142, 192, 100,  80,  43, 124,  31,  97, 200, 
	 40,  86, 158, 190, 232, 112,  78, 164,  52, 123, 
	 87,  99, 126, 169, 224, 126, 200,  32,  86, 152, 
	 62, 234, 144,  79,  44,  52,  29, 215,  73, 158, 
	182, 232, 118, 206, 166, 212, 122, 223,  99,  24, 
	 41, 202, 158, 215,  40,  94, 158, 184, 104, 114, 
	174, 165, 188, 123,  49, 227,  84,  73, 255, 118, 
	192,  38, 208,  26, 220,  11,  25, 199,  74, 210, 
	183,  29, 182, 137, 182, 230, 246, 202, 198, 215, 
	 18, 222, 141, 152, 101, 170, 171,  63,  63,  80, 
	 16,  60,  12,  17, 197, 204,  83,  21, 253, 207, 
	  1, 148,   0, 111,  64,  44,  48,  29, 212,   9, 
	159,  70, 232,  50, 206, 149, 148, 111,  47, 108, 
	 28,  45, 201, 221, 150, 217, 174, 218, 252,  91, 
	  1, 251,  64,  67, 112,  49, 228,  20,  75,  79, 
	119, 116,  38, 167,  90, 250, 187,   3,  51,  65, 
	213, 240,  95,   4,  56,   3,  82, 129, 253, 160, 
	 65, 184,  48, 114, 148,  37, 175,  91,  60,  59, 
	 81, 211, 124,  93, 225, 249, 136,  66, 230, 177, 
	138, 244, 103,   7, 106, 130, 175,  33, 188,  24, 
	113, 202, 164,  87,  59, 126, 147,  96, 109, 232, 
	 45, 142, 157, 164, 105, 187, 110, 243, 108,  69, 
	237, 243,  13, 133, 197, 163,  19,  57, 205, 210, 
	213, 157, 159,  41, 168,  30, 254, 136,  64, 102, 
	176,  42, 244,  31,   7,  72,   2, 182, 129, 182, 
	224, 118, 200,  38, 214, 154, 222, 235,  24,  79, 
	 74, 180,  55,  55,  86, 150, 190, 238, 240,  76, 
	 68,  53, 243,  87,   5, 254, 131,   0,  97, 192, 
	 40,  80,  30, 188,   8, 113, 198, 164,  82, 251, 
	125, 131,  97, 161, 232, 120,  78, 162, 180, 121, 
	183,  98, 246, 169, 134, 254, 226, 192,  73, 144, 
	 54, 236,  22, 205, 206, 213, 148,  95,  47, 120, 
	 28,  34, 137, 217, 166, 218, 250, 219,   3,  27, 
	 65, 203, 112,  87, 100,  62, 171,  80, 127, 124, 
	 32,  33, 216,  24,  90, 138, 187,  39,  51,  90, 
	149, 251,  47,   3,  92,   1, 249, 192,  66, 208, 
	 49, 156,  20, 105, 207, 110, 212,  44,  95,  93, 
	248,  57, 130, 146, 225, 173, 136, 125, 166, 161, 
	186, 248, 115,   2, 165, 193, 187,  16, 115,  76, 
	 37, 245, 219,   7,  27,  66, 139, 113, 167, 100, 
	122, 171,  99,  63, 105, 208,  46, 220,  28,  89, 
	201, 250, 214, 195,  30, 209, 200,  92,  86, 185, 
	254, 242, 192,  69, 144,  51,  44,  21, 221, 207, 
	 25, 148,  10, 239,  71,  12,  50, 133, 213, 163, 
	 31,  57, 200,  18, 214, 141, 158, 229, 168,  75, 
	 62, 183,  80, 118, 188,  38, 241, 218, 196,  91, 
	 19, 123,  77, 227, 117, 137, 231,  38, 202, 154, 
	215,  43,  30, 159,  72, 104,  54, 174, 150, 252, 
	110, 193, 236,  80,  77, 252,  53, 129, 215,  32, 
	 94, 152,  56, 106, 146, 175,  45, 188,  29, 177, 
	201, 180,  86, 247, 126, 198, 160,  82, 248,  61, 
	130, 145, 161, 172, 120, 125, 226, 161, 137, 184, 
	102, 242, 170, 197, 191,  19,  48,  13, 212,   5, 
	159,  67,  40,  49, 222, 148,  88, 111, 122, 172, 
	 35,  61, 217, 209, 154, 220, 107,  25, 239,  74, 
	204,  55,  21, 214, 143,  30, 228,   8,  75,  70, 
	183, 114, 246, 165, 134, 251,  34, 195,  89, 145, 
	250, 236,  67,  13, 241, 197, 132,  83,  35, 125, 
	217, 225, 154, 200, 107,  22, 175,  78, 252,  52, 
	 65, 215, 112,  94, 164,  56, 123,  82, 163, 125, 
	185, 225, 178, 200, 117, 150, 167,  46, 250, 156, 
	 67,  41, 241, 222, 196,  88,  83, 122, 189, 227, 
	 49, 137, 212, 102, 223, 106, 216,  47,  26, 156, 
	 11,  41, 199,  94, 210, 184,  93, 178, 185, 181, 
	178, 247,  53, 134, 151,  34, 238, 153, 140, 106, 
	229, 239,  11,  12,   7,  69, 194, 179,  17, 181, 
	204, 119,  21, 230, 143,  10, 228,   7,  11,  66, 
	135, 113, 162, 164, 121, 187,  98, 243, 105, 133, 
	238, 227,  12,  73, 197, 246, 211,   6, 221, 194, 
	217, 145, 154, 236, 107,  13, 239,  69, 140,  51, 
	 37, 213, 219,  31,  27,  72,  11, 118, 135, 102, 
	226, 170, 201, 191,  22, 240,  14, 196,   4,  83, 
	 67, 125, 241, 225, 132,  72,  99, 118, 169, 230, 
	254, 202, 192,  87,  16,  62, 140,  16, 101, 204, 
	 43,  21, 223,  79,  24,  52,  10, 151,  71,  46, 
	178, 156, 117, 169, 231,  62, 202, 144,  87,  44, 
	 62, 157, 208, 105, 156,  46, 233, 220,  78, 217, 
	244,  90, 199, 123,  18, 163,  77, 185, 245, 178, 
	199,  53, 146, 151,  45, 174, 157, 188, 105, 177, 
	238, 244,  76,  71, 117, 242, 167,   5, 186, 131, 
	 51,  33, 213, 216,  95,  26, 184,  11,  50, 135, 
	 85, 162, 191,  57, 176,  18, 244,  13, 135,  69, 
	162, 179,  57, 181, 210, 247,  29, 134, 137, 162, 
	230, 249, 138, 194, 231,  17, 138, 140, 103,  37, 
	234, 155,  15,  43,  68,  31, 115,  72,  37, 246, 
	155,   6, 235,  66, 207, 113, 148,  36, 111,  91, 
	108,  59, 109, 211, 109, 157, 237, 169, 141, 190, 
	229, 176,  75,  52,  55,  87,  86, 190, 190, 240, 
	112,  68,  36,  51,  91,  85, 251, 127,   3,  96, 
	  1, 232,   0,  78, 128,  52,  96,  23, 104,  14, 
	174, 132, 124,  99,  97, 233, 232,  78, 206, 180, 
	 84, 119, 127, 102, 160,  42, 248,  31,   2, 136, 
	  1, 166, 128, 122, 224,  35,   8,  25, 198, 138, 
	210, 231,  29, 138, 137, 167,  38, 250, 154, 195, 
	 43,  17, 223,  76,  88,  53, 250, 151,   3,  46, 
	129, 220,  96,  89, 232,  58, 206, 147,  20, 109, 
	207, 109, 148,  45, 175,  93, 188,  57, 177, 210, 
	244,  93, 135, 121, 162, 162, 249, 185, 130, 242, 
	225, 133, 136,  99,  38, 169, 218, 254, 219,   0, 
	 91,  64,  59, 112,  19, 100,  13, 235,  69, 143, 
	115,  36,  37, 219,  91,  27, 123,  75,  99, 119, 
	105, 230, 174, 202, 252,  87,   1, 254, 128,  64, 
	 96,  48,  40,  20,  30, 143,  72, 100,  54, 171, 
	 86, 255, 126, 192,  32,  80,  24,  60,  10, 145, 
	199,  44,  82, 157, 253, 169, 129, 190, 224, 112, 
	 72,  36,  54, 155,  86, 235, 126, 207,  96,  84, 
	 40,  63,  94, 144,  56, 108,  18, 173, 205, 189, 
	149, 177, 175,  52, 124,  23,  97, 206, 168,  84, 
	126, 191,  96, 112,  40,  36,  30, 155,  72, 107, 
	118, 175, 102, 252,  42, 193, 223,  16,  88,  12, 
	 58, 133, 211,  35,  29, 217, 201, 154, 214, 235, 
	 30, 207,  72,  84,  54, 191,  86, 240,  62, 196, 
	 16,  83,  76,  61, 245, 209, 135,  28,  98, 137, 
	233, 166, 206, 250, 212,  67,  31, 113, 200,  36, 
	 86, 155, 126, 235,  96,  79, 104,  52,  46, 151, 
	 92, 110, 185, 236, 114, 205, 229, 149, 139,  47, 
	 39,  92,  26, 185, 203,  50, 215,  85, 158, 191, 
	 40, 112,  30, 164,   8, 123,  70, 163, 114, 249, 
	229, 130, 203,  33, 151,  88, 110, 186, 172, 115, 
	 61, 229, 209, 139,  28, 103,  73, 234, 182, 207, 
	 54, 212,  22, 223,  78, 216,  52,  90, 151, 123, 
	 46, 163,  92, 121, 249, 226, 194, 201, 145, 150, 
	236, 110, 205, 236,  85, 141, 255,  37, 128,  27, 
	 32,  11,  88,   7, 122, 130, 163,  33, 185, 216, 
	114, 218, 165, 155,  59,  43,  83,  95, 125, 248, 
	 33, 130, 152,  97, 170, 168, 127,  62, 160,  16, 
	120,  12,  34, 133, 217, 163,  26, 249, 203,   2, 
	215,  65, 158, 176, 104, 116,  46, 167,  92, 122, 
	185, 227,  50, 201, 213, 150, 223,  46, 216,  28, 
	 90, 137, 251,  38, 195,  90, 209, 251,  28,  67, 
	 73, 241, 246, 196,  70, 211, 114, 221, 229, 153
};


/* ------------------------------------------------------------------------- */


/* This is the new implementation of P- and Q-parity generation.
   It needs about the same computing time as the old implementation (both
   with gcc -O2 on AMD 64 bit). Measurements indicate that about 280 MIPS
   are needed for 48x CD speed (7.1 MB/s).
*/

static unsigned char burn_rspc_mult(unsigned char a, unsigned char b)
{
	if (a == 0 || b == 0)
		return 0;
	/* Optimization of (a == 0 || b == 0) by D. Hugh Redelmeier
	if((((int)a - 1) | ((int)b - 1)) < 0)
		return 0;
	*/

        return gfpow[gflog[a] + gflog[b]];
	/* % 255 not necessary because gfpow is unrolled up to index 510 */
}


/* Divide by polynomial 0x03. Derived from burn_rspc_div() and using the
   unrolled size of the gfpow[] array.
*/
static unsigned char burn_rspc_div_3(unsigned char a)
{
	if (a == 0)
		return 0;
	return gfpow[230 + gflog[a]];
}


static void burn_rspc_p0p1(unsigned char *sector, int col, 
                          unsigned char *p0_lsb, unsigned char *p0_msb,
                          unsigned char *p1_lsb, unsigned char *p1_msb)
{
	unsigned char *start, b;
	unsigned int i, sum_v_lsb = 0, sum_v_msb = 0;
	unsigned int hxv_lsb = 0, hxv_msb = 0;

	start = sector + 12 + 2 * col;
	for(i = 0; i < 24; i++) {
		b = *start;
		sum_v_lsb ^= b;

#ifdef Libburn_use_h_matriceS
		hxv_lsb ^= burn_rspc_mult(b, h26[i]);
#else
		hxv_lsb ^= burn_rspc_mult(b, gfpow[25 - i]);
#endif

		b = *(start + 1);
		sum_v_msb ^= b;

#ifdef Libburn_use_h_matriceS
		hxv_msb ^= burn_rspc_mult(b, h26[i]);
#else
		hxv_msb ^= burn_rspc_mult(b, gfpow[25 - i]);
#endif

		start += 86;
	}

				/* 3 = gfpow[1] ^ gfpow[0] , 2 = gfpow[1] */
	*p0_lsb = burn_rspc_div_3(burn_rspc_mult(2, sum_v_lsb) ^ hxv_lsb);
	*p0_msb = burn_rspc_div_3(burn_rspc_mult(2, sum_v_msb) ^ hxv_msb);
	*p1_lsb = sum_v_lsb ^ *p0_lsb;
	*p1_msb = sum_v_msb ^ *p0_msb;
}


void burn_rspc_parity_p(unsigned char *sector)
{
	int i;
	unsigned char p0_lsb, p0_msb, p1_lsb, p1_msb;

	/* Loop over P columns */
	for(i = 0; i < 43; i++) {
		burn_rspc_p0p1(sector, i, &p0_lsb, &p0_msb, &p1_lsb, &p1_msb);
		sector[2162 + 2 * i]     =  p0_lsb;
		sector[2162 + 2 * i + 1] =  p0_msb;
		sector[2076 + 2 * i]     =  p1_lsb;
		sector[2076 + 2 * i + 1] =  p1_msb;

#ifdef Libburn_with_lec_generatoR
		if(verbous) {
			printf("p %2d :  %2.2X  %2.2X  ", i,
				(unsigned int) p0_lsb, (unsigned int) p0_msb);
			printf("%2.2X  %2.2X  ",
				(unsigned int) p1_lsb, (unsigned int) p1_msb);
			printf("-> %d,%d\n", 2162 + 2 * i, 2076 + 2 * i);
		}
#endif /* Libburn_with_lec_generatoR */

	}
}


static void burn_rspc_q0q1(unsigned char *sector, int diag,
                          unsigned char *q0_lsb, unsigned char *q0_msb,
                          unsigned char *q1_lsb, unsigned char *q1_msb)
{
	unsigned char *start, b;
	unsigned int i, idx, sum_v_lsb = 0, sum_v_msb = 0;
	unsigned int hxv_lsb = 0, hxv_msb = 0;

	start = sector + 12;
	idx = 2 * 43 * diag;
	for(i = 0; i < 43; i++) {
		if (idx >= 2236)
			idx -= 2236;
		b = start[idx];
		sum_v_lsb ^= b;

#ifdef Libburn_use_h_matriceS
		hxv_lsb ^= burn_rspc_mult(b, h45[i]);
#else
		hxv_lsb ^= burn_rspc_mult(b, gfpow[44 - i]);
#endif

		b = start[idx + 1];
		sum_v_msb ^= b;

#ifdef Libburn_use_h_matriceS
		hxv_msb ^= burn_rspc_mult(b, h45[i]);
#else
		hxv_msb ^= burn_rspc_mult(b, gfpow[44 - i]);
#endif

		idx += 88;
	}
				/* 3 = gfpow[1] ^ gfpow[0] , 2 = gfpow[1] */
	*q0_lsb = burn_rspc_div_3(burn_rspc_mult(2, sum_v_lsb) ^ hxv_lsb);
	*q0_msb = burn_rspc_div_3(burn_rspc_mult(2, sum_v_msb) ^ hxv_msb);
	*q1_lsb = sum_v_lsb ^ *q0_lsb;
	*q1_msb = sum_v_msb ^ *q0_msb;
}


void burn_rspc_parity_q(unsigned char *sector)
{
	int i;
	unsigned char q0_lsb, q0_msb, q1_lsb, q1_msb;

	/* Loop over Q diagonals */
	for(i = 0; i < 26; i++) {
		burn_rspc_q0q1(sector, i, &q0_lsb, &q0_msb, &q1_lsb, &q1_msb);
		sector[2300 + 2 * i]     =  q0_lsb;
		sector[2300 + 2 * i + 1] =  q0_msb;
		sector[2248 + 2 * i]     =  q1_lsb;
		sector[2248 + 2 * i + 1] =  q1_msb;

#ifdef Libburn_with_lec_generatoR
		if(verbous) {
			printf("q %2d :  %2.2X  %2.2X  ", i,
				(unsigned int) q0_lsb, (unsigned int) q0_msb);
			printf("%2.2X  %2.2X  ",
				(unsigned int) q1_lsb, (unsigned int) q1_msb);
			printf("-> %d,%d\n", 2300 + 2 * i, 2248 + 2 * i);
		}
#endif /* Libburn_with_lec_generatoR */

	}
}

/* ------------------------------------------------------------------------- */


/* The new implementation of the ECMA-130 Annex B scrambler.
   It is totally unoptimized. One should make use of larger word operations. 
   Measurements indicate that about 50 MIPS are needed for 48x CD speed.
*/

void burn_ecma130_scramble(unsigned char *sector) 
{
        int i;
	unsigned char *s;

	s = sector + 12;
        for (i = 0; i < 2340; i++)
                s[i] ^= ecma_130_annex_b[i];
}


/* ------------------------------------------------------------------------- */


/* The following code is not needed for libburn but rather documents the
   origin of the tables above. In libburn it will not be compiled.
*/


#ifdef Libburn_with_lec_generatoR


/* This function produced the content of gflog[] and gfpow[]
*/
static int burn_rspc_setup_tables(void)
{
	unsigned int b, l;

	memset(gflog, 0, sizeof(gflog));
	memset(gfpow, 0, sizeof(gfpow));
	b = 1;
	for (l = 0; l < 255; l++) {
		gfpow[l] = (unsigned char) b;
		gflog[b] = (unsigned char) l;
		b = b << 1;
		if (b & 256)
			b = b ^ 0x11d;
	}
	return 0;
}


/* This function printed the content of gflog[] and gfpow[] as C code
   and compared the content with the tables of the old implementation.
   h26[] and h45[] are reverted order copies of gfpow[]
*/
static int burn_rspc_print_tables(void)
{
 int i;

 printf("static unsigned char gfpow[255] = {");
 printf("\n\t"); 
 for(i= 0; i < 255; i++) {
   printf("%3u, ", gfpow[i]);

#ifdef Libburn_with_old_lec_comparisoN
   if(gfpow[i] != gf8_ilog[i])
     fprintf(stderr, "*** ILOG %d : %d != %d ***\n", i, gfpow[i], gf8_ilog[i]);
#endif

   if((i % 10) == 9)
     printf("\n\t"); 
 }
 printf("\n};\n\n");

 printf("static unsigned char gflog[256] = {");
 printf("\n\t"); 
 for(i= 0; i < 256; i++) {
   printf(" %3u,", gflog[i]);

#ifdef Libburn_with_old_lec_comparisoN
   if(gflog[i] != gf8_log[i])
     fprintf(stderr, "*** LOG %d : %d != %d ***\n", i, gflog[i], gf8_log[i]);
#endif

   if((i % 10) == 9)
     printf("\n\t"); 
 }
 printf("\n};\n\n");

 printf("static unsigned char h26[26] = {");
 printf("\n\t");
 for(i= 0; i < 26; i++) {
   printf(" %3u,", gfpow[25 - i]);
   if((i % 10) == 9)
     printf("\n\t"); 
 }
 printf("\n};\n\n");

 printf("static unsigned char h45[45] = {");
 printf("\n\t");
 for(i= 0; i < 45; i++) {
   printf(" %3u,",gfpow[44 - i]);
   if((i % 10) == 9)
     printf("\n\t"); 
 }
 printf("\n};\n\n");

 return 0;
}


/* This code was used to generate the content of array ecma_130_annex_b[].
*/
static unsigned short ecma_130_fsr = 1;

static int next_bit(void)
{
	int ret;

	ret = ecma_130_fsr & 1;
	ecma_130_fsr = (ecma_130_fsr >> 1) & 0x3fff;
	if (ret ^ (ecma_130_fsr & 1))
		ecma_130_fsr |= 0x4000;
	return ret;
}


static int print_ecma_130_scrambler(void)
{
	int i, j, b;

	ecma_130_fsr = 1;
	printf("static unsigned char ecma_130_annex_b[2340] = {");
	printf("\n\t");
	for (i = 0; i < 2340; i++) {
		b = 0;
		for (j = 0; j < 8; j++)
			b |= next_bit() << j;

		printf("%3u, ", b);
		if ((i % 10) == 9)
			printf("\n\t");
	}
	printf("\n};\n");
	return 1;
}


#ifdef Libburn_with_general_rspc_diV

/* This is a general polynomial division function.
   burn_rspc_div_3() has been derived from this by setting b to constant 3.
*/
static unsigned char burn_rspc_div(unsigned char a, unsigned char b)
{
	int d;

	if (a == 0)
		return 0;
	if (b == 0)
		return -1;
	d = gflog[a] - gflog[b];
	if (d < 0)
		d += 255;
	return gfpow[d];
}

#endif /* Libburn_with_general_rspc_diV */


#endif /* Libburn_with_lec_generatoR */

