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

/* Host build of the QR Map encoder: one hex-encoded message per input line,
 * prints the mask chosen, then the 29 rows as hex bit masks (bit x = column x).
 *
 *   cc -O2 -o host_qr host_qr.c && echo 67656f3a | ./host_qr */
#include <stdio.h>
#include <string.h>
#include "../qr.c"

int main(void){
    char line[256];
    while(fgets(line,sizeof line,stdin)){
        uint8_t s[QR_MAX];
        unsigned len=0;
        for(char *p=line;p[0]&&p[1]&&p[0]!='\n'&&len<QR_MAX;p+=2){
            unsigned b;
            if(sscanf(p,"%2x",&b)!=1) break;
            s[len++]=(uint8_t)b;
        }
        uint32_t m[QR_N];
        printf("%u",qr_encode(s,len,m));
        for(unsigned y=0;y<QR_N;y++) printf(" %08x",m[y]);
        printf("\n");
    }
    return 0;
}
