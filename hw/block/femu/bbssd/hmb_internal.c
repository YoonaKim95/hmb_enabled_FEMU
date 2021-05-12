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


#include "hmb_internal.h"
#include "hmb_types.h"
#include "hmb_utils.h"
#include "hmb_spaceMgmt.h" /* for HMB_HAS_NO_ENTRY in hmb_meta_init() */

#include <stdio.h>
#include <string.h>
#include <stdlib.h> /* *abs() */
#include <math.h> /* log2() */

bool hmb_is_addr_remapped; /* extern-ed in "hmb_internal.h" */

uint32_t hmb_hashing(uint64_t value, uint8_t bits)
{
	return (uint32_t)((value * HMB_HASH_MULTIPLIER) >> (64 - bits));
}

bool hmb_write(HmbHostAddr host_addr, void *dev_addr, uint64_t len)
{
	return (pci_dma_write(HMB_CTRL.dev_pci, host_addr, dev_addr, len) == 0);
}

bool hmb_read(HmbHostAddr host_addr, void *dev_addr, uint64_t len)
{
	return (pci_dma_read(HMB_CTRL.dev_pci, host_addr, dev_addr, len) == 0);
}

HmbSeg *hmb_get_seg_by_id(int32_t id)
{
	return &(HMB_CTRL.segs[id]);
}

HmbMeta *hmb_meta_get(bool for_write)
{
	if(for_write)
	{
		return (HmbMeta *)(HMB_CTRL.meta_mapped.w);
	}
	return (HmbMeta *)(HMB_CTRL.meta_mapped.r);
}

HmbSegEnt *hmb_get_segEnt_by_id(bool for_write, int32_t id)
{
	uint32_t seg, offset;

	seg = id / HMB_CTRL.segent_split_unit;
	offset = id % HMB_CTRL.segent_split_unit;

	if(for_write)
	{
		return &(((HmbSegEnt *)(HMB_CTRL.segent_split_mapped[seg].w))[offset]);
	}
	return &(((HmbSegEnt *)(HMB_CTRL.segent_split_mapped[seg].r))[offset]);
}

uint32_t hmb_segEnt_get_max_cnt(void)
{
	/* This value is already set before invoking here exceptionally */
	uint32_t cache_size = HMB_SPACEMGMT_CTRL.cache_unit;


		
	hmb_debug("cache size: %u", cache_size);
	/*
	   Equation
	   ==> #segents = sizeof(HMB) - sizeof(HmbMeta) - (#segents * sizeof(HmbSegEnt)
	 */


	// hmb_debug("seg cnt : %u", (HMB_CTRL.size - HMB_OFFSET_SEG0_METADATA) / (cache_size + sizeof(HmbSegEnt)));
	// hmb_debug("size of seg entry : %u",  sizeof(HmbSegEnt));
	hmb_debug ("segEnt max cnt: %u ", ((HMB_CTRL.size) / (HMB_CTRL.page_size)));
	return (HMB_CTRL.size) / (HMB_CTRL.page_size);
	//return (HMB_CTRL.size - HMB_OFFSET_SEG0_METADATA) / (cache_size + sizeof(HmbSegEnt));
	// return seg0_size * 0.1 / sizeof(HmbSegEnt); /* 5% of 0th segment's size  */
	//return (seg0_size * 0.1 < 32768) ? seg0_size * 0.1 : 32768; /* min(seg0's size * 10%, 64K) */
}

int32_t hmb_segEnt_get_new_id(void)
{
	uint32_t ret;

	if(hmb_segEnt_bm_get_empty(&ret) == false)
	{
		hmb_debug("Failed to fine empty space in HMB.");
		return -1;
	}

	if(hmb_segEnt_bm_set(true, ret) == false)
	{
		hmb_debug("Failed to fine empty space in HMB.");
		return -1;
	}

	return ret;
}

bool hmb_segEnt_bm_get_empty(uint32_t *val)
{
	HmbBitmap32 *bm;
	uint32_t idx_bits;
	int32_t ret;

	if((ret = *hmb_segEnt_bm_empty_get_head()) == HMB_HAS_NO_ENTRY)
	{
		//hmb_debug("The bitmap is already full!");
		return false;
	}

	bm = HMB_CTRL.segent_bm;

	idx_bits = ~(bm[ret]);
	idx_bits = (HmbBitmap32)log2(idx_bits);
	*val = (HMB_SEGENT_BITMAP_BITS_PER_PART * (ret)) + idx_bits;

	return true;
}

bool hmb_segEnt_bm_set(bool enable, uint32_t val)
{
	HmbBitmap32 *bm;
	uint32_t n_shifts, n_parts;

	bm = HMB_CTRL.segent_bm;

	n_shifts = val % HMB_SEGENT_BITMAP_BITS_PER_PART;
	n_parts = val / HMB_SEGENT_BITMAP_BITS_PER_PART;

	if(enable && (bm[n_parts] & (1 << n_shifts)))
	{
		hmb_debug("Already filled! (val: %u, n_shifts: %u, n_parts: %u)", val, n_shifts, n_parts);
		return false;
	}

	if(!enable && !(bm[n_parts] & (1 << n_shifts)))
	{
		hmb_debug("Already empty! (val: %u, n_shifts: %u, n_parts: %u)", val, n_shifts, n_parts);
		return false;
	}

	if(enable)
	{
		bm[n_parts] |= (1 << n_shifts);

		if(bm[n_parts] == HMB_SEGENT_BITMAP_PART_MAX_VALUE)
		{
			if(hmb_segEnt_bm_empty_delete(n_parts) == false)
			{
				hmb_debug("Invalid relationship.");
				return false;
			}
		}
	}

	else
	{
		if(bm[n_parts] == HMB_SEGENT_BITMAP_PART_MAX_VALUE)
		{
			if(hmb_segEnt_bm_empty_insert(n_parts) == false)
			{
				hmb_debug("Invalid relationship.");
				return false;
			}
		}
		bm[n_parts] &= ~(1 << n_shifts);
	}

	return true;
}

bool hmb_segEnt_bm_fill_overflowed(uint32_t from, uint32_t to)
{
	uint32_t i;

	for(i=from; i<to; i++)
	{
		if(hmb_segEnt_bm_set(true, i) == false)
		{
			hmb_debug("Unexpected result. Is '%u' already filled?", i);
		}
	}

	return true;
}

HmbDLL* hmb_segEnt_bm_empty_get_by_idx(uint32_t idx)
{
	return &(HMB_CTRL.segent_bm_table[idx]);
}

int32_t* hmb_segEnt_bm_empty_get_head(void)
{
	return &(HMB_CTRL.segent_bm_empty);
}

bool hmb_segEnt_bm_empty_set_head(uint32_t idx)
{
	HmbDLL *entry;
	int32_t *head;

	entry = hmb_segEnt_bm_empty_get_by_idx(idx);
	head = hmb_segEnt_bm_empty_get_head();

	if(*head != HMB_HAS_NO_ENTRY)
	{
		hmb_debug("head is already set!");
		return false;
	}

	*head = idx;
	entry->e_next = idx;
	entry->e_prev = idx;

	return true;
}

bool hmb_segEnt_bm_empty_insert(uint32_t idx)
{
	HmbDLL *head, *tail, *entry;
	uint32_t idx_head, idx_tail;
	int32_t ret;

	if((ret = *hmb_segEnt_bm_empty_get_head()) == HMB_HAS_NO_ENTRY)
	{
		return hmb_segEnt_bm_empty_set_head(idx);
	}

	entry = hmb_segEnt_bm_empty_get_by_idx(idx);

	idx_head = ret;
	head = hmb_segEnt_bm_empty_get_by_idx(idx_head);

	idx_tail = head->e_prev;
	tail = hmb_segEnt_bm_empty_get_by_idx(idx_tail);

	tail->e_next = idx;
	head->e_prev = idx;

	entry->e_next = idx_head;
	entry->e_prev = idx_tail;

	return true;

}

bool hmb_segEnt_bm_empty_delete(uint32_t idx)
{
	HmbDLL *entry, *prev, *next, *head;
	uint32_t idx_prev, idx_next, idx_head;
	int32_t ret;

	entry = hmb_segEnt_bm_empty_get_by_idx(idx);

	idx_next = entry->e_next; /* get index of the next entry of the 'entry' */
	idx_prev = entry->e_prev; /* get index of the previous entry of the 'entry' */

	prev = hmb_segEnt_bm_empty_get_by_idx(idx_prev);
	next = hmb_segEnt_bm_empty_get_by_idx(idx_next);

	prev->e_next = idx_next;
	next->e_prev = idx_prev;

	if((ret = *hmb_segEnt_bm_empty_get_head()) == HMB_HAS_NO_ENTRY)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	idx_head = ret;
	head = hmb_segEnt_bm_empty_get_by_idx(idx_head);

	if(head == entry)
	{
		int32_t *head_idx;

		head_idx = hmb_segEnt_bm_empty_get_head();

		if(idx == idx_next)
		{
			*head_idx = HMB_HAS_NO_ENTRY;
		}

		else
		{
			*head_idx = idx_next;
		}
	}

	return true;
}

HmbSplitTable *hmb_segEnt_ST_get_by_idx(bool for_write, int16_t idx)
{
	if(for_write)
	{
		return &(((HmbSplitTable *)(HMB_CTRL.segent_ST_mapped.w))[idx]);
	}
	return &(((HmbSplitTable *)(HMB_CTRL.segent_ST_mapped.r))[idx]);
}

int64_t hmb_get_max_allocable_size_byte(void)
{
	uint32_t i;
	uint64_t size_max;

	size_max = HMB_CTRL.segs[0].size_allocable_max;
	for(i=1; i<HMB_CTRL.list_cnt; i++)
	{
		if(size_max < HMB_CTRL.segs[i].size_allocable_max)
		{
			size_max = HMB_CTRL.segs[i].size_allocable_max;
		}
	}

	return size_max;
}

int64_t hmb_get_total_allocable_size_byte(void)
{
	uint32_t i;
	int64_t size_accu = 0;

	for(i=0; i<HMB_CTRL.list_cnt; i++)
	{
		size_accu += HMB_CTRL.segs[i].size_allocable_max;
	}

	return size_accu;
}

bool hmb_seg_update_max_allocable_size(HmbSeg *segment)
{
	HmbSegEmpty *target;
	int64_t max;

	if((target = hmb_segEmpty_get_head(segment)) == NULL)
	{
		//hmb_debug("Segment has no empty space!");
		segment->size_allocable_max = 0;
		return true;
	}

	max = target->offset_to - target->offset_from + 1;
	while((target = target->next) != hmb_segEmpty_get_head(segment))
	{
		int64_t gap = target->offset_to - target->offset_from + 1;

		if(gap <= 0)
		{
			hmb_debug("Invalid relationship (offset_to: %u, offset_from: %u)", \
					target->offset_to, target->offset_from);
			return false;
		}

		if(max < gap)
		{
			max = gap;
		}
	}
	segment->size_allocable_max = max;
	hmb_debug("Seg %u  New max. allocable size: %llu",segment->id,  max);

	return true;
}
///////////////////////////////////
// YA FTL_MAP_INFO
Hmb_FTL_MapInfo *hmb_FTL_mapInfo_new(void)
{
	return (Hmb_FTL_MapInfo *)calloc(1, sizeof(Hmb_FTL_MapInfo));
}

void hmb_FTL_mapInfo_free(Hmb_FTL_MapInfo *target)
{
	free(target);
}


bool hmb_FTL_mapInfo_insert(Hmb_FTL_MapInfo *target)
{
	int64_t hashed;
	Hmb_FTL_MapInfo *head;
	
	if((hashed = hmb_FTL_mapInfo_get_hashed_idx_by_obj(target)) < 0)
	{
		hmb_debug("Invalid argument");
		return false;
	}

	if((head = HMB_CTRL.FTL_mappings[hashed]) == NULL)
	{
		HMB_CTRL.FTL_mappings[hashed] = target;
		target->next = target->prev = target;
	}

	else
	{
		Hmb_FTL_MapInfo *tail = head->prev;
		
		tail->next = target;
		head->prev = target;

		target->next = head;
		target->prev = tail;
	}

	return true;
}

/** [1] Function for removing 'target' from the list **/
bool hmb_FTL_mapInfo_delete(Hmb_FTL_MapInfo *target)
{
	int64_t hashed;
	Hmb_FTL_MapInfo *head;

	if((hashed = hmb_FTL_mapInfo_get_hashed_idx_by_obj(target)) < 0)
	{
		hmb_debug("Invalid argument.");
		return false;
	}

	if((head = HMB_CTRL.FTL_mappings[hashed]) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if(target == head)
	{
		if(target == target->next) /* If the mapping list has just one entry, */
		{
			HMB_CTRL.FTL_mappings[hashed] = NULL;
			return true;
		}
		else
		{
			HMB_CTRL.FTL_mappings[hashed] = target->next;
		}
	}

	target->prev->next = target->next;
	target->next->prev = target->prev;

	return true;
}
/** [1] **/

Hmb_FTL_MapInfo *hmb_FTL_mapInfo_search(HmbMappedAddr *addr)
{
	int64_t hashed;
	Hmb_FTL_MapInfo *target;

	if((hashed = hmb_FTL_mapInfo_get_hashed_idx_by_mapped_addr(addr)) < 0)
	{
		hmb_debug("Invalid argument.");
		return NULL;
	}

	if((target = HMB_CTRL.FTL_mappings[hashed]) == NULL)
	{
		hmb_debug("It has no an entry for the mapped address.");
		return NULL;
	}
	
	do
	{
		if(target->addr.w == addr->w && target->addr.r == addr->r)
		{
			return target;
		}
	} while((target = target->next) != HMB_CTRL.FTL_mappings[hashed]);

	return NULL;
}

int64_t hmb_FTL_mapInfo_get_hashed_idx_by_obj(Hmb_FTL_MapInfo *target)
{
	return hmb_hashing((uint64_t)target->addr.r, HMB_CTRL.mappings_bits);
}

int64_t hmb_FTL_mapInfo_get_hashed_idx_by_mapped_addr(HmbMappedAddr *addr)
{
	return hmb_hashing((uint64_t)addr->r, HMB_CTRL.mappings_bits);
}
/////////////////////////////////////////////


HmbMapInfo *hmb_mapInfo_new(void)
{
	return (HmbMapInfo *)calloc(1, sizeof(HmbMapInfo));
}

void hmb_mapInfo_free(HmbMapInfo *target)
{
	free(target);
}

bool hmb_mapInfo_insert(HmbMapInfo *target)
{
	int64_t hashed;
	HmbMapInfo *head;
	
	if((hashed = hmb_mapInfo_get_hashed_idx_by_obj(target)) < 0)
	{
		hmb_debug("Invalid argument");
		return false;
	}

	if((head = HMB_CTRL.mappings[hashed]) == NULL)
	{
		HMB_CTRL.mappings[hashed] = target;
		target->next = target->prev = target;
	}

	else
	{
		HmbMapInfo *tail = head->prev;
		
		tail->next = target;
		head->prev = target;

		target->next = head;
		target->prev = tail;
	}

	return true;
}

/** [1] Function for removing 'target' from the list **/
bool hmb_mapInfo_delete(HmbMapInfo *target)
{
	int64_t hashed;
	HmbMapInfo *head;

	if((hashed = hmb_mapInfo_get_hashed_idx_by_obj(target)) < 0)
	{
		hmb_debug("Invalid argument.");
		return false;
	}

	if((head = HMB_CTRL.mappings[hashed]) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if(target == head)
	{
		if(target == target->next) /* If the mapping list has just one entry, */
		{
			HMB_CTRL.mappings[hashed] = NULL;
			return true;
		}
		else
		{
			HMB_CTRL.mappings[hashed] = target->next;
		}
	}

	target->prev->next = target->next;
	target->next->prev = target->prev;

	return true;
}
/** [1] **/

HmbMapInfo *hmb_mapInfo_search(HmbMappedAddr *addr)
{
	int64_t hashed;
	HmbMapInfo *target;

	if((hashed = hmb_mapInfo_get_hashed_idx_by_mapped_addr(addr)) < 0)
	{
		hmb_debug("Invalid argument.");
		return NULL;
	}

	if((target = HMB_CTRL.mappings[hashed]) == NULL)
	{
		hmb_debug("It has no an entry for the mapped address.");
		return NULL;
	}
	
	do
	{
		if(target->addr.w == addr->w && target->addr.r == addr->r)
		{
			return target;
		}
	} while((target = target->next) != HMB_CTRL.mappings[hashed]);

	return NULL;
}

int64_t hmb_mapInfo_get_hashed_idx_by_obj(HmbMapInfo *target)
{
	return hmb_hashing((uint64_t)target->addr.r, HMB_CTRL.mappings_bits);
}

int64_t hmb_mapInfo_get_hashed_idx_by_mapped_addr(HmbMappedAddr *addr)
{
	return hmb_hashing((uint64_t)addr->r, HMB_CTRL.mappings_bits);
}

HmbSegEmpty *hmb_segEmpty_new(void)
{
	return (HmbSegEmpty *)calloc(1, sizeof(HmbSegEmpty));
}

void hmb_segEmpty_free(HmbSegEmpty *target)
{
	free(target);
}

bool hmb_segEmpty_set_head(HmbSeg *segment, HmbSegEmpty *target)
{
	if(hmb_segEmpty_get_head(segment) == NULL)
	{
		target->segment_id = segment->id;
		target->prev = target->next = target;
	}

	segment->empty_list = target;

	return true;
}

HmbSegEmpty *hmb_segEmpty_get_head(HmbSeg *segment)
{
	return segment->empty_list;
}

bool hmb_segEmpty_insert(HmbSeg *segment, HmbSegEmpty *target)
{
	return hmb_segEmpty_insert_tail(segment, target);
}

bool hmb_segEmpty_insert_tail(HmbSeg *segment, HmbSegEmpty *target)
{
	HmbSegEmpty *head;

	if((head = hmb_segEmpty_get_head(segment)) == NULL)
	{
		return hmb_segEmpty_set_head(segment, target);
	}

	target->segment_id = segment->id;
	target->next = head;
	target->prev = head->prev;

	head->prev->next = target;
	head->prev = target;

	return true;
}

bool hmb_segEmpty_delete(HmbSeg *segment, HmbSegEmpty *target)
{
	HmbSegEmpty *head;

	if((head = hmb_segEmpty_get_head(segment)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if(target == hmb_segEmpty_get_head(segment))
	{
		if(target == target->next)
		{
			segment->empty_list = NULL;
			return true;
		}
		else
		{
			target->prev->next = target->next;
			target->next->prev = target->prev;
			return hmb_segEmpty_set_head(segment, target->next);
		}
	}

	target->prev->next = target->next;
	target->next->prev = target->prev;

	return true;
}

HmbSegEmpty *hmb_segEmpty_search(uint32_t offset)
{
	HmbSegEmpty *target;
	HmbSeg *segment;

	int16_t i;

	for(i=0; i<HMB_CTRL.list_cnt; i++)
	{
		segment = hmb_get_seg_by_id(i);

		if((target = hmb_segEmpty_get_head(segment)) == NULL)
		{
			//hmb_debug("Empty list is empty! (seg %d)", i);
			continue;
		}

		do
		{
			if(offset >= target->offset_from && offset <= target->offset_to)
			{
				return target;
			}
		} while((target = target->next) != hmb_segEmpty_get_head(segment));
	}

	return NULL;
}

HmbSegEmpty *hmb_segEmpty_search_max(void)
{
	HmbSegEmpty *target, *entry_max;
	HmbSeg *segment;
	uint64_t size_max;

	int16_t i;

	size_max = 0;
	entry_max = NULL;

	for(i=0; i<HMB_CTRL.list_cnt; i++)
	{
		segment = hmb_get_seg_by_id(i);

		if((target = hmb_segEmpty_get_head(segment)) == NULL)
		{
			hmb_debug("Empty list is empty! (seg %d)", i);
			continue;
		}

		do
		{   
			if(entry_max != NULL)
			{
				if(size_max < target->offset_to - target->offset_from + 1)
				{   
					size_max = target->offset_to - target->offset_from + 1;
					entry_max = target;
				}   
			}

			else
			{
				size_max = target->offset_to - target->offset_from + 1;
				entry_max = target;
			}
		} while((target = target->next) != hmb_segEmpty_get_head(segment));
	}

	return entry_max;
}

HmbSegEmpty *hmb_segEmpty_search_proper(uint64_t size)
{
	HmbSegEmpty *target, *entry_proper;
	HmbSeg *segment;
	int64_t gap_proper;

	int16_t i;

	entry_proper = NULL;
	gap_proper = -1;

	for(i=0; i<HMB_CTRL.list_cnt; i++)
	{
		segment = hmb_get_seg_by_id(i);

		if((target = hmb_segEmpty_get_head(segment)) == NULL)
		{
			//hmb_debug("Empty list is empty! (seg %d)", i);
			continue;
		}

		do
		{   
			int64_t gap_new;
		   
			if(size > target->offset_to - target->offset_from + 1)
			{
				continue;
			}

			gap_new = llabs((target->offset_to - target->offset_from + 1) - size);

			if(gap_proper >= 0)
			{
				if(gap_proper > gap_new)
				{   
					gap_proper = gap_new;
					entry_proper = target;
				}   
			}
			else
			{
				gap_proper = gap_new;
				entry_proper = target;
			}

			if(gap_proper == 0)
			{
				return entry_proper;
			}
		} while((target = target->next) != hmb_segEmpty_get_head(segment));
	}

	return entry_proper;
}

bool hmb_segEmpty_filling(HmbSeg *segment, HmbSegEmpty *target, uint32_t size)
{
	uint32_t size_total;

	if((size_total = target->offset_to - target->offset_from + 1) < size)
	{
		hmb_debug("Invalid request.");
		return false;
	}

	if(size_total == size)
	{
		if(hmb_segEmpty_delete(segment, target) == false)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}

		hmb_segEmpty_free(target);
	}

	else 
	{
		target->offset_from += size;
	}

	return true;
}

bool hmb_segEmpty_emptying(HmbSeg *segment, uint32_t offset, uint32_t size)
{
	HmbSegEmpty *i, *target;
	uint32_t offset_from, offset_to;

	offset_from = offset;
	offset_to = offset + size - 1;

	/** [1] If empty list is empty, new element will be added. **/
	if((i = hmb_segEmpty_get_head(segment)) == NULL)
	{
		if((target = hmb_segEmpty_new()) == NULL)
		{
			hmb_debug("Failed to allocate memory for empty information.");
			return false;
		}

		target->offset_from = offset_from;
		target->offset_to = offset_to;
		target->segment_id = segment->id;

		if(hmb_segEmpty_set_head(segment, target) == false)
		{
			hmb_debug("Failed to set head for empty information.");
			return false;
		}

		return true;
	}
	/** [1] **/

	/*  Can it be inserted in front of or back an empty object? */
	do
	{
		if(offset_to+1 == i->offset_from)
		{
			i->offset_from = offset_from;
			return true;
		}

		if(offset_from-1 == i->offset_to)
		{
			i->offset_to = offset_to;
			return true;
		}
	} while ((i = i->next) != hmb_segEmpty_get_head(segment));

	/** [1] Add new empty ojbect **/
	if((target = hmb_segEmpty_new()) == false)
	{
		hmb_debug("Failed to allocate memory for empty information.");
		return false;
	}
	/** [1] **/

	target->offset_from = offset_from;
	target->offset_to = offset_to;
	target->segment_id = segment->id;

	if(hmb_segEmpty_insert(segment, target) == false)
	{
		hmb_debug("Failed to add new entry for empty information.");
		return false;
	}

	return true;
}

bool hmb_meta_init(void)
{
	uint64_t size_alloc;
	/**
		Works related with METADATA
	**/
	HMB_CTRL.segent_cnt_max = hmb_segEnt_get_max_cnt();
	HMB_CTRL.segent_cnt = 0;

	HMB_CTRL.meta_host = HMB_CTRL.list[0]->addr + HMB_OFFSET_SEG0_METADATA;
	HMB_CTRL.meta_mapped_size = size_alloc = sizeof(HmbMeta);

	HMB_CTRL.meta_mapped.w = pci_dma_map(
			HMB_CTRL.dev_pci,
			HMB_CTRL.meta_host,
			&size_alloc,
			HMB_FOR_WRITE);

	if(HMB_CTRL.meta_mapped.w == NULL || HMB_CTRL.meta_mapped_size != size_alloc)
	{
		hmb_debug("Requested size: %llu, but allocated size: %llu", HMB_CTRL.meta_mapped_size, size_alloc);

		if(HMB_CTRL.meta_mapped.w != NULL)
		{
			pci_dma_unmap(HMB_CTRL.dev_pci, \
					HMB_CTRL.meta_mapped.w, \
					size_alloc, \
					HMB_FOR_WRITE, \
					size_alloc);
		}
		return false;
	}

	HMB_CTRL.meta_mapped.r = pci_dma_map(
			HMB_CTRL.dev_pci,
			HMB_CTRL.meta_host,
			&size_alloc,
			HMB_FOR_READ);

	if(HMB_CTRL.meta_mapped.r == NULL || HMB_CTRL.meta_mapped_size != size_alloc)
	{
		hmb_debug("Failed to map HMB area for metadata.");
		pci_dma_unmap(HMB_CTRL.dev_pci, \
				HMB_CTRL.meta_mapped.w, \
				size_alloc, \
				HMB_FOR_WRITE, \
				size_alloc);
		
		if(HMB_CTRL.meta_mapped.r != NULL)
		{
			pci_dma_unmap(HMB_CTRL.dev_pci, \
					HMB_CTRL.meta_mapped.r, \
					size_alloc, \
					HMB_FOR_READ, \
					size_alloc);
		}
		return false;
	}

	hmb_meta_get(true)->HMB__SE_num_max = HMB_CTRL.segent_cnt_max;
	hmb_meta_get(true)->C__table_ST_seg_id = -1; /* Firstly, unused */

	hmb_meta_get(true)->lock = 0; /* Initial lock */
	hmb_meta_get(true)->C__n_cached = 0;
	hmb_meta_get(true)->C__n_dirty = 0;

	return true;
}

bool hmb_segEnt_init(void)
{
	int i;
	uint64_t size_alloc, dbg_cnt_acc = 0;

	// HMB_CTRL.segent_mapped_size = sizeof(HmbSegEnt) * HMB_CTRL.segent_cnt_max;
	HMB_CTRL.segent_mapped_size = sizeof(HmbSegEnt) * ((HMB_CTRL.size ) / ( sizeof(HmbSegEnt)));
	HMB_CTRL.segent_split_num = HMB_CTRL.list_cnt;
	if(HMB_CTRL.segent_cnt_max % HMB_CTRL.segent_split_num)
	{
		HMB_CTRL.segent_split_unit = HMB_CTRL.segent_cnt_max / HMB_CTRL.segent_split_num + 1;
	}
	else
	{
		HMB_CTRL.segent_split_unit = HMB_CTRL.segent_cnt_max / HMB_CTRL.segent_split_num;
	}

	hmb_debug("#### HmbSegEnt's Split Table was initialized! ####");
	hmb_debug("  - segent_cnt_max: %u", HMB_CTRL.segent_cnt_max);
	hmb_debug("  - segent_split_num : %u", HMB_CTRL.segent_split_num);
	hmb_debug("  - segent_split_unit: %u", HMB_CTRL.segent_split_unit);

	HMB_CTRL.segent_ST_host = HMB_CTRL.list[0]->addr + HMB_OFFSET_SEG0_SEGENT_SPLIT_TABLE;
	HMB_CTRL.segent_ST_size = HMB_CTRL.segent_split_num * sizeof(HmbSplitTable);

	size_alloc = HMB_CTRL.segent_ST_size;


	hmb_debug("  - segent_ST_size: %u", size_alloc);
	
	HMB_CTRL.segent_ST_mapped.r = (HmbSegEnt *)pci_dma_map( \
			HMB_CTRL.dev_pci, \
			HMB_CTRL.segent_ST_host, \
			&size_alloc, \
			HMB_FOR_READ);
	if(HMB_CTRL.segent_ST_mapped.r == NULL || \
			size_alloc != HMB_CTRL.segent_ST_size)
	{
		hmb_debug("Failed to map host's pci address");
		if(size_alloc != HMB_CTRL.segent_ST_size)
		{
			hmb_debug("  - requested: %luB(%lfMB), allocated: %luB(%lfMB)", \
				   HMB_CTRL.segent_ST_size, ((double)HMB_CTRL.segent_ST_size)/1024/1024, \
				   size_alloc, ((double)size_alloc)/1024/1024);
		}
		else
		{
			hmb_debug("  - Returned address  is NULL!");
		}
		return false;
	}

	size_alloc = HMB_CTRL.segent_ST_size;
	HMB_CTRL.segent_ST_mapped.w = (HmbSegEnt *)pci_dma_map( \
			HMB_CTRL.dev_pci, \
			HMB_CTRL.segent_ST_host, \
			&size_alloc, \
			HMB_FOR_WRITE);
	if(HMB_CTRL.segent_ST_mapped.w == NULL || \
			size_alloc != HMB_CTRL.segent_ST_size)
	{
		hmb_debug("Failed to map host's pci address");
		if(size_alloc != HMB_CTRL.segent_ST_size)
		{
			hmb_debug("  - requested: %luB(%lfMB), allocated: %luB(%lfMB)", \
				   HMB_CTRL.segent_ST_size, ((double)HMB_CTRL.segent_ST_size)/1024/1024, \
				   size_alloc, ((double)size_alloc)/1024/1024);
		}
		else
		{
			hmb_debug("  - Returned address  is NULL!");
		}
		return false;
	}

	hmb_meta_get(true)->HMB__SE_ST_num = HMB_CTRL.segent_split_num;
	hmb_meta_get(true)->HMB__SE_ST_unit = HMB_CTRL.segent_split_unit;
	hmb_meta_get(true)->HMB__SE_ST_seg_id = 0;
	hmb_meta_get(true)->HMB__SE_ST_offset = HMB_OFFSET_SEG0_SEGENT_SPLIT_TABLE;

	HMB_CTRL.segent_split_mapped = (HmbMappedAddr *)calloc( \
			HMB_CTRL.segent_split_num, sizeof(HmbMappedAddr));
	if(HMB_CTRL.segent_split_mapped == NULL)
	{
		hmb_debug("Failed to allocate memory for mapping address");
		return false;
	}

	for(i=0; i<HMB_CTRL.segent_split_num; i++)
	{
		HmbSplitTable *entry_w;
		uint64_t addr_host;
		uint64_t size, dbg_cnt;

		if(i == 0)
		{
			addr_host = HMB_CTRL.segent_ST_host + HMB_CTRL.segent_ST_size;
		}
		else
		{
			addr_host = HMB_CTRL.list[i]->addr;
		}

		if(i != HMB_CTRL.segent_split_num-1)
		{
			size = HMB_CTRL.segent_split_unit;
			dbg_cnt = size;
			size *= sizeof(HmbSegEnt);
		}
		else
		{
			size = HMB_CTRL.segent_cnt_max % HMB_CTRL.segent_split_unit;
			if(size == 0)
			{
				size = HMB_CTRL.segent_split_unit;
			}
			dbg_cnt = size;
			size *= sizeof(HmbSegEnt);
		}

		dbg_cnt_acc += dbg_cnt;
		hmb_debug("  - [%2d] Host address: 0x%16llX, Mapping size: %lu (%lu x %dB) ", i, addr_host, size, dbg_cnt, sizeof(HmbSegEnt));

		size_alloc = size;
		HMB_CTRL.segent_split_mapped[i].r = (HmbSegEnt *)pci_dma_map( \
				HMB_CTRL.dev_pci, \
				addr_host, \
				&size_alloc, \
				HMB_FOR_READ);
		if(HMB_CTRL.segent_split_mapped[i].r == NULL || size_alloc != size)
		{
			hmb_debug("Failed to map host's pci address. (i: %d, addr_host: 0x%llX, unit: %u)", i, addr_host, HMB_CTRL.segent_split_unit);
			hmb_debug("  - requested: %luB(%lfMB), allocated: %luB(%lfMB)", \
					size, ((double)size)/1024/1024, \
					size_alloc, ((double)size_alloc)/1024/1024);
			if(HMB_CTRL.segent_split_mapped[i].r == NULL)
			{
				hmb_debug("  - Returned address  is NULL!");
			}
			return false;
		}

		size_alloc = size;
		HMB_CTRL.segent_split_mapped[i].w = (HmbSegEnt *)pci_dma_map( \
				HMB_CTRL.dev_pci, \
				addr_host, \
				&size_alloc, \
				HMB_FOR_WRITE);
		if(HMB_CTRL.segent_split_mapped[i].w == NULL || size_alloc != size)
		{
			hmb_debug("Failed to map host's pci address.");
			if(size_alloc != size)
			{
				hmb_debug("  - requested: %luB(%lfMB), allocated: %luB(%lfMB)", \
						size, ((double)size)/1024/1024, \
						size_alloc, ((double)size_alloc)/1024/1024);
			}
			else
			{
				hmb_debug("  - Returned address  is NULL!");
			}
			return false;
		}

		memset(HMB_CTRL.segent_split_mapped[i].w, 0, size_alloc);
		entry_w = hmb_segEnt_ST_get_by_idx(true, i);

		entry_w->seg_id = i;
		entry_w->offset = (uint32_t)(addr_host - HMB_CTRL.list[i]->addr);
		//hmb_debug("  - [%2d] <in SplitTable> Segment id: %3d, Offset: %u", i, i, (uint32_t)(addr_host - HMB_CTRL.list[i]->addr));
	}
	hmb_debug("##################################################");

	if(dbg_cnt_acc != HMB_CTRL.segent_cnt_max)
	{
		hmb_debug("dbg_cnt_acc: %lu, HMB_CTRL.segent_cnt_max: %u", dbg_cnt_acc, HMB_CTRL.segent_cnt_max);
		return false;
	}

	for(i=0; i<HMB_CTRL.segent_cnt_max; i++)
	{
		HmbSegEnt *ent_w;

		ent_w = hmb_get_segEnt_by_id(true, i);
		ent_w->id = HMB_SEGENT_UNUSED;
	}
	/** [1] **/

	HMB_CTRL.segent_bm_parts_num = HMB_CTRL.segent_cnt_max / HMB_SEGENT_BITMAP_BITS_PER_PART;
	hmb_debug("segent bm parts num: %u ", HMB_CTRL.segent_bm_parts_num);

	if(HMB_CTRL.segent_cnt_max % HMB_SEGENT_BITMAP_BITS_PER_PART)
	{
		HMB_CTRL.segent_bm_parts_num++;
	}

	HMB_CTRL.segent_bm = (HmbBitmap32 *)calloc(HMB_CTRL.segent_bm_parts_num, sizeof(HmbBitmap32));
	if(HMB_CTRL.segent_bm == NULL)
	{
		hmb_debug("Failed to allocate memory for bitmap of Segment Entries.");
		return false;
	}

	HMB_CTRL.segent_bm_table = (HmbDLL *)calloc(HMB_CTRL.segent_bm_parts_num, sizeof(HmbDLL));
	if(HMB_CTRL.segent_bm_table == NULL)
	{
		hmb_debug("Failed to allocate memory for bitmap empty table for Segment Entries.");
		return false;
	}

	HMB_CTRL.segent_bm_empty = HMB_HAS_NO_ENTRY;
	for(i=0; i<HMB_CTRL.segent_bm_parts_num; i++)
	{
		if(hmb_segEnt_bm_empty_insert(i) == false)
		{
			hmb_debug("Failed to initialize bitmap empty list for Segment Entries.");
			return false;
		}
	}

	if(hmb_segEnt_bm_fill_overflowed(HMB_CTRL.segent_cnt_max, \
				HMB_CTRL.segent_bm_parts_num * HMB_SEGENT_BITMAP_BITS_PER_PART) == false)
	{
		hmb_debug("Failed to fill overflowed bitmap area for Segment Entries.");
		return false;
	}

//#if 0
	hmb_debug("###### [INIT] \"Segment entry\" ######");
	hmb_debug("  - Host addr      : 0x%llX ~ 0x%llX", HMB_CTRL.segent_ST_host, HMB_CTRL.segent_ST_host + HMB_CTRL.segent_mapped_size -1);
	hmb_debug("  - Mapped size    : %llu", HMB_CTRL.segent_mapped_size);
	hmb_debug("  - #total entries : %u", HMB_CTRL.segent_cnt_max);
	hmb_debug("  - #bitmap parts  : %u", HMB_CTRL.segent_bm_parts_num);
	hmb_debug("######################################");
//#endif

	return true;
}

bool hmb_seg_init(void)
{
	int16_t i;

	if((HMB_CTRL.segs = (HmbSeg *)calloc(HMB_CTRL.list_cnt, sizeof(HmbSeg))) == NULL)
	{
		hmb_debug("Failed to allocate memory for segment-related data.");
		return false;
	}

	hmb_debug("### [INIT] \"Segment\"");
	for(i=0; i<HMB_CTRL.list_cnt; i++)
	{
		HmbSeg *segment;

		if((segment = hmb_get_seg_by_id(i)) == NULL)
		{
			hmb_debug("Failed to get pointer for the %d segment.", i);
			return false;
		}

		segment->id = i;
		segment->host_addr = HMB_CTRL.list[i]->addr;
		segment->size_max = HMB_CTRL.list[i]->size * HMB_CTRL.page_size;
		if(hmb_seg_init_empty(segment) == false)
		{
			hmb_debug("Failed to initialize segment empty list.");
			return false;
		}
		hmb_debug("  - ID: %2d, Host addr: 0x%10llX, max. size: %llu, max. allocable size: %lu", \
				segment->id, segment->host_addr, segment->size_max, segment->size_allocable_max);
	}
	hmb_debug("");

	return true;
}

bool hmb_seg_init_empty(HmbSeg *segment)
{
	HmbSegEmpty *target;

	if(segment == NULL)
	{
		hmb_debug("Invalid argument.");
		return false;
	}

	if((target = hmb_segEmpty_new()) == NULL)
	{
		hmb_debug("Failed to allocate memory for segment empty.");
		return false;
	}

	if(segment->id == 0)
	{
		target->offset_from = HMB_CTRL.meta_mapped_size + \
							  HMB_CTRL.segent_ST_size + \
							  (HMB_CTRL.segent_split_unit * sizeof(HmbSegEnt));
	}

	else if(segment->id == HMB_CTRL.list_cnt-1)
	{
		target->offset_from = (HMB_CTRL.segent_cnt_max % HMB_CTRL.segent_split_unit) * sizeof(HmbSegEnt);
	}

	else
	{
		target->offset_from = HMB_CTRL.segent_split_unit * sizeof(HmbSegEnt);
	}
	target->offset_to = segment->size_max-1;
	
	if((target->segment_id = segment->id) == HMB_SEGENT_UNUSED)
	{
		hmb_debug("Invalid relationship!");
		return false;
	}

	if(hmb_segEmpty_set_head(segment, target) == false)
	{
		hmb_debug("Failed to set head pointer for the empty list.");
		return false;
	}

	if(hmb_seg_update_max_allocable_size(segment) == false)
	{
		hmb_debug("Failed to update maximum allocable empty size.");
		return false;
	}

	return true;
}

bool hmb_mapInfo_init(uint64_t n)
{
	uint64_t i;

	HMB_CTRL.mappings_bits = log2(n);
	HMB_CTRL.mappings_hashed_max = pow(2, HMB_CTRL.mappings_bits) - 1;

	// YA 
	if((HMB_CTRL.FTL_mappings = (Hmb_FTL_MapInfo **)calloc( \
			HMB_CTRL.mappings_hashed_max+1, sizeof(Hmb_FTL_MapInfo *))) == NULL)
	{
		hmb_debug("Failed to allocate heap memory for \"HmbMapInfo\"");
		return false;
	}

	for(i=0; i<=HMB_CTRL.mappings_hashed_max; i++)
	{
		HMB_CTRL.FTL_mappings[i] = NULL;
	}

	HMB_CTRL.FTL_free_mappings = i;


	////
	if((HMB_CTRL.mappings = (HmbMapInfo **)calloc( \
			HMB_CTRL.mappings_hashed_max+1, sizeof(HmbMapInfo *))) == NULL)
	{
		hmb_debug("Failed to allocate heap memory for \"HmbMapInfo\"");
		return false;
	}

	for(i=0; i<=HMB_CTRL.mappings_hashed_max; i++)
	{
		HMB_CTRL.mappings[i] = NULL;
	}

	hmb_debug("mappings hased max: %u ", i);
	hmb_debug("Succeeded to allocate heap memory for \"HmbMapInfo\" and initilize");

	return true;
}

