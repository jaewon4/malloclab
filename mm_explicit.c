/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void add_free_block(void *bp);
static void remove_free_block(void *bp);

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "SWJungle_Week06_06",
    /* First member's full name */
    "Jaewon Kim",
    /* First member's email address */
    "grape0901@naver.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

// CONSTANTS
#define WSIZE 4 // 워드의 크기
#define DSIZE 8 // 더블 워드의 크기
#define CHUNKSIZE (1<<12) // 2^12 = 4KB

// MACROS
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7) // 더블워드 정렬이기 때문에 size보다 큰 8의 배수로 올림
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc)) // 블록의 크기와 할당 여부를 pack
#define GET(p) (*(unsigned int*)(p)) // p의 주소의 값 확인
#define PUT(p, val) (*(unsigned int*)(p) = val) // p의 주소에 val 값 저장
#define GET_SIZE(p) (GET(p) & ~0x7) // 블록의 크기 반환
#define GET_ALLOC(p) (GET(p) & 0x1)  // 블록의 할당여부 반환(마지막 비트에 할당을 했다는 의미의 1을 넣음)
#define HDRP(bp) ((char *)(bp) - WSIZE) // 블록의 header 주소 반환(블록포인터에서 한칸 뒤로가서 )
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 블록의 footer 주소 반환
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // 다음 블록의 주소 반환
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // 이전 블록의 주소 반환
#define NEXT_LINK(bp) (*(char **)(bp + WSIZE))                           // next 포인터 위치
#define PREV_LINK(bp) (*(char **)(bp))                                   // prev 포인터 위치
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static char *heap_listp = NULL; // 힙의 시작 주소를 가리킴
static char *free_listp = NULL; // 명시적 가용 리스트(explicit)의 첫 노드를 가리킴

int mm_init(void)
{
    /* 메모리에서 6words를 가져오고 이걸로 빈 가용 리스트 초기화 */
    /* padding, prol_header, NEXT, PREV, prol_footer, epil_header */
    if ((heap_listp = mem_sbrk(6*WSIZE)) == (void*)-1)
        return -1;

    PUT(heap_listp, 0);  // Alignment padding. 더블 워드 경계로 정렬된 미사용 패딩.
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE * 2, 1));  // prologue header
    PUT(heap_listp + (2*WSIZE), (int)NULL);  // prologue block안의 PREV 포인터 NULL로 초기화
    PUT(heap_listp + (3*WSIZE), (int)NULL);  // prologue block안의 NEXT 포인터 NULL로 초기화
    PUT(heap_listp + (4*WSIZE), PACK(DSIZE * 2, 1));  // prologue footer
    PUT(heap_listp + (5*WSIZE), PACK(0, 1));      // epliogue header

    free_listp = heap_listp + DSIZE; // free_listp를 탐색의 시작점으로 둔다. 
   
    /* 그 후 CHUNKSIZE만큼 힙을 확장해 초기 가용 블록을 생성한다. */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) //실패하면 -1 리턴
        return -1;

    return 0;
}

/*
 * extend_heap : 힙의 크기를 늘려주는 함수이다. 
            매개변수 words 를 통해 늘려주고자 하는 힙의 크기를 받는다.
            더블워드 정렬을 위해 짝수개의 DSIZE만큼의 크기를 늘린다.
            새로 추가된 힙영역은 하나의 가용 블럭이므로 
            header, footer 등의 값을 초기화한다.
            이 후 coalesce()를 호출하여 전 블럭과 병합이 가능한 경우 병합을 진행한다.
 */
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0)); // 추가된 free 블록의 header 정보 삽입
    PUT(FTRP(bp), PACK(size, 0)); // 추가된 free 블록의 footer 정보 삽입
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // Epilogue header

    return coalesce(bp); // 연속된 free 블록 합체
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free (void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0)); // header 및 footer에 할당 정보 0으로 변경
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp); // 이전 이후 블럭의 할당 정보를 확인하여 병합하고, free-list에 추가
}

// bp가 들어오면 next, prev블록을 이어준다.
static void add_free_block(void *bp){
    NEXT_LINK(bp) = free_listp;
    PREV_LINK(free_listp) = bp;
    PREV_LINK(bp) = NULL;
    // free블록을 추가하므로 free를 갱신하여 LIFO구조를 유지시킨다.
    free_listp = bp;
}

// 맨앞이나 중간 free블록을 해제 연결하는 함수
static void remove_free_block(void *bp){
    // free list에서 맨앞 블록을 삭제하는 경우
    if (free_listp == bp){
        PREV_LINK(NEXT_LINK(bp)) = NULL;
        // 맨 앞부분을 삭제하는 것이므로 free를 뒤로 이동시켜줘야함
        free_listp = NEXT_LINK(bp);
    }
    // free list에서 중간 블록을 삭제하는 경우
    else{
        PREV_LINK(NEXT_LINK(bp)) = PREV_LINK(bp);
        NEXT_LINK(PREV_LINK(bp)) = NEXT_LINK(bp);
        // free는 LIFO구조로 신규노드 삽입시 항상 맨압 신규노드의 bp를 
        // 가리키고 있으므로 중간블록을 삭제하는 경우 갱신하지 않는다.
    }
}

/*
 * coalesce : 현재 bp가 가리키는 블록의 이전 블록과 다음 블록의 할당 여부를 
            확인하여 가용 블럭(free)이 있다면 현재 블록과 인접 가용 블럭을 
            하나의 가용 블럭으로 합친다.
 */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc){
        // 1. bp의 위치에서 이어져있는 앞뒤 모두 할당인경우
        add_free_block(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc){
        // 2. bp의 위치에서 이어져있는 뒤가 프리인경우
        remove_free_block(NEXT_BLKP(bp)); // free_list에서 해당 연결을 끊어주고 뒤쪽을 이어주는 작업을 한다.
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        add_free_block(bp); // free_list에 블록 신규 추가
    }
    else if (!prev_alloc && next_alloc){
        // 3. bp의 위치에서 이어져있는 앞이 프리인 경우
        remove_free_block(PREV_BLKP(bp)); // free_list에서 해당 연결을 끊어주고 앞 리스트와 이어주는 작업을 한다.
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp); // bp을 이동해준다.
        add_free_block(bp); // free_list에 블록 신규 추가
    }
    else{
        // 4. bp의 위치에서 이어져있는 앞뒤 프리인 경우
        remove_free_block(NEXT_BLKP(bp));
        remove_free_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        add_free_block(bp);
    }
    return bp;
}


/* 
 * mm_malloc - brk 포인터를 증가시켜 블록을 할당합니다.
 *            항상 크기가 정렬의 배수인 블록을 할당합니다.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0) // 할당 사이즈가 0보다 같거나 작으면 NULL을 반환한다.
        return NULL;
    
    if (size <= DSIZE) // 할당할 사이즈가 8보다 작으면 asize를 16 Byte으로 한다.
        asize = 2 * DSIZE;
    else // 더블 워드 정렬을 위해 size보다 크거나 같은 8의 배수로 크기를 재조정
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((bp = find_fit(asize)) != NULL){ // free-list에서 size보다 큰 리스트 탐색 
        place(bp, asize); // 탐색에 성공하면 place()를 통해 할당
        return bp;
    }

    // 힙자체에 할당할수있는 free인 메모리가 없으므로 힙자체를 늘려줘야한다.
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * find_fit : 할당할 블록을 최초 할당 방식으로 찾는 함수
            명시적 가용 리스트의 처음부터 마지막부분에 도달할 때까지
            가용 리스트를 탐색하면서 사이즈가 asize보다 크거나 같은 블럭을
            찾으면 그 블럭의 주소를 반환한다. 
 */
static void *find_fit(size_t asize){
    // first-fit search
    char* bp;

    // start the search from the begining of the heap
    for (bp = free_listp; GET_ALLOC(HDRP(bp)) != 1; bp = NEXT_LINK(bp)){
        if(asize <= GET_SIZE(HDRP(bp))){
            return bp;
        }
    }
    
    return NULL;
}

/*
 * place : 지정된 크기의 블럭을 find_fit을 통해 찾은 free 블럭에 배치(할당)한다.
        만약 free 블럭에서 동적할당을 받고자하는 블럭의 크기를 제하여도
        또 다른 free블럭을 만들 수 있다면(2 * DSIZE 보다 큰 경우), free 블럭을 쪼갠다.
 */
static void place(void *bp, size_t asize){
    size_t old_size = GET_SIZE(HDRP(bp));
    remove_free_block(bp);
    // 할당할 크기가 이전 사이즈 - 넣을 사이즈 해서 16바이트(4워드) 이상인 경우
    if ((old_size - asize) >= (2 * DSIZE)){
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(old_size - asize, 0));
        PUT(FTRP(bp), PACK(old_size - asize, 0));
        add_free_block(bp);
    }
    // old_size - asize가 4워드 미만인 경우 즉 2워드인 경우 -> 이 경우는 헤더와 푸터를 넣으면 payload를 넣을 공간이 없으므로
    else{
        PUT(HDRP(bp), PACK(old_size, 1));
        PUT(FTRP(bp), PACK(old_size, 1));
    }
}

/*
 * mm_realloc - 기존에 할당된 메모리 블록의 크기를 변경하고, 변경된 크기에 맞게 
                새로운 메모리 블록을 할당하고 기존 메모리 블록의 데이터를 새로운
                메모리 블록으로 복사하는 것
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr; // realloc 함수가 호출될 때 인자로 전달된 기존 메모리 블록의 주소를 저장
    void *newptr; // realloc 함수에서 새로 할당할 메모리 블록의 주소를 저장할 변수
    size_t copySize; // realloc 함수에서 기존 메모리 블록에서 새로운 메모리 블록으로 복사할 데이터의 크기를 저장할 변수
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    //copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    copySize = GET_SIZE(HDRP(oldptr));
    // size를 줄여서 재할당하는 경우
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize); // 기존 메모리 블록에서 새로운 메모리 블록으로 데이터를 복사
    mm_free(oldptr); // 기존 메모리 블록을 해제
    return newptr; // 새로 할당된 메모리 블록의 주소를 반환
}














