#include <stdint.h>

#define Q 14

#define FACTOR 1 << Q   /* scaling factor */

#define INT_TO_FP(n) (n << Q)

#define FP_TO_INT_FLOOR(x) (x >> Q)

#define FP_TO_INT_NEAREST(x) ((x >= 0) ? ((x + (FACTOR/2)) >> Q) : ((x - (FACTOR/2)) >> Q))

#define FP_INT_ADD(x, n) (x + (n << Q))

#define FP_INT_SUB(x, n) (x - (n << Q))

#define FP_MULT(x, y) ((((int64_t) x) * y) >> Q)

#define FP_DIV(x, y) ((((int64_t) x) * FACTOR) / y)

typedef int fp_t;
