#ifndef __SSD__HMB_CONFIG_H__
#define __SSD__HMB_CONFIG_H__

/*
    If ( 16384*(n-1) < sizeof(logical_page) <= 16384*(n) )
		HMB_CONFIG_NUMBER_OF_TABLE_BITMAP must be set to n.
 */
#define HMB_CONFIG_NUMBER_OF_TABLE_BITMAP_PARTS 1 
//#define HMB_SPACEMGMT_DEBUG_CLOSELY

#endif /* __SSD__HMB_CONFIG_H__ */

