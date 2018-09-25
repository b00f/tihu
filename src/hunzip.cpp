/*******************************************************************************
* This file is part of Tihu.
*
* Tihu is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* Tihu is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Tihu.  If not, see <http://www.gnu.org/licenses/>.
*
* Contributor(s):
*    Mostafa Sedaghat Joo (mostafa.sedaghat@gmail.com)
*
*******************************************************************************/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "hunzip.h"

#define CODELEN 65536
#define BASEBITREC 5000

#define UNCOMPRESSED '\002'
#define MAGIC "hz0"
#define MAGIC_ENCRYPT "hz1"
#define MAGICLEN (sizeof(MAGIC) - 1)

int Hunzip::fail(const char* err, const char* par)
{
    fprintf(stderr, err, par);
    return -1;
}

Hunzip::Hunzip(const char* file, const char* key)
    :  inbits(0), bufsiz(0), lastbit(0), inc(0),outc(0)
{
    in[0] = out[0] = line[0] = '\0';
    filename = file;
    if(getcode(key) == -1) {
        bufsiz = -1;
    } else {
        bufsiz = getbuf();
    }
}

int Hunzip::getcode(const char* key)
{
    unsigned char c[2];
    int i, j, n;
    int allocatedbit = BASEBITREC;
    const char* enc = key;

    if(filename.empty()) {
        return -1;
    }

    fin.open(filename, std::ios_base::in | std::ios_base::binary);
    if(!fin.is_open()) {
        return -1;
    }

    // read magic number
    if(!fin.read(in, 3) ||
       !(strncmp(MAGIC, in, MAGICLEN) == 0 ||
         strncmp(MAGIC_ENCRYPT, in, MAGICLEN) == 0)) {
        return fail(MSG_FORMAT, filename.c_str());
    }

    // check encryption
    if(strncmp(MAGIC_ENCRYPT, in, MAGICLEN) == 0) {
        unsigned char cs;
        if(!key) {
            return fail(MSG_KEY, filename.c_str());
        }
        if(!fin.read(reinterpret_cast<char*>(c), 1)) {
            return fail(MSG_FORMAT, filename.c_str());
        }
        for(cs = 0; *enc; enc++) {
            cs ^= *enc;
        }
        if(cs != c[0]) {
            return fail(MSG_KEY, filename.c_str());
        }
        enc = key;
    } else {
        key = NULL;
    }

    // read record count
    if(!fin.read(reinterpret_cast<char*>(c), 2)) {
        return fail(MSG_FORMAT, filename.c_str());
    }

    if(key) {
        c[0] ^= *enc;
        if(*(++enc) == '\0') {
            enc = key;
        }
        c[1] ^= *enc;
    }

    n = ((int)c[0] << 8) + c[1];
    dec.resize(BASEBITREC);
    dec[0].v[0] = 0;
    dec[0].v[1] = 0;

    // read codes
    for(i = 0; i < n; i++) {
        unsigned char l;
        if(!fin.read(reinterpret_cast<char*>(c), 2)) {
            return fail(MSG_FORMAT, filename.c_str());
        }
        if(key) {
            if(*(++enc) == '\0') {
                enc = key;
            }
            c[0] ^= *enc;
            if(*(++enc) == '\0') {
                enc = key;
            }
            c[1] ^= *enc;
        }
        if(!fin.read(reinterpret_cast<char*>(&l), 1)) {
            return fail(MSG_FORMAT, filename.c_str());
        }
        if(key) {
            if(*(++enc) == '\0') {
                enc = key;
            }
            l ^= *enc;
        }
        if(!fin.read(in, l / 8 + 1)) {
            return fail(MSG_FORMAT, filename.c_str());
        }
        if(key)
            for(j = 0; j <= l / 8; j++) {
                if(*(++enc) == '\0') {
                    enc = key;
                }
                in[j] ^= *enc;
            }
        int p = 0;
        for(j = 0; j < l; j++) {
            int b = (in[j / 8] & (1 << (7 - (j % 8)))) ? 1 : 0;
            int oldp = p;
            p = dec[p].v[b];
            if(p == 0) {
                lastbit++;
                if(lastbit == allocatedbit) {
                    allocatedbit += BASEBITREC;
                    dec.resize(allocatedbit);
                }
                dec[lastbit].v[0] = 0;
                dec[lastbit].v[1] = 0;
                dec[oldp].v[b] = lastbit;
                p = lastbit;
            }
        }
        dec[p].c[0] = c[0];
        dec[p].c[1] = c[1];
    }
    return 0;
}

Hunzip::~Hunzip()
{
}

int Hunzip::getbuf()
{
    int p = 0;
    int o = 0;
    do {
        if(inc == 0) {
            fin.read(in, BUFSIZE);
            inbits = fin.gcount() * 8;
        }
        for(; inc < inbits; inc++) {
            int b = (in[inc / 8] & (1 << (7 - (inc % 8)))) ? 1 : 0;
            int oldp = p;
            p = dec[p].v[b];
            if(p == 0) {
                if(oldp == lastbit) {
                    fin.close();
                    // add last odd byte
                    if(dec[lastbit].c[0]) {
                        out[o++] = dec[lastbit].c[1];
                    }
                    return o;
                }
                out[o++] = dec[oldp].c[0];
                out[o++] = dec[oldp].c[1];
                if(o == BUFSIZE) {
                    return o;
                }
                p = dec[p].v[b];
            }
        }
        inc = 0;
    } while(inbits == BUFSIZE * 8);
    return fail(MSG_FORMAT, filename.c_str());
}

bool Hunzip::getline(std::string &dest)
{
    char linebuf[BUFSIZE];
    int l = 0, eol = 0, left = 0, right = 0;
    if(bufsiz == -1) {
        return false;
    }
    while(l < bufsiz && !eol) {
        linebuf[l++] = out[outc];
        switch(out[outc]) {
        case '\t':
            break;
        case 31: {  // escape
            if(++outc == bufsiz) {
                bufsiz = getbuf();
                outc = 0;
            }
            linebuf[l - 1] = out[outc];
            break;
        }
        case ' ':
            break;
        default:
            if(((unsigned char)out[outc]) < 47) {
                if(out[outc] > 32) {
                    right = out[outc] - 31;
                    if(++outc == bufsiz) {
                        bufsiz = getbuf();
                        outc = 0;
                    }
                }
                if(out[outc] == 30) {
                    left = 9;
                } else {
                    left = out[outc];
                }
                linebuf[l - 1] = '\n';
                eol = 1;
            }
        }
        if(++outc == bufsiz) {
            outc = 0;
            bufsiz = fin.is_open() ? getbuf() : -1;
        }
    }
    if(right) {
        strcpy(linebuf + l - 1, line + strlen(line) - right - 1);
    } else {
        linebuf[l] = '\0';
    }
    strcpy(line + left, linebuf);
    dest.assign(line);
    return true;
}
