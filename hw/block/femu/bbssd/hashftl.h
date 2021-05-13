#define CLEAN 0
#define DIRTY 1
#define H_VALID 1
#define H_INVALID 2



/*
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
uint64_t md5_u(uint32_t *initial_msg, size_t initial_len); */ 
