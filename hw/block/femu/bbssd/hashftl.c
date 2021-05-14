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
	temp = shifted % (BLKS_PER_PL - 1);

	return temp;
}



static int hash_garbage_collection(struct ssd *ssd, uint32_t max_pba, uint32_t max_vba) {

    struct ssdparams *spp = &ssd->sp;
	struct ppa *ppa = NULL;
	struct nand_block *victim = NULL; 
	//ssd->reserved = NULL;

    struct nand_lun *lunp;
	ppa->ppa_hash = -1;

	ppa->ppa = 0;
	ppa->g.ch = 0;
	ppa->g.lun = 0;
	ppa->g.pg = 0;
	ppa->g.blk = max_pba;    // GC victim blk max_pba
	ppa->g.pl = 0;
	ftl_assert(ppa.g.pl == 0);

	// GC 1 get victim blk
	victim = get_blk(ssd, ppa);
	if(victim->ipc > 0) {
		lunp = get_lun(ssd, ppa);
		clean_one_block(ssd, ppa);
		mark_block_free(ssd, ppa);
		if (spp->enable_gc_delay) {
			struct nand_cmd gce;
			gce.type = GC_IO;
			gce.cmd = NAND_ERASE;
			gce.stime = 0;
			ssd_advance_status(ssd, ppa, &gce, 0);
		}
		lunp->gc_endtime = lunp->next_lun_avail_time;
		mark_line_free(ssd, ppa);
		ssd->num_GC = ssd->num_GC + 1; 
	} else { //  (victim->ipc == 0) 
		// nothing can be erased --> error 
		ftl_debug("selected victim has zero invalid pages cannot be erased");
	}

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

static int32_t get_vba_from_table(struct ssd * ssd, struct ppa * ppa, int32_t lpa) {
	int32_t vba, hid;
	uint32_t lpa_md5;
	uint64_t md5_res;
	size_t len = sizeof(lpa);

	if(ppa->hid == -1 || ppa->ppid == -1 || ppa->ppa_hash == -1) {
		ftl_debug("[HASH READ ERROR] hash mapping info returns -1 \n");
		ftl_debug("lpa: %d   hid: %d   ppid: %d   ppa_hash: %d \n", lpa, ppa->hid, ppa->ppid, ppa->ppa_hash);
	}

	hid = ppa->hid;

	lpa_md5 = lpa >> ssd->lpa_sft;
	md5_res = md5_u(&lpa_md5, len);

	vba = get_vba_from_md5(md5_res, hid);

	return vba;
}


struct ppa get_new_page_hash(struct ssd *ssd, int32_t lpa) 
{
	struct nand_block *block;

	struct ppa return_ppa; 

	//H_OOB *hash_OOB = &(ssd->hash_OOB); 
	
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
			ssd->hash_OOB[ppa->ppa_hash].lpa = lpa;
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
				ppa->ppa_hash = -1;

				return_ppa.ppa = 0;
				return_ppa.g.ch = ppa->g.ch;
				return_ppa.g.lun = ppa->g.lun;
				return_ppa.g.pg = ppa->g.pg;
				return_ppa.g.blk = ppa->g.blk;
				return_ppa.g.pl = ppa->g.pl;
				ftl_assert(return_ppa.g.pl == 0);


				ftl_debug("[HASH WRITE ERROR] HASH COLLISION CANNOT BE HANDLED for lpa %d \n", lpa);
				return return_ppa;
			}

		} else {  //GC occurs 
			pba = hash_garbage_collection(ssd, max_pba, max_vba);  //TODO 
			ppa = check_written(ssd, pba);
			hid = max_hid;
		}
	}

			
	ssd->hash_OOB[ppa->ppa_hash].lpa = lpa;

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




/* Function: hash_get
 * Process: 
 * gives pull request to lower level.
 * reads mapping data.
 * !!if not mapped, does not pull!!
 */

int hash_read(struct ssd *ssd, struct ppa * ppa, uint64_t lpn) {
	int32_t found_ppa;

	// shared virtual block variables
	int32_t vba, vba_pos, vba_srt, vba_temp, pba;
	int i;

	//H_OOB *hash_OOB = &(ssd->hash_OOB); 


	// shared virtual block read process
	vba = get_vba_from_table(ssd, ppa, lpn);

	if(vba == -1){
		ftl_debug("[HASH READ ERROR] returned vba is -1 of lpn \n", lpn);
	}

	vba_pos = vba % 4;
	vba_srt = vba - vba_pos;

	for(i = 0; i < 4; i++){
		vba_temp = vba_srt + vba_pos;
		pba = (ssd->vbt[vba_temp]).pba;
		found_ppa = pba * BLKS_PER_PL + ppa->ppid;

		if(ssd->hash_OOB[found_ppa].lpa == lpn){
			if(found_ppa != ppa->ppa){
				printf("\nppa mismatch\n");
			
			}
			// shr_read_cnt[i]++;
			return 0;
		}	
		vba_pos = (vba_pos + 1) % 4;
	}

	// ERROR 
	ftl_debug("\nppa miss\n");	
	/*
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
	} */
	return -1; 
}
