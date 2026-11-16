#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include "asn1_test.asn1.h"
struct C{unsigned ic,sc;size_t sl;const unsigned char*base;size_t len;int bad;};
static void ck(struct C*c,const void*v,size_t vl){const unsigned char*p=v;if(vl&&(p<c->base||p+vl>c->base+c->len))c->bad=1;}
int asn1_test_note_int(void*x,size_t h,unsigned char t,const void*v,size_t vl){struct C*c=x;(void)h;(void)t;c->ic++;ck(c,v,vl);return 0;}
int asn1_test_note_str(void*x,size_t h,unsigned char t,const void*v,size_t vl){struct C*c=x;(void)h;(void)t;c->sc++;c->sl=vl;ck(c,v,vl);return 0;}
int main(int argc,char**argv){
  unsigned char b[512];int n=0;
  for(int i=1;i<argc;i++){unsigned v;sscanf(argv[i],"%x",&v);b[n++]=v;}
  struct C c={0};c.base=b;c.len=n;
  int r=asn1_ber_decoder(&asn1_test_decoder,&c,b,n);
  printf("len=%d ret=%d int_calls=%u str_calls=%u str_len=%zu bad_slice=%d\n",n,r,c.ic,c.sc,c.sl,c.bad);
  return 0;
}
