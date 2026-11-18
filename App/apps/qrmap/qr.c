/* Copyright 2026 Johan Denoyer F4WAT
 * https://github.com/jdenoy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

/* The encoder follows the structure of the QR Code generator library by
 * Project Nayuki (https://www.nayuki.io/page/qr-code-generator-library):
 * Reed-Solomon divisor and remainder, function patterns, format bits,
 * zigzag codeword placement, mask penalty. Rewritten here for one fixed
 * version, uint32_t rows and no division, but derived from it, under its
 * licence:
 *
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 *
 * QR Code is a registered trademark of DENSO WAVE INCORPORATED. The symbol
 * itself is specified by ISO/IEC 18004:2015.
 */

/* QR Code encoder, version 3-L: see qr.h.
 *
 * The steps of ISO/IEC 18004 for one fixed version, so every size is a
 * constant: bit stream, Reed-Solomon codewords, function patterns, data in
 * the zigzag order, then the mask with the lowest penalty and its format
 * information. A row of the symbol is one uint32_t, so the whole symbol and
 * its function-pattern map take 2 x 116 bytes of stack.
 *
 * No division: the masks need x mod 3 and x / 3, kept as running counters
 * (the Cortex-M0+ has no divide instruction). */
#include <stdbool.h>
#include "qr.h"

#define QR_DATA 55u                 /* data codewords, version 3-L  */
#define QR_ECC  15u                 /* error correction codewords   */
#define QR_ALL  (QR_DATA+QR_ECC)
#define QR_LAST (QR_N-1u)

/* GF(256) product, modulo x^8+x^4+x^3+x^2+1 (0x11D). */
static uint8_t gfMul(uint8_t x,uint8_t y){
    unsigned z=0;
    for(unsigned i=8;i--;){
        z<<=1;
        if(z&0x100u) z^=0x11Du;
        if((y>>i)&1u) z^=x;
    }
    return (uint8_t)z;
}

/* Bit stream (mode, count, bytes, terminator, pad), then the ECC codewords. */
static void codewords(const uint8_t *s,unsigned len,uint8_t cw[QR_ALL]){
    unsigned pos=0;
    for(unsigned i=0;i<QR_ALL;i++) cw[i]=0;
    /* byte mode 0100, an 8-bit count, then the bytes: 4 bits out of step */
    uint32_t acc=0x4u<<8|len;
    unsigned n=12;
    for(unsigned i=0;;i++){
        while(n>=8u){ n-=8u; cw[pos++]=(uint8_t)(acc>>n); }
        if(i==len) break;
        acc=acc<<8|s[i]; n+=8u;
    }
    /* the last 4 bits, then the 4-bit terminator: both fit, since 12 + 8 x 53
     * leaves 4 bits free in the 55 codewords */
    cw[pos++]=(uint8_t)(acc<<4);
    for(unsigned pad=0xECu;pos<QR_DATA;pad^=0xECu^0x11u) cw[pos++]=(uint8_t)pad;

    /* generator polynomial of degree 15, roots 2^0..2^14, leading 1 implied */
    uint8_t g[QR_ECC];
    for(unsigned i=0;i<QR_ECC;i++) g[i]=0;
    g[QR_ECC-1u]=1;
    uint8_t root=1;
    for(unsigned i=0;i<QR_ECC;i++){
        for(unsigned j=0;j<QR_ECC;j++){
            g[j]=gfMul(g[j],root);
            if(j+1u<QR_ECC) g[j]^=g[j+1u];
        }
        root=gfMul(root,2);
    }
    /* remainder of the data polynomial, straight into the ECC codewords */
    uint8_t *ec=cw+QR_DATA;
    for(unsigned i=0;i<QR_DATA;i++){
        uint8_t f=(uint8_t)(cw[i]^ec[0]);
        for(unsigned j=0;j<QR_ECC;j++)
            ec[j]=(uint8_t)((j+1u<QR_ECC?ec[j+1u]:0u)^gfMul(g[j],f));
    }
}

static void set(uint32_t *m,unsigned x,unsigned y,bool dark){
    if(dark) m[y]|=1u<<x; else m[y]&=~(1u<<x);
}
static void setF(uint32_t *m,uint32_t *fn,unsigned x,unsigned y,bool dark){
    fn[y]|=1u<<x;
    set(m,x,y,dark);
}
static unsigned dist(int dx,int dy){
    if(dx<0) dx=-dx;
    if(dy<0) dy=-dy;
    return (unsigned)(dx>dy?dx:dy);
}

/* The 15 format bits of level L (01) with this mask, in their two copies. */
static void format(uint32_t *m,uint32_t *fn,unsigned mask){
    unsigned d=1u<<3|mask, r=d;
    for(unsigned i=0;i<10;i++) r=(r<<1)^((r>>9)*0x537u);
    unsigned bits=(d<<10|r)^0x5412u;
    for(unsigned i=0;i<15;i++){
        bool b=(bits>>i)&1u;
        /* first copy, around the top-left finder */
        if(i<6u)       setF(m,fn,8,i,b);
        else if(i<8u)  setF(m,fn,8,i+1u,b);
        else if(i==8u) setF(m,fn,7,8,b);
        else           setF(m,fn,14u-i,8,b);
        /* second copy, split between the two other finders */
        if(i<8u) setF(m,fn,QR_LAST-i,8,b);
        else     setF(m,fn,8,QR_N-15u+i,b);
    }
    setF(m,fn,8,QR_N-8u,true);      /* the dark module */
}

static void patterns(uint32_t *m,uint32_t *fn){
    for(unsigned i=0;i<QR_N;i++){ m[i]=0; fn[i]=0; }
    for(unsigned i=0;i<QR_N;i++){   /* timing */
        setF(m,fn,6,i,!(i&1u));
        setF(m,fn,i,6,!(i&1u));
    }
    static const uint8_t fx[3]={3,QR_N-4u,3}, fy[3]={3,3,QR_N-4u};
    for(unsigned f=0;f<3;f++)       /* finders and their separators */
        for(int dy=-4;dy<=4;dy++)
            for(int dx=-4;dx<=4;dx++){
                int x=fx[f]+dx, y=fy[f]+dy;
                unsigned d=dist(dx,dy);
                if(x>=0 && x<(int)QR_N && y>=0 && y<(int)QR_N)
                    setF(m,fn,(unsigned)x,(unsigned)y,d!=2u && d!=4u);
            }
    for(int dy=-2;dy<=2;dy++)       /* the alignment pattern of version 3 */
        for(int dx=-2;dx<=2;dx++)
            setF(m,fn,(unsigned)(22+dx),(unsigned)(22+dy),dist(dx,dy)!=1u);
    format(m,fn,0);                 /* reserve the format areas */
}

/* Codeword bits in the zigzag order: two-column strips from the right,
 * up then down, column 6 (timing) skipped. The 7 remainder bits stay 0. */
static void place(uint32_t *m,const uint32_t *fn,const uint8_t *cw){
    unsigned i=0;
    for(int right=(int)QR_LAST;right>=1;right-=2){
        if(right==6) right=5;
        bool up=!((right+1)&2);
        for(unsigned v=0;v<QR_N;v++)
            for(unsigned j=0;j<2;j++){
                unsigned x=(unsigned)right-j, y=up?QR_LAST-v:v;
                if((fn[y]>>x)&1u || i>=QR_ALL*8u) continue;
                set(m,x,y,(cw[i>>3]>>(7u-(i&7u)))&1u);
                i++;
            }
    }
}

/* XOR the mask over the data modules: applying it twice undoes it. */
static void applyMask(uint32_t *m,const uint32_t *fn,unsigned k){
    unsigned y3=0;                       /* y mod 3 */
    for(unsigned y=0;y<QR_N;y++){
        unsigned x3=0, xd3=0;            /* x mod 3, x / 3 */
        for(unsigned x=0;x<QR_N;x++){
            unsigned p=x3*y3;            /* x*y mod 3 is p mod 3, p <= 4 */
            if(p>=3u) p-=3u;
            unsigned xy2=x&y&1u, s2=(x+y)&1u;
            bool inv;
            switch(k){
            case 0:  inv=!s2; break;
            case 1:  inv=!(y&1u); break;
            case 2:  inv=!x3; break;
            case 3:  inv=x3+y3==0u || x3+y3==3u; break;
            case 4:  inv=!(((y>>1)+xd3)&1u); break;
            case 5:  inv=!xy2 && !p; break;
            case 6:  inv=!((xy2+p)&1u); break;
            default: inv=!((s2+p)&1u); break;
            }
            if(inv && !((fn[y]>>x)&1u)) m[y]^=1u<<x;
            if(++x3==3u){ x3=0; xd3++; }
        }
        if(++y3==3u) y3=0;
    }
}

/* Rules 1 and 3 on one row (col false) or one column. */
static unsigned lineScore(const uint32_t *m,unsigned a,bool col){
    unsigned score=0, run=0, prev=2;
    uint32_t win=0;                      /* the last 11 modules, light before the edge */
    for(unsigned i=0;i<QR_N+4u;i++){     /* 4 light modules past the far edge */
        unsigned c=0;
        if(i<QR_N){
            c=col?(m[i]>>a)&1u:(m[a]>>i)&1u;
            if(c==prev){
                if(++run==5u) score+=3u;
                else if(run>5u) score++;
            } else { prev=c; run=1; }
        }
        win=(win<<1|c)&0x7FFu;
        if(win==0x05Du || win==0x5D0u) score+=40u;   /* 1:1:3:1:1 and 4 light */
    }
    return score;
}

static unsigned penalty(const uint32_t *m){
    unsigned score=0, dark=0;
    for(unsigned i=0;i<QR_N;i++)
        score+=lineScore(m,i,false)+lineScore(m,i,true);
    for(unsigned y=0;y<QR_N;y++){
        for(unsigned x=0;x<QR_N;x++){
            unsigned c=(m[y]>>x)&1u;
            dark+=c;
            if(x<QR_LAST && y<QR_LAST){  /* rule 2: 2 x 2 blocks of one colour */
                unsigned b=(m[y]>>x)&3u, b2=(m[y+1u]>>x)&3u;
                if(b==b2 && (b==0u || b==3u)) score+=3u;
            }
        }
    }
    /* rule 4: 10 points per full 5 % away from half dark */
    unsigned total=QR_N*QR_N;
    unsigned d20=dark*20u, t10=total*10u, diff=d20>t10?d20-t10:t10-d20;
    for(unsigned k=1;diff>k*total;k++) score+=10u;
    return score;
}

unsigned qr_encode(const uint8_t *s,unsigned len,uint32_t m[QR_N]){
    uint8_t cw[QR_ALL];
    uint32_t fn[QR_N];
    if(len>QR_MAX) len=QR_MAX;
    codewords(s,len,cw);
    patterns(m,fn);
    place(m,fn,cw);
    unsigned best=0, bestScore=~0u;
    for(unsigned k=0;k<8u;k++){
        applyMask(m,fn,k);
        format(m,fn,k);
        unsigned p=penalty(m);
        if(p<bestScore){ bestScore=p; best=k; }
        applyMask(m,fn,k);
    }
    applyMask(m,fn,best);
    format(m,fn,best);
    return best;
}
