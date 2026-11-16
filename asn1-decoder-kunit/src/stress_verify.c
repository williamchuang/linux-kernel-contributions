#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <unistd.h>
#include "asn1_test.asn1.h"
static sigjmp_buf jb; static void on_fault(int s){(void)s;siglongjmp(jb,1);}
static const unsigned char *g_base; static size_t g_len; static int g_slice_bad;
static void ck(const void*v,size_t vl){const unsigned char*p=v; if(vl&&(p<g_base||p+vl>g_base+g_len))g_slice_bad=1;}
int asn1_test_note_int(void*c,size_t h,unsigned char t,const void*v,size_t vl){(void)c;(void)h;(void)t;ck(v,vl);return 0;}
int asn1_test_note_str(void*c,size_t h,unsigned char t,const void*v,size_t vl){(void)c;(void)h;(void)t;ck(v,vl);return 0;}
static unsigned char *region; static size_t pg;
static unsigned long long n=0,fault=0,slice=0,ok=0,rej=0;
static void run(const unsigned char*in,size_t len){
    if(len>pg) len=pg;
    unsigned char*data=region+pg-len; struct{unsigned a,b;}x={0,0};
    memcpy(data,in,len); g_base=data; g_len=len; g_slice_bad=0;
    int r; if(sigsetjmp(jb,1)){fault++;fprintf(stderr,"FAULT len=%zu:",len);for(size_t i=0;i<len;i++)fprintf(stderr," %02x",in[i]);fprintf(stderr,"\n");n++;return;}
    r=asn1_ber_decoder(&asn1_test_decoder,&x,data,len); n++;
    if(g_slice_bad){slice++;fprintf(stderr,"SLICE len=%zu:",len);for(size_t i=0;i<len;i++)fprintf(stderr," %02x",in[i]);fprintf(stderr,"\n");}
    else if(r==0)ok++; else rej++;
}
static uint64_t s0=0x9e3779b97f4a7c15ULL,s1=0xbf58476d1ce4e5b9ULL;
static uint64_t rnd(){uint64_t x=s0; const uint64_t y=s1; s0=y; x^=x<<23; s1=x^y^(x>>17)^(y>>26); return s1+y;}
int main(int argc,char**argv){
    pg=sysconf(_SC_PAGESIZE);
    region=mmap(NULL,pg*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    mprotect(region+pg,pg,PROT_NONE);
    struct sigaction sa={0};sa.sa_handler=on_fault;sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV,&sa,NULL);sigaction(SIGBUS,&sa,NULL);
    unsigned char buf[4096];
    unsigned long long N = argc>1?strtoull(argv[1],0,10):100000000ULL;
    /* (A) crafted adversarial corpus */
    /* deep definite nesting: 40 nested SEQUENCEs then a leaf -> cons stack overflow, must not crash */
    {int L=0; for(int i=0;i<40;i++){buf[L++]=0x30;buf[L++]=0x7f;} buf[L++]=0x05;buf[L++]=0x00; run(buf,L);}
    /* deep indefinite nesting: 40x (0x30 0x80) then EOCs */
    {int L=0; for(int i=0;i<40;i++){buf[L++]=0x30;buf[L++]=0x80;} for(int i=0;i<40;i++){buf[L++]=0x00;buf[L++]=0x00;} run(buf,L);}
    /* long-form length claiming huge size, little data */
    {unsigned char a[]={0x30,0x84,0xff,0xff,0xff,0xff,0x02,0x01,0x00}; run(a,sizeof(a));}
    {unsigned char a[]={0x30,0x82,0xff,0xff,0x02,0x01,0x00}; run(a,sizeof(a));}
    /* truncated long-form length */
    {unsigned char a[]={0x30,0x82,0xff}; run(a,sizeof(a));}
    /* indefinite on primitive (not permitted) */
    {unsigned char a[]={0x30,0x05,0x02,0x80,0x01,0x00,0x00}; run(a,sizeof(a));}
    /* (B) uniform random */
    for(unsigned long long i=0;i<N;i++){ size_t len=rnd()% 300; for(size_t j=0;j<len;j++) buf[j]=rnd()&0xff; run(buf,len);} 
    /* (C) grammar-biased random: structural alphabet */
    static const unsigned char A[]={0x30,0x30,0x31,0x02,0x04,0x05,0x06,0xa0,0x00,0x1f,0x80,0x81,0x82,0x84,0x00,0x01,0x02,0x7f,0xff};
    for(unsigned long long i=0;i<N;i++){ size_t len=rnd()%64; for(size_t j=0;j<len;j++) buf[j]=A[rnd()%sizeof(A)]; run(buf,len);}
    printf("STRESS: decoded=%llu ok=%llu rejected=%llu FAULTS=%llu BAD_SLICES=%llu\n",n,ok,rej,fault,slice);
    return (fault||slice)?2:0;
}
