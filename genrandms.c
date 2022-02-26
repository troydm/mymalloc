/*
The MIT License (MIT)

Copyright (c) 2015 Dmitry "troydm" Geurkov (d.geurkov@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>

#define null 0

int main(int argc, char* argv[]){
    if(argc == 1){
        printf("./genrandms [-m 32[kmg]b] [-M 16[kmg]b] [-c 128] [-p 16] filename.ms - malloc memory random simulation generator\n");
        return 1;
    }

    size_t min_size=32;
    size_t max_size=16*1024*1024;
    uint32_t allocs = 128;
    uint32_t ptrs = 16;

    int c;
    int sl;
    size_t s;
    while((c = getopt(argc,argv,"m:M:c:p:")) != -1){
        switch(c){
            case 'c':
                allocs = atoi(optarg);
                break;
            case 'p':
                ptrs = atoi(optarg);
                break;
            case 'm':
                sl = strlen(optarg);
                s = atoi(optarg);
                if(optarg[sl-2]=='k')
                    s *= 1024;
                if(optarg[sl-2]=='m')
                    s *= 1024*1024;
                if(optarg[sl-2]=='g')
                    s *= 1024*1024*1024;
                min_size = s;
                break;
            case 'M':
                sl = strlen(optarg);
                s = atoi(optarg);
                if(optarg[sl-2]=='k')
                    s *= 1024;
                if(optarg[sl-2]=='m')
                    s *= 1024*1024;
                if(optarg[sl-2]=='g')
                    s *= 1024*1024*1024;
                max_size = s;
                break;
            default:
                break;
        }
    }

    printf("generating min=%ld bytes max=%ld bytes %d allocations over %d pointers into %s\n",min_size,max_size,allocs,ptrs,argv[argc-1]);
    if(min_size < 0 ||  max_size < 0){
        printf("min size or max size can't be negative\naborting generation\n");    
        return 1;
    }
    if(min_size == 0 ||  max_size == 0){
        printf("min size or max size can't be 0\naborting generation\n");    
        return 1;
    }
    if(min_size > max_size){
        printf("min size can't be greater than max size\naborting generation\n");    
        return 1;
    }

    srand(time(NULL));

    FILE* f = fopen(argv[argc-1],"w+");

    uint32_t* ptr = malloc(ptrs*sizeof(uint32_t));
    memset(ptr,0,ptrs*sizeof(uint32_t));

    for(uint32_t i = 0; i < allocs;++i){
        uint32_t r = (rand() % (max_size-min_size+1)) + min_size;
        int ppos = r % ptrs;
        if(ptr[ppos] == null || r % 2 == 0){
            fprintf(f,"%d=%d",ppos,r);
            ptr[ppos] = r;
        }else{
            fprintf(f,"%d",ppos);
            ptr[ppos] = null;
        }
        if((i+1) % 8 == 0)
            fprintf(f,"\n");
        else
            fprintf(f," ");
    }

    for(int i = 0; i < ptrs; ++i){
        if(ptr[i] != null){
            fprintf(f,"%d",i);
            if((i+1) % 8 == 0)
                fprintf(f,"\n");
            else
                fprintf(f," ");
        }
    }
    fprintf(f,"\n");
    free(ptr);

    fclose(f);

    return 0;
}

