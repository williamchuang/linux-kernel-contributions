#include <stdio.h>
#include <string.h>
#include "asn1_test.asn1.h"
struct ctx { unsigned n_int, n_str; };
int asn1_test_note_int(void *c,size_t h,unsigned char t,const void*v,size_t vl){((struct ctx*)c)->n_int++;return 0;}
int asn1_test_note_str(void *c,size_t h,unsigned char t,const void*v,size_t vl){((struct ctx*)c)->n_str++;return 0;}
int main(void){
    struct ctx x={0};
    const unsigned char der[]={0x30,0x07,0x02,0x01,0x2a,0x04,0x02,0x68,0x69};
    int r=asn1_ber_decoder(&asn1_test_decoder,&x,der,sizeof(der));
    printf("valid decode: ret=%d int=%u str=%u (expect 0,1,1)\n",r,x.n_int,x.n_str);
    const unsigned char bad[]={0x30,0x07,0x02,0x01,0x2a,0x04,0x02,0x68};
    r=asn1_ber_decoder(&asn1_test_decoder,&x,bad,sizeof(bad));
    printf("truncated:    ret=%d (expect -74 EBADMSG)\n",r);
    return 0;
}
