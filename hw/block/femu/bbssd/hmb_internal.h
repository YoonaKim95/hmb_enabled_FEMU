/**
   #Group:  SSLab <sswlab.kw.ac.kr>
   #Author: Kyusik Kim <kks@kw.ac.kr> and Taeseok Kim <tskim@kw.ac.kr>

   #Project Name: HMB-supported DRAM-less SSD Simulator
   #Module Name: HMB Management
   #File Name: hmb.h

   #Version: v0.1
   #Last Modified: April 9, 2018

   #Description:
     Functions, definitions and structures for managing HMB

     (1) for supporting HMB features of the NVMe 
       --> struct HmbEntry, ...
 	 (2) for managing and allocating HMB space
       --> hmb_malloc(), hmb_calloc() and hmb_free()
	   --> hmb_map_*(), hmb_segment_*(), ...
**/

/**
    #Revision History
	  v0.1
	    - First draft
**/


#ifndef __SSD__HMB_INTERNAL_H__
#define __SSD__HMB_INTERNAL_H__

#include "hmb_types.h"
#include "hmb_debug.h"

#include "qemu/osdep.h"
#include "hw/pci/pci.h"

#include <stdint.h>
#include <stdbool.h>

/* HMB: Offset for HMB METADATA in the first segment */
#define HMB_OFFSET_SEG0_METADATA     (0)

#define HMB_OFFSET_SEG0_SEGENT_SPLIT_TABLE (HMB_OFFSET_SEG0_METADATA + sizeof(HmbMeta))

/*
	HMB: If an segment entry is not used,
	 - value of segment id in the structure for the entry is -1
*/
#define HMB_SEGENT_UNUSED (-1)

#define HMB_MAPINFO_NOT_MAPPED (NULL)

#define HMB_SEGENT_BITMAP_BITS_PER_PART (8 * sizeof(HmbBitmap32))
#define HMB_SEGENT_BITMAP_PART_MAX_VALUE (0xFFFFFFFF)


extern bool hmb_is_addr_remapped;

#define HMB_SPACEMGMT_HASH_GOLDEN_RATIO_64 0x61C8864680B583EBull
#define HMB_HASH_MULTIPLIER HMB_SPACEMGMT_HASH_GOLDEN_RATIO_64
uint32_t hmb_hashing(uint64_t value, uint8_t bits);	// **

/** [1] HMB: Direct read/write functions from/into HMB (i.e. copying through PCI) **/
bool hmb_write(HmbHostAddr host_addr, void *dev_addr, uint64_t len);
bool hmb_read(HmbHostAddr host_addr, void *dev_addr, uint64_t len);
/** [1] **/


HmbSeg *hmb_get_seg_by_id(int32_t id); // **
HmbMeta *hmb_meta_get(bool for_write); // **

/*
	HMB: Calculate the number of maximum segment entries
      = min ( 10% of 1st segment's size,
        limitation of variable for the ID (i.e. 32768))
      --> FIXME: is it right description?
*/
HmbSegEnt* hmb_get_segEnt_by_id(bool for_write, int32_t id); // **
uint32_t    hmb_segEnt_get_max_cnt(void);
int32_t    hmb_segEnt_get_new_id(void); /* Get new entry ID */

bool hmb_segEnt_bm_get_empty       (uint32_t *val); // *
bool hmb_segEnt_bm_set             (bool enable, uint32_t val); // *
bool hmb_segEnt_bm_fill_overflowed (uint32_t from, uint32_t to);

HmbDLL*  hmb_segEnt_bm_empty_get_by_idx (uint32_t idx);
int32_t* hmb_segEnt_bm_empty_get_head   (void);
bool     hmb_segEnt_bm_empty_set_head   (uint32_t idx);
bool     hmb_segEnt_bm_empty_insert     (uint32_t idx);
bool     hmb_segEnt_bm_empty_delete     (uint32_t idx);

HmbSplitTable* hmb_segEnt_ST_get_by_idx(bool for_write, int16_t idx);

/*
	HMB: Calculate maximum byte size of allocable HMB 
      = max (HmbSeg->size_allocable_max)
*/
int64_t hmb_get_max_allocable_size_byte(void);

/*
	HMB: Calculate total byte size of allocable HMB 
      = sum (HmbSeg->size_allocable_max)
*/
int64_t hmb_get_total_allocable_size_byte(void);
bool hmb_seg_update_max_allocable_size(HmbSeg *segment);


/** HMB: [1] Mapping information related functions **/
HmbMapInfo *hmb_mapInfo_new(void);
void hmb_mapInfo_free(HmbMapInfo *target);

/* HMB: Insert 'target' to the list (default action: to tail) */
bool hmb_mapInfo_insert(HmbMapInfo *target); 
/* HMB: Insert 'target' to the tail */
bool hmb_mapInfo_insert_tail(HmbMapInfo *target); 
/* HMB: Remove 'target' from the list */
bool hmb_mapInfo_delete(HmbMapInfo *target); 
/* HMB: Search a target by mapped address*/
HmbMapInfo *hmb_mapInfo_search(HmbMappedAddr *addr); 

int64_t hmb_mapInfo_get_hashed_idx_by_obj(HmbMapInfo *target);
int64_t hmb_mapInfo_get_hashed_idx_by_mapped_addr(HmbMappedAddr *addr);
/** [1] **/


/** [1] HMB: Segment empty related functions **/
HmbSegEmpty *hmb_segEmpty_new(void);
void hmb_segEmpty_free(HmbSegEmpty *target);
/*
	HMB: Set 'target' to head (i.e. segment->empty_list)
      --> And set structure relationship
*/
bool hmb_segEmpty_set_head(HmbSeg *segment, HmbSegEmpty *target); 
/* HMB: Get pointer for the head (i.e. segment->empty_list) */
HmbSegEmpty *hmb_segEmpty_get_head(HmbSeg *segment); 
/* HMB: Insert 'target' to the list (default action: to tail) */
bool hmb_segEmpty_insert(HmbSeg *segment, HmbSegEmpty *target); 
/* HMB: Insert 'target' to the tail */
bool hmb_segEmpty_insert_tail(HmbSeg *segment, HmbSegEmpty *target);
/* HMB: Remove 'target' from the list */ 
bool hmb_segEmpty_delete(HmbSeg *segment, HmbSegEmpty *target); 
/* HMB: Search a target */
HmbSegEmpty *hmb_segEmpty_search(uint32_t offset); 
/* HMB: Search a target that has maximum space */
HmbSegEmpty *hmb_segEmpty_search_max(void); 
/* HMB: Search a target that has proper space */
HmbSegEmpty *hmb_segEmpty_search_proper(uint64_t size); 

/** [2] HMB: After allocating HMB space, these functions are called. **/
bool hmb_segEmpty_filling(HmbSeg *segment, HmbSegEmpty *target, uint32_t size);
bool hmb_segEmpty_emptying(HmbSeg *segment, uint32_t offset, uint32_t size);
/** [2] **/
/** [1] **/

/** [1] HMB: Initialization **/
bool hmb_meta_init(void);
bool hmb_segEnt_init(void);
bool hmb_seg_init(void);
bool hmb_seg_init_empty(HmbSeg *segment);
bool hmb_mapInfo_init(uint64_t n);
/** [1] **/

#endif /* __SSD__HMB_INTERNAL_H__ */

