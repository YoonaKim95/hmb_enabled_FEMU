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

#ifndef __SSD__HMB_SPACEMGMT_H__
#define __SSD__HMB_SPACEMGMT_H__

#include "hmb_debug.h"
#include "hmb_types.h"

#include <stdint.h>
#include <stdbool.h>

#define HMB_SPACEMGMT_BITMAP_BITS_PER_PART (8 * sizeof(HmbBitmap32))
#define HMB_SPACEMGMT_BITMAP_PART_MAX_VALUE (0xFFFFFFFF)

#define HMB_SPACEMGMT_UNLOCK (0)
#define HMB_SPACEMGMT_LOCK (1)

enum {
	HMB_SPACEMGMT_UL_DISABLED = 0,
	HMB_SPACEMGMT_UL_URGENT = 1,
	HMB_SPACEMGMT_UL_HIGH   = 2,
	HMB_SPACEMGMT_UL_MIDDLE = 3,
	HMB_SPACEMGMT_UL_LOW    = 4,
};
#define HMB_SPACEMGMT_UL_NUMBER (HMB_UL_NUMBER)

#define HMB_SPACEMGMT_IMPLICIT_FLUSH_RATIO (0.1)
#define HMB_SPACEMGMT_IMPLICIT_FLUSH_DURATION_NS (1E9) /* 1E9ns = 1sec. */
 
void hmb_spaceMgmt_heap_rw(bool is_write, uint64_t offset, uint64_t len, void *buf); // 

HmbSortedEnt *hmb_spaceMgmt_sorted_get_by_idx(bool for_write, uint32_t idx); // *
HmbSplitTable *hmb_spaceMgmt_sorted_ST_get_by_idx(bool for_write, uint32_t idx);

int32_t *hmb_spaceMgmt_victimAll_get(bool for_write); // *
int32_t *hmb_spaceMgmt_victimRc_get(bool for_write); // *

int32_t hmb_spaceMgmt_RC_sorted_get_head_idx (void); // *
bool    hmb_spaceMgmt_RC_sorted_set_head     (uint32_t idx); // *
bool    hmb_spaceMgmt_RC_sorted_insert_tail  (uint32_t idx); // *
bool    hmb_spaceMgmt_RC_sorted_insert_after (uint32_t idx, uint32_t idx_after); //  XXX
bool    hmb_spaceMgmt_RC_sorted_delete       (uint32_t idx); // *

int32_t hmb_spaceMgmt_RCOnly_sorted_get_head_idx (void); // *
bool    hmb_spaceMgmt_RCOnly_sorted_set_head     (uint32_t idx); // *
bool    hmb_spaceMgmt_RCOnly_sorted_insert_tail  (uint32_t idx); // *
bool    hmb_spaceMgmt_RCOnly_sorted_delete       (uint32_t idx); // *

int32_t  hmb_spaceMgmt_WB_sorted_get_head_idx (int32_t urgency); // *
int32_t* hmb_spaceMgmt_WB_sorted_get_head     (bool for_write, int32_t urgency); // *
bool     hmb_spaceMgmt_WB_sorted_set_head     (int32_t urgency, uint32_t idx); // *
bool     hmb_spaceMgmt_WB_sorted_insert_tail  (int32_t urgency, uint32_t idx); // *
bool     hmb_spaceMgmt_WB_sorted_delete       (int32_t urgency, uint32_t idx); // *
bool     hmb_spaceMgmt_WB_sorted_delete_head  (int32_t urgency);

bool hmb_spaceMgmt_RC_reorder (uint32_t idx); // *
bool hmb_spaceMgmt_RC_update  (uint32_t idx); // 
bool hmb_spaceMgmt_RC_evict   (uint32_t n_evict, int64_t *expire_time); // *
bool hmb_spaceMgmt_RC_caching (uint64_t lpn, uint32_t n_lb, bool do_data_copy, int64_t *expire_time); // *

bool hmb_spaceMgmt_RC_reorder_LRU (uint32_t idx); // *
bool hmb_spaceMgmt_RC_update_LRU  (uint32_t idx); // *
bool hmb_spaceMgmt_RC_evict_LRU   (uint32_t n_evict, int64_t *expire_time); // *

uint32_t      hmb_spaceMgmt_table_max_entries (void); // *
HmbSharedEnt* hmb_spaceMgmt_table_get_by_idx  (bool for_write, uint32_t idx); // *
HmbSharedEnt* hmb_spaceMgmt_table_get_by_lpn  (bool for_write, uint64_t lpn); // *
void          hmb_spaceMgmt_table_nCached_inc(bool is_inc);
void          hmb_spaceMgmt_table_nDirty_inc(bool is_inc);
bool          hmb_spaceMgmt_table_is_full(void);
uint32_t      hmb_spaceMgmt_table_get_cache_num(void);

HmbSplitTable* hmb_spaceMgmt_table_ST_get_by_idx(bool for_write, uint32_t idx);

HmbSplitTable*      hmb_spaceMgmt_table_bm_ST_get_by_idx      (bool for_write, uint32_t idx);
HmbSharedBitmapEnt* hmb_spaceMgmt_table_bm_get_by_idx         (bool for_write, uint32_t idx); // *
bool                hmb_spaceMgmt_table_bm_isCached_fully     (uint32_t idx);
bool                hmb_spaceMgmt_table_bm_isCached_partially (uint32_t idx, uint32_t idx_internal);
bool                hmb_spaceMgmt_table_bm_set                (bool enable, uint32_t idx, uint32_t idx_internal);
bool                hmb_spaceMgmt_table_bm_set_fully          (bool enable, uint32_t idx);
bool                hmb_spaceMgmt_table_bm_fill_overflowed    (uint32_t idx, uint32_t from, uint32_t to);

uint8_t hmb_spaceMgmt_hash_calc_bits (uint32_t max_entries); // *
bool    hmb_spaceMgmt_hash_verify   (uint32_t idx); // 

/** [1] Low-level APIs for HmbBitmap32 **/
bool hmb_spaceMgmt_bm_get_empty       (uint32_t *val); // *
bool hmb_spaceMgmt_bm_set             (bool enable, uint32_t val); // *
bool hmb_spaceMgmt_bm_fill_overflowed (uint32_t from, uint32_t to);

HmbDLL*  hmb_spaceMgmt_bm_empty_get_by_idx (bool for_write, uint32_t idx);
int32_t* hmb_spaceMgmt_bm_empty_get_head   (bool for_write);
bool     hmb_spaceMgmt_bm_empty_set_head   (uint32_t idx);
bool     hmb_spaceMgmt_bm_empty_insert     (uint32_t idx);
bool     hmb_spaceMgmt_bm_empty_delete     (uint32_t idx);

/** [1] **/

/** [1] Low-level APIs **/

void *hmb_spaceMgmt_mappedAddr_get_by_idx(bool for_write, uint32_t idx); // *
void *hmb_spaceMgmt_mappedAddr_get_by_lpn(bool for_write, uint32_t lpn); // XXX

HmbHeads* hmb_spaceMgmt_heads_get_by_idx (bool for_write, uint32_t idx); // *
uint32_t hmb_spaceMgmt_heads_get_size   (void); // *
HmbSplitTable *hmb_spaceMgmt_heads_ST_get_by_idx(bool for_write, uint32_t idx);

HmbSharedEnt* hmb_spaceMgmt_shared_get_head_by_lpn    (bool for_write, uint64_t lpn); // *
bool          hmb_spaceMgmt_shared_set_head           (uint32_t idx);	// *
bool          hmb_spaceMgmt_shared_insert_tail        (uint32_t idx); // *
bool          hmb_spaceMgmt_shared_insert_after       (uint32_t idx, uint32_t idx_after);	// XXX
bool          hmb_spaceMgmt_shared_insert             (uint32_t idx); /* By default, new entry is inserted to tail. */ // *
bool          hmb_spaceMgmt_shared_delete_by_idx      (uint32_t idx); // *
void          hmb_spaceMgmt_shared_set_dirty          (bool to_dirty, uint32_t idx);
void          hmb_spaceMgmt_shared_set_enable         (bool enable, uint32_t idx);
int32_t       hmb_spaceMgmt_shared_get_new_entry_idx  (uint64_t lpn); // *
bool          hmb_spaceMgmt_shared_is_reusable_by_lpn (uint64_t lpn); //
bool          hmb_spaceMgmt_shared_is_reusable_by_idx (uint32_t idx); //

bool          hmb_spaceMgmt_shared_insert_LRU(uint32_t idx);	// *

/** [1] **/

bool hmb_spaceMgmt_bm_init(uint32_t n_parts); // *
bool hmb_spaceMgmt_heads_init(uint8_t n_bits); // 
bool hmb_spaceMgmt_sorted_init(uint32_t n_parts); // *
bool hmb_spaceMgmt_entry_init(void); // 
bool hmb_spaceMgmt_init(void); // 
void hmb_spaceMgmt_init_cache_unit(uint32_t cache_unit);

void hmb_spaceMgmt_lock(void);
void hmb_spaceMgmt_unlock(void);

void hmb_spaceMgmt_WB_flusher(void *opaque);

bool hmb_spaceMgmt_WB_flush_implicit(void *ssd, int64_t *expire_time);
bool hmb_spaceMgmt_WB_flush_explicit(uint32_t n_lb, void *ssd, int64_t *expire_time);
bool hmb_spaceMgmt_WB_flush(uint32_t n_lb, void *ssd, int64_t *expire_time);

bool nvme_flush_from_hmb(void *nvmeReq);

void hmb_spaceMgmt_debug_ssd_delay_state(void);

uint64_t hmb_spaceMgmt_lba_to_lpn(uint64_t lba);
uint64_t hmb_spaceMgmt_lpn_to_lba(uint64_t lpn);
uint64_t hmb_spaceMgmt_lpn_to_bytes(uint64_t lpn);

uint32_t hmb_spaceMgmt_nlp_to_nlb(uint32_t n_lp);
uint32_t hmb_spaceMgmt_nlb_to_nlp(uint32_t n_lb, uint64_t slba);
#endif /* #ifndef __SSD__HMB_SPACEMGMT_H__ */

