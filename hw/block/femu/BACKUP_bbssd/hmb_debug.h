/**
   #Group:  SSLab <sswlab.kw.ac.kr>
   #Author: Kyusik Kim <kks@kw.ac.kr> and Taeseok Kim <tskim@kw.ac.kr>

   #Project Name: HMB-supported DRAM-less SSD Simulator
   #Module Name: Common Functions
   #File Name: common.h

   #Version: v0.1
   #Last Modified: April 9, 2018
   
   #Description:
     Totally used functions in this simulator

     (1) for printing debugging messages
       --> hmb_debug() and hmb_printf(), ...
     (2) for shared memory
       --> hmb_shm_*(), ...
     (3) for FIFO
       --> hmb_fifo_*(), ...
     (4) for task management
       --> hmb_gettid() and hmb_tkill(), ...
**/

/**
    #Revision History
	  v0.1
	    - First draft
**/


#ifndef __HMB_DEBUG_H__
#define __HMB_DEBUG_H__

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

extern uint64_t HMB_DEBUG__N_WRITTEN_PAGES;
extern uint64_t HMB_DEBUG__N_WRITE_REQUESTED_SECTORS;

extern uint64_t HMB_DEBUG__N_READ_PAGES;
extern uint64_t HMB_DEBUG__N_READ_REQUESTED_SECTORS;

extern uint64_t HMB_DEBUG__N_REQUESTS;
extern uint64_t HMB_DEBUG__N_OVERFLOWED;
extern uint64_t HMB_DEBUG__SUM_OVERFLOWED_TIME;

/** [1] For debugging messages **/
void hmb_printf(const char *file, int line, const char *func, const char *format, ...);
#define hmb_debug(fmt, ...) hmb_printf(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
bool hmb_debug_file_name(const char *idx, const char *format_filename, ...);
/** [1] **/

/** [1]	Linux task management-related functions **/
pid_t hmb_gettid(void);
bool hmb_tkill(int tid, int sig);
/** [1] **/

#endif /* #ifdef __HMB_DEBUG_H__ */

