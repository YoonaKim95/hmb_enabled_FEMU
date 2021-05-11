/**
   #Group:  SSLab <sswlab.kw.ac.kr>
   #Author: Kyusik Kim <kks@kw.ac.kr> and Taeseok Kim <tskim@kw.ac.kr>

   #Project Name: HMB-supported DRAM-less SSD Simulator
   #Module Name: Read-ahead Cache
   #File Name: m_cache.h

   #Version: v0.1
   #Last Modified: April 9, 2018

   #Description:
     Functions, definitions and structures for read-ahead Cache

     (1) for caching contents 
       --> hmb_spaceMgmt_RC_caching()
     (2) for supporting hash table
	   --> hmb_spaceMgmt_*_hash*(), ...
 	 (3) for managing the cache
       --> hmb_spaceMgmt_*() excepts for contents mentioned in '(1)' and '(2)
**/

/**
    #Revision History
	  v0.1
	    - First draft
**/

#include "hmb_spaceMgmt.h"
#include "hmb.h"
#include "hmb_internal.h"
#include "hmb_utils.h"
#include "hmb_types.h"

#include "hw/block/block.h"
#include "sysemu/block-backend.h"
#include "../nvme.h" /* NVME_NO_COMPLETE */
#include "qemu/atomic.h"
//#include "ssd.h" /* SSD_WRITE */

#include <stdlib.h>
#include <string.h> /* memcpy() */
#include <limits.h> /* for caculate maximum size of a variable */
#include <math.h> /* sqrt() and log2() */

HmbSpaceMgmtCtrl HMB_SPACEMGMT_CTRL;
int64_t HMB_SPACEMGMT_BASE_TIME = HMB_HAS_NO_ENTRY;

const uint32_t HMB_SPACEMGMT_UL_WEIGHT[] = {4, 3, 2, 1};

void hmb_spaceMgmt_heap_rw(bool is_write, uint64_t offset, uint64_t len, void *buf)
{
	/*
	void *src, *dest;
	NvmeCtrl *n = (struct NvmeCtrl*)HMB_CTRL.parent;

	if(is_write)
	{
		src  = buf;
		dest = n->heap_storage + offset;
	}

	else //  i.e. if(!is_write) 
	{
		src  = n->heap_storage + offset;
		dest = buf;
	}

	memcpy(dest, src, len); */
	/*hmb_debug("Direct I/O to/from heap storage was requested and completed.");*/
}

int32_t *hmb_spaceMgmt_victimAll_get(bool for_write)
{
	if(for_write)
	{
		return (int32_t *)(HMB_SPACEMGMT_CTRL.victimAll->w);
	}

	return (int32_t *)(HMB_SPACEMGMT_CTRL.victimAll->r);
}

int32_t *hmb_spaceMgmt_victimRc_get(bool for_write)
{
	if(for_write)
	{
		return (int32_t *)(HMB_SPACEMGMT_CTRL.victimRc->w);
	}

	return (int32_t *)(HMB_SPACEMGMT_CTRL.victimRc->r);
}

int32_t hmb_spaceMgmt_RC_sorted_get_head_idx(void)
{
	return *(hmb_spaceMgmt_victimAll_get(false));
}

bool hmb_spaceMgmt_RC_sorted_set_head(uint32_t idx)
{
	int32_t ret;
	
	ret = hmb_spaceMgmt_RC_sorted_get_head_idx();
	if(ret == HMB_HAS_NO_ENTRY)
	{
		HmbSortedEnt *entry_w;
		
		entry_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx);

		entry_w->e_own  = idx;
		entry_w->e_next = idx;
		entry_w->e_prev = idx;
	}

	*(hmb_spaceMgmt_victimAll_get(true)) = idx;

	return true;
}

bool hmb_spaceMgmt_RC_sorted_insert_tail(uint32_t idx)
{
	HmbSortedEnt *entry_w, *head_r, *head_w, *tail_w;
	uint32_t idx_head, idx_tail;
	int32_t ret;

	ret = hmb_spaceMgmt_RC_sorted_get_head_idx();
	if(ret == HMB_HAS_NO_ENTRY)
	{
		return hmb_spaceMgmt_RC_sorted_set_head(idx);
	}
	
	idx_head = (uint32_t)ret;

	head_r = hmb_spaceMgmt_sorted_get_by_idx(false, idx_head);
	head_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_head);

	idx_tail = head_r->e_prev;
	tail_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_tail);

	entry_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx);

	/* [1] Set the 'entry' to most recently used entry */
	tail_w->e_next = idx;
	head_w->e_prev = idx;

	entry_w->e_next = idx_head;
	entry_w->e_prev = idx_tail;

	return true;
}

bool hmb_spaceMgmt_RC_sorted_insert_after(uint32_t idx, uint32_t idx_after)
{
	HmbSortedEnt *entry_w, *after_r, *after_w, *after_next_w;
	uint32_t idx_after_next;
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	int32_t ret;

	ret = hmb_spaceMgmt_RC_sorted_get_head_idx();

	if(ret == HMB_HAS_NO_ENTRY)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
#endif

	/* "entry" will be inserted between "after" and "after_next" */
	after_r = hmb_spaceMgmt_sorted_get_by_idx(false, idx_after);
	after_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_after);
	entry_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx);

	idx_after_next = after_r->e_next;
	after_next_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_after_next);

	after_w->e_next = idx;
	after_next_w->e_prev = idx;

	entry_w->e_next = idx_after_next;
	entry_w->e_prev = idx_after;

	return true;
}

bool hmb_spaceMgmt_RC_sorted_delete(uint32_t idx)
{
	HmbSortedEnt *entry_r, *prev_w, *next_w;
	uint32_t idx_prev, idx_next;

	entry_r = hmb_spaceMgmt_sorted_get_by_idx(false, idx);
	idx_next = entry_r->e_next; /* get index of the next entry of the 'entry' */
	idx_prev = entry_r->e_prev; /* get index of the previous entry of the 'entry' */

	prev_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_prev);
	next_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_next);

	prev_w->e_next = idx_next;
	next_w->e_prev = idx_prev;

	if(idx_next == idx)
	{
		*hmb_spaceMgmt_victimAll_get(true) = HMB_HAS_NO_ENTRY;
	}

	else if(hmb_spaceMgmt_RC_sorted_get_head_idx() == idx)
	{
		return hmb_spaceMgmt_RC_sorted_set_head(idx_next);
	}

	return true;
}

int32_t hmb_spaceMgmt_RCOnly_sorted_get_head_idx(void)
{
	return *(hmb_spaceMgmt_victimRc_get(false));
}

bool hmb_spaceMgmt_RCOnly_sorted_set_head(uint32_t idx)
{
	int32_t ret;
	
	ret = hmb_spaceMgmt_RCOnly_sorted_get_head_idx();
	if(ret == HMB_HAS_NO_ENTRY)
	{
		HmbSortedEnt *entry_w;
		
		entry_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx);

		entry_w->e_own  = idx;
		entry_w->r_e_next = idx;
		entry_w->r_e_prev = idx;
	}

	*(hmb_spaceMgmt_victimRc_get(true)) = idx;

	return true;
}

bool hmb_spaceMgmt_RCOnly_sorted_insert_tail(uint32_t idx)
{
	HmbSortedEnt *entry_w, *head_r, *head_w, *tail_w;
	uint32_t idx_head, idx_tail;
	int32_t ret;

	ret = hmb_spaceMgmt_RCOnly_sorted_get_head_idx();
	if(ret == HMB_HAS_NO_ENTRY)
	{
		return hmb_spaceMgmt_RCOnly_sorted_set_head(idx);
	}
	
	idx_head = (uint32_t)ret;

	head_r = hmb_spaceMgmt_sorted_get_by_idx(false, idx_head);
	head_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_head);

	idx_tail = head_r->r_e_prev;
	tail_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_tail);

	entry_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx);

	/* [1] Set the 'entry' to most recently used entry */
	tail_w->r_e_next = idx;
	head_w->r_e_prev = idx;

	entry_w->r_e_next = idx_head;
	entry_w->r_e_prev = idx_tail;

	return true;
}

bool hmb_spaceMgmt_RCOnly_sorted_delete(uint32_t idx)
{
	HmbSortedEnt *entry_r, *prev_w, *next_w;
	uint32_t idx_prev, idx_next;

	entry_r = hmb_spaceMgmt_sorted_get_by_idx(false, idx);
	idx_next = entry_r->r_e_next; /* get index of the next entry of the 'entry' */
	idx_prev = entry_r->r_e_prev; /* get index of the previous entry of the 'entry' */

	prev_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_prev);
	next_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_next);

	prev_w->r_e_next = idx_next;
	next_w->r_e_prev = idx_prev;

	if(idx_next == idx)
	{
		*hmb_spaceMgmt_victimRc_get(true) = HMB_HAS_NO_ENTRY;
	}

	else if(hmb_spaceMgmt_RCOnly_sorted_get_head_idx() == idx)
	{
		return hmb_spaceMgmt_RCOnly_sorted_set_head(idx_next);
	}

	return true;
}

int32_t hmb_spaceMgmt_WB_sorted_get_head_idx(int32_t urgency)
{
	return *(hmb_spaceMgmt_WB_sorted_get_head(false, urgency));
}

int32_t* hmb_spaceMgmt_WB_sorted_get_head(bool for_write, int32_t urgency)
{
	if(for_write)
	{
		return (&(((int32_t *)(HMB_SPACEMGMT_CTRL.urgency->w))[urgency-1]));
	}
	return (&(((int32_t *)(HMB_SPACEMGMT_CTRL.urgency->r))[urgency-1]));
}

bool hmb_spaceMgmt_WB_sorted_set_head(int32_t urgency, uint32_t idx)
{
	int32_t ret;

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(!(urgency >= HMB_SPACEMGMT_UL_URGENT && urgency <= HMB_SPACEMGMT_UL_LOW))
	{
		hmb_debug("Out of range (urgency: %d)", urgency);
		return false;
	}
#endif
	
	ret = hmb_spaceMgmt_WB_sorted_get_head_idx(urgency);
	if(ret == HMB_HAS_NO_ENTRY)
	{
		HmbSortedEnt *entry_w;
		
		entry_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx);

		entry_w->w_e_next = idx;
		entry_w->w_e_prev = idx;
	}

	*(hmb_spaceMgmt_WB_sorted_get_head(true, urgency)) = idx;

	return true;
}

bool hmb_spaceMgmt_WB_sorted_insert_tail(int32_t urgency, uint32_t idx)
{
	HmbSortedEnt *entry_w, *head_r, *head_w, *tail_w;
	int32_t idx_head, idx_tail;

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(!(urgency >= HMB_SPACEMGMT_UL_URGENT && urgency <= HMB_SPACEMGMT_UL_LOW))
	{
		hmb_debug("Out of range (urgency: %d)", urgency);
		return false;
	}
#endif

	idx_head = hmb_spaceMgmt_WB_sorted_get_head_idx(urgency);
	if(idx_head == HMB_HAS_NO_ENTRY)
	{
		return hmb_spaceMgmt_WB_sorted_set_head(urgency, idx);
	}
	
	head_r = hmb_spaceMgmt_sorted_get_by_idx(false, idx_head);
	head_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_head);

	idx_tail = head_r->w_e_prev;
	tail_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_tail);

	entry_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx);

	/* [1] Set the 'entry' to most recently used entry */
	tail_w->w_e_next = idx;
	head_w->w_e_prev = idx;

	entry_w->w_e_next = idx_head;
	entry_w->w_e_prev = idx_tail;

	return true;
}

bool hmb_spaceMgmt_WB_sorted_delete(int32_t urgency, uint32_t idx)
{
	HmbSortedEnt *entry_r, *prev_w, *next_w;
	int32_t idx_prev, idx_next;

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(!(urgency >= HMB_SPACEMGMT_UL_URGENT && urgency <= HMB_SPACEMGMT_UL_LOW))
	{
		hmb_debug("Out of range (urgency: %d)", urgency);
		return false;
	}
#endif

	entry_r = hmb_spaceMgmt_sorted_get_by_idx(false, idx);
	idx_next = entry_r->w_e_next; /* get index of the next entry of the 'entry' */
	idx_prev = entry_r->w_e_prev; /* get index of the previous entry of the 'entry' */

	prev_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_prev);
	next_w = hmb_spaceMgmt_sorted_get_by_idx(true, idx_next);

	prev_w->w_e_next = idx_next;
	next_w->w_e_prev = idx_prev;

	if(idx_next == idx)
	{
		*hmb_spaceMgmt_WB_sorted_get_head(true, urgency) = HMB_HAS_NO_ENTRY;
	}

	else if(hmb_spaceMgmt_WB_sorted_get_head_idx(urgency) == idx)
	{
		return hmb_spaceMgmt_WB_sorted_set_head(urgency, idx_next);
	}

	return true;
}

bool hmb_spaceMgmt_WB_sorted_delete_head(int32_t urgency)
{
	return hmb_spaceMgmt_WB_sorted_delete(urgency, hmb_spaceMgmt_WB_sorted_get_head_idx(urgency));
}

HmbSortedEnt *hmb_spaceMgmt_sorted_get_by_idx(bool for_write, uint32_t idx)
{
	uint32_t seg, offset;

	seg = idx / HMB_SPACEMGMT_CTRL.sorted_split_unit;
	offset = idx % HMB_SPACEMGMT_CTRL.sorted_split_unit;

	if(for_write)
	{
		return &(((HmbSortedEnt *)(HMB_SPACEMGMT_CTRL.sorted_split_mapped[seg]->w))[offset]);
	}
	return &(((HmbSortedEnt *)(HMB_SPACEMGMT_CTRL.sorted_split_mapped[seg]->r))[offset]);
}

HmbSplitTable *hmb_spaceMgmt_sorted_ST_get_by_idx(bool for_write, uint32_t idx)
{
	if(for_write)
	{    
		return &(((HmbSplitTable *)(HMB_SPACEMGMT_CTRL.sorted_ST_mapped->w))[idx]);
	}    
	return &(((HmbSplitTable *)(HMB_SPACEMGMT_CTRL.sorted_ST_mapped->r))[idx]);
}

bool hmb_spaceMgmt_bm_get_empty(uint32_t *val)
{
	HmbBitmap32 *bm_r;
	uint32_t idx_bits;

	int32_t ret;

	if((ret = *hmb_spaceMgmt_bm_empty_get_head(false)) == HMB_HAS_NO_ENTRY)
	{
		//hmb_debug("The bitmap is already full!");
		return false;
	}

	bm_r = (HmbBitmap32 *)HMB_SPACEMGMT_CTRL.bm->r;

	idx_bits = ~(bm_r[ret]);
	idx_bits = (HmbBitmap32)log2(idx_bits);
	*val = (HMB_SPACEMGMT_BITMAP_BITS_PER_PART * (ret)) + idx_bits;

#if 0
	hmb_debug("empty_r: %d, bm[*entry_r] = 0x%X, ~bm[*empty_r]: 0x%X, idx_bits: %u, val: %u", *empty_r, bm_r[*empty_r], ~(bm_r[*empty_r]), idx_bits, *val);
#endif

	return true;
}

bool hmb_spaceMgmt_bm_set(bool enable, uint32_t val)
{
	uint32_t n_shifts, n_parts;

	HmbBitmap32 *bm_r, *bm_w;

	bm_r = (HmbBitmap32 *)HMB_SPACEMGMT_CTRL.bm->r;
	bm_w = (HmbBitmap32 *)HMB_SPACEMGMT_CTRL.bm->w;

	n_shifts = val % HMB_SPACEMGMT_BITMAP_BITS_PER_PART;
	n_parts = val / HMB_SPACEMGMT_BITMAP_BITS_PER_PART;

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(enable && (bm_r[n_parts] & (1 << n_shifts)))
	{
		hmb_debug("Already filled! (val: %u, n_shifts: %u, n_parts: %u)", val, n_shifts, n_parts);
		return false;
	}

	if(!enable && !(bm_r[n_parts] & (1 << n_shifts)))
	{
		hmb_debug("Already empty! (val: %u, n_shifts: %u, n_parts: %u)", val, n_shifts, n_parts);
		return false;
	}
#endif

	if(enable)
	{
		bm_w[n_parts] |= (1 << n_shifts);

		if(bm_r[n_parts] == HMB_SPACEMGMT_BITMAP_PART_MAX_VALUE)
		{
			if(hmb_spaceMgmt_bm_empty_delete(n_parts) == false)
			{
				hmb_debug("Invalid relationship.");
				return false;
			}
		}
	}

	else
	{
		if(bm_r[n_parts] == HMB_SPACEMGMT_BITMAP_PART_MAX_VALUE)
		{
			if(hmb_spaceMgmt_bm_empty_insert(n_parts) == false)
			{
				hmb_debug("Invalid relationship.");
				return false;
			}
		}

		bm_w[n_parts] &= ~(1 << n_shifts);
	}

	return true;
}

bool hmb_spaceMgmt_bm_fill_overflowed(uint32_t from, uint32_t to)
{
	uint32_t i;

	for(i=from; i<to; i++)
	{
		if(hmb_spaceMgmt_bm_set(true, i) == false)
		{
			hmb_debug("Unexpected result. Is '%u' already filled?", i);
		}
	}

	return true;
}

HmbDLL *hmb_spaceMgmt_bm_empty_get_by_idx(bool for_write, uint32_t idx)
{
	if(for_write)
	{
		return &(((HmbDLL *)(HMB_SPACEMGMT_CTRL.bm_empty_table->w))[idx]);
	}
	return &(((HmbDLL *)(HMB_SPACEMGMT_CTRL.bm_empty_table->r))[idx]);
}

int32_t *hmb_spaceMgmt_bm_empty_get_head(bool for_write)
{
	if(for_write)
	{
		return (int32_t *)(HMB_SPACEMGMT_CTRL.bm_empty->w);
	}
	return (int32_t *)(HMB_SPACEMGMT_CTRL.bm_empty->r);
}

bool hmb_spaceMgmt_bm_empty_set_head(uint32_t idx)
{
	HmbDLL *entry_w;
	int32_t *head_w;
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	int32_t *head_r;
#endif

	entry_w = hmb_spaceMgmt_bm_empty_get_by_idx(true, idx);
	head_w = hmb_spaceMgmt_bm_empty_get_head(true);

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	head_r = hmb_spaceMgmt_bm_empty_get_head(false);
	if((*head_r) != HMB_HAS_NO_ENTRY)
	{
		hmb_debug("head is already set!");
		return false;
	}
#endif

	*head_w = idx;
	entry_w->e_next = idx;
	entry_w->e_prev = idx;

	return true;
}

/* Insert to tail */
bool hmb_spaceMgmt_bm_empty_insert(uint32_t idx) 
{
	HmbDLL *head_r, *head_w, *tail_w, *entry_w;
	uint32_t idx_head, idx_tail;
	int32_t ret;

	if((ret = *hmb_spaceMgmt_bm_empty_get_head(false)) == HMB_HAS_NO_ENTRY)
	{
		return hmb_spaceMgmt_bm_empty_set_head(idx);
	}

	entry_w = hmb_spaceMgmt_bm_empty_get_by_idx(true, idx);

	idx_head = ret;
	head_r = hmb_spaceMgmt_bm_empty_get_by_idx(false, idx_head);
	head_w = hmb_spaceMgmt_bm_empty_get_by_idx(true, idx_head);

	idx_tail = head_r->e_prev;
	tail_w = hmb_spaceMgmt_bm_empty_get_by_idx(true, idx_tail);

	tail_w->e_next = idx;
	head_w->e_prev = idx;

	entry_w->e_next = idx_head;
	entry_w->e_prev = idx_tail;

	return true;
}

/* Delete from head */
bool hmb_spaceMgmt_bm_empty_delete(uint32_t idx)
{
	HmbDLL *entry_r, *prev_w, *next_w, *head_r;
	uint32_t idx_prev, idx_next, idx_head;
	int32_t ret;

	entry_r = hmb_spaceMgmt_bm_empty_get_by_idx(false, idx);

	idx_next = entry_r->e_next; /* get index of the next entry of the 'entry' */
	idx_prev = entry_r->e_prev; /* get index of the previous entry of the 'entry' */

	prev_w = hmb_spaceMgmt_bm_empty_get_by_idx(true, idx_prev);
	next_w = hmb_spaceMgmt_bm_empty_get_by_idx(true, idx_next);

	prev_w->e_next = idx_next;
	next_w->e_prev = idx_prev;

	if((ret = *hmb_spaceMgmt_bm_empty_get_head(false)) == HMB_HAS_NO_ENTRY)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	idx_head = ret;
	head_r = hmb_spaceMgmt_bm_empty_get_by_idx(false, idx_head);

	/* [1] If requested entry to delete is a head */
	if(head_r == entry_r)
	{
		int32_t *head_idx_w;

		head_idx_w = hmb_spaceMgmt_bm_empty_get_head(true);

		if(idx == idx_next)
		{
			*head_idx_w = HMB_HAS_NO_ENTRY;
		}

		else
		{
			*head_idx_w = idx_next;
		}
	}
	/* [1] */

	return true;
}

bool hmb_spaceMgmt_hash_verify(uint32_t idx)
{
	if(idx < HMB_SPACEMGMT_CTRL.heads_cnt)
	{
		return true;
	}

	return false;
}

/* [1] FIXME: "idx" must be verified by caller.  */
HmbSharedEnt *hmb_spaceMgmt_table_get_by_idx(bool for_write, uint32_t idx)
{
	uint32_t seg, offset;

	seg = idx / HMB_SPACEMGMT_CTRL.table_split_unit;
	offset = idx % HMB_SPACEMGMT_CTRL.table_split_unit;

	if(for_write)
	{
		return &(((HmbSharedEnt *)(HMB_SPACEMGMT_CTRL.table_split_mapped[seg]->w))[offset]);
	}
	return &(((HmbSharedEnt *)(HMB_SPACEMGMT_CTRL.table_split_mapped[seg]->r))[offset]);
}

HmbSharedEnt *hmb_spaceMgmt_table_get_by_lpn(bool for_write, uint64_t lpn)
{
	HmbSharedEnt *loop, *head;
   
	/* Get appropriate "heads" */
	if((head = hmb_spaceMgmt_shared_get_head_by_lpn(false, lpn)) == NULL)
	{
		return NULL;
	}

	if(head->lpn == lpn)
	{
		if(!for_write)
		{
			return head;
		}
		return hmb_spaceMgmt_table_get_by_idx(true, head->e_own);
	}

	for(loop = hmb_spaceMgmt_table_get_by_idx(false, head->e_next); \
			loop != head; \
			loop = hmb_spaceMgmt_table_get_by_idx(false, loop->e_next))
	{
		if(loop->lpn == lpn)
		{
			if(!for_write)
			{
				return loop;
			}
			return hmb_spaceMgmt_table_get_by_idx(true, loop->e_own);
		}
	}

	return NULL;
}

 /* [1] FIXME: "idx" must be verified by caller.  */
void *hmb_spaceMgmt_mappedAddr_get_by_idx(bool for_write, uint32_t idx)
{
	if(for_write)
	{
		return HMB_SPACEMGMT_CTRL.table_mapped[idx]->w;
	}

	return HMB_SPACEMGMT_CTRL.table_mapped[idx]->r;
}

void *hmb_spaceMgmt_mappedAddr_get_by_lpn(bool for_write, uint32_t lpn)
{
	HmbSharedEnt *head, *loop;

	head = loop = NULL;

	if((head = hmb_spaceMgmt_shared_get_head_by_lpn(false, lpn)) == NULL)
	{
		return NULL;
	}

	if(head->lpn == lpn)
	{
		return hmb_spaceMgmt_mappedAddr_get_by_idx(for_write, head->e_own);
	}

	for(loop = hmb_spaceMgmt_table_get_by_idx(false, head->e_next); \
			loop != head; \
			loop = hmb_spaceMgmt_table_get_by_idx(false, loop->e_next))
	{
		if(loop->lpn == lpn)
		{
			return hmb_spaceMgmt_mappedAddr_get_by_idx(for_write, loop->e_own);
		}
	}

	return NULL;
}

HmbSharedEnt *hmb_spaceMgmt_shared_get_head_by_lpn(bool for_write, uint64_t lpn)
{
	uint32_t hashed;
	int32_t idx;

	hashed = hmb_hashing(lpn, HMB_SPACEMGMT_CTRL.heads_hash_bit);

	idx = *(hmb_spaceMgmt_heads_get_by_idx(false, hashed));
	if(idx == HMB_HAS_NO_ENTRY)
	{
		return NULL;
	}

	return hmb_spaceMgmt_table_get_by_idx(for_write, idx);
}

HmbHeads *hmb_spaceMgmt_heads_get_by_idx(bool for_write, uint32_t idx)
{
	uint32_t seg, offset;

	seg = idx / HMB_SPACEMGMT_CTRL.heads_split_unit;
	offset = idx % HMB_SPACEMGMT_CTRL.heads_split_unit;

	if(for_write)
	{
		return &(((HmbHeads *)(HMB_SPACEMGMT_CTRL.heads_split_mapped[seg]->w))[offset]);
	}
	return &(((HmbHeads *)(HMB_SPACEMGMT_CTRL.heads_split_mapped[seg]->r))[offset]);
}

bool hmb_spaceMgmt_shared_set_head(uint32_t idx)
{
	HmbSharedEnt *entry_r, *entry_w;
	int32_t *head_w;
	uint32_t hashed;
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	int32_t *head_r;
#endif

	entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx);
	
	hashed = hmb_hashing(entry_r->lpn, HMB_SPACEMGMT_CTRL.heads_hash_bit);

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	head_r = hmb_spaceMgmt_heads_get_by_idx(false, hashed);
	if((*head_r) != HMB_HAS_NO_ENTRY)
	{
		hmb_debug("head is already set!");
		return false;
	}
#endif

	head_w = hmb_spaceMgmt_heads_get_by_idx(true, hashed);
	entry_w = hmb_spaceMgmt_table_get_by_idx(true, idx);

	*head_w = idx;

	entry_w->e_own = idx;
	entry_w->e_next = idx;
	entry_w->e_prev = idx;

	hmb_spaceMgmt_table_nCached_inc(true);

	return true;
}

bool hmb_spaceMgmt_shared_insert_tail(uint32_t idx)
{
	HmbSharedEnt *head_r, *head_w, *tail_r, *tail_w, *entry_w, *entry_r;
	uint32_t idx_head, idx_tail;

	/* [1] If the LRU list for 'entry' has no entry */
	entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx);
	if((head_r = hmb_spaceMgmt_shared_get_head_by_lpn(false, entry_r->lpn)) == NULL)
	{
		return hmb_spaceMgmt_shared_set_head(idx);
	}
	/* [1] */

	/* "tail": the most recently used entry */
	entry_w = hmb_spaceMgmt_table_get_by_idx(true, idx);
	head_w = hmb_spaceMgmt_table_get_by_idx(true, head_r->e_own);

	tail_r = hmb_spaceMgmt_table_get_by_idx(false, head_r->e_prev);
	tail_w = hmb_spaceMgmt_table_get_by_idx(true, head_r->e_prev);

	idx_head = head_r->e_own;  /* entry index for head (i.e. least recently used) */
	idx_tail = tail_r->e_own;  /* entry index for tail (i.e. most recently used) */

	/* [1] Set the 'entry' to most recently used entry */
	tail_w->e_next = idx;
	head_w->e_prev = idx;

	entry_w->e_own = idx;
	entry_w->e_next = idx_head;
	entry_w->e_prev = idx_tail;
	/* [1] */

	hmb_spaceMgmt_table_nCached_inc(true);

	return true;
}

bool hmb_spaceMgmt_shared_insert_after(uint32_t idx, uint32_t idx_after)
{
	HmbSharedEnt *entry_w, *after_r, *after_w, *after_next_w;
	uint32_t idx_after_next;

	/* "entry" will be inserted between "after" and "after_next" */
	after_r = hmb_spaceMgmt_table_get_by_idx(false, idx_after);

	idx_after_next = after_r->e_next;

	entry_w = hmb_spaceMgmt_table_get_by_idx(true, idx);
	after_w = hmb_spaceMgmt_table_get_by_idx(true, idx_after);
	after_next_w = hmb_spaceMgmt_table_get_by_idx(true, idx_after_next);

	after_w->e_next = idx;
	after_next_w->e_prev = idx;

	entry_w->e_next = idx_after_next;
	entry_w->e_prev = idx_after;

	hmb_spaceMgmt_table_nCached_inc(true);

	return true;
}

/* By default, new entry is inserted to tail. */
bool hmb_spaceMgmt_shared_insert(uint32_t idx)
{
	return hmb_spaceMgmt_shared_insert_LRU(idx);
}

bool hmb_spaceMgmt_shared_insert_LRU(uint32_t idx)
{
	return hmb_spaceMgmt_shared_insert_tail(idx);
}

bool hmb_spaceMgmt_shared_delete_by_idx(uint32_t idx)
{
	HmbSharedEnt *entry_r, *prev_w, *next_w, *head_r;
	uint32_t idx_prev, idx_next;

	entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx);

	idx_next = entry_r->e_next; /* get index of the next entry of the 'entry' */
	idx_prev = entry_r->e_prev; /* get index of the previous entry of the 'entry' */

	prev_w = hmb_spaceMgmt_table_get_by_idx(true, idx_prev);
	next_w = hmb_spaceMgmt_table_get_by_idx(true, idx_next);

	prev_w->e_next = idx_next;
	next_w->e_prev = idx_prev;

	head_r = hmb_spaceMgmt_shared_get_head_by_lpn(false, entry_r->lpn);
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(head_r == NULL)
	{
		hmb_debug("Invalid relationship!");
		return false;	
	}
#endif

	/* [1] If requested entry to delete is a head */
	if(head_r == entry_r)
	{
		HmbHeads *heads_w;
		uint32_t hashed;

		hashed = hmb_hashing(entry_r->lpn, HMB_SPACEMGMT_CTRL.heads_hash_bit);

		heads_w = hmb_spaceMgmt_heads_get_by_idx(true, hashed);

		if(idx == idx_next)
		{
			*heads_w = HMB_HAS_NO_ENTRY;
		}

		else
		{
			*heads_w = idx_next;
		}
	}
	/* [1] */

	if(hmb_spaceMgmt_bm_set(false, idx) == false)
	{
		hmb_debug("Failed to disable bitmap.");
		return false;
	}
	
#if 0
	if(hmb_spaceMgmt_table_bm_set_fully(false, idx) == false)
	{
		hmb_debug("Failed to disable table bitmap.");
		return false;
	}
#endif


#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(entry_r->dirty != 0)
	{
		hmb_debug("Invalid relationship: It must be clean!");
#if 0
		hmb_spaceMgmt_table_get_by_idx(true, idx)->urgency = HMB_SPACEMGMT_UL_DISABLED;
		hmb_spaceMgmt_shared_set_dirty(false, idx);
		hmb_spaceMgmt_table_nDirty_inc(false);
#endif
	}
#endif
	hmb_spaceMgmt_table_nCached_inc(false);

	return true;
}

bool hmb_spaceMgmt_RC_reorder(uint32_t idx)
{
	return hmb_spaceMgmt_RC_reorder_LRU(idx);
}

/* [1] Move "entry" to tail (i.e. most recently used entry) */
bool hmb_spaceMgmt_RC_reorder_LRU(uint32_t idx)
{
	/* Remove the "entry" from the LRU list */
	if(hmb_spaceMgmt_RC_sorted_delete(idx) == false)
	{
		hmb_debug("Failed to delete entry.");
		return false;
	}

	if(hmb_spaceMgmt_RC_sorted_insert_tail(idx) == false)
	{
		hmb_debug("Failed to re-insert for reordering.");
		return false;
	}

	if(hmb_spaceMgmt_table_get_by_idx(false, idx)->dirty == 0)
	{
		/* Remove the "entry" from the LRU list */
		if(hmb_spaceMgmt_RCOnly_sorted_delete(idx) == false)
		{
			hmb_debug("Failed to delete entry.");
			return false;
		}

		if(hmb_spaceMgmt_RCOnly_sorted_insert_tail(idx) == false)
		{
			hmb_debug("Failed to re-insert for reordering.");
			return false;
		}
	}
	return true;
}
/* [1] */

uint32_t hmb_spaceMgmt_heads_get_size(void)
{
	return HMB_SPACEMGMT_CTRL.heads_cnt * sizeof(int32_t);
}

HmbSplitTable *hmb_spaceMgmt_heads_ST_get_by_idx(bool for_write, uint32_t idx)
{
	if(for_write)
	{    
		return &(((HmbSplitTable *)(HMB_SPACEMGMT_CTRL.heads_ST_mapped->w))[idx]);
	}    
	return &(((HmbSplitTable *)(HMB_SPACEMGMT_CTRL.heads_ST_mapped->r))[idx]);
}

uint32_t hmb_spaceMgmt_table_max_entries(void)
{
	double size_per_entry;
	uint64_t max_entry;

	/**
		Considerations
		  = entry size
		     + allocated HMB space to each entry
			 + bitmap per each entry
	**/
	/* Bitmap is not located in HMB, currently. */
	size_per_entry = sizeof(HmbSharedEnt) \
					 + sizeof(HmbSortedEnt) \
					 + HMB_SPACEMGMT_CTRL.cache_unit;

	max_entry = hmb_get_total_allocable_size_byte();
	max_entry -= sizeof(int32_t) - (sizeof(HmbBitmap32) * 2);  /* bitmap and its victim */
	max_entry -= HMB_CTRL.list_cnt * sizeof(HmbSplitTable) * 2;
	max_entry = ((uint64_t)(max_entry / size_per_entry));

	// YA
    // max_entry = 32; 
	hmb_debug(" max entry: %u ", max_entry);
	hmb_debug(" size per entry: %lu ", size_per_entry);
	hmb_debug("# MAX CACHE ENTRIES: %lu (%.6lfMBytes)", max_entry, \
			((double)max_entry) * HMB_SPACEMGMT_CTRL.cache_unit / 1024 / 1024);

	return max_entry;
}

void hmb_spaceMgmt_table_nCached_inc(bool is_inc)
{
	if(is_inc)
	{
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
		if(hmb_meta_get(false)->C__n_max_entries == hmb_meta_get(false)->C__n_cached)
		{
			hmb_debug("Already # cached entries reached # max. cachable entries!");
			return;
		}
#endif
		hmb_meta_get(true)->C__n_cached++;
	}

	else
	{
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
		if(hmb_meta_get(false)->C__n_cached == 0)
		{
			hmb_debug("Alread # cache entries reached zero!");
			return;
		}
#endif
		hmb_meta_get(true)->C__n_cached--;
	}
}

void hmb_spaceMgmt_table_nDirty_inc(bool is_inc)
{
	if(is_inc)
	{
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
		if(hmb_meta_get(false)->C__n_dirty == hmb_meta_get(false)->C__n_cached)
		{
			hmb_debug("Already # dirty entries reached # cached entries!");
			return;
		}
#endif
		hmb_meta_get(true)->C__n_dirty++;
	}

	else
	{
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
		if(hmb_meta_get(false)->C__n_dirty == 0)
		{
			hmb_debug("Already # dirty entries reached zero!");
			return;
		}
#endif
		hmb_meta_get(true)->C__n_dirty--;
	}
}

bool hmb_spaceMgmt_table_is_full(void)
{
	if(hmb_meta_get(false)->C__n_cached >= hmb_meta_get(false)->C__n_max_entries)
	{
		return true;
	}

	return false;
}

uint32_t hmb_spaceMgmt_table_get_cache_num(void)
{
	return hmb_meta_get(false)->C__n_cached;
}

HmbSplitTable *hmb_spaceMgmt_table_ST_get_by_idx(bool for_write, uint32_t idx)
{
	if(for_write)
	{    
		return &(((HmbSplitTable *)(HMB_SPACEMGMT_CTRL.table_ST_mapped->w))[idx]);
	}    
	return &(((HmbSplitTable *)(HMB_SPACEMGMT_CTRL.table_ST_mapped->r))[idx]);
}

HmbSplitTable *hmb_spaceMgmt_table_bm_ST_get_by_idx(bool for_write, uint32_t idx)
{
	if(for_write)
	{    
		return &(((HmbSplitTable *)(HMB_SPACEMGMT_CTRL.table_bm_ST_mapped->w))[idx]);
	}    
	return &(((HmbSplitTable *)(HMB_SPACEMGMT_CTRL.table_bm_ST_mapped->r))[idx]);
}

HmbSharedBitmapEnt *hmb_spaceMgmt_table_bm_get_by_idx(bool for_write, uint32_t idx)
{
	uint32_t seg, offset;

	seg = idx / HMB_SPACEMGMT_CTRL.table_split_unit;
	offset = idx % HMB_SPACEMGMT_CTRL.table_split_unit;

	if(for_write)
	{
		return &(((HmbSharedBitmapEnt *)(HMB_SPACEMGMT_CTRL.table_bm_split_mapped[seg]->w))[offset]);
	}
	return &(((HmbSharedBitmapEnt *)(HMB_SPACEMGMT_CTRL.table_bm_split_mapped[seg]->r))[offset]);
}

bool hmb_spaceMgmt_table_bm_isCached_fully(uint32_t idx)
{
	HmbBitmap32 *bm_r = &(hmb_spaceMgmt_table_bm_get_by_idx(false, idx)->filled);

	if((*bm_r) == HMB_SPACEMGMT_BITMAP_PART_MAX_VALUE)
	{
		return true;
	}

	return false;
}

bool hmb_spaceMgmt_table_bm_isCached_partially(uint32_t idx, uint32_t idx_internal)
{
	HmbBitmap32 *bm_r = &(hmb_spaceMgmt_table_bm_get_by_idx(false, idx)->filled);

	if((*bm_r) & (1 << idx_internal))
	{
		return true;
	}

	return false;
}

bool hmb_spaceMgmt_table_bm_set(bool enable, uint32_t idx, uint32_t idx_internal)
{
	HmbBitmap32 *bm_w;
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	HmbBitmap32 *bm_r;
#endif

	bm_w = &(hmb_spaceMgmt_table_bm_get_by_idx(true, idx)->filled);

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	bm_r = &(hmb_spaceMgmt_table_bm_get_by_idx(false, idx)->filled);
	if(enable && ((*bm_r) & (1 << idx_internal)))
	{
		hmb_debug("Already filled! (idx: %u, idx_internal: %u)", idx, idx_internal);
		return false;
	}

	if(!enable && !((*bm_r) & (1 << idx_internal)))
	{
		hmb_debug("Already empty! (idx: %u, idx_internal: %u)", idx, idx_internal);
		return false;
	}
#endif

	if(enable)
	{
		(*bm_w) |= (1 << idx_internal);
	}

	else
	{
		(*bm_w) &= ~(1 << idx_internal);
	}

	return true;
}

bool hmb_spaceMgmt_table_bm_set_fully(bool enable, uint32_t idx)
{
	HmbBitmap32 *bm_w = &(hmb_spaceMgmt_table_bm_get_by_idx(true, idx)->filled);

	if(enable)
	{
		(*bm_w) = HMB_SPACEMGMT_BITMAP_PART_MAX_VALUE;
	}

	else
	{
		(*bm_w) = 0;

		hmb_spaceMgmt_table_bm_fill_overflowed( \
				idx, \
				1 << (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits), \
				HMB_SPACEMGMT_BITMAP_BITS_PER_PART);
	}

	return true;
}

bool hmb_spaceMgmt_table_bm_fill_overflowed(uint32_t idx, uint32_t from, uint32_t to)
{
	uint32_t i;

	for(i=from; i<to; i++)
	{
		if(hmb_spaceMgmt_table_bm_set(true, idx, i) == false)
		{
			hmb_debug("Unexpected result. Is '%u' already filled?", i);
		}
	}

	return true;
}

uint8_t hmb_spaceMgmt_hash_calc_bits(uint32_t max_entries)
{
	// uint32_t i;
	uint8_t i =30;

	hmb_debug(" hash cal bits max_entries: %u", max_entries);
	// uint32_t limit = (uint32_t)sqrt(max_entries);
 /*	uint32_t limit = max_entries;

	hmb_debug(" hash cal bits max_entries: %u", max_entries);
	for(i=1; ;i++) 	{

	hmb_debug(" hash cal bits		   %u ", i);
	hmb_debug(" hash cal bits shifted  %u ",1 << i);
		if((1 << i) == limit)
		{
			break;
		}

		else if((1 << i) > limit)
		{
			i--;
			break;
		}
	} */ 

	hmb_debug(" hash cal bits %u ", i);
	 return i;
	
}

bool hmb_spaceMgmt_RC_update(uint32_t idx)
{
	return hmb_spaceMgmt_RC_update_LRU(idx);
}

void hmb_spaceMgmt_shared_set_dirty(bool to_dirty, uint32_t idx)
{
	HmbSharedEnt *entry_w;

	entry_w = hmb_spaceMgmt_table_get_by_idx(true, idx);

	if(to_dirty)
	{
		entry_w->dirty = 1;
	}
	else
	{
		entry_w->dirty = 0;
	}
}

void hmb_spaceMgmt_shared_set_enable(bool enable, uint32_t idx)
{
	HmbSharedEnt *entry_w;

	entry_w = hmb_spaceMgmt_table_get_by_idx(true, idx);

	if(enable)
	{
		entry_w->usable = 1;
	}
	else
	{
		entry_w->usable = 0;
	}
}

bool hmb_spaceMgmt_RC_update_LRU(uint32_t idx)
{
	HmbSharedEnt *entry_r;
	HmbMappedAddr *mapped_w;

	entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx);
	mapped_w = hmb_spaceMgmt_mappedAddr_get_by_idx(true, idx);

	hmb_spaceMgmt_heap_rw( \
				false, \
				(entry_r->lpn) << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
				HMB_SPACEMGMT_CTRL.cache_unit, \
				mapped_w);
	hmb_spaceMgmt_table_bm_set_fully(true, idx);

	return true;
}

int32_t hmb_spaceMgmt_shared_get_new_entry_idx(uint64_t lpn)
{
	uint32_t new_idx;
	HmbSharedEnt *entry_w;
	HmbSortedEnt *entry_sorted_w;

	if(hmb_spaceMgmt_table_is_full())
	{
		return -1;
	}

	if(hmb_spaceMgmt_bm_get_empty(&new_idx) == false)
	{
		//hmb_debug("Bitmap is already full!");
		return -1;
	}
	if(hmb_spaceMgmt_bm_set(true, new_idx) == false)
	{
		hmb_debug("Invalid relationship: failed to fill data into bitmap.");
		return -1;
	}

	if(hmb_spaceMgmt_table_bm_set_fully(false, new_idx) == false)
	{
		hmb_debug("Invalid relationship: failed to fill data into table bitmap.");
		return -1;
	}
	if((entry_w = hmb_spaceMgmt_table_get_by_idx(true, new_idx)) == NULL)
	{
		hmb_debug("Failed to get new entry by index");
		return -1;
	}

	if((entry_sorted_w = hmb_spaceMgmt_sorted_get_by_idx(true, new_idx)) == NULL)
	{
		hmb_debug("Faield to get new entry by index");
		return -1;
	}

	entry_w->e_own = new_idx;
	entry_w->lpn = lpn;
	entry_w->usable = 0;
	entry_w->dirty = 0;
	entry_w->urgency = HMB_SPACEMGMT_UL_DISABLED;

	entry_sorted_w->e_own = new_idx;

	return (int32_t)new_idx;
}

bool hmb_spaceMgmt_shared_is_reusable_by_lpn(uint64_t lpn)
{
	HmbSharedEnt *entry_r;

	if((entry_r = hmb_spaceMgmt_table_get_by_lpn(false, lpn)) == NULL)
	{
		return false;

	}

	//if(entry_r->usable == 1 && entry_r->dirty == 0)
	//if(lpn == entry_r->lpn && entry_r->usable == 1)
	if(lpn == entry_r->lpn)
	{
		return true;
	}

	return false;
}

bool hmb_spaceMgmt_shared_is_reusable_by_idx(uint32_t idx)
{
	HmbSharedEnt *entry_r;

	if((entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx)) == NULL)
	{
		return false;
	}

	if(entry_r->usable == 1)
	{
		return true;
	}

	return false;
}

bool hmb_spaceMgmt_RC_caching(uint64_t lpn, uint32_t n_lb, bool do_data_copy, int64_t *expire_time)
{
	/*uint64_t i;
	NvmeCtrl *n = (struct NvmeCtrl*)HMB_CTRL.parent;
	uint64_t max_dev_cap;
	HmbSharedEnt *entry_r;

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(!HMB_CTRL.enable_hmb || !HMB_SPACEMGMT_CTRL.inited)
	{
		hmb_debug("HMB is not enabled!");
		return false;
	}
#endif

	//hmb_spaceMgmt_lock();
	max_dev_cap = blk_getlength(n->conf.blk);
	if(max_dev_cap < ((lpn + n_lb) << HMB_SPACEMGMT_CTRL.cache_unit_bits))
	{
		hmb_debug("Out of range! (from 0x%llX, +%llu, max: 0x%llXB(=%lluMB))",  \
				lpn << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
			   	n_lb << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
				max_dev_cap, 
				max_dev_cap/1024/1024);

		if((lpn << HMB_SPACEMGMT_CTRL.sector_size_bits) >= max_dev_cap)
		{
			hmb_debug("Caching range can not be modified.");
			//hmb_spaceMgmt_unlock();
			return false;
		}

		// FIXME: Need to modify - this is not right 
		hmb_debug("  - Before changing #sectors: %u", n_lb);
		n_lb = max_dev_cap - (lpn << HMB_SPACEMGMT_CTRL.cache_unit_bits);
		n_lb >>= HMB_SPACEMGMT_CTRL.cache_unit_bits;
		hmb_debug("  -  After changing #sectors: %u", n_lb);
	}

	for(i=0; i<n_lb; i++)
	{
		uint64_t lpn_cur = lpn + i;

		entry_r = hmb_spaceMgmt_table_get_by_lpn(false, lpn_cur);

		// If entry for the 'lpn_cur' is not cached 
		if(entry_r == NULL)
		{
			int32_t ret;
			uint32_t idx_entry;

			ret = hmb_spaceMgmt_shared_get_new_entry_idx(lpn_cur);

			// If it has no space to make new entry 
			if(ret < 0)
			{
				// Least recently used entries must be evicted. 
				if(hmb_spaceMgmt_RC_evict(n_lb - i, expire_time) == false)
				{
					hmb_debug("Failed to evict entry.");
					return false;
				}
				if((ret = hmb_spaceMgmt_shared_get_new_entry_idx(lpn_cur)) < 0)
				{
					hmb_debug("Invalid relationship!: %u entries were already evicted!", n_lb - i);
					return false;
				}
			}

			idx_entry = (uint32_t)ret;
			entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx_entry);
			//hmb_debug("    Before: %u <-> %u <-> %u (cur: %u, max: %u)", entry_r->e_prev, entry_r->e_own, entry_r->e_next, HMB_SPACEMGMT_CTRL.table_cnt, HMB_SPACEMGMT_CTRL.table_cnt_max);
			if(hmb_spaceMgmt_shared_insert(idx_entry) == false)
			{
				hmb_debug("Failed to insert entry to adequate LRU list.");
				return false;
			}

			if(hmb_spaceMgmt_RC_sorted_insert_tail(idx_entry) == false)
			{
				hmb_debug("Failed to insert new entry for sorting");
				return false;
			}

			if(do_data_copy)
			{
				if(hmb_spaceMgmt_RCOnly_sorted_insert_tail(idx_entry) == false)
				{
					hmb_debug("Failed to insert new entry for sorting");
					return false;
				}
			}

			//hmb_debug("Inserted-1: %u <-> %u <-> %u (cur: %u, max: %u)", entry_r->e_prev, entry_r->e_own, entry_r->e_next, HMB_SPACEMGMT_CTRL.table_cnt, HMB_SPACEMGMT_CTRL.table_cnt_max);
			//			hmb_debug("Inserted: %u <-> %u <-> %u (cur: %u, max: %u, head: %d)", entry_r->e_prev, entry_r->e_own, entry_r->e_next, HMB_SPACEMGMT_CTRL.table_cnt, HMB_SPACEMGMT_CTRL.table_cnt_max, hmb_spaceMgmt_shared_get_head_by_lpn(false, entry_r->e_own));
		} // if(entry_r == NULL)  

		else
		{

			if(hmb_spaceMgmt_RC_reorder(entry_r->e_own) == false)
			{
				hmb_debug("Failed to reorder list.");
				return false;
			}
			if(do_data_copy == false && entry_r->dirty == 0)
			{
				if(hmb_spaceMgmt_RCOnly_sorted_delete(entry_r->e_own) == false)
				{
					hmb_debug("Failed to delete entry.");
					return false;
				}
			}

#if 0
			if(hmb_spaceMgmt_shared_is_reusable_by_idx(entry_r->e_own) == true)
			{
				continue;
			}
#endif
			// [2] 
		
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
			if(entry_r->dirty != 0 && entry_r->urgency == HMB_SPACEMGMT_UL_DISABLED)
			{
				hmb_debug("Invalid relationship: dirtyness: %u, urgency level: %u", \
						entry_r->dirty, entry_r->urgency);
				return false;
			}
#endif
			hmb_spaceMgmt_shared_set_enable(false, entry_r->e_own);
		}

		if(do_data_copy)
		{
			if(hmb_spaceMgmt_RC_update(entry_r->e_own) == false)
			{
				hmb_debug("Failed to update information in entry");
				return false;
			}
			hmb_spaceMgmt_shared_set_enable(true, entry_r->e_own);
		}

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
		if(entry_r->e_own == entry_r->e_next && \
				entry_r != hmb_spaceMgmt_shared_get_head_by_lpn(false, entry_r->lpn))
		{
			hmb_debug("Inserted: %u <-> %u <-> %u (cur: %u, max: %u, head: %d)", entry_r->e_prev, entry_r->e_own, entry_r->e_next, HMB_SPACEMGMT_CTRL.table_cnt, HMB_SPACEMGMT_CTRL.table_cnt_max, hmb_spaceMgmt_shared_get_head_by_lpn(false, entry_r->lpn)->e_own);
		}
#endif

	}
*/
	//hmb_spaceMgmt_unlock();
	return true;
}

bool hmb_spaceMgmt_RC_evict(uint32_t n_evict, int64_t *expire_time)
{
	return hmb_spaceMgmt_RC_evict_LRU(n_evict, expire_time);
}

bool hmb_spaceMgmt_RC_evict_LRU(uint32_t n_evict, int64_t *expire_time)
{
	/*uint32_t i;

	uint64_t prev_lpn;
	uint64_t prev_nlp;
	bool prev_applied = false;
	bool prev_with_read = false;

	uint64_t expire_time_cur;
	struct ssdstate *ssd = &(((NvmeCtrl *)HMB_CTRL.parent)->ssd);

	for(i=0; i<n_evict; i++)
	{
		int32_t ret;
		uint32_t idx_sorted_head;
		HmbSharedEnt *entry_r;

		ret = hmb_spaceMgmt_RC_sorted_get_head_idx();
		if(ret == HMB_HAS_NO_ENTRY)
		{
			if(i == 0)
			{
				return false;
			}
			return true;
		}
		idx_sorted_head = (uint32_t)ret;

		hmb_spaceMgmt_shared_set_enable(false, idx_sorted_head);
		entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx_sorted_head);

#if 0
		hmb_debug("%lu --> %lu / %lu --> %lu", entry_r->lpn, hmb_spaceMgmt_lpn_to_lba(entry_r->lpn), \
				1, hmb_spaceMgmt_nlp_to_nlb(1));
#endif

		if(entry_r->dirty)
		{
#ifdef HMB_DEBUGGING_TIME
			hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__bm_op, true);
#endif
			if(hmb_spaceMgmt_table_bm_isCached_fully(idx_sorted_head) == true)
			{
#ifdef HMB_DEBUGGING_TIME
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__bm_op, false);
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__memcpy, true);
#endif
				hmb_spaceMgmt_heap_rw( \
							true, \
							(entry_r->lpn) << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
							HMB_SPACEMGMT_CTRL.cache_unit, \
							hmb_spaceMgmt_mappedAddr_get_by_idx(false, idx_sorted_head));

#ifdef HMB_DEBUGGING_TIME
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__memcpy, false);
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb, false);
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_femu, true);
#endif
#if 0
				if(*expire_time < (expire_time_cur = SSD_WRITE(ssd, \
							   	hmb_spaceMgmt_nlp_to_nlb(1), \
								hmb_spaceMgmt_lpn_to_lba(entry_r->lpn))))
				{
					*expire_time = expire_time_cur;
				}
#endif
				if(prev_applied == false)
				{
					prev_lpn = entry_r->lpn;
					prev_nlp = 1;
					prev_applied = true;
					prev_with_read = false;
				}

				else
				{
					if(prev_lpn + prev_nlp == entry_r->lpn)
					{
						if(prev_with_read == true)
						{
							expire_time_cur = SSD_READ(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}

							expire_time_cur = SSD_WRITE(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}

							prev_lpn = entry_r->lpn;
							prev_nlp = 1;

							prev_with_read = false;
						}
						else
						{
							prev_nlp++;
						}
					}
					else
					{
						if(prev_with_read == true)
						{
							expire_time_cur = SSD_READ(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}
						}

						expire_time_cur = SSD_WRITE(ssd, \
								hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
								hmb_spaceMgmt_lpn_to_lba(prev_lpn));
						if(*expire_time < expire_time_cur)
						{
							*expire_time = expire_time_cur;
						}

						prev_lpn = entry_r->lpn;
						prev_nlp = 1;
						prev_with_read = false;
					}
				}
#ifdef HMB_DEBUGGING_TIME
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_femu, false);
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb, true);
#endif
#if 0
				hmb_debug("[F] LBA: 0x%010lX", entry_r->lpn);
#endif
			}
			else
			{
				uint32_t j;
				uint32_t last = hmb_spaceMgmt_nlp_to_nlb(1);

#ifdef HMB_DEBUGGING_TIME
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__bm_op, false);
#endif

				for(j=0; j<last; j++)
				{
#ifdef HMB_DEBUGGING_TIME
					hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__bm_op, true);
#endif
					if(hmb_spaceMgmt_table_bm_isCached_partially(idx_sorted_head, j) == true)
					{
						uint64_t len, offset, diff;
						void *buf;
#ifdef HMB_DEBUGGING_TIME
						hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__bm_op, false);
#endif

						len = 512;
						offset = hmb_spaceMgmt_lpn_to_bytes(entry_r->lpn);
						buf = hmb_spaceMgmt_mappedAddr_get_by_idx(false, idx_sorted_head);

						diff = (j << 9);
						offset += diff;
						buf += diff;

#ifdef HMB_DEBUGGING_TIME
						hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__memcpy, true);
#endif
						hmb_spaceMgmt_heap_rw(true, offset, len, buf);
#ifdef HMB_DEBUGGING_TIME
						hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__memcpy, false);
#endif
#if 0
						hmb_debug("[P-%3d] LBA: 0x%010lX, OFFSET: 0x%010lx, INT-IDX: %1u, DIFF: %4lu, BM: 0x%8X", \
								i, entry_r->lpn, offset, j, diff, *hmb_spaceMgmt_table_bm_get_by_idx(false, idx_sorted_head));
#endif
					}
					else
					{
#ifdef HMB_DEBUGGING_TIME
						hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__bm_op, false);
#endif
					}
#ifdef HMB_DEBUGGING_TIME
					hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__bm_op, true);
#endif
				}

				// In the eviction process, fully caching is not needed. 
#ifdef HMB_DEBUGGING_TIME
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb, false);
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_femu, true);
#endif
				//
				   Consideration of partially written(i.e. dirty) data
				   - If some of cached data is cached and dirty, they need to be written to NAND.
				   -> However, we cannot distinguish what is either clean or dirty each other.
				   -> As a result, we only flush cached data all at once without their dirtiness.
				   - And some part of data in NAND must be read in this case.
				   -> Consequently, we must read one logical page.
				 
				if(prev_applied == false)
				{
					prev_lpn = entry_r->lpn;
					prev_nlp = 1;
					prev_applied = true;
					prev_with_read = true;
				}

				else
				{
					if(prev_lpn + prev_nlp == entry_r->lpn)
					{
						if(prev_with_read == false)
						{
							expire_time_cur = SSD_WRITE(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}

							prev_lpn = entry_r->lpn;
							prev_nlp = 1;

							prev_with_read = true;
						}
						else
						{
							prev_nlp++;
						}
					}
					else
					{
						if(prev_with_read == true)
						{
							expire_time_cur = SSD_READ(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}
						}

						expire_time_cur = SSD_WRITE(ssd, \
								hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
								hmb_spaceMgmt_lpn_to_lba(prev_lpn));
						if(*expire_time < expire_time_cur)
						{
							*expire_time = expire_time_cur;
						}

						prev_lpn = entry_r->lpn;
						prev_nlp = 1;
						prev_with_read = true;
					}
				}
#ifdef HMB_DEBUGGING_TIME
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_femu, false);
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb, true);
#endif
			}

#ifdef HMB_DEBUGGING_TIME
			hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__delete, true);
#endif
			if(hmb_spaceMgmt_WB_sorted_delete(entry_r->urgency, idx_sorted_head) == false)
			{
#ifdef HMB_DEBUGGING_TIME
				hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__delete, false);
#endif
				hmb_debug("Invalid relationship");
				return false;
			}
#ifdef HMB_DEBUGGING_TIME
			hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__delete, false);
#endif
			hmb_spaceMgmt_table_get_by_idx(true, idx_sorted_head)->urgency = HMB_SPACEMGMT_UL_DISABLED;
			hmb_spaceMgmt_shared_set_dirty(false, idx_sorted_head);
			hmb_spaceMgmt_table_nDirty_inc(false);
		}

		else
		{
#ifdef HMB_DEBUGGING_TIME
			hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__delete, true);
#endif
			if(hmb_spaceMgmt_RCOnly_sorted_delete(idx_sorted_head) == false)
			{
				hmb_debug("Invalid relationship.");
				return false;
			}
#ifdef HMB_DEBUGGING_TIME
			hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__delete, false);
#endif
		}
		
#ifdef HMB_DEBUGGING_TIME
		hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__delete, true);
#endif
		// Step 3. Remove the entry from its heads 
		if(hmb_spaceMgmt_RC_sorted_delete(idx_sorted_head) == false)
		{
#ifdef HMB_DEBUGGING_TIME
			hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__delete, false);
#endif
			hmb_debug("Invalid relationship.");
			return false;
		}

		if(hmb_spaceMgmt_shared_delete_by_idx(idx_sorted_head) == false)
		{
			hmb_debug("Failed to remove one entry.");
#ifdef HMB_DEBUGGING_TIME
			hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__delete, false);
#endif
			return false;
		}
#ifdef HMB_DEBUGGING_TIME
		hmb_timeDbg_record(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb__delete, false);
#endif
	}

	if(prev_applied == true)
	{
		if(prev_with_read == true)
		{
			expire_time_cur = SSD_READ(ssd, \
					hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
					hmb_spaceMgmt_lpn_to_lba(prev_lpn));
			if(*expire_time < expire_time_cur)
			{
				*expire_time = expire_time_cur;
			}
		}

		expire_time_cur = SSD_WRITE(ssd, \
				hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
				hmb_spaceMgmt_lpn_to_lba(prev_lpn));
		if(*expire_time < expire_time_cur)
		{
			*expire_time = expire_time_cur;
		}
	}
*/
	//hmb_debug("requested: %u, evicted: %u, cur: %u, max: %u", n_evict, i, HMB_SPACEMGMT_CTRL.table_cnt, HMB_SPACEMGMT_CTRL.table_cnt_max); 
	return true;
}

bool hmb_spaceMgmt_bm_init(uint32_t n_parts)
{
	uint32_t i;

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(n_parts == 0)
	{
		hmb_debug("Invalid argument.");
		return false;
	}
#endif

	/* 1 bit is used for 1 entry */
	HMB_SPACEMGMT_CTRL.bm_parts_cnt = n_parts / HMB_SPACEMGMT_BITMAP_BITS_PER_PART;
	if(n_parts % HMB_SPACEMGMT_BITMAP_BITS_PER_PART)
	{
		HMB_SPACEMGMT_CTRL.bm_parts_cnt++;
	}
	hmb_meta_get(true)->C__bm_parts_cnt = HMB_SPACEMGMT_CTRL.bm_parts_cnt;

	if((HMB_SPACEMGMT_CTRL.bm = hmb_calloc( \
					HMB_SPACEMGMT_CTRL.bm_parts_cnt * sizeof(HmbBitmap32))) == NULL)
	{
		hmb_debug("Failed to allocate bitmap area into HMB.");
		return false;
	}

	if((HMB_SPACEMGMT_CTRL.bm_empty = hmb_malloc( \
					sizeof(HmbBitmapEmpty))) == NULL)
	{
		hmb_debug("Failed to allocate bitmap empty area into HMB.");
		return false;
	}

	if((HMB_SPACEMGMT_CTRL.bm_empty_table = hmb_calloc( \
					sizeof(HmbDLL) * HMB_SPACEMGMT_CTRL.bm_parts_cnt)) == NULL)
	{
		hmb_debug("Failed to allocate bitmap empty table area into HMB.");
		return false;
	}

	*hmb_spaceMgmt_bm_empty_get_head(true) = HMB_HAS_NO_ENTRY;
	for(i=0; i<HMB_SPACEMGMT_CTRL.bm_parts_cnt; i++)
	{
		if(hmb_spaceMgmt_bm_empty_insert(i) == false)
		{
			hmb_debug("Failed to initialize bitmap empty list.");
			return false;
		}
	}

	if(hmb_spaceMgmt_bm_fill_overflowed( \
				n_parts, \
				HMB_SPACEMGMT_CTRL.bm_parts_cnt * HMB_SPACEMGMT_BITMAP_BITS_PER_PART) == false)
	{
		hmb_debug("Failed to fill overflowed bitmap area.");
		return false;
	}

	return true;
}

bool hmb_spaceMgmt_heads_init(uint8_t n_bits)
{
	uint32_t i;

		
	hmb_debug("space mgmt head init in .");
	HMB_SPACEMGMT_CTRL.heads_hash_bit = n_bits;
	HMB_SPACEMGMT_CTRL.heads_cnt = 1 << HMB_SPACEMGMT_CTRL.heads_hash_bit;

	HMB_SPACEMGMT_CTRL.heads_split_num = HMB_CTRL.list_cnt;

	hmb_debug("space mgmt head init in 1 ");
	if(HMB_SPACEMGMT_CTRL.heads_cnt % HMB_CTRL.list_cnt)
	{
		HMB_SPACEMGMT_CTRL.heads_split_unit = (HMB_SPACEMGMT_CTRL.heads_cnt \
			   	/ HMB_SPACEMGMT_CTRL.heads_split_num) + 1;
	}
	else
	{
		HMB_SPACEMGMT_CTRL.heads_split_unit = (HMB_SPACEMGMT_CTRL.heads_cnt \
				/ HMB_SPACEMGMT_CTRL.heads_split_num);
	}

	hmb_debug("space mgmt head init in 2 ");
	HMB_SPACEMGMT_CTRL.heads_ST_size = HMB_SPACEMGMT_CTRL.heads_split_num * sizeof(HmbSplitTable);
	if((HMB_SPACEMGMT_CTRL.heads_ST_mapped = hmb_calloc( \
					HMB_SPACEMGMT_CTRL.heads_ST_size)) == NULL)
	{
		hmb_debug("Failed to map split table into HMB.");
		return false;
	}

	hmb_debug("space mgmt head init in 3 ");
	if((HMB_SPACEMGMT_CTRL.heads_split_mapped = \
				(HmbMappedAddr **)calloc(HMB_SPACEMGMT_CTRL.heads_split_num, \
				sizeof(HmbMappedAddr *))) == NULL)
	{
		hmb_debug("Failed to map split mapped table.");
		return false;
	}

	hmb_debug("space mgmt head init in 4 ");

		
	hmb_debug("space mgmt head split num %u  ", HMB_SPACEMGMT_CTRL.heads_split_num);
	for(i=0; i<HMB_SPACEMGMT_CTRL.heads_split_num; i++) {

	
		uint64_t size;
		HmbMapInfo *map;
		HmbSegEnt *se;
		HmbSplitTable *entry_w;//, *entry_r;

		hmb_debug("space mgmt head init in 4-0  ");
		if(i != HMB_SPACEMGMT_CTRL.heads_split_num-1)
		{
			size = HMB_SPACEMGMT_CTRL.heads_split_unit;
			size *= sizeof(HmbHeads);
		}
		else
		{
			size = HMB_SPACEMGMT_CTRL.heads_cnt % HMB_SPACEMGMT_CTRL.heads_split_unit;
			if(size == 0)
			{
				size = HMB_SPACEMGMT_CTRL.heads_split_unit;
			}
			size *= sizeof(HmbHeads);
		}

		hmb_debug("space mgmt head size:  %u  ", size);
		hmb_debug("space mgmt head init in 4-1  ");
		if((HMB_SPACEMGMT_CTRL.heads_split_mapped[i] = hmb_calloc(size)) == NULL)
		{
			hmb_debug("Failed to map cache entry table into HMB.");
			return false;
		}
		
		hmb_debug("space mgmt head init in 4-11  ");
		if((map = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.heads_split_mapped[i])) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}

		hmb_debug("space mgmt head init in 4-2  ");
		if((se = hmb_get_segEnt_by_id(false, map->entry_id)) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}

		hmb_debug("space mgmt head init in 4-3  ");
		entry_w = hmb_spaceMgmt_heads_ST_get_by_idx(true, i);

		hmb_debug("space mgmt head init in 4-4  ");
		entry_w->seg_id = se->segment_id;
		entry_w->offset = se->offset;

#if 0
		hmb_debug("[HEADS] (%3u) ORI/sid: %3d, ORI/off: %7u, SHRD/sid: %3d, SHRD/off: %7u", \
				i, se->segment_id, se->offset, entry_r->seg_id, entry_r->offset);
#endif

		hmb_debug("space mgmt head init in 4-5  ");
	}

	hmb_debug("space mgmt head init in 5 ");

	for(i=0; i<HMB_SPACEMGMT_CTRL.heads_cnt; i++)
	{
		*(hmb_spaceMgmt_heads_get_by_idx(true, i)) = HMB_HAS_NO_ENTRY;
	}

	hmb_debug("#heads: %u, splitTable_unit: %u, splitTable_num: %d", \
			HMB_SPACEMGMT_CTRL.heads_cnt, HMB_SPACEMGMT_CTRL.heads_split_unit, \
			HMB_SPACEMGMT_CTRL.heads_split_num);

	return true;
}

bool hmb_spaceMgmt_sorted_init(uint32_t n_parts)
{
	uint32_t i;

	HMB_SPACEMGMT_CTRL.sorted_split_num = HMB_CTRL.list_cnt;
	if(n_parts % HMB_CTRL.list_cnt)
	{
		HMB_SPACEMGMT_CTRL.sorted_split_unit = (n_parts / HMB_SPACEMGMT_CTRL.sorted_split_num) + 1;
	}
	else
	{
		HMB_SPACEMGMT_CTRL.sorted_split_unit = (n_parts / HMB_SPACEMGMT_CTRL.sorted_split_num);
	}

	HMB_SPACEMGMT_CTRL.sorted_ST_size = HMB_SPACEMGMT_CTRL.sorted_split_num * sizeof(HmbSplitTable);
	if((HMB_SPACEMGMT_CTRL.sorted_ST_mapped = hmb_calloc( \
					HMB_SPACEMGMT_CTRL.sorted_ST_size)) == NULL)
	{
		hmb_debug("Failed to map split table into HMB.");
		return false;
	}

	if((HMB_SPACEMGMT_CTRL.sorted_split_mapped = \
				(HmbMappedAddr **)calloc(HMB_SPACEMGMT_CTRL.sorted_split_num, \
				sizeof(HmbMappedAddr *))) == NULL)
	{
		hmb_debug("Failed to map split mapped table.");
		return false;
	}

	for(i=0; i<HMB_SPACEMGMT_CTRL.sorted_split_num; i++)
	{
		uint64_t size;
		HmbMapInfo *map;
		HmbSegEnt *se;
		HmbSplitTable *entry_w;//, *entry_r;

		if(i != HMB_SPACEMGMT_CTRL.sorted_split_num-1)
		{
			size = HMB_SPACEMGMT_CTRL.sorted_split_unit;
			size *= sizeof(HmbSortedEnt);
		}
		else
		{
			size = n_parts % HMB_SPACEMGMT_CTRL.sorted_split_unit;
			if(size == 0)
			{
				size = HMB_SPACEMGMT_CTRL.sorted_split_unit;
			}
			size *= sizeof(HmbSortedEnt);
		}

		if((HMB_SPACEMGMT_CTRL.sorted_split_mapped[i] = hmb_calloc(size)) == NULL)
		{
			hmb_debug("Failed to map cache entry table into HMB.");
			return false;
		}
		
		if((map = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.sorted_split_mapped[i])) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}

		if((se = hmb_get_segEnt_by_id(false, map->entry_id)) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}

		entry_w = hmb_spaceMgmt_sorted_ST_get_by_idx(true, i);
		//entry_r = hmb_spaceMgmt_sorted_ST_get_by_idx(false, i);

		entry_w->seg_id = se->segment_id;
		entry_w->offset = se->offset;

#if 0
		hmb_debug("[SRT] (%3u) ORI/sid: %3d, ORI/off: %7u, SHRD/sid: %3d, SHRD/off: %7u", \
				i, se->segment_id, se->offset, entry_r->seg_id, entry_r->offset);
#endif
	}

	if((HMB_SPACEMGMT_CTRL.victimAll = hmb_calloc(sizeof(int32_t))) == NULL)
	{
		hmb_debug("Failed to map cache sorted head into HMB.");
		return false;
	}

	*hmb_spaceMgmt_victimAll_get(true) = HMB_HAS_NO_ENTRY;

	if((HMB_SPACEMGMT_CTRL.victimRc = hmb_calloc(sizeof(int32_t))) == NULL)
	{
		hmb_debug("Failed to map cache sorted head into HMB.");
		return false;
	}

	*hmb_spaceMgmt_victimRc_get(true) = HMB_HAS_NO_ENTRY;

	if((HMB_SPACEMGMT_CTRL.urgency = hmb_calloc( \
					sizeof(int32_t) * HMB_SPACEMGMT_UL_NUMBER)) == NULL)
	{
		hmb_debug("Failed to map urgency list into HMB.");
		return false;
	}

	for(i=0; i<HMB_SPACEMGMT_UL_NUMBER; i++)
	{
		*(hmb_spaceMgmt_WB_sorted_get_head(true, i+1)) = HMB_HAS_NO_ENTRY;
	}

	return true;
}

bool hmb_spaceMgmt_entry_init(void)
{
	uint32_t i;
	HmbMapInfo *hmb_map;

	HMB_SPACEMGMT_CTRL.table_split_num = HMB_CTRL.list_cnt;
	if(HMB_SPACEMGMT_CTRL.table_cnt_max % HMB_CTRL.list_cnt)
	{
		HMB_SPACEMGMT_CTRL.table_split_unit = (HMB_SPACEMGMT_CTRL.table_cnt_max / HMB_SPACEMGMT_CTRL.table_split_num) + 1;
	}
	else
	{
		HMB_SPACEMGMT_CTRL.table_split_unit = (HMB_SPACEMGMT_CTRL.table_cnt_max / HMB_SPACEMGMT_CTRL.table_split_num);
	}

	HMB_SPACEMGMT_CTRL.table_ST_size = HMB_SPACEMGMT_CTRL.table_split_num * sizeof(HmbSplitTable);
	if((HMB_SPACEMGMT_CTRL.table_ST_mapped = hmb_calloc( \
					HMB_SPACEMGMT_CTRL.table_ST_size)) == NULL)
	{
		hmb_debug("Failed to map split table into HMB.");
		return false;
	}

	if((HMB_SPACEMGMT_CTRL.table_split_mapped = \
				(HmbMappedAddr **)calloc(HMB_SPACEMGMT_CTRL.table_split_num, \
				sizeof(HmbMappedAddr *))) == NULL)
	{
		hmb_debug("Failed to map split mapped table.");
		return false;
	}

	HMB_SPACEMGMT_CTRL.table_bm_ST_size = HMB_SPACEMGMT_CTRL.table_split_num * sizeof(HmbSplitTable);
	if((HMB_SPACEMGMT_CTRL.table_bm_ST_mapped = hmb_calloc( \
					HMB_SPACEMGMT_CTRL.table_bm_ST_size)) == NULL)
	{
		hmb_debug("Failed to map split table into HMB.");
		return false;
	}
	
	if((HMB_SPACEMGMT_CTRL.table_bm_split_mapped = \
				(HmbMappedAddr **)calloc(HMB_SPACEMGMT_CTRL.table_split_num, \
				sizeof(HmbMappedAddr *))) == NULL)
	{
		hmb_debug("Failed to map split mapped table.");
		return false;
	}

	for(i=0; i<HMB_SPACEMGMT_CTRL.table_split_num; i++)
	{
		uint64_t size, size_bm;
		HmbMapInfo *map;
		HmbSegEnt *se;
		HmbSplitTable *entry_w, *entry_bm_w;

		if(i != HMB_SPACEMGMT_CTRL.table_split_num-1)
		{
			size_bm = size = HMB_SPACEMGMT_CTRL.table_split_unit;
		}
		else
		{
			size_bm = size = HMB_SPACEMGMT_CTRL.table_cnt_max % HMB_SPACEMGMT_CTRL.table_split_unit;
			if(size == 0)
			{
				size_bm = size = HMB_SPACEMGMT_CTRL.table_split_unit;
			}
		}
		size *= sizeof(HmbSharedEnt);
		size_bm *= sizeof(HmbSharedBitmapEnt);

		if((HMB_SPACEMGMT_CTRL.table_split_mapped[i] = hmb_calloc(size)) == NULL)
		{
			hmb_debug("Failed to map cache entry table into HMB.");
			return false;
		}
		
		if((map = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.table_split_mapped[i])) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}

		if((se = hmb_get_segEnt_by_id(false, map->entry_id)) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}

		entry_w = hmb_spaceMgmt_table_ST_get_by_idx(true, i);
		entry_w->seg_id = se->segment_id;
		entry_w->offset = se->offset;

		if((HMB_SPACEMGMT_CTRL.table_bm_split_mapped[i] = hmb_calloc(size_bm)) == NULL)
		{
			hmb_debug("Failed to map cache entry table into HMB.");
			return false;
		}

		if((map = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.table_bm_split_mapped[i])) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}

		if((se = hmb_get_segEnt_by_id(false, map->entry_id)) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}

		entry_bm_w = hmb_spaceMgmt_table_bm_ST_get_by_idx(true, i);
		entry_bm_w->seg_id = se->segment_id;
		entry_bm_w->offset = se->offset;

#if 0
		hmb_debug("[TBL] (%3u) ORI/sid: %3d, ORI/off: %7u, SHRD/sid: %3d, SHRD/off: %7u", \
				i, se->segment_id, se->offset, entry_r->seg_id, entry_r->offset);
#endif
	}

	if((HMB_SPACEMGMT_CTRL.table_mapped \
				= (HmbMappedAddr **)calloc( \
					HMB_SPACEMGMT_CTRL.table_cnt_max, \
					sizeof(HmbMappedAddr *))) == NULL)
	{
		hmb_debug("Failed to allocate heap memory.");
		return false;
	}

	for(i=0; i<HMB_SPACEMGMT_CTRL.table_cnt_max; i++)
	{
		uint32_t j;

		HmbSegEnt *hmb_segment_entry_r;
		HmbSharedEnt *entry_w = hmb_spaceMgmt_table_get_by_idx(true, i);
		HmbMappedAddr **mapped = &(HMB_SPACEMGMT_CTRL.table_mapped[i]);

		if((*mapped = hmb_calloc(HMB_SPACEMGMT_CTRL.cache_unit)) == NULL)
		{
			uint32_t prev_table_cnt_max = HMB_SPACEMGMT_CTRL.table_cnt_max;
			uint32_t prev_bm_parts_cnt = HMB_SPACEMGMT_CTRL.bm_parts_cnt;
			uint16_t prev_split_num = HMB_SPACEMGMT_CTRL.table_split_num;

			hmb_debug("Requested #entries: %u, but #allocated entries: %u", prev_table_cnt_max, i); 

			HMB_SPACEMGMT_CTRL.table_cnt_max = i;
			hmb_meta_get(true)->C__n_max_entries = HMB_SPACEMGMT_CTRL.table_cnt_max;

			HMB_SPACEMGMT_CTRL.table_size_max = HMB_SPACEMGMT_CTRL.table_cnt_max * sizeof(HmbSharedEnt);

			if(HMB_SPACEMGMT_CTRL.table_cnt_max % HMB_SPACEMGMT_CTRL.table_split_unit)
			{
				HMB_SPACEMGMT_CTRL.table_split_num = HMB_SPACEMGMT_CTRL.table_cnt_max / HMB_SPACEMGMT_CTRL.table_split_unit + 1;
			}
			else
			{
				HMB_SPACEMGMT_CTRL.table_split_num = HMB_SPACEMGMT_CTRL.table_cnt_max / HMB_SPACEMGMT_CTRL.table_split_unit;
			}

			if(HMB_SPACEMGMT_CTRL.table_split_num != prev_split_num)
			{
				HMB_SPACEMGMT_CTRL.sorted_split_num = HMB_SPACEMGMT_CTRL.table_split_num;

				for(j = HMB_SPACEMGMT_CTRL.table_split_num; j < prev_split_num; j++)
				{
					hmb_free(HMB_SPACEMGMT_CTRL.table_split_mapped[j]);
					hmb_free(HMB_SPACEMGMT_CTRL.table_bm_split_mapped[j]);

					hmb_free(HMB_SPACEMGMT_CTRL.sorted_split_mapped[j]);
				}
				hmb_debug("***** Split number was decreased from %u to %u", \
						prev_split_num, HMB_SPACEMGMT_CTRL.table_split_num);
			}

			HMB_SPACEMGMT_CTRL.bm_parts_cnt = HMB_SPACEMGMT_CTRL.table_cnt_max / (sizeof(HmbBitmap32) * 8);
			if(HMB_SPACEMGMT_CTRL.table_cnt_max % (sizeof(HmbBitmap32) * 8))
			{
				HMB_SPACEMGMT_CTRL.bm_parts_cnt++;
			}

			if(hmb_spaceMgmt_bm_fill_overflowed(i, prev_table_cnt_max)== false)
			{
				hmb_debug("Failed to fill overflowed bitmap area.");
				return false;
			}

			for(j=HMB_SPACEMGMT_CTRL.bm_parts_cnt; j<prev_bm_parts_cnt; j++)
			{
				if(hmb_spaceMgmt_bm_empty_delete(j) == false)
				{
					hmb_debug("Invalid relationship.");
					return false;
				}
			}
			hmb_meta_get(true)->C__bm_parts_cnt = HMB_SPACEMGMT_CTRL.bm_parts_cnt;

			hmb_debug("%u bitmap parts are deleted.", prev_bm_parts_cnt - HMB_SPACEMGMT_CTRL.bm_parts_cnt);

			return true;
		}	

		hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__search_map_info, true);
		if((hmb_map = hmb_mapInfo_search(*mapped)) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}
		hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__search_map_info, false);

		hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__get_id, true);
		if((hmb_segment_entry_r = \
				   	hmb_get_segEnt_by_id(false, hmb_map->entry_id)) == NULL)
		{
			hmb_debug("Invalid relationship.");
			return false;
		}
		hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__get_id, false);

		entry_w->usable = 0;
		entry_w->segment = hmb_segment_entry_r->segment_id;
		entry_w->offset = hmb_segment_entry_r->offset;

		hmb_spaceMgmt_table_bm_set_fully(false, i);
	}

	hmb_debug("Initialization was completed!");

	return true;
}

bool hmb_spaceMgmt_init(void)
{

	
	hmb_debug("space Mgmt 1");
	
	/* HmbMeta *meta_w;
	//HmbMapInfo *m_table_ST, *m_heads_ST, *m_sorted_ST, 
	//		   *m_victimAll, *m_bm, *m_bm_empty, *m_urgency, *m_victimRc, 
	//		   *m_bm_empty_table, *m_table_bm_ST;
	//HmbSegEnt *e_table_ST, *e_heads_ST, *e_sorted_ST, 
	//	*e_victimAll, *e_bm, *e_bm_empty, *e_urgency, *e_victimRc, 
	//	*e_bm_empty_table, *e_table_bm_ST; */

	hmb_meta_get(true)->C__pctg_explicitFlush = 100;

	HMB_SPACEMGMT_CTRL.table_cnt = 0;

	HMB_SPACEMGMT_CTRL.table_cnt_max = hmb_spaceMgmt_table_max_entries();
	
	HMB_SPACEMGMT_CTRL.table_size_max \
		= HMB_SPACEMGMT_CTRL.table_cnt_max * sizeof(HmbSharedEnt);

	hmb_debug("space Mgmt 2");
	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_heads, true);

/*
	hmb_debug("space Mgmt 2-1");
	if(hmb_spaceMgmt_heads_init(hmb_spaceMgmt_hash_calc_bits(HMB_SPACEMGMT_CTRL.table_cnt_max)) == false)
	{
		hmb_debug("Failed to initialize cache heads");
		return false;
	}

	hmb_debug("space Mgmt 3");
	
	
	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_heads, false);

	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_sorted, true);
	if(hmb_spaceMgmt_sorted_init(HMB_SPACEMGMT_CTRL.table_cnt_max) == false)
	{
		hmb_debug("Failed to initialize sorted array.");
		return false;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_sorted, false);

	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_bitmap, true);
	if(hmb_spaceMgmt_bm_init(HMB_SPACEMGMT_CTRL.table_cnt_max) == false)
	{
		hmb_debug("Failed to initialize bitmap");
		return false;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_bitmap, false);

	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_entry, true);
	if(hmb_spaceMgmt_entry_init() == false)
	{
		hmb_debug("Failed to initialize cache table.");
		return false;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_entry, false);


	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_insert_map_info, true);
	// [1] Write metadata table information in HMB's metadata area 
	if((m_table_ST = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.table_ST_mapped)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_table_ST = hmb_get_segEnt_by_id(false, m_table_ST->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if((m_heads_ST = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.heads_ST_mapped)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_heads_ST = hmb_get_segEnt_by_id(false, m_heads_ST->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if((m_sorted_ST = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.sorted_ST_mapped)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_sorted_ST = hmb_get_segEnt_by_id(false, m_sorted_ST->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if((m_victimAll = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.victimAll)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_victimAll = hmb_get_segEnt_by_id(false, m_victimAll->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if((m_bm = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.bm)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_bm = hmb_get_segEnt_by_id(false, m_bm->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if((m_bm_empty = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.bm_empty)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_bm_empty = hmb_get_segEnt_by_id(false, m_bm_empty->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if((m_urgency = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.urgency)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_urgency = hmb_get_segEnt_by_id(false, m_urgency->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if((m_victimRc = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.victimRc)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_victimRc = hmb_get_segEnt_by_id(false, m_victimRc->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if((m_bm_empty_table = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.bm_empty_table)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_bm_empty_table = hmb_get_segEnt_by_id(false, m_bm_empty_table->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if((m_table_bm_ST = hmb_mapInfo_search(HMB_SPACEMGMT_CTRL.table_bm_ST_mapped)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}
	if((e_table_bm_ST = hmb_get_segEnt_by_id(false, m_table_bm_ST->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	if(HMB_CTRL.meta_mapped.r == NULL || HMB_CTRL.meta_mapped.w == NULL)
	{
		hmb_debug("Invalid relationship.");
		return false;
	}

	meta_w = hmb_meta_get(true);

	meta_w->C__table_ST_seg_id = e_table_ST->segment_id;
	meta_w->C__table_ST_offset = e_table_ST->offset;
	meta_w->C__table_ST_num = HMB_SPACEMGMT_CTRL.table_split_num;
	meta_w->C__table_ST_unit = HMB_SPACEMGMT_CTRL.table_split_unit;

	meta_w->C__n_max_entries = HMB_SPACEMGMT_CTRL.table_cnt_max;

	meta_w->C__heads_ST_seg_id = e_heads_ST->segment_id;
	meta_w->C__heads_ST_offset = e_heads_ST->offset;
	meta_w->C__heads_ST_num = HMB_SPACEMGMT_CTRL.heads_split_num;
	meta_w->C__heads_ST_unit = HMB_SPACEMGMT_CTRL.heads_split_unit;

	meta_w->C__heads_hash_bit = HMB_SPACEMGMT_CTRL.heads_hash_bit;
	meta_w->C__heads_cnt_max = HMB_SPACEMGMT_CTRL.heads_cnt;

	meta_w->C__sorted_ST_seg_id = e_sorted_ST->segment_id;
	meta_w->C__sorted_ST_offset = e_sorted_ST->offset;
	meta_w->C__sorted_ST_num = HMB_SPACEMGMT_CTRL.sorted_split_num;
	meta_w->C__sorted_ST_unit = HMB_SPACEMGMT_CTRL.sorted_split_unit;

	meta_w->C__victimAll_seg_id = e_victimAll->segment_id;
	meta_w->C__victimAll_offset = e_victimAll->offset;

	meta_w->C__bm_seg_id = e_bm->segment_id;
	meta_w->C__bm_offset = e_bm->offset;

	meta_w->C__bm_empty_seg_id = e_bm_empty->segment_id;
	meta_w->C__bm_empty_offset = e_bm_empty->offset;

	meta_w->C__urgency_seg_id = e_urgency->segment_id;
	meta_w->C__urgency_offset = e_urgency->offset;

	meta_w->C__victimRc_seg_id = e_victimRc->segment_id;
	meta_w->C__victimRc_offset = e_victimRc->offset;

	meta_w->C__bm_empty_table_seg_id = e_bm_empty_table->segment_id;
	meta_w->C__bm_empty_table_offset = e_bm_empty_table->offset;

	meta_w->C__table_bm_ST_seg_id = e_table_bm_ST->segment_id;
	meta_w->C__table_bm_ST_offset = e_table_bm_ST->offset;

	meta_w->C__cache_unit_bits = HMB_SPACEMGMT_CTRL.cache_unit_bits;

	hmb_timeDbg_record(&HMB_DEBUG_TIME.SM__init_insert_map_info, false);

	hmb_debug("table_ST in %d.%u", \
			hmb_meta_get(false)->C__table_ST_seg_id, \
			hmb_meta_get(false)->C__table_ST_offset);
	hmb_debug("table_bm_ST in %d.%u", \
			hmb_meta_get(false)->C__table_bm_ST_seg_id, \
			hmb_meta_get(false)->C__table_bm_ST_offset);
	hmb_debug("sorted_ST in %d.%u", \
			hmb_meta_get(false)->C__sorted_ST_seg_id, \
			hmb_meta_get(false)->C__sorted_ST_offset);
	
	hmb_debug("######################## TIME-DEBUG ########################");
	hmb_debug("  [1] In hmb_malloc() and hmb_calloc()");
	hmb_debug("     - Calculating maximum allocable size : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.ll__calc_max_allocable_size));
	hmb_debug("     - Searching empty space              : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.ll__search_empty_space));
	hmb_debug("     - Getting New ID                     : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.ll__get_new_id));
	hmb_debug("     - Getting ID of some structures      : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.ll__get_id));
	hmb_debug("     - Allocating memory in the ctrl.     : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.ll__ctrl_malloc));
	hmb_debug("     - Mapping HMB address into ctrl.     : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.ll__map_pci));
	hmb_debug("     - Inserting entry                    : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.ll__insert_entry));
	hmb_debug("     - Setting memory to zero             : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.ll__memset));
	hmb_debug("     - Searching mapping info.            : %.9lf\n", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.ll__search_map_info));
	hmb_debug("  [2] In initializing Space Managmemt Module ...");
	hmb_debug("     - Initializing Heads  related parts  : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.SM__init_heads));
	hmb_debug("     - Initializing Sorted related parts  : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.SM__init_sorted));
	hmb_debug("     - Initializing Bitmap related parts  : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.SM__init_bitmap));
	hmb_debug("     - Initializing Entry  related parts  : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.SM__init_entry));
	hmb_debug("     - Inserting mapping infomration      : %.9lf", hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME.SM__init_insert_map_info));
	hmb_debug("############################################################");
	// [1] 

 	// Initialization is completed! */
	HMB_SPACEMGMT_CTRL.inited = true;

#if 0
	HMB_SPACEMGMT_CTRL.timer_WB = timer_new_ns(QEMU_CLOCK_REALTIME, hmb_spaceMgmt_WB_flusher, NULL);
	timer_mod(HMB_SPACEMGMT_CTRL.timer_WB, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + 1E9);
#endif

	return true;
}

void hmb_spaceMgmt_init_cache_unit(uint32_t cache_unit)
{
	HMB_SPACEMGMT_CTRL.cache_unit_bits = cache_unit;
	HMB_SPACEMGMT_CTRL.cache_unit      = 1 << cache_unit;

	hmb_debug("");
	hmb_debug("#### INITIALIZATION OF CACHE SIZE ####");
	hmb_debug("  - Cache unit: %lubytes (--> %u bits)\n", \
			HMB_SPACEMGMT_CTRL.cache_unit, HMB_SPACEMGMT_CTRL.cache_unit_bits);
}

void hmb_spaceMgmt_lock(void)
{
	HmbSync *lock_r = &(hmb_meta_get(false)->lock);
	HmbSync *lock_w = &(hmb_meta_get(true)->lock);

	smp_mb();
	__atomic_add_fetch(lock_w, 1, __ATOMIC_SEQ_CST);

	smp_mb();
	while(__atomic_load_n(lock_r, __ATOMIC_SEQ_CST) != 1)
	{
		smp_mb();
	}
}

void hmb_spaceMgmt_unlock(void)
{
	HmbSync *lock_w = &(hmb_meta_get(true)->lock);

	smp_mb();
	__atomic_sub_fetch(lock_w, 1, __ATOMIC_SEQ_CST);
	smp_mb();
}

void hmb_spaceMgmt_WB_flusher(void *opaque)
{
	static uint64_t cnt = 0;

	hmb_debug("[%d] <%lu>", hmb_gettid(), ++cnt);
#if 0
	if(hmb_spaceMgmt_WB_flush_implcit() == false)
	{
		hmb_debug("Failed to do flush.");
	}
#endif
	timer_mod(HMB_SPACEMGMT_CTRL.timer_WB, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + 1E9);
}

bool hmb_spaceMgmt_WB_flush_implicit(void *ssd, int64_t *expire_time)
{
	return hmb_spaceMgmt_WB_flush(\
			hmb_spaceMgmt_table_get_cache_num() * HMB_SPACEMGMT_IMPLICIT_FLUSH_RATIO, \
			ssd, expire_time);
}

bool hmb_spaceMgmt_WB_flush_explicit(uint32_t n_lb, void *ssd, int64_t *expire_time)
{
	bool ret;

	//hmb_spaceMgmt_lock();
	ret = hmb_spaceMgmt_WB_flush(n_lb, ssd, expire_time);
	//hmb_spaceMgmt_unlock();

	return ret;
}

bool hmb_spaceMgmt_WB_flush(uint32_t n_lb, void *_ssd, int64_t *expire_time)
{
/*	uint32_t i;
	uint32_t flush_num_per_ul[HMB_SPACEMGMT_UL_NUMBER] = {0, };
	uint32_t flush_num_total = 0, flush_num_allocated = 0;
	uint32_t weight_total = 0;
	int64_t expire_time_cur;

	struct ssdstate *ssd = _ssd;

	uint64_t prev_lpn;
	uint64_t prev_nlp;
	bool prev_applied = false;
	bool prev_with_read = false;

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(n_lb == 0)
	{
		hmb_debug("WARNING: is it right?");
		return true;
	}
#endif

	flush_num_total = n_lb;
	for(i=0; i<HMB_SPACEMGMT_UL_NUMBER; i++)
	{
		weight_total += HMB_SPACEMGMT_UL_WEIGHT[i];
	}

	for(i=0; i<HMB_SPACEMGMT_UL_NUMBER; i++)
	{
		double r = ((double)HMB_SPACEMGMT_UL_WEIGHT[i]) / weight_total;

		flush_num_per_ul[i] = flush_num_total * r;
		flush_num_allocated += flush_num_per_ul[i];
	}

	if(flush_num_allocated < flush_num_total)
	{
		flush_num_per_ul[0] += (flush_num_total - flush_num_allocated);
	}

	flush_num_allocated = 0;
	for(i=0; i<HMB_SPACEMGMT_UL_NUMBER; i++)
	{
		while(flush_num_per_ul[i])
		{
			HmbSharedEnt *entry_r, *entry_w;
			int32_t idx_head;
			//uint64_t t_dbg = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

			idx_head = hmb_spaceMgmt_WB_sorted_get_head_idx(i+1);
			if(idx_head < 0)
			{
				break;
			}

			entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx_head);
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
			if(entry_r->dirty == 0)
			{
				hmb_debug("Invalid relationship!: It must be dirty.");
				return false;
			}
#endif

			if(hmb_spaceMgmt_table_bm_isCached_fully(idx_head) == true)
			{
				// Read data from HMB to NAND(i.e. heap) 
				hmb_spaceMgmt_heap_rw( \
						true, \
						(entry_r->lpn) << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
						HMB_SPACEMGMT_CTRL.cache_unit, \
						hmb_spaceMgmt_mappedAddr_get_by_idx(false, idx_head));

#if 0
				if(*expire_time < (expire_time_cur = SSD_WRITE(ssd, \
								hmb_spaceMgmt_nlp_to_nlb(1), \
								hmb_spaceMgmt_lpn_to_lba(entry_r->lpn))))
				{
					*expire_time = expire_time_cur;
				}
#endif

				if(prev_applied == false)
				{
					prev_lpn = entry_r->lpn;
					prev_nlp = 1;
					prev_applied = true;
					prev_with_read = false;
				}

				else
				{
					if(prev_lpn + prev_nlp == entry_r->lpn)
					{
						if(prev_with_read == true)
						{
							expire_time_cur = SSD_READ(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}

							expire_time_cur = SSD_WRITE(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}

							prev_lpn = entry_r->lpn;
							prev_nlp = 1;

							prev_with_read = false;
						}
						else
						{
							prev_nlp++;
						}
					}
					else
					{
						if(prev_with_read == true)
						{
							expire_time_cur = SSD_READ(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}
						}

						expire_time_cur = SSD_WRITE(ssd, \
								hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
								hmb_spaceMgmt_lpn_to_lba(prev_lpn));
						if(*expire_time < expire_time_cur)
						{
							*expire_time = expire_time_cur;
						}

						prev_lpn = entry_r->lpn;
						prev_nlp = 1;
						prev_with_read = false;
					}
				}
#if 0
				hmb_debug("[F] LBA: 0x%010lX", entry_r->lpn);
#endif
			}
			else
			{
				uint32_t j;
				uint32_t last = hmb_spaceMgmt_nlp_to_nlb(1);

				for(j=0; j<last; j++)
				{
					if(hmb_spaceMgmt_table_bm_isCached_partially(idx_head, j) == true)
					{
						uint64_t len, offset, diff;
						void *buf;

						len = 512;
						offset = hmb_spaceMgmt_lpn_to_bytes(entry_r->lpn);
						buf = hmb_spaceMgmt_mappedAddr_get_by_idx(false, idx_head);

						diff = (j << 9);
						offset += diff;
						buf += diff;

						hmb_spaceMgmt_heap_rw(true, offset, len, buf);
					}
#if 0
					hmb_debug("[P] SECTOR: 0x%08lX, LBA: 0x%08lX, BM: 0x%8X, ID_OWN: %6u, ID_INTER: %1u, ID_NXT: %6u, ID_PRV: %6u, CACHED: %1d, U: %lu, D: %lu / %lu", \
							entry_r->lpn << (HMB_SPACEMGMT_CTRL.cache_unit_bits - 9), entry_r->lpn, \
							hmb_spaceMgmt_table_bm_get_by_idx(false, idx_head)->filled, idx_head, j, entry_r->e_prev, entry_r->e_next, \
							hmb_spaceMgmt_table_bm_isCached_partially(idx_head, j), entry_r->usable, entry_r->dirty, t_dbg);
#endif
				}
				//
				   Flushed cache will be filled fully.
				 
				hmb_spaceMgmt_heap_rw( \
						false, \
						(entry_r->lpn) << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
						HMB_SPACEMGMT_CTRL.cache_unit, \
						hmb_spaceMgmt_mappedAddr_get_by_idx(true, idx_head));
				hmb_spaceMgmt_table_bm_set_fully(true, idx_head);

				
				//   Consideration of partially written(i.e. dirty) data
				//	   - If some of cached data is cached and dirty, they need to be written to NAND.
				//		   -> However, we cannot distinguish what is either clean or dirty each other.
				//		   -> As a result, we only flush cached data all at once without their dirtiness.
				//	   - And some part of data in NAND must be read in this case.
				//		   -> Consequently, we must read one logical page.
				 
				if(prev_applied == false)
				{
					prev_lpn = entry_r->lpn;
					prev_nlp = 1;
					prev_applied = true;
					prev_with_read = true;
				}

				else
				{
					if(prev_lpn + prev_nlp == entry_r->lpn)
					{
						if(prev_with_read == false)
						{
							expire_time_cur = SSD_WRITE(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}

							prev_lpn = entry_r->lpn;
							prev_nlp = 1;

							prev_with_read = true;
						}
						else
						{
							prev_nlp++;
						}
					}
					else
					{
						if(prev_with_read == true)
						{
							expire_time_cur = SSD_READ(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}
						}

						expire_time_cur = SSD_WRITE(ssd, \
								hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
								hmb_spaceMgmt_lpn_to_lba(prev_lpn));
						if(*expire_time < expire_time_cur)
						{
							*expire_time = expire_time_cur;
						}

						prev_lpn = entry_r->lpn;
						prev_nlp = 1;
						prev_with_read = true;
					}
				}
			}

			if(hmb_spaceMgmt_WB_sorted_delete(i+1, idx_head) == false)
			{
				hmb_debug("Failed to delete sorted for WB.");
				return false;
			}

			entry_w = hmb_spaceMgmt_table_get_by_idx(true, idx_head);
			entry_w->urgency = HMB_SPACEMGMT_UL_DISABLED;
			hmb_spaceMgmt_shared_set_dirty(false, idx_head);
			hmb_spaceMgmt_table_nDirty_inc(false);

			if(hmb_spaceMgmt_RCOnly_sorted_insert_tail(idx_head) == false)
			{
				hmb_debug("Failed to insert new entry into LRU for clean caches");
				return false;
			}

			++flush_num_allocated;
			--flush_num_per_ul[i];
		}
	}

	if(flush_num_allocated < flush_num_total)
	{
		for(i=0; i<HMB_SPACEMGMT_UL_NUMBER; i++)
		{
			HmbSharedEnt *entry_r, *entry_w;
			int32_t idx_head;

			if(flush_num_per_ul[i])
			{
				continue;
			}

			while(flush_num_allocated < flush_num_total)
			{
				//	uint64_t t_dbg = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

				idx_head = hmb_spaceMgmt_WB_sorted_get_head_idx(i+1);
				if(idx_head < 0)
				{
					break;
				}

				entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx_head);
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
				if(entry_r->dirty == 0)
				{
					hmb_debug("Invalid relationship!: it must be clean.");
					return false;
				}
#endif

				if(hmb_spaceMgmt_table_bm_isCached_fully(idx_head) == true)
				{
					hmb_spaceMgmt_heap_rw( \
							true, \
							(entry_r->lpn) << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
							HMB_SPACEMGMT_CTRL.cache_unit, \
							hmb_spaceMgmt_mappedAddr_get_by_idx(false, idx_head));

#if 0
					if(*expire_time < (expire_time_cur = SSD_WRITE(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(1), \
									hmb_spaceMgmt_lpn_to_lba(entry_r->lpn))))
					{
						*expire_time = expire_time_cur;
					}
#endif
					if(prev_applied == false)
					{
						prev_lpn = entry_r->lpn;
						prev_nlp = 1;
						prev_applied = true;
						prev_with_read = false;
					}

					else
					{
						if(prev_lpn + prev_nlp == entry_r->lpn)
						{
							if(prev_with_read == true)
							{
								expire_time_cur = SSD_READ(ssd, \
										hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
										hmb_spaceMgmt_lpn_to_lba(prev_lpn));
								if(*expire_time < expire_time_cur)
								{
									*expire_time = expire_time_cur;
								}

								expire_time_cur = SSD_WRITE(ssd, \
										hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
										hmb_spaceMgmt_lpn_to_lba(prev_lpn));
								if(*expire_time < expire_time_cur)
								{
									*expire_time = expire_time_cur;
								}

								prev_lpn = entry_r->lpn;
								prev_nlp = 1;

								prev_with_read = false;
							}
							else
							{
								prev_nlp++;
							}
						}
						else
						{
							if(prev_with_read == true)
							{
								expire_time_cur = SSD_READ(ssd, \
										hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
										hmb_spaceMgmt_lpn_to_lba(prev_lpn));
								if(*expire_time < expire_time_cur)
								{
									*expire_time = expire_time_cur;
								}
							}

							expire_time_cur = SSD_WRITE(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}

							prev_lpn = entry_r->lpn;
							prev_nlp = 1;
							prev_with_read = false;
						}
					}
				}
#if 0
				hmb_debug("[F] SECTOR: 0x%08lX, LBA: 0x%08lX, BM: 0x%8X, ID_OWN: %6u, CACHED: %1d, U: %lu, D: %lu, SAME?: %lu", \
						entry_r->lpn << (HMB_SPACEMGMT_CTRL.cache_unit_bits - 9), entry_r->lpn, \
						hmb_spaceMgmt_table_bm_get_by_idx(false, idx_head)->filled, idx_head, \
						hmb_spaceMgmt_table_bm_isCached_fully(idx_head), entry_r->usable, entry_r->dirty, t_dbg);
#endif
				else
				{
					uint32_t j;
					uint32_t last = hmb_spaceMgmt_nlp_to_nlb(1);

					for(j=0; j<last; j++)
					{
						if(hmb_spaceMgmt_table_bm_isCached_partially(idx_head, j) == true)
						{
							uint64_t len, offset, diff;
							void *buf;

							len = 512;
							offset = hmb_spaceMgmt_lpn_to_bytes(entry_r->lpn);
							buf = hmb_spaceMgmt_mappedAddr_get_by_idx(false, idx_head);

							diff = (j << 9);
							offset += diff;
							buf += diff;

							hmb_spaceMgmt_heap_rw(true, offset, len, buf);
						}
#if 0
						hmb_debug("[P] SECTOR: 0x%08lX, LBA: 0x%08lX, BM: 0x%8X, ID_OWN: %6u, ID_INTER: %1u, ID_NXT: %6u, ID_PRV: %6u, CACHED: %1d, U: %lu, D: %lu / %lu", \
								entry_r->lpn << (HMB_SPACEMGMT_CTRL.cache_unit_bits - 9), entry_r->lpn, \
								hmb_spaceMgmt_table_bm_get_by_idx(false, idx_head)->filled, idx_head, j, entry_r->e_prev, entry_r->e_next, \
								hmb_spaceMgmt_table_bm_isCached_partially(idx_head, j), entry_r->usable, entry_r->dirty, t_dbg);
#endif
					}
					//
					//   Flushed cached will be filled fully.
					 
					hmb_spaceMgmt_heap_rw( \
							false, \
							(entry_r->lpn) << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
							HMB_SPACEMGMT_CTRL.cache_unit, \
							hmb_spaceMgmt_mappedAddr_get_by_idx(true, idx_head));
					hmb_spaceMgmt_table_bm_set_fully(true, idx_head);

					if(prev_applied == false)
					{
						prev_lpn = entry_r->lpn;
						prev_nlp = 1;
						prev_applied = true;
						prev_with_read = true;
					}

					else
					{
						if(prev_lpn + prev_nlp == entry_r->lpn)
						{
							if(prev_with_read == false)
							{
								expire_time_cur = SSD_WRITE(ssd, \
										hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
										hmb_spaceMgmt_lpn_to_lba(prev_lpn));
								if(*expire_time < expire_time_cur)
								{
									*expire_time = expire_time_cur;
								}

								prev_lpn = entry_r->lpn;
								prev_nlp = 1;

								prev_with_read = true;
							}
							else
							{
								prev_nlp++;
							}
						}
						else
						{
							
							//   Consideration of partially written(i.e. dirty) data
							//   - If some of cached data is cached and dirty, they need to be written to NAND.
							 //  -> However, we cannot distinguish what is either clean or dirty each other.
							//   -> As a result, we only flush cached data all at once without their dirtiness.
							//   - And some part of data in NAND must be read in this case.
							//   -> Consequently, we must read one logical page.
						
							if(prev_with_read == true)
							{
								expire_time_cur = SSD_READ(ssd, \
										hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
										hmb_spaceMgmt_lpn_to_lba(prev_lpn));
								if(*expire_time < expire_time_cur)
								{
									*expire_time = expire_time_cur;
								}
							}

							expire_time_cur = SSD_WRITE(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(*expire_time < expire_time_cur)
							{
								*expire_time = expire_time_cur;
							}

							prev_lpn = entry_r->lpn;
							prev_nlp = 1;
							prev_with_read = true;
						}
					}
				}

				if(hmb_spaceMgmt_WB_sorted_delete(i+1, idx_head) == false)
				{
					hmb_debug("Failed to delete WB metadata.");
					return false;
				}

				entry_w = hmb_spaceMgmt_table_get_by_idx(true, idx_head);
				entry_w->urgency = HMB_SPACEMGMT_UL_DISABLED;
				hmb_spaceMgmt_shared_set_dirty(false, idx_head);
				hmb_spaceMgmt_table_nDirty_inc(false);

				if(hmb_spaceMgmt_RCOnly_sorted_insert_tail(idx_head) == false)
				{
					hmb_debug("Failed to insert new entry into LRU for clean caches");
					return false;
				}

				flush_num_allocated++;
			}
		}
	}

	if(prev_applied == true)
	{
		if(prev_with_read == true)
		{
			expire_time_cur = SSD_READ(ssd, \
					hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
					hmb_spaceMgmt_lpn_to_lba(prev_lpn));
			if(*expire_time < expire_time_cur)
			{
				*expire_time = expire_time_cur;
			}
		}

		expire_time_cur = SSD_WRITE(ssd, \
				hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
				hmb_spaceMgmt_lpn_to_lba(prev_lpn));
		if(*expire_time < expire_time_cur)
		{
			*expire_time = expire_time_cur;
		}
	}
*/
	return true;
}

bool nvme_flush_from_hmb(void *nvmeReq)
{
	/*
	uint64_t i;
	int32_t idx_head;
	bool ret = true;
	uint64_t n_loops = 0;
	int64_t expire_time = 0, expire_time_cur;
	NvmeRequest *req = nvmeReq;
	struct ssdstate *ssd = &(((NvmeCtrl *)HMB_CTRL.parent)->ssd);

	uint64_t prev_lpn;
	uint64_t prev_nlp;
	bool prev_applied = false;
	bool prev_with_read = false;

#if 0
	hmb_debug("[AMP-INFO] Write: x %.6lf (%lu/%lu),\tread: x %.6lf (%lu/%lu)", \
			((double)HMB_DEBUG__N_WRITTEN_PAGES * 4096) / (HMB_DEBUG__N_WRITE_REQUESTED_SECTORS * 512), \
			HMB_DEBUG__N_WRITTEN_PAGES * 4096, HMB_DEBUG__N_WRITE_REQUESTED_SECTORS * 512, \
			((double)HMB_DEBUG__N_READ_PAGES * 4096) / (HMB_DEBUG__N_READ_REQUESTED_SECTORS * 512), \
			HMB_DEBUG__N_READ_PAGES * 4096, HMB_DEBUG__N_READ_REQUESTED_SECTORS * 512);

	if(HMB_DEBUG__N_OVERFLOWED != 0)
	{
		hmb_debug("Average NAND I/O time overflowed: %12.9lfs [%3lu/%6lu]",
				((double)(HMB_DEBUG__SUM_OVERFLOWED_TIME)) / HMB_DEBUG__N_OVERFLOWED / 1E9, \
				HMB_DEBUG__N_OVERFLOWED, HMB_DEBUG__N_REQUESTS);
	}

	else
	{
		hmb_debug("Now, we do not have overflowed results. (#total requests: /%lu)",
				HMB_DEBUG__N_REQUESTS);
	}
#endif

#ifdef HMB_DEBUGGING_TIME
	hmb_debug("[TIME-DBG] NAND simulating: %lf sec., HMB simulating: %lf sec.", \
			hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME_PER_REQUEST.of_femu) / HMB_DEBUG__N_REQUESTS, \
			hmb_timeDbg_get_time_s(&HMB_DEBUG_TIME_PER_REQUEST.of_hmb) / HMB_DEBUG__N_REQUESTS);
#endif

	if(!(HMB_CTRL.enable_hmb && \
				((NvmeCtrl *)HMB_CTRL.parent)->id_ctrl.vwc && \
				((NvmeCtrl *)HMB_CTRL.parent)->features.volatile_wc))
	{
		return true;
	}

	hmb_spaceMgmt_lock();

	req->expire_time = HMB_SPACEMGMT_BASE_TIME = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

	for(i=0; i<HMB_SPACEMGMT_UL_NUMBER; i++) 
	{    
		while((idx_head = hmb_spaceMgmt_WB_sorted_get_head_idx(i+1)) \
				!= HMB_HAS_NO_ENTRY)
		{    
			HmbSharedEnt *entry_r, *entry_w;
			//uint64_t t_dbg = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

			entry_r = hmb_spaceMgmt_table_get_by_idx(false, idx_head);
#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
			if(entry_r->dirty == 0)
			{    
				hmb_debug("Invalid relationship!: It must be dirty");
				ret = false;

				break;
			}    
#endif

			if(hmb_spaceMgmt_table_bm_isCached_fully(idx_head) == true)
			{
				hmb_spaceMgmt_heap_rw( \
						true, \
						(entry_r->lpn) << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
						HMB_SPACEMGMT_CTRL.cache_unit, \
						hmb_spaceMgmt_mappedAddr_get_by_idx(false, idx_head));
#if 0
				if(expire_time < (expire_time_cur = SSD_WRITE(ssd, \
								hmb_spaceMgmt_nlp_to_nlb(1), \
								hmb_spaceMgmt_lpn_to_lba(entry_r->lpn))))
				{
					expire_time = expire_time_cur;
				}
#endif
				if(prev_applied == false)
				{
					prev_lpn = entry_r->lpn;
					prev_nlp = 1;
					prev_applied = true;
					prev_with_read = false;
				}

				else
				{
					if(prev_lpn + prev_nlp == entry_r->lpn)
					{
						if(prev_with_read == true)
						{
							expire_time_cur = SSD_READ(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(expire_time < expire_time_cur)
							{
								expire_time = expire_time_cur;
							}

							expire_time_cur = SSD_WRITE(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(expire_time < expire_time_cur)
							{
								expire_time = expire_time_cur;
							}

							prev_lpn = entry_r->lpn;
							prev_nlp = 1;
							prev_with_read = false;
						}
						else
						{
							prev_nlp++;
						}
					}
					else
					{
						if(prev_with_read == true)
						{
							expire_time_cur = SSD_READ(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(expire_time < expire_time_cur)
							{
								expire_time = expire_time_cur;
							}
						}

						expire_time_cur = SSD_WRITE(ssd, \
								hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
								hmb_spaceMgmt_lpn_to_lba(prev_lpn));
						if(expire_time < expire_time_cur)
						{
							expire_time = expire_time_cur;
						}

						prev_lpn = entry_r->lpn;
						prev_nlp = 1;
						prev_with_read = false;
					}
				}
			}
#if 0
			hmb_debug("[F] LBA: 0x%010lX", entry_r->lpn);
#endif
			else
			{
				uint32_t j;
				uint32_t last = hmb_spaceMgmt_nlp_to_nlb(1);

				for(j=0; j<last; j++)
				{
					if(hmb_spaceMgmt_table_bm_isCached_partially(idx_head, j) == true)
					{
						uint64_t len, offset, diff;
						void *buf;

						len = 512;
						offset = hmb_spaceMgmt_lpn_to_bytes(entry_r->lpn);
						buf = hmb_spaceMgmt_mappedAddr_get_by_idx(false, idx_head);

						diff = (j << 9);
						offset += diff;
						buf += diff;

						hmb_spaceMgmt_heap_rw(true, offset, len, buf);
					}
#if 0
					hmb_debug("[P] SECTOR: 0x%08lX, LBA: 0x%08lX, BM: 0x%8X, ID_OWN: %6u, ID_INTER: %1u, ID_NXT: %6u, ID_PRV: %6u, CACHED: %1d, U: %lu, D: %lu / %lu", \
							entry_r->lpn << (HMB_SPACEMGMT_CTRL.cache_unit_bits - 9), entry_r->lpn, \
							hmb_spaceMgmt_table_bm_get_by_idx(false, idx_head)->filled, idx_head, j, entry_r->e_prev, entry_r->e_next, \
							hmb_spaceMgmt_table_bm_isCached_partially(idx_head, j), entry_r->usable, entry_r->dirty, t_dbg);
#endif
				}
				//
				/ /  Flushed cached will be filled fully.
				 
				hmb_spaceMgmt_heap_rw( \
							false, \
							(entry_r->lpn) << HMB_SPACEMGMT_CTRL.cache_unit_bits, \
							HMB_SPACEMGMT_CTRL.cache_unit, \
							hmb_spaceMgmt_mappedAddr_get_by_idx(true, idx_head));
				hmb_spaceMgmt_table_bm_set_fully(true, idx_head);

				//
				//   Consideration of partially written(i.e. dirty) data
				//   - If some of cached data is cached and dirty, they need to be written to NAND.
				//   -> However, we cannot distinguish what is either clean or dirty each other.
				//   -> As a result, we only flush cached data all at once without their dirtiness.
				//   - And some part of data in NAND must be read in this case.
				//   -> Consequently, we must read one logical page.
				
				if(prev_applied == false)
				{
					prev_lpn = entry_r->lpn;
					prev_nlp = 1;
					prev_applied = true;
					prev_with_read = true;
				}

				else
				{
					if(prev_lpn + prev_nlp == entry_r->lpn)
					{
						if(prev_with_read == false)
						{
							expire_time_cur = SSD_WRITE(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(expire_time < expire_time_cur)
							{
								expire_time = expire_time_cur;
							}

							prev_lpn = entry_r->lpn;
							prev_nlp = 1;

							prev_with_read = true;
						}
						else
						{
							prev_nlp++;
						}
					}
					else
					{
						//
						//   Consideration of partially written(i.e. dirty) data
						//   - If some of cached data is cached and dirty, they need to be written to NAND.
						//   -> However, we cannot distinguish what is either clean or dirty each other.
						//   -> As a result, we only flush cached data all at once without their dirtiness.
						//   - And some part of data in NAND must be read in this case.
						//   -> Consequently, we must read one logical page.
						//
						if(prev_with_read == true)
						{
							expire_time_cur = SSD_READ(ssd, \
									hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
									hmb_spaceMgmt_lpn_to_lba(prev_lpn));
							if(expire_time < expire_time_cur)
							{
								expire_time = expire_time_cur;
							}
						}

						expire_time_cur = SSD_WRITE(ssd, \
								hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
								hmb_spaceMgmt_lpn_to_lba(prev_lpn));
						if(expire_time < expire_time_cur)
						{
							expire_time = expire_time_cur;
						}

						prev_lpn = entry_r->lpn;
						prev_nlp = 1;
						prev_with_read = true;
					}
				}
			}

			if(hmb_spaceMgmt_WB_sorted_delete(i+1, idx_head) == false)
			{
				hmb_debug("Failed to delete sorted for WB.");
				ret = false;

				break;
			}

			entry_w = hmb_spaceMgmt_table_get_by_idx(true, idx_head);
			entry_w->urgency = HMB_SPACEMGMT_UL_DISABLED;
			hmb_spaceMgmt_shared_set_dirty(false, idx_head);
			hmb_spaceMgmt_table_nDirty_inc(false);

			if(hmb_spaceMgmt_RCOnly_sorted_insert_tail(idx_head) == false)
			{
				hmb_debug("Failed to insert new entry into LRU for clean caches");
				ret = false;

				break;
			}
			n_loops++;
		}
	}

	if(prev_applied == true)
	{
		if(prev_with_read == true)
		{
			expire_time_cur = SSD_READ(ssd, \
					hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
					hmb_spaceMgmt_lpn_to_lba(prev_lpn));
			if(expire_time < expire_time_cur)
			{
				expire_time = expire_time_cur;
			}
		}

		expire_time_cur = SSD_WRITE(ssd, \
				hmb_spaceMgmt_nlp_to_nlb(prev_nlp), \
				hmb_spaceMgmt_lpn_to_lba(prev_lpn));
		if(expire_time < expire_time_cur)
		{
			expire_time = expire_time_cur;
		}
	}

#if 0
	req->expire_time = 0;
#endif
	req->expire_time += expire_time;
//	hmb_debug("<F> Expire time: %.4lf sec. (#flushed LBs: %lu)", ((double)expire_time) / 1E9, n_loops);

#ifdef HMB_SPACEMGMT_DEBUG_CLOSELY
	if(expire_time > 2E9)
	{
		hmb_spaceMgmt_debug_ssd_delay_state();
	}
#endif

	hmb_spaceMgmt_unlock();
*/
	return true;
}

void hmb_spaceMgmt_debug_ssd_delay_state(void)
{
	/*struct ssdstate *ssd = &(((NvmeCtrl *)HMB_CTRL.parent)->ssd);
	struct ssdconf *sc = &(ssd->ssdparams);
	int CHANNEL_NB = sc->CHANNEL_NB;
	int FLASH_NB  = sc->FLASH_NB;
	int64_t *chnl_next_avail_time = ssd->chnl_next_avail_time;
	int64_t *chip_next_avail_time = ssd->chip_next_avail_time;
	uint64_t i;
	int64_t t_cur = HMB_SPACEMGMT_BASE_TIME;

	hmb_debug("#### FLASH DELAY INFO. #### ");
	for(i=0; i<FLASH_NB; i++) 
	{        
		hmb_debug("  - %3lu: %lfs. (%lfs.)", i, \
				((double)chip_next_avail_time[i] - t_cur)/1E9, \
				((double)chip_next_avail_time[i] - t_cur)/1E9);
	}        

	hmb_debug("#### CHANNEL DELAY INFO. #### ");
	for(i=0; i<CHANNEL_NB; i++) 
	{        
		hmb_debug("  - %3lu: %lfs. (%lfs.)", i, \
				((double)chnl_next_avail_time[i] - t_cur)/1E9, \
				((double)chnl_next_avail_time[i] - t_cur)/1E9);
	} */   
}

uint64_t hmb_spaceMgmt_lba_to_lpn(uint64_t lba)
{
	return lba >> (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits);
}

uint64_t hmb_spaceMgmt_lpn_to_lba(uint64_t lpn)
{
	return lpn << (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits);
}

uint64_t hmb_spaceMgmt_lpn_to_bytes(uint64_t lpn)
{
	return lpn << HMB_SPACEMGMT_CTRL.cache_unit_bits;
}

uint32_t hmb_spaceMgmt_nlb_to_nlp(uint32_t n_lb, uint64_t slba)
{
	uint32_t ret;
	uint64_t slba_cvted = slba % (1 << (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits));

	ret = ((uint32_t)slba_cvted + n_lb) >> (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits);
	if(((uint32_t)slba_cvted + n_lb) % (1 << (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits)))
	{
		++ret;
	}

#if 0
	if(n_lb % (1 << (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits)))
	{
		ret = (n_lb >> (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits)) + 1;
	}

	ret = n_lb >> (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits);
#endif

	return ret;
}

uint32_t hmb_spaceMgmt_nlp_to_nlb(uint32_t n_lp)
{
	return n_lp << (HMB_SPACEMGMT_CTRL.cache_unit_bits - HMB_SPACEMGMT_CTRL.sector_size_bits);
}
