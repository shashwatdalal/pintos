/* Demonstrate that the stack can grow.
   This must succeed. */

#include <string.h>
#include "tests/arc4.h"
#include "tests/cksum.h"
#include "tests/lib.h"
#include "tests/main.h"

void
test_main (void)
{
  // msg("> (1)\n");
  char stack_obj[4096];
  // msg("> (2)\n");
  struct arc4 arc4;
  // msg("> (3)\n");


  arc4_init (&arc4, "foobar", 6);
  // msg("> (4)\n");
  memset (stack_obj, 0, sizeof stack_obj);
  // msg("> (5)\n");
  arc4_crypt (&arc4, stack_obj, sizeof stack_obj);
  // msg("> (6)\n");
  msg ("cksum: %lu", cksum (stack_obj, sizeof stack_obj));
}
