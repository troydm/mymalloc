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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "libmemsim.h"

#define null 0

void print_tild(){
    printf("-------------------------------------------------------------\n");
}

int main(int argc, char* argv[]){
    if(argc == 1){
        printf("./sysmemsim [-t 1] [-r 1] [-s] [-d] filename.ms - malloc memory allocation simulator that uses system malloc\n");
        return 1;
    }

    int debug = 0;
    int silent = 0;
    int threads = 1;
    int repeat = 1;

    int c;
    while((c = getopt(argc,argv,"t:r:sd")) != -1){
        switch(c){
            case 'r':
                repeat = atoi(optarg);
                break;
            case 't':
                threads = atoi(optarg);
                if(threads == 0){
                    fprintf(stderr, "threads can't be 0");                
                    exit(1);
                }
                break;
            case 's':
                silent = 1;
                break;
            case 'd':
                debug = 1;
                break;
        }
    }

    memsim(repeat, threads, argv[argc-1], silent, silent ? null : &print_tild,debug);
    
    return 0;
}

