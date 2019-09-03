#ifndef FIXED_POINT
#define FIXED_POINT

/* Fixed-point real arithmetic */
#define P 17        // Integer bits
#define Q 14        // Fraction bits
#define S 1         // Sign bit
#define F 1 << (Q) // Fraction

/* Here x and y are fixed-point (17.14) reals and n is a 32 bit integer */
#define TO_FP(n) ((n) * (F))
#define TO_INT_ZERO(x) ((x) / (F))
#define TO_INT_NEAREST(x) (((x) >= 0 ? ((x) + (F) / 2) / (F) : \
                             ((x) - (F) / 2) / (F)))
#define ADD(x, y) ((x) + (y))
#define SUB(x, y) ((x) - (y))
#define ADD_INT(x, n) ((x) + (n) * (F))
#define SUB_INT(x, n) ((x) - (n) * (F))
#define INT_SUB(n, x) ((n) * (F) - (x))
#define MULTIPLY(x, y) (((int64_t)(x)) * (y) / (F))
#define MULTIPLY_INT(x, n) ((x) * (n))
#define DIVIDE(x, y) (((int64_t)(x)) * (F) / (y))
#define DIVIDE_INT(x, n) ((x) / (n))

#endif
