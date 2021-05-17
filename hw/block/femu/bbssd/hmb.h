
#ifndef __SSD__HMB_H__
#define __SSD__HMB_H__

//#define HMB_ENTRIES 0
#define HMB_ENTRIES 4096

	//id->hmpre = cpu_to_le32(2048);  /* HMB: Preferred size is 8MB */
	//id->hmpre = cpu_to_le32(8192);  /* HMB: Preferred size is 32MB */
	// id->hmpre = cpu_to_le32(16384);  /* HMB: Preferred size is 64MB */
	// id->hmpre = cpu_to_le32(32768);  /* HMB: Preferred size is 128MB */
	//id->hmpre = cpu_to_le32(65536);  /* HMB: Preferred size is 256MB */
	//id->hmpre = cpu_to_le32(131072); /* HMB: Preferred size is 512MB */
	//id->hmpre = cpu_to_le32(262144); /* HMB: Preferred size is 1GB */
	//id->hmpre = cpu_to_le32(524288); /* HMB: Preferred size is 2GB */
	//id->hmpre = cpu_to_le32(786432); /* HMB: Preferred size is 3GB */	
	// id->hmpre = cpu_to_le32(1048576);  // HMB: Preferred size is 4GB 

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

