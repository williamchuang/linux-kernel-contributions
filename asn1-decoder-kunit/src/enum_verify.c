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

static sigjmp_buf jb;
static void on_fault(int s){ (void)s; siglongjmp(jb, 1); }

/* bounds of the current input, for the slice-validity invariant (I2) */
static const unsigned char *g_base; static size_t g_len; static int g_slice_bad;
static void check_slice(const void *v, size_t vl){
    const unsigned char *p = v;
    if (vl && (p < g_base || p + vl > g_base + g_len)) g_slice_bad = 1;
}
int asn1_test_note_int(void*c,size_t h,unsigned char t,const void*v,size_t vl){(void)c;(void)h;(void)t;check_slice(v,vl);return 0;}
int asn1_test_note_str(void*c,size_t h,unsigned char t,const void*v,size_t vl){(void)c;(void)h;(void)t;check_slice(v,vl);return 0;}

static unsigned char *region; static size_t pg;
/* place input flush against a PROT_NONE guard page: data+len == guard start */
static int decode_guarded(const unsigned char *in, size_t len, int *faulted){
    unsigned char *data = region + pg - len;
    struct { unsigned a,b; } x = {0,0};
    memcpy(data, in, len);
    g_base = data; g_len = len; g_slice_bad = 0;
    if (sigsetjmp(jb,1)) { *faulted = 1; return -999; }
    int r = asn1_ber_decoder(&asn1_test_decoder, &x, data, len);
    *faulted = 0;
    return r;
}

static unsigned long long n_dec=0, n_ok=0, n_rej=0, n_fault=0, n_slice=0;
static void run(const unsigned char *in, size_t len){
    int f; int r = decode_guarded(in, len, &f);
    n_dec++;
    if (f) { n_fault++; fprintf(stderr,"OVER-READ FAULT len=%zu bytes:",len); for(size_t i=0;i<len;i++)fprintf(stderr," %02x",in[i]); fprintf(stderr,"\n"); }
    else if (g_slice_bad) { n_slice++; fprintf(stderr,"BAD SLICE len=%zu bytes:",len); for(size_t i=0;i<len;i++)fprintf(stderr," %02x",in[i]); fprintf(stderr,"\n"); }
    else if (r==0) n_ok++; else n_rej++;
}

int main(int argc, char**argv){
    pg = sysconf(_SC_PAGESIZE);
    region = mmap(NULL, pg*2, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (region==MAP_FAILED){perror("mmap");return 1;}
    if (mprotect(region+pg, pg, PROT_NONE)){perror("mprotect");return 1;}
    struct sigaction sa={0}; sa.sa_handler=on_fault; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV,&sa,NULL); sigaction(SIGBUS,&sa,NULL);

    /* (1) EXHAUSTIVE over all byte strings of length 0..MAXEXH */
    int MAXEXH = argc>1?atoi(argv[1]):3;
    unsigned char buf[8];
    for (int L=0; L<=MAXEXH; L++){
        unsigned long long total=1; for(int i=0;i<L;i++) total*=256;
        for (unsigned long long v=0; v<total; v++){
            unsigned long long t=v; for(int i=0;i<L;i++){ buf[i]=t&0xff; t>>=8; }
            run(buf, L);
        }
        fprintf(stderr,"[exhaustive L=%d done] dec=%llu\n", L, n_dec);
    }
    printf("EXHAUSTIVE 0..%d: decoded=%llu ok=%llu rejected=%llu faults=%llu bad_slices=%llu\n",
           MAXEXH, n_dec, n_ok, n_rej, n_fault, n_slice);
    return (n_fault||n_slice)?2:0;
}
