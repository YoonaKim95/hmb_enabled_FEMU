#include "ftl.h"
#include "hashftl.h"


#include <stdint.h>
#include <math.h>


/* Func: get_vba_from_md5
 * extract pba from md5 
 */
static int32_t get_vba_from_md5(uint64_t md5_result, int32_t hid){
	int32_t temp;
	uint64_t shifted;

	//shift result using hid;
	shifted = md5_result >> (hid*1);

	//temp = shifted % num_vbt;
	temp = shifted % BLKS_PER_PL;

	return temp;
}

static int hash_garbage_collection(struct ssd *ssd, uint32_t max_pba, uint32_t max_vba) {
	//struct ppa_hash ppa_hash; 

	ssd->num_GC = ssd->num_GC + 1; 

	return 0;
}


static struct ppa *check_written(struct ssd *ssd, int32_t pba)
{	
	int64_t temp2 = -1, ppa_addr = -1;

	struct nand_block *blk;

	struct ppa *ppa = NULL;

	ppa->ppa_hash = -1;
	ppa->ppa = 0;
	ppa->g.ch = 0;
	ppa->g.lun = 0;
	ppa->g.pg = 0;
	ppa->g.blk = pba;
	ppa->g.pl = 0;

	ftl_assert(ppa.g.pl == 0);

	blk = get_blk(ssd, ppa);

	temp2 = blk->wp;

	// corresponding block is full;
	if(temp2 == PGS_PER_BLK){
		return ppa;
	}

	if(temp2 > PGS_PER_BLK){
		printf("overflow\n");
		return ppa;
	}

	ppa_addr = (pba * PGS_PER_BLK) + temp2;
		
	ppa->g.pg = temp2;

	ppa->ppa_hash = ppa_addr; 
	ppa->ppid = temp2;

	return ppa; 
}

static uint64_t md5_u(uint32_t *initial_msg, size_t initial_len)
{
	uint32_t *msg = NULL;
	uint32_t h0, h1, h2, h3;
	int new_len, offset;
	uint32_t temp;
	
	uint64_t tmp, result;

	// per-round shift amounts
	uint32_t r[] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
					5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
					4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
					6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };

	// Use binary integer part of the sines of integers (in radians) as constants// Initialize variables:
	uint32_t k[] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
		0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
		0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
		0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
		0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
		0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
		0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
		0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
		0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };

	h0 = 0x67452301;
	h1 = 0xefcdab89;
	h2 = 0x98badcfe;
	h3 = 0x10325476;

	//int new_len;
	for (new_len = initial_len * 8 + 1; new_len % 512 != 448; new_len++);
	new_len /= 8;

	if((msg = (uint32_t*)calloc(new_len + 64, 1)) == NULL){
		printf("md5 msg allocation is failed\n");
		return 1;
	}; // also appends "0" bits
								   // (we alloc also 64 extra bytes...)
	
	memcpy(msg, initial_msg, initial_len);
	msg[initial_len] = 128; // write the "1" bit

	// note, we append the len

	// in bits at the end of t nks:
	//for each 512-bit chunk of message:

	for (offset = 0; offset < new_len; offset += (512 / 8)) {

		// break chunk into sixteen 32-bit words w[j], 0 �� j �� 15
		uint32_t *w = (uint32_t *)(msg + offset);
		
		// Initialize hash value
		uint32_t a = h0;
		uint32_t b = h1;
		uint32_t c = h2;
		uint32_t d = h3;

		// Main loop:
		uint32_t i;
		for (i = 0; i < 64; i++) {
			uint32_t f, g;

			if (i < 16) {
				f = (b & c) | ((~b) & d);
				g = i;
			}
			else if (i < 32) {
				f = (d & b) | ((~d) & c);
				g = (5 * i + 1) % 16;
			}
			else if (i < 48) {
				f = b ^ c ^ d;
				g = (3 * i + 5) % 16;
			}
			else {
				f = c ^ (b | (~d));
				g = (7 * i) % 16;
			}

			temp = d;
			d = c;
			c = b;

			b = b + LEFTROTATE((a + f + k[i] + w[g]), r[i]);
			a = temp;



		}

		// Add this chunk's hash to result so far:

		h0 += a;
		h1 += b;
		h2 += c;
		h3 += d;

	}
	
	result = h2;

	tmp = h3 * pow(2,32);

	result = result + tmp;
	// cleanup
	free(msg);
	return result;
}

/*static struct ppa ppa_hash_to_ppa(struct ssd *ssd, struct ppa_hash ppa_hash) 
{
	struct ppa ppa; 

	struct write_pointer *wpp = &ssd->wp;
	
	ppa.ppa = 0;
	ppa.g.ch = 0;
	ppa.g.lun = 0;
	ppa.g.pg = wpp->pg;
	ppa.g.blk = wpp->blk;
	ppa.g.pl = 0;
	ftl_assert(ppa.g.pl == 0);


	ppa.hid = ppa_hash.hid; 
	ppa.ppid = ppa_hash.ppid; 

	return ppa; 
}*/

struct ppa get_new_page_hash(struct ssd *ssd, int32_t lpa) 
{
	struct nand_block *block;

	struct ppa return_ppa; 

	struct ppa *ppa = NULL; 
	struct ppa *shr_ppa = NULL;

	int32_t vba;
	int32_t max_pba = -1, max_vba = -1, max_inv = -1, max_hid = -1, pba = -1, cur_inv = -1; 

	uint32_t lpa_md5,  hid = 0;  //, cal_ppid = 0;
	uint64_t md5_res;
	
	int i;
	size_t len = sizeof(lpa);

	// variables for shared virtual block
	int32_t shr_num_free[3];
	int32_t shr_free_pba[3]; 
	int32_t shr_free_hid[3];				// record vba with max free pages
	int32_t shr_num_inv[3];
	int32_t shr_inv_vba[3];
	int32_t shr_inv_hid[3];					// record vba with max invalid pages
	int32_t vba_pos, vba_srt, vba_temp;			// caculate vba's position
	int32_t shr_pba, shr_vba, shr_hid, shr_chk;
	//////////////

	// get value from md5
	lpa_md5 = lpa >> ssd->lpa_sft;
	md5_res = md5_u(&lpa_md5, len);

	// initialize variables for GC and shared virtual block
	max_pba = -1;
	max_inv = 0;
	for(i = 0; i < 3 ; i++){
		shr_num_free[i] = 0;
		shr_free_pba[i] = -1; 
		shr_free_hid[i] = -1;
		shr_num_inv[i] = 0;
		shr_inv_vba[i] = -1;
		shr_inv_hid[i] = -1;
	}


	shr_ppa->ppa = 0;
	shr_ppa->g.ch = 0;
	shr_ppa->g.lun = 0;
	shr_ppa->g.pg = 0;
	shr_ppa->g.blk = 0;
	shr_ppa->g.pl = 0;
	ftl_assert(shr_ppa.g.pl == 0);


	while(hid != ssd->hid_secondary) {
		vba = get_vba_from_md5(md5_res, hid);

		vba_pos = vba % 4;
		vba_srt = vba - vba_pos;

		/* convert virtual block to physical block  */
		pba = ssd->vbt[vba].pba;

		ppa = check_written(ssd, pba);

		if(ppa->ppa_hash != -1) {
			ppa->hid = hid; 

			break;
		}


		block = get_blk(ssd, ppa); 

		//cur_inv = ssd->barray[pba].ipc;
		cur_inv = block->ipc;

		if(cur_inv > max_inv){
			max_pba = pba;
			max_hid = hid;
			max_inv = cur_inv;
			max_vba = vba;
		}

		// check shared virtual block info.
		for(i = 0; i < 3 ; i++){
			vba_pos = (vba_pos + 1) % 4;
			vba_temp = vba_srt + vba_pos;
			shr_pba = ssd->vbt[vba_temp].pba;

			shr_ppa->g.blk = shr_pba; 
			block = get_blk(ssd, shr_ppa);

			if(shr_num_free[i] == 0 && PGS_PER_BLK - (block->wp) > 0){
				shr_free_pba[i] = shr_pba;
				shr_num_free[i] = PGS_PER_BLK - (block->wp);
				shr_free_hid[i] = hid;
			}
			if(shr_num_inv[i] < block->ipc){
				shr_num_inv[i] = block->ipc;
				shr_inv_vba[i] = vba_temp;
				shr_inv_hid[i] = hid;
			}
		}

		hid = hid + 1;
	}


	/* corresponding blocks are full */
	if(hid == ssd->hid_secondary) {
		/* Log for 1st gc occurs */
		shr_chk = 0;
		if (max_pba == -1 || max_inv == 0) {  // GC cannot be triggered ... 
			shr_chk = 0;
			for(i = 0; i < 3; i++){
				if(shr_num_free[i] > 0){
					shr_chk = 1;
					shr_pba = shr_free_pba[i];
					shr_hid = shr_free_hid[i];
					break;	
				}
				if(shr_num_inv[i] > 0){
					shr_chk = 2;
					shr_vba = shr_inv_vba[i];
					shr_pba = ssd->vbt[shr_vba].pba;
					shr_hid = shr_inv_hid[i];
					break;
				}
			}

			if(shr_chk == 1){
				ppa = check_written(ssd, shr_pba);
				hid = shr_hid;
			} if(shr_chk == 2){
				pba = hash_garbage_collection(ssd, shr_pba, shr_vba);
				ppa = check_written(ssd, pba);
				hid = shr_hid;
			} else if(shr_chk == 0) {	
				//print log for collision
				printf("\nno free space for lpa ||  HASH COLLISION cannot be handled !!\n");
				hid = 1;
				while(hid != ssd->hid_secondary) {
					vba = get_vba_from_md5(md5_res, hid);
					/* convert virtual block to physical block  */
					pba = ssd->vbt[vba].pba;
					shr_ppa->g.blk = pba; 
					block = get_blk(ssd, shr_ppa);

					printf("\nHID: %d,\tPBA: %d,\tINVALID: %d\n", hid, pba, block->ipc);
					hid = hid + 1;
				}
				// exit(3);
				ppa->ppa_hash = -1;

				return_ppa.ppa = 0;
				return_ppa.g.ch = ppa->g.ch;
				return_ppa.g.lun = ppa->g.lun;
				return_ppa.g.pg = ppa->g.pg;
				return_ppa.g.blk = ppa->g.blk;
				return_ppa.g.pl = ppa->g.pl;
				ftl_assert(return_ppa.g.pl == 0);


				return return_ppa;
			}

		} else {  //GC occurs 
			pba = hash_garbage_collection(ssd, max_pba, max_vba);  //TODO 
			ppa = check_written(ssd, pba);
			hid = max_hid;
		}
	}

	ppa->hid = hid;
	return_ppa.ppa = 0;
	return_ppa.g.ch = ppa->g.ch;
	return_ppa.g.lun = ppa->g.lun;
	return_ppa.g.pg = ppa->g.pg;
	return_ppa.g.blk = ppa->g.blk;
	return_ppa.g.pl = ppa->g.pl;
	ftl_assert(return_ppa.g.pl == 0);





	return return_ppa;
}



///////////////////////////////////////////////////////////////////
/* 
   uint32_t hash_create(lower_info *li, algorithm *algo){
	int temp, bit_cnt, blk_cnt;
	printf("Start creating hash-based FTL\n");

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

	BM_Free(bm);


	free(pri_table);
	free(vbt);
	free(hash_OOB);
}
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
} */ 
