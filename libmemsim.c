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
#include <errno.h>
#include <alloca.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>
#include "libmemsim.h"

#define null 0

// time in milliseconds since 1970 jan 1 00:00
uint32_t get_time(){
    struct timeval t;
    gettimeofday(&t,null);
    uint64_t r = t.tv_sec*1000000; 
    r += t.tv_usec;
    return r/1000;
}

/*
description of memsim file format
0=123 - means ptrs[0]=malloc(123) if ptrs[0] is not empty it uses realloc instead
0 - mean free(ptrs[0]) 
s - calls stats function
e - stops memsim 

example of .ms file
0=123 0 1=31 1 s
1=123 2=31 2 1 s e
*/
void run_memsim(char* filename, int silent, void (*stats_fun)(), int debug){
    int f = open(filename,O_RDONLY);
    char buf[256];
    char ptr_buf[80];
    if(f == -1){
        fprintf(stderr,"couldn't open %s\n",filename);
        fprintf(stderr,"%s\n",strerror(errno)); 
        return;
    }

    if(silent)
        debug = 0;

    void** stack = alloca(4096*sizeof(void*));
    memset(stack,0,4096*sizeof(void*));
    int mem_pos = -1;
    int mem_size = 0;
    
    uint64_t t_malloc = 0;
    uint64_t t_realloc = 0;
    uint64_t t_free = 0;
    uint64_t t_stats = 0;
    uint32_t c_malloc = 0;
    uint32_t c_realloc = 0;
    uint32_t c_free = 0;
    uint32_t c_stats = 0;

    int rd = 0;
    int i = 0;
    while((rd = read(f,buf,256)) > 0){
        char* c = buf;
        int j = 0;
        while(*c != null){
            if(rd == j)
                break;
            if(*c == 'e')
                return;
            if(*c == ' ' || *c == '\n' || *c == '=' || *c == 0){
                ptr_buf[i] = 0;
                i = 0;

                if(strlen(ptr_buf) == 0){
                }else if(ptr_buf[0] == 's'){
                    // call stats function
                    if(debug){
                        printf("calling stats function [\n");
                    }
                    uint64_t t = get_time();
                    if(stats_fun != null)
                        stats_fun();
                    t = get_time()-t;
                    t_stats += t;
                    if(debug){
                        printf("stats function called took %ums ]\n",(unsigned int)t);
                    }
                    ++c_stats;
                }else if(*c == '='){
                    mem_pos = atoi(ptr_buf);
                }else{
                    mem_size = atoi(ptr_buf);
                    if(mem_pos == -1){
                        mem_pos = mem_size;
                        // free memory
                        void** ptr = &(stack[mem_pos]);
                        if(debug){
                            printf("free memory at %d with ptr %p [\n",mem_pos,*ptr);
                        }
                        uint64_t t = get_time();
                        free(*ptr);
                        t = get_time()-t;
                        t_free += t;
                        if(errno > 0)
                            fprintf(stderr,"%s\n",strerror(errno)); 
                        *ptr = null;
                        if(debug){
                            printf("memory freed at %d took %ums ]\n",mem_pos,(unsigned int)t);
                        }
                        ++c_free;
                    }else{
                        void** ptr = &(stack[mem_pos]);
                        if(*ptr == null){
                            if(debug){
                                printf("allocating memory at %d with %d size [\n",mem_pos,mem_size);
                            }
                            uint64_t t = get_time();
                            *ptr = malloc(mem_size);
                            t = get_time()-t;
                            t_malloc += t;
                            if(errno > 0)
                                fprintf(stderr,"%s\n",strerror(errno)); 
                            if(debug){
                                printf("memory allocated at %d with %d size with ptr %p took %ums ]\n",mem_pos,mem_size,*ptr,(unsigned int)t);
                            }
                            ++c_malloc;
                        }else{
                            if(debug){
                                printf("reallocating memory at %d with %d size [\n",mem_pos,mem_size);
                            }
                            uint64_t t = get_time();
                            *ptr = realloc(*ptr, mem_size);
                            t = get_time()-t;
                            t_realloc += t;
                            if(errno > 0)
                                fprintf(stderr,"%s\n",strerror(errno)); 
                            if(debug){
                                printf("memory reallocated at %d with %d size with ptr %p took %ums ]\n",mem_pos,mem_size,*ptr,(unsigned int)t);
                            }
                            ++c_realloc;
                        }
                    }
                    mem_pos = -1;
                    mem_size = 0;
                }
            }else{
                ptr_buf[i] = *c;
                ++i;
            }
            ++c; ++j;
        } 

    }
    
    if(!silent){
        printf("malloc %d calls took %ums\n",c_malloc,(unsigned int)t_malloc);
        printf("realloc %d calls took %ums\n",c_realloc,(unsigned int)t_realloc);
        printf("free %d calls took %ums\n",c_free,(unsigned int)t_free);
        printf("stats %d calls took %ums\n",c_stats,(unsigned int)t_stats);
    }

    close(f);
}

struct memsim_args_t {
    char* filename;
    void (*stats_fun)();
    int repeat;
    int silent;
    int debug;
};

void* run_memsim_thread(void* a){
    struct memsim_args_t* args = (struct memsim_args_t*)a;
    for(int i=0;i < args->repeat;++i)
        run_memsim(args->filename,args->silent,args->stats_fun,args->debug);
    pthread_exit(0);
    return NULL;
}

void memsim(int repeat, int threads, char* filename, int silent,  void (*stats_fun)(), int debug){
    uint64_t st = get_time();

    if(threads == 1){
        for(int i=0;i<repeat;++i)
            run_memsim(filename,silent,stats_fun,debug);
    }else{
        pthread_t* thread_ids = alloca(threads*sizeof(pthread_t)); 
        for(int i = 0; i < threads; ++i){
            struct memsim_args_t* args = (struct memsim_args_t*)alloca(sizeof(struct memsim_args_t));
            args->filename = filename;
            args->stats_fun = stats_fun;
            args->debug = debug;
            args->silent = silent;
            args->repeat = repeat;
            if(pthread_create((thread_ids+i),NULL,&run_memsim_thread,args) != 0){
                fprintf(stderr,"%s\n",strerror(errno));
                exit(1);
            }
        }
        for(int i = 0; i < threads; ++i){
            pthread_join(*(thread_ids+i),NULL);
        }
    }

    if(!silent)
        printf("memory simulation took %ums\n",(unsigned int)(get_time()-st));
}

