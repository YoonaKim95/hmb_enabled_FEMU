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


#include "hmb.h"
#include "hmb_spaceMgmt.h"
#include "hmb_utils.h"

#include <stdio.h>
#include <string.h> /* memcpy() */
#include <stdlib.h> /* *abs() */

HmbCtrl HMB_CTRL;
bool hmb_is_reallocated;

HmbMappedAddr *hmb_malloc(uint64_t size)
{
	HmbSegEmpty *target_empty;
	HmbSegEnt *target_entry_w;
	HmbSeg *target_segment;
	HmbMappedAddr *map_addr;
	HmbMapInfo *map_new;
	int64_t max_allocable_size;
	uint64_t size_alloc;
	int32_t ret;

	if(size == 0)
	{
		hmb_debug("Requested size is 0!");
		return NULL;
	}

	/** [1] Confirm allocation space: Has it proper empty space? **/
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__calc_max_allocable_size, true);
	if((max_allocable_size = hmb_get_max_allocable_size_byte()) < 0)
	{
		hmb_debug("Failed to caculate maximum allocable empty space size.");
		return NULL;
	}

	hmb_debug("max allocable size : %u  and size: %u", max_allocable_size, size);
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__calc_max_allocable_size, false);
	/** [1] **/

	if(size > max_allocable_size)
	{
		hmb_debug("HMB space is not enough to allocate %llu bytes. (max: %lld)", size, max_allocable_size);
		return NULL;
	}

	if(HMB_CTRL.segs == NULL)
	{
		hmb_debug("Invalid relationship.");
		return NULL;
	}
		
	hmb_debug("HMB_CTRL.segs: %u ", HMB_CTRL.segs);
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__search_empty_space, true);
	/** [1] Select proper empty space. (not max!) **/
	if((target_empty = hmb_segEmpty_search_proper(size)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return NULL;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__search_empty_space, false);
	/** [1] **/

	/** [1] for getting entry ID **/
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__get_new_id, true);
	if((ret = hmb_segEnt_get_new_id()) < 0)
	{
		hmb_debug("Failed to get new entry id.");
		return NULL;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__get_new_id, false);
	/** [1] **/

	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__ctrl_malloc, true);
	if((map_new = hmb_mapInfo_new()) == false)
	{
		hmb_debug("Failed to allocate memory for mapping information.");
		return NULL;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__ctrl_malloc, false);

	//hmb_debug("New id is: %d", ret);

	/** [1] Record mapping information - part 1 **/
	map_new->entry_id = ret;
	/** [1] **/

	/** [1] Get mapped address for the HMB entry header **/
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__get_id, true);
	target_entry_w = hmb_get_segEnt_by_id(true, map_new->entry_id);
	target_segment = hmb_get_seg_by_id(target_empty->segment_id);
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__get_id, false);
	/** [1] **/

	target_entry_w->id = map_new->entry_id;
	target_entry_w->segment_id = target_empty->segment_id;
	target_entry_w->offset = target_empty->offset_from;
	target_entry_w->size = size;
	/** [1] **/

	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__ctrl_malloc, true);
	if((map_addr = calloc(1, sizeof(HmbMappedAddr))) == NULL)
	{
		hmb_debug("Failed to allocate memory for two mapped address.");
		return NULL;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__ctrl_malloc, false);
	
	/** [1] Do mapping **/
	size_alloc = size;
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__map_pci, true);
	if((map_addr->w = pci_dma_map( HMB_CTRL.dev_pci, \
					target_segment->host_addr + target_empty->offset_from, \
					&size_alloc, \
					HMB_FOR_WRITE)) == NULL)
	{
		hmb_debug("Failed to do mapping for write");
		hmb_debug(" - Host addr.: 0x%llx", target_segment->host_addr + target_empty->offset_from);
		hmb_debug(" - size: %llu", size);
		goto FAIL_AND_FREE;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__map_pci, false);
		hmb_debug(" - Host addr.: 0x%llx", target_segment->host_addr + target_empty->offset_from);
		hmb_debug(" - size: %llu", size);
	if(size_alloc != size)
	{ 
		hmb_debug("Warning: requested size is %lluB, but only %lluB is allocated.", size, size_alloc);
		goto FAIL_AND_FREE;
	}

	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__map_pci, true);
	if((map_addr->r = pci_dma_map( HMB_CTRL.dev_pci, \
					target_segment->host_addr + target_empty->offset_from, \
					&size_alloc, \
					HMB_FOR_READ)) == NULL)
	{
		hmb_debug("Failed to do twice mapping for read");
		hmb_debug(" - Host addr.: 0x%llx", target_segment->host_addr + target_empty->offset_from);
		hmb_debug(" - size: %llu", size);
		goto FAIL_AND_FREE;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__map_pci, false);

	/** [1] **/

	/** [1] Record mapping information - part 2 **/
	map_new->addr.w = map_addr->w;
	map_new->addr.r = map_addr->r;
	//memcpy(&map_new->addr, map_addr, sizeof(HmbMappedAddr));

	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__insert_entry, true);
	if(hmb_mapInfo_insert(map_new) == false)
	{
		hmb_debug("Failed to update mapping information.");
		goto FAIL_AND_FREE;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__insert_entry, false);
	/** [1] **/

	/** [1] Update empty space information **/
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__insert_entry, true);
	if(hmb_segEmpty_filling(
				target_segment,
				target_empty,
				size) == false)
	{
		hmb_debug("Failed to renewal empty space information.");
		goto FAIL_AND_FREE;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__insert_entry, false);
	/** [1] **/

	/** [1] Update maximum allocable size of the modified segment **/
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__calc_max_allocable_size, true);
	if(hmb_seg_update_max_allocable_size(target_segment) == false)
	{
		hmb_debug("Failed to update maximum allocable size.");
		goto FAIL_AND_FREE;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__calc_max_allocable_size, false);
	/** [1] **/

	HMB_CTRL.segent_cnt++;

	return map_addr;

FAIL_AND_FREE:
	hmb_mapInfo_free(map_new);
	return NULL;
}

HmbMappedAddr *hmb_calloc(uint64_t size)
{
	HmbMappedAddr *ret;

	if((ret = hmb_malloc(size)) == NULL)
	{
		hmb_debug("Failed to allocate hmb space.");
		return NULL;
	}
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__memset, true);
	memset(ret->w, 0, size);
	hmb_timeDbg_record(&HMB_DEBUG_TIME.ll__memset, false);

	return ret;
}

void hmb_free(HmbMappedAddr *addr)
{
	HmbMapInfo *target_map;
	HmbSeg *target_segment;
	HmbSegEnt *target_segment_entry_r, *target_segment_entry_w;

	if(addr == NULL)
	{
		hmb_debug("Invalid argument.");
		return;
	}

	if((target_map = hmb_mapInfo_search(addr)) == NULL)
	{
		hmb_debug("Invalid request: 0x%x was not mapping address.", addr);
		return;
	}

	if((target_segment_entry_r = hmb_get_segEnt_by_id(false, target_map->entry_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return;
	}

	if((target_segment = hmb_get_seg_by_id(target_segment_entry_r->segment_id)) == NULL)
	{
		hmb_debug("Invalid relationship.");
		return;
	}

	/** Update segment entry information **/
	target_segment_entry_w = hmb_get_segEnt_by_id(true, target_map->entry_id);
	target_segment_entry_w->id = HMB_SEGENT_UNUSED;


	/** [1] Do unmapping **/
	pci_dma_unmap(
			HMB_CTRL.dev_pci,
			addr->r,
			target_segment_entry_r->size,
			HMB_FOR_READ,
			target_segment_entry_r->size);
	pci_dma_unmap(
			HMB_CTRL.dev_pci,
			addr->w,
			target_segment_entry_r->size,
			HMB_FOR_WRITE,
			target_segment_entry_r->size);
	/** [1] **/


	/** [1] Update mapping information **/
	if(hmb_mapInfo_delete(target_map) == false)
	{
		hmb_debug("Failed to update mapping information.");
	}
	hmb_mapInfo_free(target_map);
	target_map = NULL;
	/** [1] **/


	/** [1] Update empty space in segment information **/
	if(hmb_segEmpty_emptying(target_segment, target_segment_entry_r->offset, target_segment_entry_r->size) == false)
	{
		hmb_debug("Failed to do emptying");
	}
	/** [1] **/


	/** [1] Update maximum allocable size of the modified segment **/
	if(hmb_seg_update_max_allocable_size(target_segment) == false)
	{
		hmb_debug("Failed to update maximum allocable size.");
	}
	/** [1] **/


	/* Update information: #segment entries */
	HMB_CTRL.segent_cnt--;

	free(addr);
}

bool hmb_init_structure(void)
{
	if(hmb_meta_init() == false)
	{
		hmb_debug("Failed to initialize metadata.");
		return false;
	}

	if(hmb_segEnt_init() == false)
	{
		hmb_debug("Failed to initialize segment entry.");
		return false;
	}

	if(hmb_seg_init() == false)
	{
		hmb_debug("Failed to initialize segment.");
		return false;
	}

	if(hmb_mapInfo_init(HMB_CTRL.segent_cnt_max) == false)
	{
		hmb_debug("Failed to initialize mapping information.");
		return false;
	}
/*
	if(hmb_spaceMgmt_init() == false)
	{
		hmb_debug("Failed to initialize space management parts.");
		return false;
	}
*/
	return true;
}

bool hmb_enable(uint32_t page_size, uint32_t dw11,
		uint32_t dw12, uint32_t dw13, uint32_t dw14, uint32_t dw15)
{
	/** [1] HMB: When the Set Feature command requests HMB to be enabled **/
	if((HMB_CTRL.enable_hmb = dw11 & 0x1))
	{
		int i;

		hmb_debug("HMB will be enabled.");

		/* HMB: Memory Return */
		HMB_CTRL.mem_ret   = dw11 & 0x2; 

		/* HMB: HMB Size (unit: page size) */   
		// 16  
		HMB_CTRL.size_pg   = dw12; 

		// 64MB
		/* HMB: HMB Size (unit: byte) */
		HMB_CTRL.size      = ((uint64_t)HMB_CTRL.size_pg) * page_size; 

		hmb_debug("page size: %u ", page_size);


		/* HMB: Host Memory Descriptor List Lower Addr. */
		HMB_CTRL.list_addr_l = dw13 & (~0xF); 
		/* HMB: Host Memory Descriptor List Upper Addr. */
		HMB_CTRL.list_addr_u = dw14; 
		/* HMB: 64bits host address for the list */
		HMB_CTRL.list_addr =  HMB_CTRL.list_addr_l \
							  + (((uint64_t)HMB_CTRL.list_addr_u) << 32);

		HMB_CTRL.page_size = page_size;

		/* HMB: Host Memory Descriptor List Entry Count */
		// HMB_CTRL.list_cnt  = dw15 - 1;             

		HMB_CTRL.list_cnt  = dw15;             

		HMB_CTRL.list = (HmbEntry **)calloc(HMB_CTRL.list_cnt, sizeof(HmbEntry *));

		hmb_debug("hmbsize pg : %u ", HMB_CTRL.size_pg);
		hmb_debug("hmbsize: %u ", HMB_CTRL.size);
		hmb_debug("hmbpage size: %u ", HMB_CTRL.page_size);


		for(i=0; i<HMB_CTRL.list_cnt; i++)
		{

			HmbEntry new_entry;

			hmb_debug("entry list %u: ", i);
			
			hmb_read(HMB_CTRL.list_addr + sizeof(HmbEntry) * i, \
					&new_entry, sizeof(HmbEntry));

			HMB_CTRL.list[i] = (HmbEntry *)calloc(1, sizeof(HmbEntry));
			HMB_CTRL.list[i]->addr = le64_to_cpu(new_entry.addr);
			HMB_CTRL.list[i]->size = le32_to_cpu(new_entry.size);
		
			hmb_debug("entry addr %u: ", new_entry.addr);
			hmb_debug("entry size %u: \n", new_entry.size);
		}


		if(hmb_init_structure() == false)
		{
			hmb_debug("Failed to make HMB structure.");
			return false;
		}

		hmb_debug("###### HMB initialization was completed!");
	}
	/** [1] **/


	/** [1] When HMB is disabled, **/
	else
	{
		int i;

		/** [2] Free memory for HMB entries **/
		if(HMB_CTRL.list != NULL)
		{
			for(i=0; i<HMB_CTRL.list_cnt; i++)
				free(HMB_CTRL.list[i]);
			free(HMB_CTRL.list);
			HMB_CTRL.list = NULL;
		}
		/** [2] **/

		/* FIXME: More details are needed */

		HMB_CTRL.enable_hmb = false;

		hmb_debug("HMB was disabled.");
	}
	/** [1] **/


	return true;
}

bool hmb_init(void *dev_pci, void *nvme_ctrl)
{
	if((HMB_CTRL.dev_pci = dev_pci) == NULL \
			|| (HMB_CTRL.parent = nvme_ctrl) == NULL)
	{
		hmb_debug("Invalid argument");
		return false;
	}

	HMB_CTRL.segs = NULL;
	HMB_SPACEMGMT_CTRL.inited = false;

	hmb_debug("sizeof(HmbSync): %u", sizeof(HmbSync));

	return true;
}

