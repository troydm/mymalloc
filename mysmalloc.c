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

#define _GNU_SOURCE
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <mymalloc.h>
#include <sched.h>
#include <sys/mman.h>
#include <linux/mman.h>

// for code clarity for pointers we use null instead of 0
#define null 0

// initial values
#define PAGE_SIZE (sysconf(_SC_PAGESIZE))
#define MIN_BLOCK_SIZE 32 // bytes
#define ALLOC_SIZE 33554432 // 32 MiB or 8192 pages if page size is 4096
#define GIVE_BACK_SIZE 33554432 // 32 MiB or 8192 pages if page size is 4096
#define MMAP_SIZE 1048576 // 1 MiB or 1024 pages if page size is 4096
#define MERGE_ADJ_ON_REALLOC 1 // try to merge with adjacent blocks on realloc

// memory block structure
typedef struct memory_block_t {
    size_t size;
    struct memory_block_t* prev;
    struct memory_block_t* next;
} memory_block;

// free memory block list
#define FREELIST_SIZE 4 // number of freelists
static memory_block freelists[] = { 
    { 0, null, &(freelists[1]) },  { 0, &(freelists[0]), null},
    { 0, null, &(freelists[3]) },  { 0, &(freelists[2]), null},
    { 0, null, &(freelists[5]) },  { 0, &(freelists[4]), null},
    { 0, null, &(freelists[7]) },  { 0, &(freelists[6]), null},
};
#define freelist_start(i) (freelists[0+i].next)
#define freelist_begin(i) (&(freelists[0+i]))
#define freelist_end(i) (&(freelists[1+i]))

// heap 
static memory_block* heap_start = null;
static memory_block* heap_end = null;
static uint32_t heap_size;
static uint32_t mmap_size;

// mmap
#define is_mmap_block(b) (!(heap_start <= b && b < heap_end))

// locking
volatile bool glob_lock = false;
volatile bool freelist_locks[] = { false, false, false, false };

static inline void lock(volatile bool* lock){
    if (!__sync_bool_compare_and_swap(lock, 0, 1)){ 
        int i = 0;
        do { 
            if (__sync_bool_compare_and_swap(lock, 0, 1))
                break; 
            else{
                if(i == 10){
                    i = 0;
                    sched_yield(); 
                }else
                    ++i;
            }
        } while (1); 
    }
}

#define unlock(lock) \
    __asm__ __volatile__ ("" ::: "memory"); \
    *lock = 0

#define global_lock() \
    lock(&glob_lock)

#define global_unlock() \
    unlock(&glob_lock)

// find unlocked freelist that is not fj
static inline uint8_t freelist_lock(uint8_t fj){
    int j = 0;
    while(1){
        for(uint8_t i = 0; i < FREELIST_SIZE; ++i){
            if(fj == i*2)
                continue;
            if (__sync_bool_compare_and_swap(&(freelist_locks[i]), 0, 1)){ 
                return i*2;
            }
        }
        ++j;
        if(j == 10){
            j = 0;
            sched_yield(); 
        }
    }
}

static inline uint8_t freelist_lock_any(){
    int j = 0;
    while(1){
        for(uint8_t i = 0; i < FREELIST_SIZE; ++i){
            if (__sync_bool_compare_and_swap(&(freelist_locks[i]), 0, 1)){ 
                return i*2;
            }
        }
        ++j;
        if(j == 10){
            j = 0;
            sched_yield(); 
        }
    }
}

static inline void freelist_lock_all(){
    int j = 0;
    for(uint8_t i = 0; i < FREELIST_SIZE; ++i){
        while(1){
            if (__sync_bool_compare_and_swap(&(freelist_locks[i]), 0, 1)){
                ++i;
                break;
            }
            ++j;
            if(j == 10){
                j = 0;
                sched_yield(); 
            }
        }
    }
}

#define unlock_freelist(fi) \
    unlock(&(freelist_locks[fi/2]))

#define unlock_all_freelists() \
    for(uint8_t i = 0; i < FREELIST_SIZE; ++i){ \
        unlock(&(freelist_locks[i])); \
    }

// uncomment for debug use only
/* #define lock */
/* #define unlock */

// useful macros
#define byte_ptr(p) ((uint8_t*)p)
#define shift_ptr(p,s) (byte_ptr(p)+s)
#define shift_block_ptr(b,s) ((memory_block*)(shift_ptr(b,s)))
#define block_data(b) (shift_ptr(b,sizeof(size_t)))
#define data_block(p) (shift_block_ptr(p,-sizeof(size_t)))
#define block_end(b) (shift_block_ptr(b,b->size))

#define block_link(lb,rb) \
    rb->prev = lb; \
    lb->next = rb;

#define block_link_left(lb,b) \
    block_link(b->prev,lb) \
    block_link(lb,b)

#define block_link_right(b,rb) \
    block_link(rb,b->next) \
    block_link(b,rb)

#define block_unlink(b) \
    block_link(b->prev,b->next)

#define block_unlink_right(b) \
    block_unlink(b->next);

#define block_replace(b,nb) \
    block_link(b->prev,nb) \
    block_link(nb,b->next)

// print block information to stdout
// for debug use only
static inline void print_block(memory_block* b){
    printf("block %p size %ld prev %p next %p\n",b,b->size,b->prev,b->next);
}

// add block to free list
static inline void add_block(uint8_t fi, memory_block* block){
    if(freelist_start(fi) != freelist_end(fi)){
        // find superseding memory block
        // and insert current one before it
        memory_block* b = freelist_start(fi);
        while(1){
            // superseding memory block will have higher memory address
            if(b > block){
                block_link_left(block,b);
                break;            
            }
            // check if we hit end
            if(b->next == freelist_end(fi)){
                block_link_right(b,block);
                break;
            }else{
                b = b->next;
            }
        }

        // merge adjacent blocks
        bool merged = true;
        while(merged){
            // merge right adjacent block
            if(block_end(block) == block->next){
                block->size += block->next->size;
                block_unlink_right(block);
                continue;
            }                    
            // merge left adjacent block
            if(block_end(block->prev) == block){
                block = block->prev;
                block->size += block->next->size;
                block_unlink_right(block);
                continue;
            }
            merged = false;
        }
    }else{
        // add first memory block
        memory_block* b = freelist_begin(fi);
        block_link_right(b, block);
    }
}

// split memory block into 2 pieces one of size s and the other is remainder e.g. memory_block_size-s
// if remainder is less than MIN_BLOCK_SIZE we just take whole block
static inline memory_block* split_memory_block(memory_block* b, size_t s){
    size_t remainder = b->size - s;
    if(remainder >= MIN_BLOCK_SIZE){
        memory_block* nb = shift_block_ptr(b,s);
        nb->size = remainder;
        block_replace(b,nb);
        b->size = s;
    }else{
        block_unlink(b);
    }
    return b;
}

static inline size_t find_optimal_memory_size(size_t s){
    size_t suitable_size = MIN_BLOCK_SIZE;
    
    while(suitable_size < s)
        suitable_size = suitable_size << 1;

    return suitable_size;
}

// find suitable memory block for size s
static inline memory_block* find_suitable_block(uint8_t fi, size_t ns){

    memory_block* b = freelist_start(fi);
    while(b != freelist_end(fi)){
        if(b->size >= ns){
            return split_memory_block(b,ns);                    
        }
        b = b->next;    
    }

    return null;
}

void* malloc(size_t s){
    // check for 0 size
    if(s == 0)
        return null;
    // add size of size_t as we need to save size of memory block
    s += sizeof(size_t);
    // find suitable memory size
    size_t ns = find_optimal_memory_size(s);

    // if size is greater than or equals MMAP_SIZE we are going to use mmap
    if(ns >= MMAP_SIZE){
        void* m = mmap(NULL,s,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);    
        if(m == MAP_FAILED)
            return null;
        memory_block* b = (memory_block*)m;
        b->size = s;
        global_lock();
        mmap_size += s;
        global_unlock();
        return block_data(b);
    }

    memory_block* block;
    uint8_t fj = FREELIST_SIZE*2;
    uint8_t fi = fj;
    while(fj > 0){
        fi = freelist_lock(fi);
        // find free memory block
        block = find_suitable_block(fi,ns);
        if(block != null){
            unlock_freelist(fi);
            // shift pointer into data block pointer
            return block_data(block);
        }            
        unlock_freelist(fi);
        --fj;
    }

    // no free memory blocks found
    // we need to allocate new one that would be suitable for our needs using sbrk
    size_t pages_size = ((ns/PAGE_SIZE)+1)*PAGE_SIZE;
    if(pages_size < ALLOC_SIZE)
        pages_size = ALLOC_SIZE;
    // allocate memory with sbrk
    global_lock();
    void* p = sbrk(pages_size);    
    if(p == (void*)-1){
        global_unlock();
        return null;
    }

    block = (memory_block*)p;
    heap_size += pages_size;
    block->size = ns;
    heap_end = shift_block_ptr(block,pages_size);
    heap_start = shift_block_ptr(heap_end,-heap_size);
    global_unlock();
    ns = pages_size - ns;
    if(ns >= MIN_BLOCK_SIZE){
        memory_block* b = shift_block_ptr(block,block->size);
        b->size = ns;

        fi = freelist_lock_any();
        add_block(fi,b);
        unlock_freelist(fi);
    }

    return block_data(block);
}

// merge with adjacent block so that overall new size would be s
static inline memory_block* merge_with_adjacent_block(uint8_t fi, memory_block* block, size_t s){

    if(freelist_start(fi) == freelist_end(fi))
        return null;

    memory_block* b = freelist_start(fi);
    memory_block* be = block_end(block);
    do {
        
        // left adjacent
        // code is slightly more complex as we need to copy data over
        if(block_end(b) == block){
            if((b->size + block->size) >= s){
                size_t remainder = (b->size + block->size) - s;
                // we need to backup block pointers as they might be overwritten by memcpy
                memory_block* temp_prev = b->prev;
                memory_block* temp_next = b->next;
                memcpy(block_data(b),block_data(block), block->size - sizeof(size_t));
                if(remainder >= MIN_BLOCK_SIZE){
                    b->size = s;
                    memory_block* nb = shift_block_ptr(b,s);
                    nb->size = remainder;
                    block_link(temp_prev,nb);
                    block_link(nb,temp_next);
                }else{
                    // unfortunetly here we can't use b->prev as 
                    // it might have been overwritten by memcpy
                    // so we need to remove remaining block entirely using temporary pointers
                    block_link(temp_prev,temp_next);
                }
                return b;
            }
        }

        // right adjacent
        if(be == b){
            if((block->size + b->size) >= s){
                size_t remainder = (block->size + b->size) - s;
                if(remainder >= MIN_BLOCK_SIZE){
                    memory_block* nb = shift_block_ptr(block,s);
                    nb->size = remainder;
                    block_replace(b,nb);
                    block->size = s;
                }else{
                    block_unlink(b);
                }
                return block;
            }
            break;
        }

        b = b->next;
    } while(b != freelist_end(fi) && b >= be);

    return null;
}

void* realloc(void* p, size_t s){
    if(p == null){
        return malloc(s);
    }else if(s == 0){
        free(p);
        return null;
    }
    
    // shift pointer back into memory block pointer
    memory_block* b = data_block(p);

    // find out which new optimal size we need
    size_t ss = s+sizeof(size_t);
    size_t ns = find_optimal_memory_size(ss);

    // if memory is mmap we need to use mremap
    global_lock();
    if(is_mmap_block(b)){
        mmap_size = (mmap_size - b->size) + ss;
        global_unlock();
        void* p = mremap(b,b->size,ss,MREMAP_MAYMOVE);
        if(p == MAP_FAILED)
            return null;
        b = (memory_block*)p;
        b->size = ss;
        return block_data(b);
    }
    global_unlock();
    
    if(ns < MMAP_SIZE){
        // check if size is already sufficient
        if(b->size >= ns){
            return p;
        }

#ifdef MERGE_ADJ_ON_REALLOC
        // try merging with adjacent blocks
        uint8_t fi = freelist_lock_any();
        memory_block* nb = merge_with_adjacent_block(fi,b,ns);
        if(nb != null){
            unlock_freelist(fi);
            // shift pointer into data block pointer
            return block_data(nb);
        }
        unlock_freelist(fi);
#endif
    }

    void* np = malloc(s);
    if(np != null){
        // copy old data block into new one
        memcpy(np,p,s > b->size ? b->size : s);

        // free old data block
        free(p);

        // return newly allocated block
        return np;
    }
    
    return null;
}

void free(void* p){
    // check for null pointer
    if(p == null)
        return;

    // shift pointer back into memory block pointer
    memory_block* b = data_block(p);

    global_lock();
    if(is_mmap_block(b)){
        mmap_size -= b->size;
        global_unlock();
        munmap(b,b->size);        
        return;    
    }
    global_unlock();

    // add removed block into freelist
    uint8_t fi;
    fi = freelist_lock_any();
    add_block(fi,b);

    // give last memory block that isn't needed back to the operating system
    b = freelist_end(fi)->prev;
    global_lock();
    if(block_end(b) == heap_end){
        intptr_t inc = b->size;
        if(inc >= GIVE_BACK_SIZE){
            if(b == heap_start){
                if(inc > GIVE_BACK_SIZE){
                    inc = inc - GIVE_BACK_SIZE;
                    sbrk(-inc);
                    b->size = GIVE_BACK_SIZE;
                }
            }else{
                block_unlink(b);
                sbrk(-inc);
                heap_size -= inc;
                heap_end = shift_block_ptr(heap_end,-inc);
                heap_start = shift_block_ptr(heap_end,-heap_size);
            }
        }
    }
    global_unlock();
    unlock_freelist(fi);
}

void* calloc(size_t nmemb, size_t size){
    size = nmemb*size;
    void* p = malloc(size);
    if(p != null)
        memset(p,0,size);
    return p;
}

void print_block_info(void* p){
    // shift pointer back into memory block pointer
    memory_block* b = data_block(p);
    print_block(b);
}

void print_freelist(){
    global_lock();
    printf("[heap size %d mb mmap_size %d mb freelists %d] ",(heap_size/(1024*1024)),(mmap_size/(1024*1024)),FREELIST_SIZE);
    freelist_lock_all();
    for(uint8_t i = 0; i < FREELIST_SIZE; ++i){
        printf("freelist %d {",i);
        memory_block* b = freelist_start(i*2);
        while(b != freelist_end(i*2)){
            printf(" -> %p[%lu|%p|%p]",b,b->size,b->prev,b->next);    
            // detect infinite loop if any
            if(b == b->next){
                printf(" -> infinite loop\n");
                break;
            }
            b = b->next;
        }
        printf(" }\n");
    }
    unlock_all_freelists()
    global_unlock();
}
