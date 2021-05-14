#include "hashftl.h"

algo_req* assign_pseudo_req(TYPE type, value_set *temp_v, request *req){
	algo_req *pseudo_my_req = (algo_req*)malloc(sizeof(algo_req));

	hash_params *params = (hash_params*)malloc(sizeof(hash_params));

	pseudo_my_req->parents = req;
	pseudo_my_req->type = type;
	params->type = type;
	params->value = temp_v;

	pseudo_my_req->end_req = hash_end_req;
	pseudo_my_req->params = (void*)params;
	return pseudo_my_req;
}

value_set* SRAM_load(SRAM* sram, int32_t ppa, int idx){
	value_set *temp_value_set;
	temp_value_set = inf_get_valueset(NULL, FS_MALLOC_R, PAGESIZE);

	// pull in gc is ALWAYS async
	__hashftl.li->read(ppa, PAGESIZE, temp_value_set, 1, assign_pseudo_req(GC_R, NULL, NULL)); 	

	sram[idx].PTR_RAM = (PTR)malloc(PAGESIZE);
	sram[idx].OOB_RAM.lpa = hash_OOB[ppa].lpa;

	hash_OOB[ppa].lpa = -1;
	return temp_value_set;
}

void SRAM_unload(SRAM* sram, int32_t ppa, int idx){
	value_set *temp_value_set;
	temp_value_set = inf_get_valueset((PTR)sram[idx].PTR_RAM, FS_MALLOC_W, PAGESIZE);

	__hashftl.li->write(ppa, PAGESIZE, temp_value_set, ASYNC, assign_pseudo_req(GC_W, temp_value_set, NULL));

	hash_OOB[ppa].lpa = sram[idx].OOB_RAM.lpa;
	BM_ValidatePage(bm, ppa);
	free(sram[idx].PTR_RAM);

}

/* Func: get_vba_from_md5
 * extract pba from md5 
 */
int32_t get_vba_from_md5(uint64_t md5_result, int32_t hid){
	int32_t temp;
	uint64_t shifted;

	//shift result using hid;
	shifted = md5_result >> (hid*1);

	temp = shifted % num_vbt;

	return temp;
}

/* Func: check_written
 * check corresponding can be written for given lpa
 * if so, return ppa
 * if not return -1
 */
int32_t check_written( int32_t	pba, int32_t lpa, int32_t* cal_ppid)
{
	//int32_t bit_fr_lpa;
	int32_t temp2, ppa;

	//bit_fr_lpa = num_page_off - num_ppid;
	temp2 = bm->barray[pba].wr_off;

	// corresponding block is full;
	if(temp2 == _g_ppb){
		return -1;
	}

	if(temp2 > _g_ppb){
		printf("overflow\n");
	}

	ppa = (pba * _g_ppb) + temp2;
	*cal_ppid = temp2;
	return ppa;
}

/* Func: get_vba_from_table 
 * get vba from md5
 */
int32_t get_vba_from_table(struct ssd * ssd, struct ppa * ppa, int32_t lpa){
	int32_t vba, hid;
	uint32_t lpa_md5;
	uint64_t md5_res;
	size_t len = sizeof(lpa);

	if(ppa->hid == -1 || ppa->ppid == -1 || ppa->ppa_hash == -1) {
		ftl_debug("[HASH READ ERROR] hash mapping info returns -1 \n");
		ftl_debug("lpa: %d   hid: %d   ppid: %d   ppa_hash: %d \n", lpa, ppa->hid, ppa->ppid, ppa->ppa_hash);
	}

	hid = ppa.hid;

	lpa_md5 = lpa >> lpa_sft;
	md5_res = md5_u(&lpa_md5, len);

	vba = get_vba_from_md5(md5_res, hid);

	return vba;
}

/* Func: alloc_page
 * get vba from md5
 * translate vba to pba
 * check corresponding pba can be written
 * if so, return ppa
 * if not, do gc to get free page
 * if selectable blocks are full with valid pages,
 * check shared virtual block
 */
int32_t alloc_page(int32_t lpa, int32_t* cal_ppid, int32_t* hid){
	int32_t ppa, vba;
	int32_t max_pba, max_vba, max_inv, max_hid, pba, cur_inv; 
	uint32_t lpa_md5;
	uint64_t md5_res;
	size_t len = sizeof(lpa);
	Block *block;
	int i;
	FILE* bl_out;

	// variables for shared virtual block
	int32_t shr_num_free[3];
	int32_t shr_free_pba[3]; 
	int32_t shr_free_hid[3];				// record vba with max free pages
	int32_t shr_num_inv[3];
	int32_t shr_inv_vba[3];
	int32_t shr_inv_hid[3];					// record vba with max invalid pages
	int32_t vba_pos, vba_srt, vba_temp;			// caculate vba's position
	int32_t shr_pba, shr_vba, shr_hid, shr_chk;


	// get value from md5
	lpa_md5 = lpa >> lpa_sft;

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

	while(*hid != hid_secondary){
		vba = get_vba_from_md5(md5_res, *hid);

		vba_pos = vba % 4;
		vba_srt = vba - vba_pos;

		/* convert virtual block to physical block  */

		pba = vbt[vba].pba;

		if((ppa = check_written(pba, lpa, cal_ppid)) != -1){	
			break;
		}

		cur_inv = bm->barray[pba].Invalid;
		if(cur_inv > max_inv){
			max_pba = pba;
			max_hid = *hid;
			max_inv = cur_inv;
			max_vba = vba;
		}

		// check shared virtual block info.
		for(i = 0; i < 3 ; i++){
			vba_pos = (vba_pos + 1) % 4;
			vba_temp = vba_srt + vba_pos;

			shr_pba = vbt[vba_temp].pba;
			block = &bm->barray[shr_pba];

			if(shr_num_free[i] == 0 && _g_ppb - block->wr_off > 0){
				shr_free_pba[i] = shr_pba;
				shr_num_free[i] = _g_ppb - block->wr_off;
				shr_free_hid[i] = *hid;
			}
			if(shr_num_inv[i] < block->Invalid){
				shr_num_inv[i] = block->Invalid;
				shr_inv_vba[i] = vba_temp;
				shr_inv_hid[i] = *hid;
			}
		}

		*hid = *hid + 1;
	}



	/* corresponding blocks are full */
	if(*hid == hid_secondary){

		/* Log for 1st gc occurs */
		shr_chk = 0;

		if (max_pba == -1 || max_inv == 0) {
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
					shr_pba = vbt[shr_vba].pba;
					shr_hid = shr_inv_hid[i];
					break;
				}
			}

			if(shr_chk == 1){
				ppa = check_written(shr_pba, lpa, cal_ppid);
				*hid = shr_hid;
			}
			if(shr_chk == 2){
				pba = hash_garbage_collection(shr_pba, shr_vba);
				ppa = check_written(pba, lpa, cal_ppid);
				*hid = shr_hid;
			}
			else if(shr_chk == 0){	
				//print log for collision
				printf("\nno free space for lpa!!\n");
				*hid = 1;

				while(*hid != hid_secondary){
					vba = get_vba_from_md5(md5_res, *hid);

					/* convert virtual block to physical block  */
					pba = vbt[vba].pba;
					printf("\nHID: %d,\tPBA: %d,\tINVALID: %d\n", *hid, pba, bm->barray[pba].Invalid);

					*hid = *hid + 1;
				}
				bl_out = fopen("result/blk_info_1st_gc.txt", "w");
				fprintf(bl_out, "Total block: %d\r\n", _g_nob);
				fprintf(bl_out, "Pages per block: %d\r\n", _g_ppb);
				fprintf(bl_out, "PBA\tWrite Offset\tInvalid\r\n");

				for(int i = 0 ; i < _g_nob; i++){
					fprintf(bl_out, "%d\t", i);
					fprintf(bl_out, "%d\t\t", bm->barray[i].wr_off);
					fprintf(bl_out, "%d\t\r\n", bm->barray[i].Invalid);
				}
				fclose(bl_out);

				exit(3);
			}
		}
		else {
			pba = hash_garbage_collection(max_pba, max_vba);
			ppa = check_written(pba, lpa, cal_ppid);
			*hid = max_hid;
		}
	}

	return ppa;
}

/* Func: map_for_gc
 * process mapping table update when gc occurs
 */
int32_t map_for_gc(int32_t lpa, int32_t ppa, int32_t ppid){
	Block *block;

	pri_table[lpa].ppa = ppa;
	pri_table[lpa].state = H_VALID;
	pri_table[lpa].ppid = ppid;
	block = &bm->barray[BM_PPA_TO_PBA(ppa)];

	block->wr_off++;

	hash_OOB[ppa].lpa = lpa;
	BM_ValidatePage(bm, ppa);

	return 0;
}
