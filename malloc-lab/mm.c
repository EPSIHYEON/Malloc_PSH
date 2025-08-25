#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include <sys/mman.h>
#include <errno.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "12team",
    /* First member's full name */
    "Park Sihyeon",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

#define CHUNKSIZE (1<<12)

#define MAX(x,y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc)) //헤더, 푸터 안에 들어갈 정보
#define PUT(p,val) (*(size_t *)(p) = (val)) // 포인터안에 값을 넣는다
#define  GET(p)  (*(size_t*) (p)) //포인터 안에 있는 "값"을 읽어온다 


#define GET_SIZE(p) (GET(p) & ~0x7) //크기
#define GET_ALLOC(p) (GET(p) & 0x1) //할당 여부

#define HDPR(bp) ((char *)(bp) - WSIZE) //헤더의 위치
#define FTPR(bp) ((char *)(bp) + GET_SIZE(HDPR(bp)) - DSIZE) //푸터의 위치  // GET_SIZE(HDPR(bp)) 해석: HDPR(bp)라는 포인터의 값을 읽어오고 연산 

// explicit 용 변수 
#define SUCCPR(bp) (*(void **)(bp))
#define PREDPR(bp) (*(void **)(bp + WSIZE))


#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)  - GET_SIZE((char *)(bp) - DSIZE))




/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t))) //size_t는 자료형임 

#define WSIZE SIZE_T_SIZE //8
#define DSIZE (2 * SIZE_T_SIZE) //16

//점수가 더 높게 나옴
// #define WSIZE 4 //8
//  #define DSIZE 8 //16

static char *heap_listp = NULL; //정밀함을 위해 1바이트씩 움직이는 char 형으로 설정(define 때문에 특정 경우에서는 바뀜)


//explicit 용 변수 
static char *free_listp = NULL;


/*
 * mm_init - initialize the malloc package.
 */

static void *find_fit(size_t size);
static void place(void *ptr, size_t size);
static void *extend_heap(size_t words);
static void *coalesce(void *ptr);
static void removeBlock(void *ptr);
static void putFreeBlock(void *ptr);

int mm_init(void)
{
    if((heap_listp = mem_sbrk(6 * WSIZE)) == (void *)-1)
    return -1;

    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(4 * WSIZE,1)); //프롤로그 헤더 (이 메모리는 DSIZE 크기이고 할당되어있습니다)
    PUT(heap_listp + (2*WSIZE), NULL); //First PREDPR
    PUT(heap_listp + (3* WSIZE), NULL); //First SUCCPR
    PUT(heap_listp + (4*WSIZE), PACK(4 * WSIZE,1)); //프롤로그 푸터
    PUT(heap_listp + (5 * WSIZE), PACK(0,1)); //에필로그 헤더 
    free_listp = heap_listp + DSIZE;

    //아마 언더플로우 때문에 패딩을 넣는 이유도 있을듯
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) // word가 몇개인지 확인해서 넣으려고(DSIZE로 나눠도 됨)
        return -1;

    return 0;
}

static void *extend_heap(size_t words){
    char *ptr;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;  //홀수면, 짝수면
    if((long)(ptr = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDPR(ptr), PACK(size,0)); // header에 크기 작성
    PUT(FTPR(ptr), PACK(size,0)); //footer 에 크기 작성
    PUT(HDPR(NEXT_BLKP(ptr)), PACK(0, 1)); //새로운 에필로그 헤더

    return coalesce(ptr);

}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
   size_t asize;
   size_t extendsize;
   void *ptr;

   if(size <= 0) //** */
   return NULL;

   if(size <= DSIZE)
   asize = 2*DSIZE;
   else
   asize = DSIZE * ((size + (DSIZE) + (DSIZE -1)) / DSIZE); //8배수 정렬로 할당하는 식 + Header,Footer,Payload 포함한 식 

   if((ptr = find_fit(asize)) != NULL){
    place(ptr,asize);
    return ptr;
   }

   // 더이상 남은 메모리가 없다면
   extendsize = MAX(asize, CHUNKSIZE);
   if((ptr = extend_heap(extendsize/ WSIZE)) == NULL)
        return NULL;
    place(ptr,asize);
    return ptr;


}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDPR(ptr));
    PUT(HDPR(ptr), PACK(size,0));
    PUT(FTPR(ptr), PACK(size, 0));
    coalesce(ptr);
}

static void *coalesce(void *ptr){
    size_t prev_alloc = GET_ALLOC(FTPR(PREV_BLKP(ptr)));
    size_t next_alloc = GET_ALLOC(HDPR(NEXT_BLKP(ptr)));
    size_t size = GET_SIZE(HDPR(ptr));


    if(prev_alloc && next_alloc){ // 1,1
        putFreeBlock(ptr);
        return ptr;
    }
    else if(!prev_alloc && next_alloc){  //0, 1
        removeBlock(PREV_BLKP(ptr));
        size += GET_SIZE(FTPR(PREV_BLKP(ptr)));
        PUT(FTPR(ptr), PACK(size, 0));
        PUT(HDPR(PREV_BLKP(ptr)), PACK(size, 0)); //**** */
        ptr = PREV_BLKP(ptr);
    }
    else if(prev_alloc && !next_alloc){//1, 0 **조금 특이함 HDPR 때문에 FTPR 가 영향을 받아서 NEXT_PTR 가 아님 
        removeBlock(NEXT_BLKP(ptr));
        size += GET_SIZE(HDPR(NEXT_BLKP(ptr)));
        PUT(HDPR(ptr), PACK(size, 0));
        PUT(FTPR(ptr), PACK(size, 0));
    } 
    else{ //0, 0
        removeBlock(PREV_BLKP(ptr));
        removeBlock(NEXT_BLKP(ptr));
        size += GET_SIZE(FTPR(PREV_BLKP(ptr)));
        size += GET_SIZE(HDPR(NEXT_BLKP(ptr)));

        PUT(HDPR(PREV_BLKP(ptr)), PACK(size , 0));
        PUT(FTPR(NEXT_BLKP(ptr)), PACK(size,0));
        ptr = PREV_BLKP(ptr);

    }

    putFreeBlock(ptr);
    return ptr;
}

static void putFreeBlock(void *ptr){
    // void *old_ptr = free_listp;

    // free_listp = ptr;
    // PREDPR(ptr) = NULL;
    // SUCCPR(ptr) = old_ptr;
    // if(old_ptr != NULL){
    //     PREDPR(old_ptr) = ptr;
    // }

    SUCCPR(ptr) = free_listp;
    PREDPR(ptr) = NULL;
    if (free_listp != NULL)
        PREDPR(free_listp) = ptr;
    free_listp = ptr;

}

// //first fit
static void *find_fit(size_t asize){
    char *ptr = free_listp;


    while(ptr != NULL){ //에필로그 헤더까지 돔

        if(GET_SIZE(HDPR(ptr)) >= asize){
            return ptr;
        }

        ptr = SUCCPR(ptr);

    }

    return NULL;

}

// //Best fit
// // static void *find_fit(size_t asize){

// //     char *ptr = heap_listp;
// //     char *maxptr =NULL;


// //     while(GET_SIZE(HDPR(ptr)) > 0 ){ //에필로그 헤더까지 돔

// //         if(GET_SIZE(HDPR(ptr)) >= asize && !GET_ALLOC(HDPR(ptr))){
// //             if(maxptr == NULL || GET_SIZE((ptr)) < GET_SIZE(HDPR(maxptr))){
// //                 maxptr = ptr;

// //             }
// //         }

// //         ptr = NEXT_BLKP(ptr);

// //     }


// //     return maxptr;

// // }

// //NextFit
// static void *find_fit(size_t asize){
//     char *ptr;

//     if(last_ptr == NULL){
//         last_ptr = heap_listp;
//     }

//     ptr = last_ptr;

//     while(GET_SIZE(HDPR(ptr)) > 0 ){ //에필로그 헤더까지 돔

//         if(GET_SIZE(HDPR(ptr)) >= asize && !GET_ALLOC(HDPR(ptr))){
//             last_ptr = ptr;
//             return ptr;
//         }

//         ptr = NEXT_BLKP(ptr);
//     }

//     ptr = heap_listp;
//     while(ptr != last_ptr){
//         if(GET_SIZE(HDPR(ptr)) >= asize && !GET_ALLOC(HDPR(ptr))){
//         last_ptr = ptr;
//         return ptr;
//         }

//         ptr = NEXT_BLKP(ptr);
//     }


//     return NULL;

// }


static void place(void *ptr, size_t asize){
    size_t csize = GET_SIZE(HDPR(ptr));
    size_t diff = csize - asize;
    removeBlock(ptr);

    if (diff < 2*DSIZE){
        PUT(HDPR(ptr), PACK(csize, 1));
        PUT(FTPR(ptr), PACK(csize, 1));

    }else{
        PUT(HDPR(ptr), PACK(asize, 1));
        PUT(FTPR(ptr), PACK(asize, 1));

        PUT(HDPR(NEXT_BLKP(ptr)), PACK(diff,0));
        PUT(FTPR(NEXT_BLKP(ptr)), PACK(diff,0)); 
        putFreeBlock(NEXT_BLKP(ptr));
    }


}


static void removeBlock(void *ptr){
 // 첫 번째 블록을 없앨 때
    // if(ptr == free_listp){
    //     PREDPR(SUCCPR(ptr)) = NULL;
    //     free_listp = SUCCPR(ptr);
    // }else{
    //     SUCCPR(PREDPR(ptr)) = SUCCPR(ptr);
    //     PREDPR(SUCCPR(ptr)) = PREDPR(ptr);
    // }

 if (PREDPR(ptr))
        SUCCPR(PREDPR(ptr)) = SUCCPR(ptr);
    else
        free_listp = SUCCPR(ptr);

    if (SUCCPR(ptr))
        PREDPR(SUCCPR(ptr)) = PREDPR(ptr);

    
    }




/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{   void *oldptr = ptr;

    if(ptr == NULL){
        return mm_malloc(size);
    }
    if(size == 0){
        mm_free(ptr);
        return NULL;
    }

    if(!GET_ALLOC(HDPR(NEXT_BLKP(ptr)))){ //[CASE 1]뒤에 공간이 비어있으면 거기부터 할당(ptr에서 뒤에 연장)
    size_t asize;
    size_t total = GET_SIZE(HDPR(NEXT_BLKP(ptr))) + GET_SIZE(HDPR(ptr));
    
    if(size <= DSIZE) asize = 2*DSIZE;
   else asize = DSIZE * ((size + (DSIZE) + (DSIZE -1)) / DSIZE); //8배수 정렬로 할당하는 식 

    if(total >= asize){
        size_t diff = total - asize;
          
    removeBlock(ptr);
    if (diff < 2*DSIZE){
        PUT(HDPR(ptr), PACK(total, 1));
        PUT(FTPR(ptr), PACK(total, 1));
        removeBlock(NEXT_BLKP(ptr));

    }else{
        PUT(HDPR(ptr), PACK(asize, 1));
        PUT(FTPR(ptr), PACK(asize, 1));

        PUT(HDPR(NEXT_BLKP(ptr)), PACK(diff,0));
        PUT(FTPR(NEXT_BLKP(ptr)), PACK(diff,0)); 
        putFreeBlock(NEXT_BLKP(ptr));
    }
  

    return ptr;
    }
    }

    void *newptr = mm_malloc(size); // [CASE 2]없으면 새로운 공간 할당해서 거기서 realloc

    if (!newptr) return NULL;

    size_t old_size = GET_SIZE(HDPR(ptr)) -DSIZE; //DSIZE = Footer + Header
    
    if(old_size > size) old_size = size;

    memcpy(newptr, oldptr, old_size);
    mm_free(oldptr);

    return newptr;

}



// //ALL GPT 코드 점수는 동일한데 보기 편함 
// // void *mm_realloc(void *ptr, size_t size)
// // {   if (ptr == NULL)
// //         return mm_malloc(size);    // NULL이면 malloc과 동일
// //     if (size == 0) {
// //         mm_free(ptr);             // size 0이면 free
// //         return NULL;
// //     }

// //     size_t asize;                 // 요청 블록 크기(정렬 포함)
// //     if (size <= DSIZE)
// //         asize = 2 * DSIZE;
// //     else
// //         asize = DSIZE * ((size + DSIZE + (DSIZE-1)) / DSIZE);

// //     size_t old_size = GET_SIZE(HDPR(ptr));

// //     // 뒤 블록이 비어있고 합치면 충분한 경우
// //     if (!GET_ALLOC(HDPR(NEXT_BLKP(ptr))) && (old_size + GET_SIZE(HDPR(NEXT_BLKP(ptr)))) >= asize) {
// //         size_t total = old_size + GET_SIZE(HDPR(NEXT_BLKP(ptr)));
// //         size_t diff = total - asize;

// //         if (diff >= 2*DSIZE) {
// //             // 쪼개기 가능
// //             PUT(HDPR(ptr), PACK(asize, 1));
// //             PUT(FTPR(ptr), PACK(asize, 1));
// //             PUT(HDPR(NEXT_BLKP(ptr)), PACK(diff, 0));
// //             PUT(FTPR(NEXT_BLKP(ptr)), PACK(diff, 0));
// //         } else {
// //             // 그냥 합치기
// //             PUT(HDPR(ptr), PACK(total, 1));
// //             PUT(FTPR(ptr), PACK(total, 1));
// //         }

// //         return ptr;
// //     }

// //     // 새 블록 할당 후 데이터 복사
// //     void *newptr = mm_malloc(size);
// //     if (!newptr) return NULL;

// //     size_t copy_size = old_size - DSIZE; // payload 크기
// //     if (size < copy_size) copy_size = size;

// //     memcpy(newptr, ptr, copy_size);
// //     mm_free(ptr);

// //     return newptr;



// // }








