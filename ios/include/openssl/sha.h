#ifndef OPENUG2_SHA256_SHIM
#define OPENUG2_SHA256_SHIM
/* SHA256() with the OpenSSL signature, implemented from FIPS 180-4. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static inline uint32_t ou_ror(uint32_t x,int n){return (x>>n)|(x<<(32-n));}
static inline void ou_sha256_block(uint32_t h[8],const unsigned char *p){
    static const uint32_t k[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64],s[8];int i;
    for(i=0;i<16;i++)w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
    for(;i<64;i++)w[i]=w[i-16]+(ou_ror(w[i-15],7)^ou_ror(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(ou_ror(w[i-2],17)^ou_ror(w[i-2],19)^(w[i-2]>>10));
    memcpy(s,h,sizeof s);
    for(i=0;i<64;i++){
        uint32_t t1=s[7]+(ou_ror(s[4],6)^ou_ror(s[4],11)^ou_ror(s[4],25))+((s[4]&s[5])^(~s[4]&s[6]))+k[i]+w[i];
        uint32_t t2=(ou_ror(s[0],2)^ou_ror(s[0],13)^ou_ror(s[0],22))+((s[0]&s[1])^(s[0]&s[2])^(s[1]&s[2]));
        s[7]=s[6];s[6]=s[5];s[5]=s[4];s[4]=s[3]+t1;s[3]=s[2];s[2]=s[1];s[1]=s[0];s[0]=t1+t2;
    }
    for(i=0;i<8;i++)h[i]+=s[i];
}
static inline unsigned char *SHA256(const unsigned char *d,size_t n,unsigned char *md){
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned char tail[128];size_t full=n/64*64,rest=n-full,pad;uint64_t bits=(uint64_t)n*8;int i;
    for(size_t o=0;o<full;o+=64)ou_sha256_block(h,d+o);
    memset(tail,0,sizeof tail);if(rest)memcpy(tail,d+full,rest);tail[rest]=0x80;
    pad=rest<56?64:128;for(i=0;i<8;i++)tail[pad-1-i]=(unsigned char)(bits>>(8*i));
    ou_sha256_block(h,tail);if(pad==128)ou_sha256_block(h,tail+64);
    for(i=0;i<8;i++){md[4*i]=(unsigned char)(h[i]>>24);md[4*i+1]=(unsigned char)(h[i]>>16);md[4*i+2]=(unsigned char)(h[i]>>8);md[4*i+3]=(unsigned char)h[i];}
    return md;
}
#endif
