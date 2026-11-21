#include <kunit/test.h>
#include "asn1_test.asn1.h"
#include "../upstream/asn1_decoder_kunit.c"
int main(void){
  int total=0,failed=0;
  for(struct kunit_case*c=asn1_decoder_test_cases; c->name; c++){
    struct kunit t={0}; c->fn(&t);
    total++;
    if(t.fails){failed++; printf("[FAIL] %s\n",c->name);} else printf("[ ok ] %s\n",c->name);
  }
  printf("\n%d/%d cases passed\n", total-failed, total);
  return failed?1:0;
}
