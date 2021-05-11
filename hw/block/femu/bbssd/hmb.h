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


#ifndef __SSD__HMB_H__
#define __SSD__HMB_H__

#include "hmb_debug.h"
#include "hmb_internal.h"

extern bool hmb_is_reallocated;

/**	[1] High-level functions related with HMB allocation **/
/* Dynamic HMB allocator */
HmbMappedAddr *hmb_malloc(uint64_t size); 
/* Dynamic HMB allocator with initializing values to zero */
HmbMappedAddr *hmb_calloc(uint64_t size); 
/* Free the allocated HMB space  */
void hmb_free(HmbMappedAddr *addr); 
/** [1] **/


/*
    Do enable or disable the HMB
      - Values of dw11 ~ dw15 are obtained from Set Feature Admin Command.
	  - page_size value is obtained from other command or PCI register value.
*/
bool hmb_enable(uint32_t page_size, uint32_t dw11, uint32_t dw12, uint32_t dw13, uint32_t dw14, uint32_t dw15); 

/*
    Initialize the HMB METADATA
      - This data is also changed by caching module
*/
bool hmb_init_structure(void); 
/* Initialize HMB module */
bool hmb_init(void *dev_pci, void *nvme_ctrl);
/** [1] **/

#endif /* __SSD__HMB_H__ */

