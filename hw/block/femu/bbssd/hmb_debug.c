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


#include "hmb_debug.h"

#include <sys/syscall.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

uint64_t HMB_DEBUG__N_WRITTEN_PAGES = 0;
uint64_t HMB_DEBUG__N_WRITE_REQUESTED_SECTORS = 0;

uint64_t HMB_DEBUG__N_READ_PAGES = 0;
uint64_t HMB_DEBUG__N_READ_REQUESTED_SECTORS = 0;

uint64_t HMB_DEBUG__N_REQUESTS = 0;
uint64_t HMB_DEBUG__N_OVERFLOWED = 0;
uint64_t HMB_DEBUG__SUM_OVERFLOWED_TIME = 0;

void hmb_printf(const char *file, int line, const char *func, const char *format, ...)
{
	va_list for_printf;
	char str_buf[1024];

	sprintf(str_buf, "[%s():%d] ", func, line);
	va_start(for_printf, format);
	vsprintf(str_buf + strlen(str_buf), format, for_printf);
	va_end(for_printf);

#if 0
	if(str_buf[strlen(str_buf)-2] == '\n')
	{
		str_buf[strlen(str_buf)-2] = '\0';
	}
#endif

	printf("%s\n", str_buf);
//	fflush(NULL);
}

bool hmb_debug_file_name(const char *idx, const char *format_filename, ...)
{
	va_list for_filename;
	char str_buf[1024];
	int fd_file;

	if(idx == NULL)
	{
		hmb_debug("Invalid argument.");
		return false;
	}

	sprintf(str_buf, "/%s/%s/", getenv("HOME"), idx);
	
	//if(mkdir(str_buf, 0777))	/* Make directory for filename debug */
	if(mkdir("~/hmb_debug/", 0777))	/* Make directory for filename debug */
	{
		hmb_debug("Failed to make directory: %s", str_buf);
	}

	va_start(for_filename, format_filename);
	vsprintf(str_buf + strlen(str_buf), format_filename, for_filename);
	va_end(for_filename);

	if((fd_file = open(str_buf, O_CREAT|O_RDWR, 0777)) < 0)
	{
		hmb_debug("Failed to make new file for file name debug.");
		return false;
	}

	close(fd_file);

	return true;
}

pid_t hmb_gettid(void)
{
	return syscall(SYS_gettid);
}

bool hmb_tkill(int tid, int sig)
{
	int ret = syscall(SYS_tkill, tid, sig);

	if(!ret)
		return true;

	return false;
}

