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
/*
ALIGNMENT의 가장 가까운 배수로 반올림합니다.
ALIGN(13) = (((13) + (8-1)) & ~0x7)
          = (((13) + (7)) & ~0x7)
          = (20 & ~0000 0111)
          = (0001 0100 & 1111 1000)
          = 16(0001 0000)
*/
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc)) // 블록의 크기와 할당 여부를 pack
/*
size: 블록의 크기 (바이트), alloc: 할당 정보. 할당되지 않은 경우 0, 할당된 경우 1
예를 들어, size가 24이고 alloc이 1인 경우를 생각해보겠습니다. 
24는 2진수로 0b11000입니다. 이를 16진수로 표현하면 0x18입니다. 
alloc이 1이므로 반환되는 값의 최하위 비트는 1이 됩니다. 따라서 반환되는 값은 0x19가 됩니다.
*/
// 인자 p는 대개 (void*)포인터 이다.
#define GET(p) (*(unsigned int*)(p)) // p의 주소의 값 확인
#define PUT(p, val) (*(unsigned int*)(p) = val) // p의 주소에 val 값 저장
#define GET_SIZE(p) (GET(p) & ~0x7) // 블록의 크기 반환
/*
 ~0x7은 마지막 3비트를 제거하는 역할을 한다.
... 0000 0111 -> ... 1111 1000이 되는데 이를 &연산을 하면 마지막 3비트가 없어진다.
*/
#define GET_ALLOC(p) (GET(p) & 0x1)  // 블록의 할당여부 반환(마지막 비트에 할당을 했다는 의미의 1을 넣음)
#define HDRP(bp) ((char *)(bp) - WSIZE) // 블록의 header 주소 반환(블록포인터에서 한칸 뒤로가서 )
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // 블록의 footer 주소 반환
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // 다음 블록의 주소 반환
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // 이전 블록의 주소 반환

// size_t는 unsigned int형을 말하는데 메모리 할당이나 객체 크기를 표현하는데 사용되는 자료형이다.
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static char *heap_listp = NULL; // 힙의 시작 주소를 가리킴

/*
 * mm_init - 초기힙을 구성하는 함수
            먼저 mem_sbrk()에 매개변수로 4 * WSIEZ를 전달하여 16 Byte만큼의 힙 공간을 늘린다.
            첫 워드(4바이트)에는 미사용 패딩 워드인 0을,
            두번째 워드에는 Prologue Header인 PACK(DSIZE, 1)을
            네번째 워드에는 Eplogue Header인 PACK(0, 1)을 넣는다.
            이 후 extend_heap()을 호출하여 CHUNKSIZE / WSIZE 만큼 힙의 크기를 늘린다. 
 *  _______________________________________                         ______________
 * |            |  PROLOGUE  |  PROLOGUE  |                        |   EPILOGUE  |
 * |   PADDING  |   HEADER   |   FOOTER   |                        |    HEADER   |
 * |------------|------------|------------|    ...    |------------|-------------|
 * |      0     |    8 / 1   |    8 / 1   |    ...    |   FOOTER   |    0 / 1    |
 * |------------|------------|------------|    ...    |------------|-------------|
 * ^                                                                             ^
 * heap_listp                                                                 mem_brk                                                                      
 * 
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) - 1)
        return -1;
    PUT(heap_listp, 0); // 미사용 패딩 워드
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); // Prologue Header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); // Prologue Footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1)); // Epilogue Header
    heap_listp += (2 * WSIZE); // heap_listp는 힙의 시작 위치를 가리키게 한다.

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL){ // 초기 힙의 크기를 늘린다.
        return -1;
    }

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
        return bp;
    }
    else if (prev_alloc && !next_alloc){
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc){
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else{
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
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
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){
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
    // 즉, 워드가 4개이상 남으면 잘라서 다시 프리 공간으로 만들어준다.
    if ((old_size - asize) >= (2 * DSIZE)){
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(old_size - asize, 0));
        PUT(FTRP(bp), PACK(old_size - asize, 0));
    }
    // 워드가 4개 미만이면 그냥 합쳐버린다. -> 2개인경우, 0개인 경우를 말함
    //  -> 헤더와 푸터 넣으면 끝이라서 의미없기에 합침
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
