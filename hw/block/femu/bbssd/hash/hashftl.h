#ifndef __H_HASHFTL__
#define __H_HASHFTL__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include "../../interface/interface.h"
#include "../../interface/queue.h"
#include "../../include/container.h"
#include "../../include/types.h"
#include "../../include/dl_sync.h"
#include "../blockmanager/BM.h"

#define TYPE uint8_t
#define DATA_R 1
#define DATA_W 2
#define GC_R 3
#define GC_W 4

#define CLEAN 0
#define DIRTY 1
#define H_VALID 1
#define H_INVALID 2

typedef struct primary_table{
	int32_t hid;
	int32_t ppid;
	int32_t ppa;
	bool state; // CLEAN or DIRTY
} PRIMARY_TABLE;

typedef struct virtual_block_table{
	int32_t pba;
	bool state; // CLEAN or DIRTY

} VIRTUAL_BLOCK_TABLE;

// OOB data structure
typedef struct hash_OOB{
	int32_t lpa;
} H_OOB;

// SRAM data structure (used to hold pages temporarily when GC)
typedef struct SRAM{
	H_OOB OOB_RAM;
	PTR PTR_RAM;
} SRAM;

typedef struct hash_params{
	value_set *value;
	dl_sync hash_mutex;
	TYPE type;
} hash_params;


extern algorithm __hashftl;

extern b_queue *free_b;
extern Heap *b_heap;

extern PRIMARY_TABLE *pri_table;     // primary table
extern VIRTUAL_BLOCK_TABLE *vbt;   //secondary table
extern H_OOB *hash_OOB;	   // Page level OOB.

extern BM_T *bm;
extern Block *reserved;    //reserved.
extern int32_t reserved_pba;

extern int32_t _g_nop;
extern int32_t _g_nob;
extern int32_t _g_ppb;

extern int32_t num_hid;
extern int32_t num_ppid;
extern int32_t num_page_off;
extern int32_t hid_secondary;
extern int32_t lpa_sft;

extern int32_t num_vbt;

extern int32_t gc_load;
extern int32_t gc_count;
extern int32_t re_gc_count;
extern int32_t re_page_count;

extern int32_t gc_val;
extern int32_t re_number;
extern int32_t gc_pri;

extern int32_t num_write;
extern int32_t num_copy;

extern int32_t num_val_victim[300];

uint32_t hash_create(lower_info*, algorithm*);
void     hash_destroy(lower_info*, algorithm*);
uint32_t hash_get(request *const);
uint32_t hash_set(request *const);
uint32_t hash_remove(request *const);
void    *hash_end_req(algo_req*);

algo_req* assign_pseudo_req(TYPE type, value_set* temp_v, request* req);
value_set* SRAM_load(SRAM* sram, int32_t ppa, int idx); // loads info on SRAM.
void SRAM_unload(SRAM* sram, int32_t ppa, int idx); // unloads info from SRAM.
int32_t get_vba_from_md5(uint64_t md5_result, uint8_t hid);
int32_t check_written(int32_t	pba, int32_t lpa, int32_t* cal_ppid);
int32_t get_vba_from_table(int32_t lpa);
int32_t alloc_page(int32_t lpa, int32_t* cal_ppid, int32_t* hid);
int32_t map_for_gc(int32_t lpa, int32_t ppa, int32_t ppid);


int32_t hash_garbage_collection(int32_t pba, int32_t vba); // hashftl GC function.

void md5(uint32_t *initial_msg, size_t initial_len, uint64_t* result);
uint64_t md5_u(uint32_t *initial_msg, size_t initial_len);
#endif
