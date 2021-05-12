#include "hashftl.h"
#include "../../bench/bench.h"

/* HASHFTL methods */
algorithm __hashftl = {
	.create  = hash_create,
	.destroy = hash_destroy,
	.read    = hash_get,
	.write   = hash_set,
	.remove  = hash_remove
};

//heap globals.
b_queue *free_b;
Heap *b_heap;

// primary table
PRIMARY_TABLE *pri_table;
// virtual table 
VIRTUAL_BLOCK_TABLE *vbt;

// OOB area
H_OOB *hash_OOB;

//blockmanager globals.
BM_T *bm;
Block *reserved;
int32_t reserved_pba;


//global for macro.
int32_t _g_nop;				// number of page
int32_t _g_nob;				// number of block
int32_t _g_ppb;				// page per block

// SEQhFTL constant
int32_t num_hid;              // number of hid bits
int32_t num_ppid;             // number of ppid bits
int32_t num_page_off;          // number of page offset bits
int32_t hid_secondary;        // hid that indicate mapping is at secondary table
int32_t lpa_sft;
int32_t num_vbt;
int32_t gc_load;
int32_t gc_count;			// number of total gc
int32_t re_gc_count;		// number of gc due to remap
int32_t re_page_count;		// number of moved page during remap

int32_t shr_read_cnt[4];
int32_t gc_val;
int32_t re_number;
int32_t gc_pri;

int32_t num_write;
int32_t num_copy;

int32_t num_val_victim[300];	//victim block valid page count

int32_t read_latency_t[10000];
struct timeval read_start[3000000];
struct timeval read_end[3000000];
struct timeval read_time;


static void print_algo_log() {
	printf("\n");
	printf(" |---------- algorithm_log : Hash FTL ------------------\n");

	printf(" | Total Blocks(Segments): %d\n", _g_nob);
	printf(" | Total Blocks for virtual block table: %d\n", num_vbt);
	printf(" | Total Blocks(Segments): %d\n", _g_nob);
	printf(" | Total Pages:            %d\n", _g_nop);
	printf(" |  -Page per Block:       %d\n |\n", _g_ppb);

	printf(" | Hash ID bits :          %d\n", num_hid);
	printf(" | page offset bits:       %d\n", num_page_off);

	printf(" | LPN shift bits:	   %d\n", lpa_sft);
	printf(" |---------- algorithm_log end -----------------------\n\n");
}

uint32_t hash_create(lower_info *li, algorithm *algo){
	int temp, bit_cnt, blk_cnt;
	printf("Start creating hash-based FTL\n");

	/* Initialize pre-defined values by using macro */
	_g_nop = _NOP;
	_g_nob = _NOS;
	_g_ppb = _PPS;

	gc_count = 0;
	re_gc_count = 0;
	re_page_count = 0;

	for(int i = 0; i < _g_ppb; i++){
		num_val_victim[i] = 0;
	}

	// check the number of page offset bits
	temp = _g_ppb;
	bit_cnt = 0;
	while(temp != 0){
		temp = temp/2;
		bit_cnt++;
	}

	num_hid = 6;
	num_page_off = bit_cnt - 1;
	num_ppid = num_page_off;
	hid_secondary = pow(2, num_hid);

	temp = _g_nob;
	blk_cnt = 0;
	while(temp != 0){
		temp = temp/2;
		blk_cnt++;
	}
	blk_cnt--;
	num_vbt = _g_nob - 1;

	lpa_sft = 8;

	num_write = 0;
	num_copy = 0;

	for(int i=0; i<4; i++){
		shr_read_cnt[i] = 0;
	}

	// Map lower info
	algo->li = li;

	//print information
	print_algo_log();

	bm = BM_Init(_g_nob, _g_ppb, 0, 1);

	BM_Queue_Init(&free_b);
	for(int i = 0; i < (_g_nob - num_vbt); i++){
		BM_Enqueue(free_b, &bm->barray[i + num_vbt]);
	}

	bm->qarray[0] = free_b;

	pri_table = (PRIMARY_TABLE*)malloc(sizeof(PRIMARY_TABLE) * _g_nop);
	hash_OOB = (H_OOB*)malloc(sizeof(H_OOB) * _g_nop);
	vbt = (VIRTUAL_BLOCK_TABLE*)malloc(sizeof(VIRTUAL_BLOCK_TABLE) * num_vbt);

	// initialize primary table & oob
	for(int i = 0; i < _g_nop; i++){
		pri_table[i].hid = 0;
		pri_table[i].ppid = -1;
		pri_table[i].ppa = -1;

		pri_table[i].state = CLEAN;

		hash_OOB[i].lpa = -1;
	}

	// initialize virtual block table
	for(int i = 0; i < num_vbt; i++){
		vbt[i].pba = i;
		vbt[i].state = CLEAN;
	}

	return 0;
}


/* Function: Hash end reqeust 
 * Process: 
 * end req differs according to type.
 * frees params and input.
 * in some case, frees inf_req.
 */
void *hash_end_req(algo_req* input){
	hash_params *params = (hash_params*)input->params;
	value_set *temp_v = params->value;
	request *res = input->parents;
	int lat_idx;

	switch(params->type){
		case DATA_R:			
			if(res){
				gettimeofday(&read_end[res->key],NULL);
				timersub(&read_end[res->key],&read_start[res->key],&read_time);
				lat_idx = read_time.tv_usec / 10;
				read_latency_t[lat_idx]++;
				res->end_req(res);
			}
			break;
		case DATA_W:
			if(res){
				res->end_req(res);
			}
			break;
		case GC_R:
			gc_load++;
			break;
		case GC_W:
			gc_load++;
			inf_free_valueset(temp_v, FS_MALLOC_W);
			break;
	}
	free(params);
	free(input);
	return NULL;
}

void hash_destroy(lower_info *li, algorithm *algo){
	FILE *fp_out;
	FILE *vbt_out;
	FILE *bl_out;
	FILE *val_victim_out;

	puts("");
	/* Print information */
	printf("---[Hash FTL Summary]:\n");
	printf("NUM of GC: %d\n", gc_count);
	printf("Requested Writes: %d\n", num_write);
	printf("Copied Writes: %d\n", num_copy);
	printf("WAF: %d \n", ((num_write+num_copy)*100/num_write));

	
	printf("\n---------------------  [DONE]  ---------------------\n\n");

	// print FTL info to .txt file
	fp_out = fopen("result/ftl_info.txt", "w");
	fprintf(fp_out, "The number of HID bits: %d\t\r\n", num_hid);
	fprintf(fp_out, "The number of Page offset bits: %d\t\r\n", num_page_off);
	fprintf(fp_out, "# of gc: %d\r\n", gc_count);
	fprintf(fp_out, "Requested writes: %d\r\n", num_write);
	fprintf(fp_out, "Copied writes: %d\r\n", num_copy);
	fprintf(fp_out, "WAF: %d\r\n", ((num_write+num_copy)*100/num_write));
	fprintf(fp_out, "LPA\tPPA\tHID\tPPID\r\n");

	for(int i = 0 ; i < _g_nop ; i++){
		fprintf(fp_out, "%d\t", i);
		fprintf(fp_out, "%d\t", pri_table[i].ppa);
		fprintf(fp_out, "%d\t", pri_table[i].hid);
		fprintf(fp_out, "%d\t\r\n", pri_table[i].ppid);
	}
	fclose(fp_out);

	// print blk info to .txt file
	bl_out = fopen("result/blk_info.txt", "w");
	fprintf(bl_out, "Total block: %d\r\n", _g_nob);
	fprintf(bl_out, "Pages per block: %d\r\n", _g_ppb);
	fprintf(bl_out, "PBA\tWrite Offset\tInvalid\r\n");

	for(int i = 0 ; i < _g_nob; i++){
		fprintf(bl_out, "%d\t", i);
		fprintf(bl_out, "%d\t\t", bm->barray[i].wr_off);
		fprintf(bl_out, "%d\t\r\n", bm->barray[i].Invalid);
	}
	fclose(bl_out);

	// print blk info to .txt file
	vbt_out = fopen("result/vbt_info.txt", "w");
	fprintf(vbt_out, "Total block: %d\r\n", _g_nob);
	fprintf(vbt_out, "Pages per block: %d\r\n", _g_ppb);
	fprintf(vbt_out, "VBA\tPBA\r\n");

	for(int i = 0 ; i < _g_nob; i++){
		fprintf(vbt_out, "%d\t", i);
		fprintf(vbt_out, "%d\t\r\n", vbt[i].pba);
	}
	fclose(vbt_out);

	val_victim_out = fopen("result/GC_valid_page_info.txt", "w");
	fprintf(val_victim_out, "Total GC: %d\r\n", gc_count);
	fprintf(val_victim_out, "Valid Page Copy: %d\r\n", num_copy);
	fprintf(val_victim_out, "# of pages\tcount\r\n");

	for(int i = 0 ; i < _g_ppb; i++){
		fprintf(val_victim_out, "%d\t", i);
		fprintf(val_victim_out, "%d\t\r\n", num_val_victim[i]);
	}
	fclose(val_victim_out);

	val_victim_out = fopen("result/read_lat_info.txt", "w");
	fprintf(val_victim_out, "us\tcount\r\n");

	for(int i = 0 ; i < 8000; i++){
		fprintf(val_victim_out, "%d\t", i * 10);
		fprintf(val_victim_out, "%d\t\r\n", read_latency_t[i]);
	}
	fclose(val_victim_out);

	/* Clear modules */
	BM_Free(bm);


	/* Clear tables */
	free(pri_table);
	free(vbt);
	free(hash_OOB);
}

/* Function: hash_get
 * Process: 
 * gives pull request to lower level.
 * reads mapping data.
 * !!if not mapped, does not pull!!
 */
uint32_t hash_get(request* const req){
	int32_t lpa;
	int32_t ppa;

	// shared virtual block variables
	int32_t vba, vba_pos, vba_srt, vba_temp, pba;
	int i;

	bench_algo_start(req);
	lpa = req->key;
	gettimeofday(&read_start[lpa],NULL);

	// shared virtual block read process
	vba = get_vba_from_table(lpa);
	if(vba == -1){
		bench_algo_end(req);
		req->type = FS_NOTFOUND_T;
		req->end_req(req);
		return 1;
	}

	vba_pos = vba % 4;
	vba_srt = vba - vba_pos;

	bench_algo_end(req);

	for(i = 0; i < 4; i++){
		vba_temp = vba_srt + vba_pos;
		pba = vbt[vba_temp].pba;
		ppa = pba * _g_ppb + pri_table[lpa].ppid;

		if(hash_OOB[ppa].lpa == lpa){
			if(ppa != pri_table[lpa].ppa){
				printf("\nppa mismatch\n");
				printf("\n%dth read\n", i);
				printf("\nlpa:%d\n", lpa);
				printf("\nppa:%d\n", ppa);
				printf("\nppa table:%d\n", pri_table[lpa].ppa);
				printf("\nppa OOB:%d\n", hash_OOB[ppa].lpa);
				exit(3);
			}
			__hashftl.li->read(ppa, PAGESIZE, req->value, ASYNC, assign_pseudo_req(DATA_R, NULL, req));
			shr_read_cnt[i]++;
			return 0;
		}	
		vba_pos = (vba_pos + 1) % 4;
	}

	// ERROR 
	printf("\nppa miss\n");	
	printf("\nlpa:%d\n", lpa);
	printf("\nppa OOB:%d\n", hash_OOB[pri_table[lpa].ppa].lpa);
	printf("\nppa table:%d\n", pri_table[lpa].ppa);
	pba = vbt[vba].pba;
	ppa = pba * _g_ppb + pri_table[lpa].ppid;
	printf("\nvba:%d\n", vba);
	printf("\npba:%d\n", pba);
	printf("\npba:%d\n", ppa);
	for(int i = 0; i < 4 ; i++){
		printf(" |Read %d: %d\n", i+1, shr_read_cnt[i]);
	}
	exit(3);
	return 0;
	// 
}

uint32_t hash_set(request* const req){
	int32_t lpa;
	int32_t ppa;
	int32_t hid = 0;
	int32_t cal_ppid = 0;
	Block *block;

	lpa = req->key;
	if(lpa > RANGE + 1){ 
		printf("range error %d\n", lpa);
		exit(3);
	}
	hid = 1;
	ppa = alloc_page(lpa, &cal_ppid, &hid);

	__hashftl.li->write(ppa, PAGESIZE, req->value, ASYNC, assign_pseudo_req(DATA_W, NULL, req));

	num_write++;

	//already mapped case.(update)
	if(pri_table[lpa].hid != 0){
		BM_InvalidatePage(bm , pri_table[lpa].ppa);
		hash_OOB[pri_table[lpa].ppa].lpa = -1; 
	}

	// put information to table and validate page
	pri_table[lpa].state = H_VALID;
	pri_table[lpa].ppa = ppa;
	pri_table[lpa].hid = hid;
	pri_table[lpa].ppid = cal_ppid;

	block = &bm->barray[BM_PPA_TO_PBA(ppa)];
	block->wr_off++;

	BM_ValidatePage(bm, ppa);
	hash_OOB[ppa].lpa = lpa;

	return 0;
}


uint32_t hash_remove(request* const req){
	int32_t lpa;
	int32_t ppa;

	bench_algo_start(req);
	lpa = req->key;
	if (lpa > RANGE + 1) {
		printf("range error %d\n",lpa);
		exit(3);
	}

	ppa = pri_table[lpa].ppa;

	pri_table[lpa].ppa = -1; //reset to default.
	hash_OOB[ppa].lpa = -1; //reset reverse_table to default.
	pri_table[lpa].hid = 0;
	pri_table[lpa].state = CLEAN;

	BM_InvalidatePage(bm, ppa);
	bench_algo_end(req);
	return 0;
}
