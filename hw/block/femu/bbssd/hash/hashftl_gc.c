#include "hashftl.h"


/* Function: Garbage Collection 
 * Process: 
 * garbage collection for hashftl.
 * find victim, exchange with op area.
 * SRAM_load -> SRAM_unload -> trim.
 * need to update mapping data as well.
 */
int32_t hash_garbage_collection(int32_t pba, int32_t vba){
	int32_t old_block;
	int32_t new_block;
	uint8_t all;
	int valid_page_num;
	Block *victim;
	Block *free;
	value_set *temp_set[_g_ppb];
	SRAM d_sram[_g_ppb];

	all = 0;
	gc_count++;

	victim = &bm->barray[pba];

	// if all invalid block
	if(victim->Invalid == _g_ppb){
		all = 1;
	}
	else if(victim->Invalid == 0){
		printf("block info\n");

		printf("block number: %d\n", pba);
		printf("invalid number: %d\n", victim->Invalid);
		printf("\n!!!full!!!\n");
		exit(2);
	}

	//exchange block
	victim->Invalid = 0;
	victim->wr_off = 0;
	old_block = victim->PBA * _g_ppb;

	free = BM_Dequeue(free_b);
	new_block = free->PBA * _g_ppb;
	vbt[vba].pba = free->PBA;



	// if all page is invalid, then just trim and return
	if(all){ 
		__hashftl.li->trim_block(old_block, false);
		BM_InitializeBlock(bm, victim->PBA);
		BM_Enqueue(free_b, victim);
		num_val_victim[0]++;
		return vbt[vba].pba;
	}

	valid_page_num = 0;
	gc_load = 0;

	for(int i=0;i<_g_ppb;i++){
		d_sram[i].PTR_RAM = NULL;
		d_sram[i].OOB_RAM.lpa = -1;
	}


	// read valid pages in block
	for(int i=old_block;i<old_block+_g_ppb;i++){
		// read valid page
		if(BM_IsValidPage(bm, i)){
			temp_set[valid_page_num] = SRAM_load(d_sram, i, valid_page_num);
			valid_page_num++;
		}
	}

	BM_InitializeBlock(bm, victim->PBA);

	while(gc_load != valid_page_num) {
	} // polling for reading all mapping data

	// copy data to memory and free dma valueset
	for(int i = 0; i < valid_page_num; i++){ 
		memcpy(d_sram[i].PTR_RAM, temp_set[i]->value, PAGESIZE);

		// free value_set in advance 
		inf_free_valueset(temp_set[i], FS_MALLOC_R); 
	}

	gc_load = 0;


	// write page into new block
	for(int i = 0; i < valid_page_num; i++){ 
		SRAM_unload(d_sram, new_block + i, i);
		map_for_gc(d_sram[i].OOB_RAM.lpa, new_block + i, i);
	}

	/* Trim block */
	__hashftl.li->trim_block(old_block, false);

	// put victim block into free block queue
	BM_Enqueue(free_b, victim);

	gc_val = gc_val + valid_page_num;
	num_copy = num_copy + valid_page_num;
	num_val_victim[valid_page_num]++;

	return vbt[vba].pba;
}
