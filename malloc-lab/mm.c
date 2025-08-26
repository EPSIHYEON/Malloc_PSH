#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"
team_t team = {
    "12team",
    "Park Sihyeon",
    "bovik@cs.cmu.edu",
    "",
    ""
};
#define CHUNKSIZE   (1 << 12)
#define MAX(x,y)    ((x) > (y) ? (x) : (y))
#define PACK(size, alloc)   ((size) | (alloc))
#define PUT(p, val)         (*(unsigned int *)(p) = (val))
#define GET(p)              (*(unsigned int *)(p))
#define GET_SIZE(p)         (GET(p) & ~0x7)
#define GET_ALLOC(p)        (GET(p) & 0x1)
#define WSIZE 4
#define DSIZE 8
/* 블록 헤더/푸터 및 이웃 블록 접근 */
#define HDPR(bp)            ((char *)(bp) - WSIZE)
#define FTPR(bp)            ((char *)(bp) + GET_SIZE(HDPR(bp)) - DSIZE)
#define NEXT_BLKP(bp)       ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)       ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE))
/* 명시적 free list payload 레이아웃: [succ(8)][pred(8)] */
#define SUCCPR(bp)          (*(void **)(bp))
#define PREDPR(bp)          (*(void **)((char *)(bp) + DSIZE))
/* 명시적 리스트용 최소 free 블록 크기 (헤더+푸터 8 포함 총 24바이트) */
#define MINBLOCK            (3 * DSIZE)
static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void  place(void *bp, size_t asize);
static void remove_free(void *bp);
/* 전역 포인터 */
static char *heap_listp = NULL;
static char *free_listp = NULL;
static char *last_bp = NULL;
/* free list 조작 (LIFO) */
static void insert_free(void *bp) {
    PREDPR(bp) = NULL;
    SUCCPR(bp) = free_listp;
    if (free_listp) PREDPR(free_listp) = bp;
    free_listp = bp;
}

static void remove_free(void *bp){
    void *prev = PREDPR(bp);
    void *next = SUCCPR(bp);
    if(prev) {SUCCPR(prev) = next;
            last_bp = prev;  }
    else     { free_listp = next;
            last_bp = next;}
    if (next) PREDPR(next) = prev; 
    
    }

int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;
    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    heap_listp += (2 * WSIZE);
    free_listp = NULL;
    

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}
static void *extend_heap(size_t words)
{
    char *ptr;
    size_t size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(ptr = mem_sbrk(size)) == -1)
        return NULL;
    PUT(HDPR(ptr), PACK(size, 0));
    PUT(FTPR(ptr), PACK(size, 0));
    PUT(HDPR(NEXT_BLKP(ptr)), PACK(0, 1));
    return coalesce(ptr); /* coalesce 내부에서 free list에 넣음 */
}
void *mm_malloc(size_t size)
{
    if (size == 0) return NULL;
    size_t asize;
    if (size <= DSIZE) asize = 2 * DSIZE; /* 최소 16 */
    else               asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);
    void *ptr = find_fit(asize);
    if (ptr != NULL) {
        place(ptr, asize);
        return ptr;
    }
    size_t extendsize = MAX(asize, CHUNKSIZE);
    ptr = extend_heap(extendsize / WSIZE);
    if (ptr == NULL) return NULL;
    place(ptr, asize);
    return ptr;
}
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDPR(ptr));
    PUT(HDPR(ptr), PACK(size, 0));
    PUT(FTPR(ptr), PACK(size, 0));
    coalesce(ptr);
}
static void *coalesce(void *ptr)
{
    size_t prev_alloc = GET_ALLOC(HDPR(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDPR(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDPR(ptr));
    if (prev_alloc && next_alloc) {
        /* 주변 모두 할당: 그냥 리스트에 삽입 */
        insert_free(ptr);
        return ptr;
    } else if (prev_alloc && !next_alloc) {
        /* 다음과 병합 */
        void *nb = NEXT_BLKP(ptr);
        remove_free(nb);
        size += GET_SIZE(HDPR(nb));
        PUT(HDPR(ptr), PACK(size, 0));
        PUT(FTPR(ptr), PACK(size, 0));
        insert_free(ptr);
        return ptr;
    } else if (!prev_alloc && next_alloc) {
        /* 이전과 병합 */
        void *pb = PREV_BLKP(ptr);
        remove_free(pb);
        size += GET_SIZE(HDPR(pb));
        PUT(HDPR(pb), PACK(size, 0));
        PUT(FTPR(ptr), PACK(size, 0));
        insert_free(pb);
        return pb;
    } else {
        /* 양쪽 모두와 병합 */
        void *pb = PREV_BLKP(ptr);
        void *nb = NEXT_BLKP(ptr);
        remove_free(pb);
        remove_free(nb);
        size += GET_SIZE(HDPR(pb)) + GET_SIZE(HDPR(nb));
        PUT(HDPR(pb), PACK(size, 0));
        PUT(FTPR(nb), PACK(size, 0));
        insert_free(pb);
        return pb;
    }
}
static void *find_fit(size_t asize){
    char *ptr;

    if(last_bp == NULL){
        last_bp  = free_listp;
    }

    ptr = last_bp ;

    while(ptr != NULL){ 

        if(GET_SIZE(HDPR(ptr)) >= asize){
            last_bp  = ptr;
            return ptr;
        }

        ptr = SUCCPR(ptr);
    }

    ptr = free_listp;
    while(ptr != last_bp ){
        if(GET_SIZE(HDPR(ptr)) >= asize){
        last_bp  = ptr;
        return ptr;
        }

        ptr = SUCCPR(ptr);
    }


    return NULL;

}
static void place(void *ptr, size_t asize)
{
    size_t csize = GET_SIZE(HDPR(ptr));
    remove_free(ptr);
    if (csize - asize >= MINBLOCK) {
        /* 앞쪽 배치 + 뒤에 free 조각(최소 24) 남김 */
        PUT(HDPR(ptr), PACK(asize, 1));
        PUT(FTPR(ptr), PACK(asize, 1));
        void *nptr = NEXT_BLKP(ptr);
        size_t rsize = csize - asize;
        PUT(HDPR(nptr), PACK(rsize, 0));
        PUT(FTPR(nptr), PACK(rsize, 0));
        insert_free(nptr);
    } else {
        /* 통째로 할당 */
        PUT(HDPR(ptr), PACK(csize, 1));
        PUT(FTPR(ptr), PACK(csize, 1));
    }
}
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }
    size_t asize;
    if (size <= DSIZE) asize = 2 * DSIZE;
    else               asize = DSIZE * ((size + DSIZE + (DSIZE - 1)) / DSIZE);
    size_t old_size = GET_SIZE(HDPR(ptr));
    /* 뒤 블록이 free이고 합치면 충분한 경우, in-place 확장 */
    void *next = NEXT_BLKP(ptr);
    if (!GET_ALLOC(HDPR(next))) {
        size_t total = old_size + GET_SIZE(HDPR(next));
        if (total >= asize) {
            remove_free(next);
            size_t diff = total - asize;
            if (diff >= MINBLOCK) {
                PUT(HDPR(ptr), PACK(asize, 1));
                PUT(FTPR(ptr), PACK(asize, 1));
                void *nptr = NEXT_BLKP(ptr);
                PUT(HDPR(nptr), PACK(diff, 0));
                PUT(FTPR(nptr), PACK(diff, 0));
                insert_free(nptr);
            } else {
                PUT(HDPR(ptr), PACK(total, 1));
                PUT(FTPR(ptr), PACK(total, 1));
            }
            return ptr;
        }
    }
    /* 새로 할당 후 복사 */
    void *newptr = mm_malloc(size);
    if (!newptr) return NULL;
    size_t copySize = old_size - DSIZE; /* payload 크기 */
    if (size < copySize) copySize = size;
    memcpy(newptr, ptr, copySize);
    mm_free(ptr);
    return newptr;
}