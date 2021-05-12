#ifndef __SSD__HMB_TYPES_H__
#define __SSD__HMB_TYPES_H__

#include <stdint.h>
#include <stdbool.h>

#include "hmb_config.h"
#include "qemu/osdep.h"
#include "qemu/timer.h"

#define HMB_FOR_READ  (0)  /* HMB: same as DMA_DIRECTION_TO_DEVICE */
#define HMB_FOR_WRITE (1)  /* HMB: same as DMA_DIRECTION_FROM_DEVICE */

#define HMB_UL_NUMBER (4) /* redefined as HMB_SPACEMGMT_UL_NUMBER in hmb_spaceMgmt.h */

#define HMB_HAS_NO_ENTRY (-1)

typedef uint32_t HmbBitmap32;
typedef int32_t HmbBitmapEmpty;

typedef int32_t HmbSync;

typedef uint64_t HmbHostAddr;

typedef struct HmbMappedAddr
{
	void *r;
	void *w;
} HmbMappedAddr;

typedef int32_t HmbHeads;

#pragma pack(push, 1)    /* for avoiding automatic structure padding */
typedef struct HmbSplitTable
{
	int16_t  seg_id;
	uint32_t offset;
} HmbSplitTable;
#pragma pack(pop)

typedef struct HmbTime
{
	uint64_t t_prev_ns;
	uint64_t t_acc_ns;
} HmbTime;

typedef struct HmbDebugTime
{
	HmbTime ll__calc_max_allocable_size;
	HmbTime ll__search_empty_space;
	HmbTime ll__get_new_id;
	HmbTime ll__get_id;
	HmbTime ll__ctrl_malloc;
	HmbTime ll__map_pci;
	HmbTime ll__insert_entry;
	HmbTime ll__memset;
	HmbTime ll__search_map_info;

	HmbTime SM__init_heads;
	HmbTime SM__init_sorted;
	HmbTime SM__init_bitmap;
	HmbTime SM__init_entry;
	HmbTime SM__init_insert_map_info;
} HmbDebugTime;

typedef struct HmbDebugTimePerRequest
{
	HmbTime of_qemu__iov;
	HmbTime of_qemu__sglist;
	HmbTime of_qemu__memcpy;

	HmbTime of_femu;
	HmbTime of_femu__get_empty_block;

	HmbTime of_hmb;
	HmbTime of_hmb__flush;
	HmbTime of_hmb__RC_caching;
	HmbTime of_hmb__table_get_by_lpn;
	HmbTime of_hmb__insert;
	HmbTime of_hmb__delete;
	HmbTime of_hmb__bm_op;
	HmbTime of_hmb__memcpy;
} HmbDebugTimePerRequest;

extern HmbDebugTime HMB_DEBUG_TIME;
extern HmbDebugTimePerRequest HMB_DEBUG_TIME_PER_REQUEST;

/**
    [1] HMB: Structure for one host memory buffer description entry
        - HMB is divided to some parts.
		- This structure describes one of them.
**/
#pragma pack(push, 1)    /* for avoiding automatic structure padding */
typedef struct HmbEntry {
	uint64_t    addr; /* 8bytes (acc: 8) */
	uint32_t    size; /* 4bytes (acc: 12) */ 
	uint32_t    rsvd; /* 4bytes (acc: 16) */
} HmbEntry;
#pragma pack(pop)

/** [1] **/

typedef struct HmbSegEmpty
{
	int16_t            segment_id;
	uint32_t           offset_from;
	uint32_t           offset_to;
	struct HmbSegEmpty *prev, *next;
} HmbSegEmpty;

/** [1] HMB: Controller-internal data structure **/
typedef struct HmbSeg
{
	int16_t            id; 
	HmbHostAddr        host_addr;
	uint64_t           size_allocable_max;
	uint64_t           size_max;
	struct HmbSegEmpty *empty_list; /* previously, "empty_list"*/
} HmbSeg;
/** [1] **/

/**
  [1] HMB: Shared data structure with host OS
  - For describing each part of HMB
  - sizeof(HmbSegEnt): 14bytes
 **/
#pragma pack(push, 1)    /* for avoiding automatic structure padding */
typedef struct HmbSegEnt
{
	int32_t  id;		 /* 4bytes (acc: 4) */
	int16_t  segment_id; /* 2bytes (acc: 6) */
	uint32_t offset;     /* 4bytes (acc: 10) */
	uint32_t size;       /* 4bytes (acc: 14) */
} HmbSegEnt;
#pragma pack(pop)
/** [1] **/

#pragma pack(push, 1)    /* for avoiding automatic structure padding */
typedef struct HmbDLL
{
	int32_t e_prev;
	int32_t e_next;
} HmbDLL;
#pragma pack(pop)

#pragma pack(push, 1)    /* for avoiding automatic structure padding */
typedef struct HmbMeta
{
	uint32_t HMB__SE_num_max;             /* 4bytes (acc: 4) */

	uint16_t HMB__SE_ST_num;      /* 2bytes (acc: 6) */
	uint32_t HMB__SE_ST_unit;     /* 4bytes (acc: 10) */

	int16_t  HMB__SE_ST_seg_id;   /* 2bytes (acc: 12) */
	uint32_t HMB__SE_ST_offset;   /* 4bytes (acc: 16) */

	int16_t  C__table_ST_seg_id;  /* 2bytes (acc: 18) */
	uint32_t C__table_ST_offset;  /* 4bytes (acc: 22) */
	
	uint16_t C__table_ST_num;     /* 2bytes (acc: 24) */
	uint32_t C__table_ST_unit;    /* 4bytes (acc: 28) */

	uint32_t C__n_max_entries;            /* 4bytes (acc: 32) */

	int16_t  C__heads_ST_seg_id;  /* 2bytes (acc: 34) */
	uint32_t C__heads_ST_offset;  /* 4bytes (acc: 38) */

	uint16_t C__heads_ST_num;     /* 2bytes (acc: 40) */
	uint32_t C__heads_ST_unit;    /* 4bytes (acc: 44) */
	
	uint8_t  C__heads_hash_bit;           /* 1byte  (acc: 45) */
	uint32_t C__heads_cnt_max;            /* 4bytes (acc: 49) */

	int16_t  C__sorted_ST_seg_id; /* 2bytes (acc: 51) */
	uint32_t C__sorted_ST_offset; /* 4bytes (acc: 55) */

	uint16_t C__sorted_ST_num;    /* 2bytes (acc: 57) */
	uint32_t C__sorted_ST_unit;   /* 4bytes (acc: 61) */
 
	int16_t  C__victimAll_seg_id;         /* 2bytes (acc: 63) */
	uint32_t C__victimAll_offset;         /* 4bytes (acc: 67) */

	int16_t  C__bm_seg_id;                /* 2bytes (acc: 69) */
	uint32_t C__bm_offset;                /* 4bytes (acc: 73) */

	int16_t  C__bm_empty_seg_id;          /* 2bytes (acc: 75) */
	uint32_t C__bm_empty_offset;          /* 4bytes (acc: 79) */

	int32_t  lock;                        /* 4bytes (acc: 83) */

	int16_t  C__urgency_seg_id;           /* 2bytes (acc: 85) */
	uint32_t C__urgency_offset;           /* 4bytes (acc: 89) */

	uint32_t C__bm_parts_cnt;             /* 4bytes (acc: 93) */

	uint32_t C__n_cached;                 /* 4bytes (acc: 97) */
	uint32_t C__n_dirty;                  /* 4bytes (acc: 101) */
	uint16_t C__pctg_explicitFlush;       /* 2bytes (acc: 103) */

	int16_t  C__victimRc_seg_id;          /* 2bytes (acc: 105) */
	uint32_t C__victimRc_offset;          /* 4bytes (acc: 109) */

	int16_t  C__bm_empty_table_seg_id;    /* 2bytes (acc: 111) */
	uint32_t C__bm_empty_table_offset;    /* 4bytes (acc: 115) */

	int16_t  C__table_bm_ST_seg_id;       /* 2bytes (acc: 117) */
	uint32_t C__table_bm_ST_offset;       /* 4bytes (acc: 121) */

	uint16_t C__cache_unit_bits;          /* 2bytes (acc: 123) */

	/* HMB: Reserved area for 8KB aligned structure size */
	uint8_t  rsvd[5];                     /* 5bytes (acc: 128) */
} HmbMeta;
#pragma pack(pop)
typedef struct Hmb_FTL_MapInfo
{
	int32_t            lba_start;
	int32_t            lba_end; 
	int32_t			   entry_id; 
	
	
	HmbMappedAddr      addr;
	struct Hmb_FTL_MapInfo* prev;
 	struct Hmb_FTL_MapInfo* next;
} Hmb_FTL_MapInfo;


typedef struct HmbMapInfo
{
	HmbMappedAddr      addr;
	int32_t            entry_id;
	struct HmbMapInfo* prev;
	struct HmbMapInfo* next;
} HmbMapInfo;

/** [1] HMB: Structrue for HMB management **/
typedef struct HmbCtrl {
	void    *parent;		 /* for NvmeCtrl **/
	void    *parent_ns;		 /* for NvmeNamespace **/
	bool     enable_hmb;     /* HMB: Enable Host Memory (from dword 11) */
	bool     mem_ret;        /* HMB: Memory Return (from dword 11) */
	uint32_t size_pg;        /* HMB: Host Memory Buffer Size (from dword 12) */
	uint64_t size;           /* HMB: Total memory buffer size */
	uint32_t list_addr_l;    /* HMB: Host Memory Descriptor List Lower Address (from dword 13) */
	uint32_t list_addr_u;    /* HMB: Host Memory Descriptor List Upper Address (from dword 14) */
	uint64_t list_addr;      /* HMB: Full address of the list */
	uint32_t list_cnt;       /* HMB: Host Memory Descriptor List Entry Count (from dword 15)*/
	struct HmbEntry **list;  /* HMB entries **/

    void *dev_pci;
	uint32_t page_size;

	HmbHostAddr   meta_host;
	HmbMappedAddr meta_mapped;
	uint64_t      meta_mapped_size;

	//HmbMappedAddr segent_mapped;
	uint64_t      segent_mapped_size;

	HmbHostAddr   segent_ST_host;
	HmbMappedAddr segent_ST_mapped;
	uint64_t      segent_ST_size;

	HmbMappedAddr *segent_split_mapped;
	uint16_t      segent_split_num;
	uint32_t      segent_split_unit;

	HmbBitmap32*    segent_bm;
	uint32_t      segent_bm_parts_num;
	int32_t       segent_bm_empty;
	HmbDLL*       segent_bm_table;
	
	struct HmbMapInfo** mappings;
	
	struct Hmb_FTL_MapInfo** FTL_mappings;
	int FTL_free_mappings; 
	
	
	uint64_t            mappings_hashed_max;
	uint8_t             mappings_bits;
 
	struct HmbSeg*     segs;

	uint32_t segent_cnt;
	uint32_t segent_cnt_max;
} HmbCtrl;
/** [1] **/


#pragma pack(push, 1)    /* for avoiding automatic structure padding */
/** [1] Shared data between host and this controller */
typedef struct HmbSharedEnt
{
	uint32_t segment;		/* 4bytes (acc: 4) */
	uint32_t offset;		/* 4bytes (acc: 8) */

	uint32_t e_own;			/* 4bytes (acc: 12) */
	uint32_t e_prev;		/* 4bytes (acc: 16) */
	uint32_t e_next;		/* 4bytes (acc: 20) */

	uint64_t lpn    : 55;	/* 8bytes (acc: 28) */
	uint64_t usable :  1;  
	uint64_t dirty  :  1;  
	uint64_t urgency:  3;
	uint64_t rsvd   :  4;  
} HmbSharedEnt;
/** [1] **/
#pragma pack(pop)

#pragma pack(push, 1)    /* for avoiding automatic structure padding */
/** [1] Shared data between host and this controller */
typedef struct HmbSharedBitmapEnt
{
	HmbBitmap32 filled;
} HmbSharedBitmapEnt;
/** [1] **/
#pragma pack(pop)

#pragma pack(push, 1)    /* for avoiding automatic structure padding */
typedef struct HmbSortedEnt
{
	uint32_t e_own;
	uint32_t e_next;
	uint32_t e_prev;

	/* [1] for Write Buffer */
	uint32_t w_e_prev;
	uint32_t w_e_next;
	/* [1] */

	uint32_t r_e_prev;
	uint32_t r_e_next;
} HmbSortedEnt;
#pragma pack(pop)

typedef struct HmbSpaceMgmtCtrl
{
	uint8_t sector_size_bits;

	uint64_t cache_unit;
	uint8_t  cache_unit_bits;

	//HmbMappedAddr *table; /* Mapped address of cache meta data list */
	HmbMappedAddr **table_mapped;
	uint32_t table_cnt; /* # used entries in the cache metadata list */
	uint32_t table_cnt_max; /* # maximum entries in the cache metadata list */
	uint64_t table_size_max; /* multiple of HmbSpaceMgmtCtrl->cache_unit */

	HmbMappedAddr *table_ST_mapped;
	uint64_t      table_ST_size;

	HmbMappedAddr **table_split_mapped;
	uint16_t      table_split_num;
	uint32_t      table_split_unit;

	HmbMappedAddr *table_bm_ST_mapped;
	uint64_t      table_bm_ST_size;

	HmbMappedAddr **table_bm_split_mapped;

	//HmbMappedAddr *heads; /* original type: uint32_t */
	HmbMappedAddr* heads_ST_mapped;
	uint64_t       heads_ST_size;

	HmbMappedAddr** heads_split_mapped;
	uint16_t        heads_split_num;
	uint32_t        heads_split_unit;
	
	uint8_t  heads_hash_bit; /* for hashing: # hash bits */
	uint32_t heads_cnt;

	//HmbBitmap32 *bm;
	HmbMappedAddr *bm;
	uint32_t  bm_parts_cnt;
	// int32_t *bm_empty;
	HmbMappedAddr *bm_empty;
	// HmbDLL *bm_empty_table;
	HmbMappedAddr *bm_empty_table;

	//HmbSortedEnt *sorted;
	//HmbMappedAddr *sorted;
	HmbMappedAddr *sorted_ST_mapped;
	uint64_t sorted_ST_size;

	HmbMappedAddr **sorted_split_mapped;
	uint16_t sorted_split_num;
	uint32_t sorted_split_unit;

	HmbMappedAddr *victimAll;
	HmbMappedAddr *victimRc;

	bool inited;

	// int32_t *urgency;
	HmbMappedAddr *urgency;

	QEMUTimer *timer_WB;
} HmbSpaceMgmtCtrl;


/* It has HMB-related information. */
extern HmbCtrl HMB_CTRL;
extern HmbSpaceMgmtCtrl HMB_SPACEMGMT_CTRL;
extern int64_t HMB_SPACEMGMT_BASE_TIME;

#endif /* __SSD__HMB_TYPES_H__ */

