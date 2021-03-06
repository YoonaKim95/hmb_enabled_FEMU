#include "ftl.h"
#include "hmb.h"
#include "hmb_internal.h"
#include "hmb_types.h"
#include "hmb_utils.h"
#include "hmb_spaceMgmt.h" /* for HMB_HAS_NO_ENTRY in hmb_meta_init() */
#include "hmb_lru.h"
//#define FEMU_DEBUG_FTL

static void *ftl_thread(void *arg);

static inline bool should_gc(struct ssd *ssd)
{
    //return (ssd->free_blk_cnt <= ssd->sp.gc_thres_blks);
    return (ssd->free_blk_cnt <= 200);
    // return (ssd->lm.free_line_cnt <= ssd->sp.gc_thres_lines);
}

static inline bool should_gc_high(struct ssd *ssd)
{
    return (ssd->free_blk_cnt <= 1);
    //return (ssd->free_blk_cnt <= ssd->sp.gc_thres_blks_high);  
	//// return (ssd->lm.free_line_cnt <= ssd->sp.gc_thres_lines_high);
}

// 0 == dirty | evicted | not_cached 
// 1 == hmb_cached 
static inline void set_hmb_cache(struct ssd *ssd, int entry, int i)
{
    ssd->hmb_cache_bm[entry] = i;
}


static inline uint8_t hmb_cached(struct ssd *ssd, int entry)
{
    return ssd->hmb_cache_bm[entry];
}

static inline struct ppa get_maptbl_ent(struct ssd *ssd, uint64_t lpn)
{
    return ssd->maptbl[lpn];
}

static inline void set_reserved_blk(struct ssd *ssd, struct nand_block *r_blk)
{
	r_blk->reserved = true;
	ssd->reserved = *r_blk;
}

static inline void set_maptbl_ent(struct ssd *ssd, uint64_t lpn, struct ppa *ppa)
{
    ftl_assert(lpn < ssd->sp.tt_pgs);
    ssd->maptbl[lpn] = *ppa;
}

uint64_t ppa2pgidx(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    uint64_t pgidx;

    pgidx = ppa->g.ch  * spp->pgs_per_ch  + \
            ppa->g.lun * spp->pgs_per_lun + \
            ppa->g.pl  * spp->pgs_per_pl  + \
            ppa->g.blk * spp->pgs_per_blk + \
            ppa->g.pg;

    ftl_assert(pgidx < spp->tt_pgs);

    return pgidx;
}

static inline uint64_t get_rmap_ent(struct ssd *ssd, struct ppa *ppa)
{
    uint64_t pgidx = ppa2pgidx(ssd, ppa);

    return ssd->rmap[pgidx];
}

/* set rmap[page_no(ppa)] -> lpn */
static inline void set_rmap_ent(struct ssd *ssd, uint64_t lpn, struct ppa *ppa)
{
    uint64_t pgidx = ppa2pgidx(ssd, ppa);

    ssd->rmap[pgidx] = lpn;
}
////////////// current version line == blk
static inline int victim_blk_cmp_pri(pqueue_pri_t next, pqueue_pri_t curr)
{
    return (next > curr);
}

static inline pqueue_pri_t victim_blk_get_pri(void *a)
{
    return ((struct line *)a)->vpc;
}

static inline void victim_blk_set_pri(void *a, pqueue_pri_t pri)
{
    ((struct line *)a)->vpc = pri;
}

static inline size_t victim_blk_get_pos(void *a)
{
    return ((struct line *)a)->pos;
}

static inline void victim_blk_set_pos(void *a, size_t pos)
{
    ((struct line *)a)->pos = pos;
}
/////////////////////////////


/////////////////////////////
static inline int victim_line_cmp_pri(pqueue_pri_t next, pqueue_pri_t curr)
{
    return (next > curr);
}

static inline pqueue_pri_t victim_line_get_pri(void *a)
{
    return ((struct line *)a)->vpc;
}

static inline void victim_line_set_pri(void *a, pqueue_pri_t pri)
{
    ((struct line *)a)->vpc = pri;
}

static inline size_t victim_line_get_pos(void *a)
{
    return ((struct line *)a)->pos;
}

static inline void victim_line_set_pos(void *a, size_t pos)
{
    ((struct line *)a)->pos = pos;
}

static void ssd_init_lines(struct ssd *ssd)
{
    struct ssdparams *spp = &ssd->sp;
    struct line_mgmt *lm = &ssd->lm;
    struct line *line;

    lm->tt_lines = spp->blks_per_pl;
    ftl_assert(lm->tt_lines == spp->tt_lines);
    lm->lines = g_malloc0(sizeof(struct line) * lm->tt_lines);

    QTAILQ_INIT(&lm->free_line_list);
	// vpc
	lm->victim_line_pq = pqueue_init(spp->tt_lines, victim_line_cmp_pri,
            victim_line_get_pri, victim_line_set_pri,
            victim_line_get_pos, victim_line_set_pos);
    QTAILQ_INIT(&lm->full_line_list);

    lm->free_line_cnt = 0;
    for (int i = 0; i < lm->tt_lines; i++) {
        line = &lm->lines[i];
        line->id = i;
        line->ipc = 0;
        line->vpc = 0;
        /* initialize all the lines as free lines */
        QTAILQ_INSERT_TAIL(&lm->free_line_list, line, entry);
        lm->free_line_cnt++;
    }

    ftl_assert(lm->free_line_cnt == lm->tt_lines);
    lm->victim_line_cnt = 0;
    lm->full_line_cnt = 0;
}

static void ssd_init_write_pointer(struct ssd *ssd)
{
    struct write_pointer *wpp = &ssd->wp;
    struct line_mgmt *lm = &ssd->lm;
    struct line *curline = NULL;

    curline = QTAILQ_FIRST(&lm->free_line_list);
    QTAILQ_REMOVE(&lm->free_line_list, curline, entry);
    lm->free_line_cnt--;

    /* wpp->curline is always our next-to-write super-block */
    wpp->curline = curline;
    wpp->ch = 0;
    wpp->lun = 0;
    wpp->pg = 0;
    wpp->blk = 0;
    wpp->pl = 0;
}

static inline void check_addr(int a, int max)
{
    ftl_assert(a >= 0 && a < max);
}

static struct line *get_next_free_line(struct ssd *ssd)
{
    struct line_mgmt *lm = &ssd->lm;
    struct line *curline = NULL;

    curline = QTAILQ_FIRST(&lm->free_line_list);
    if (!curline && !HASH_FTL) {
        do_gc(ssd, true);
		curline = QTAILQ_FIRST(&lm->free_line_list);
		if(!curline) {
			ftl_err("No free lines left in [%s] !!!!\n", ssd->ssdname);
			hmb_debug("victim=%d,full=%d,free=%d", ssd->lm.victim_line_cnt, ssd->lm.full_line_cnt, ssd->lm.free_line_cnt);

			return NULL;
		}
    }

    QTAILQ_REMOVE(&lm->free_line_list, curline, entry);
    lm->free_line_cnt--;
    return curline;
}

static void ssd_advance_block_write_pointer(struct ssd *ssd,  struct ppa *ppa)
{
	struct nand_block *blk;
	blk = get_blk(ssd, ppa); 
	blk->wp++; 
} 


static void ssd_advance_write_pointer(struct ssd *ssd)
{

	struct ssdparams *spp = &ssd->sp;
    struct write_pointer *wpp = &ssd->wp;
    struct line_mgmt *lm = &ssd->lm;

    check_addr(wpp->ch, spp->nchs);
    wpp->ch++;
    if (wpp->ch == spp->nchs) {
        wpp->ch = 0;
        check_addr(wpp->lun, spp->luns_per_ch);
        wpp->lun++;
        /* in this case, we should go to next lun */
        if (wpp->lun == spp->luns_per_ch) {
            wpp->lun = 0;
            /* go to next page in the block */
            check_addr(wpp->pg, spp->pgs_per_blk);
            wpp->pg++;
            if (wpp->pg == spp->pgs_per_blk) {
                wpp->pg = 0;
                /* move current line to {victim,full} line list */
                if (wpp->curline->vpc == spp->pgs_per_line) {
                    /* all pgs are still valid, move to full line list */
                    ftl_assert(wpp->curline->ipc == 0);
                    QTAILQ_INSERT_TAIL(&lm->full_line_list, wpp->curline, entry);
                    lm->full_line_cnt++;
                } else {
                    ftl_assert(wpp->curline->vpc >= 0 && wpp->curline->vpc < spp->pgs_per_line);
                    /* there must be some invalid pages in this line */
                    ftl_assert(wpp->curline->ipc > 0);
                    pqueue_insert(lm->victim_line_pq, wpp->curline);
                    lm->victim_line_cnt++;

                }
                /* current line is used up, pick another empty line */
                check_addr(wpp->blk, spp->blks_per_pl);
                wpp->curline = NULL;
                wpp->curline = get_next_free_line(ssd);
                if (!wpp->curline) {
                    /* TODO */
                    abort();
                }
                wpp->blk = wpp->curline->id;
				ssd->free_blk_cnt--; 
                check_addr(wpp->blk, spp->blks_per_pl);
                /* make sure we are starting from page 0 in the super block */
                ftl_assert(wpp->pg == 0);
                ftl_assert(wpp->lun == 0);
                ftl_assert(wpp->ch == 0);
                /* TODO: assume # of pl_per_lun is 1, fix later */
                ftl_assert(wpp->pl == 0);
            }
        }
    }
}

static struct ppa get_new_page(struct ssd *ssd)
{
    struct ppa ppa;	
	struct write_pointer *wpp = &ssd->wp;
	ppa.ppa = 0;
	ppa.g.ch = wpp->ch;
	ppa.g.lun = wpp->lun;
	ppa.g.pg = wpp->pg;
	ppa.g.blk = wpp->blk;
	ppa.g.pl = wpp->pl;
	ftl_assert(ppa.g.pl == 0);
    return ppa;
}

static void check_params(struct ssdparams *spp)
{
    /*
     * we are using a general write pointer increment method now, no need to
     * force luns_per_ch and nchs to be power of 2
     */

    //ftl_assert(is_power_of_2(spp->luns_per_ch));
    //ftl_assert(is_power_of_2(spp->nchs));
}

static void ssd_init_params(struct ssdparams *spp, struct ssd *ssd)
{
    spp->secsz = SECSZ;
    spp->secs_per_pg = SECS_PER_PAGE;
    spp->pgs_per_blk = PGS_PER_BLK;
    spp->blks_per_pl = BLKS_PER_PL; /* 16GB */
    spp->pls_per_lun = PLS_PER_LUN;
    spp->luns_per_ch = LUNS_PER_CH;
    spp->nchs = NCHS;

    spp->mp_rd_lat = NAND_READ_LATENCY;
    spp->pg_rd_lat = NAND_READ_LATENCY;
    spp->pg_wr_lat = NAND_PROG_LATENCY;
    spp->blk_er_lat = NAND_ERASE_LATENCY;
    spp->ch_xfer_lat = 0;

    /* calculated values */
    spp->secs_per_blk = spp->secs_per_pg * spp->pgs_per_blk;
    spp->secs_per_pl = spp->secs_per_blk * spp->blks_per_pl;
    spp->secs_per_lun = spp->secs_per_pl * spp->pls_per_lun;
    spp->secs_per_ch = spp->secs_per_lun * spp->luns_per_ch;
    spp->tt_secs = spp->secs_per_ch * spp->nchs;

    spp->pgs_per_pl = spp->pgs_per_blk * spp->blks_per_pl;
    spp->pgs_per_lun = spp->pgs_per_pl * spp->pls_per_lun;
    spp->pgs_per_ch = spp->pgs_per_lun * spp->luns_per_ch;
    spp->tt_pgs = spp->pgs_per_ch * spp->nchs;

	/*YA hmb*/
	spp->hmb_lpas_per_blk = (4096 * 8 ) / ssd->addr_bits ;
	printf("hmb lpas per blk: %d\n", spp->hmb_lpas_per_blk);

    spp->blks_per_lun = spp->blks_per_pl * spp->pls_per_lun;
    spp->blks_per_ch = spp->blks_per_lun * spp->luns_per_ch;
    spp->tt_blks = spp->blks_per_ch * spp->nchs;

    spp->pls_per_ch =  spp->pls_per_lun * spp->luns_per_ch;
    spp->tt_pls = spp->pls_per_ch * spp->nchs;

    spp->tt_luns = spp->luns_per_ch * spp->nchs;

    /* line is special, put it at the end */
    spp->blks_per_line = spp->tt_luns; /* TODO: to fix under multiplanes */
    spp->pgs_per_line = spp->blks_per_line * spp->pgs_per_blk;
    spp->secs_per_line = spp->pgs_per_line * spp->secs_per_pg;
    spp->tt_lines = spp->blks_per_lun; /* TODO: to fix under multiplanes */

    //spp->gc_thres_pcent = 0.75;
    //spp->gc_thres_pcent = 0.85;
    spp->gc_thres_pcent = 0.99;
    spp->gc_thres_lines = (int)((1 - spp->gc_thres_pcent) * spp->tt_lines);

    spp->gc_thres_blks = (int)((1 - spp->gc_thres_pcent) * BLKS_PER_PL);
	
	spp->gc_thres_pcent_high = 0.999;
    spp->gc_thres_lines_high = (int)((1 - spp->gc_thres_pcent_high) * spp->tt_lines);
    spp->gc_thres_blks_high = (int)((1 - spp->gc_thres_pcent_high) * BLKS_PER_PL);
   
	spp->enable_gc_delay = true;


    check_params(spp);
}

static void ssd_init_nand_page(struct nand_page *pg, struct ssdparams *spp)
{
    pg->nsecs = spp->secs_per_pg;
    pg->sec = g_malloc0(sizeof(nand_sec_status_t) * pg->nsecs);
    for (int i = 0; i < pg->nsecs; i++) {
        pg->sec[i] = SEC_FREE;
    }
    pg->status = PG_FREE;
}

static void ssd_init_nand_blk(struct nand_block *blk, struct ssdparams *spp, uint64_t id)
{
    blk->npgs = spp->pgs_per_blk;
    blk->pg = g_malloc0(sizeof(struct nand_page) * blk->npgs);
	for (int i = 0; i < blk->npgs; i++) {
        ssd_init_nand_page(&blk->pg[i], spp);
    }

	
	blk->blk_id = id;
    blk->ipc = 0;
    blk->vpc = 0;
    blk->erase_cnt = 0;
    blk->wp = 0;

	blk->reserved = false;
}

static void ssd_init_nand_plane(struct nand_plane *pl, struct ssdparams *spp)
{
    pl->nblks = spp->blks_per_pl;
    pl->blk = g_malloc0(sizeof(struct nand_block) * pl->nblks);
    for (int i = 0; i < pl->nblks; i++) {
        ssd_init_nand_blk(&pl->blk[i], spp, i);
    }
}

static void ssd_init_nand_lun(struct nand_lun *lun, struct ssdparams *spp)
{
    lun->npls = spp->pls_per_lun;
    lun->pl = g_malloc0(sizeof(struct nand_plane) * lun->npls);
    for (int i = 0; i < lun->npls; i++) {
        ssd_init_nand_plane(&lun->pl[i], spp);
    }
    lun->next_lun_avail_time = 0;
    lun->busy = false;
}


static void ssd_init_ch(struct ssd_channel *ch, struct ssdparams *spp)
{
    ch->nluns = spp->luns_per_ch;
    ch->lun = g_malloc0(sizeof(struct nand_lun) * ch->nluns);
    for (int i = 0; i < ch->nluns; i++) {
        ssd_init_nand_lun(&ch->lun[i], spp);
    }
    ch->next_ch_avail_time = 0;
    ch->busy = 0;
}

static void ssd_init_maptbl(struct ssd *ssd)
{
    struct ssdparams *spp = &ssd->sp;

    ssd->maptbl = g_malloc0(sizeof(struct ppa) * spp->tt_pgs);
	for (int i = 0; i < spp->tt_pgs; i++) {
		ssd->maptbl[i].ppa = UNMAPPED_PPA;
		ssd->maptbl[i].ppa_hash = -1;
		ssd->maptbl[i].ppid = -1;
		ssd->maptbl[i].hid = -1;  
	}
}

static void ssd_init_hmb_cache(struct ssd *ssd)
{
    struct ssdparams *spp = &ssd->sp;

	int available_entries = (spp->tt_pgs) / (spp->hmb_lpas_per_blk); 
	hmb_debug("available entries %u ", available_entries);

    ssd->hmb_cache_bm = g_malloc0(sizeof(int) * (available_entries));
    
	for (int i = 0; i < available_entries ; i++) {
        ssd->hmb_cache_bm[i] = 0;
	}

	// ssd->nr_hmb_cache = HMB_CTRL.FTL_free_mappings; 
	ssd->nr_hmb_cache = HMB_ENTRIES;

	if(HMB_ENTRIES != 0 ) {
		ssd->hmb_lru_list  = init_hmb_LRU_list(ssd->nr_hmb_cache);
		ssd->hmb_lru_hash  = init_hmb_LRU_hash(available_entries);
	}
}


static void ssd_init_hash_FTL(struct ssd *ssd) {
    struct ssdparams *spp = &ssd->sp;

	ssd->num_hid = HID_BITS;
	//ssd->num_page_off = 8;
	ssd->num_ppid = 8;

	int secondary = 1;
	for(int i = 0; i < ssd->num_hid; i++)
		secondary = secondary * 2;

	printf("HASH secondary: %d\n", secondary);

	ssd->hid_secondary = secondary;
	
	ssd->hash_OOB = (H_OOB*)malloc(sizeof(H_OOB) * spp->tt_pgs);
	ssd->vbt = (VIRTUAL_BLOCK_TABLE*)malloc(sizeof(VIRTUAL_BLOCK_TABLE) * (BLKS_PER_PL -1));

	// initialize primary table & oob
	for(int i = 0; i < spp->tt_pgs; i++){
		ssd->hash_OOB[i].lpa = -1;
	}

	// initialize virtual block table
	for(int i = 0; i < (BLKS_PER_PL -1); i++){
		ssd->vbt[i].pba = i;
		ssd->vbt[i].state = 0;
	}

	struct ppa reserved;
	reserved.ppa = 0;
	reserved.g.ch = 0;
	reserved.g.lun = 0;
	reserved.g.pg = 0;
	reserved.g.blk = BLKS_PER_PL -1;    // GC victim blk max_pba
	reserved.g.pl = 0;
	ftl_assert(reserved.g.pl == 0);

	struct nand_block *res = NULL;
	res = get_blk(ssd, &reserved);
	set_reserved_blk(ssd, res);
}

static void ssd_init_rmap(struct ssd *ssd)
{
    struct ssdparams *spp = &ssd->sp;

    ssd->rmap = g_malloc0(sizeof(uint64_t) * spp->tt_pgs);
    for (int i = 0; i < spp->tt_pgs; i++) {
        ssd->rmap[i] = INVALID_LPN;
    }
}

void ssd_init(FemuCtrl *n)
{
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;

    ftl_assert(ssd);
	
	// ex. 32bits for mapping || 16 bits ( 6 + 8) for hash	
	ssd->addr_bits = ADDR_BITS;

	ssd->hmb_read_hit = 0; 

	ssd->lpa_sft = HASH_SFT; 
	ssd->num_GC = 0; 
	ssd->num_GCcopy = 0;


	ssd->tot_update = 0;
	ssd->tot_write = 0;
	ssd->tot_read = 0;

	ssd->full_invalid_cnt = 0;
	ssd->blk_erase_cnt = 0;
	ssd->free_blk_cnt = BLKS_PER_PL;


    ssd_init_params(spp, ssd);

    /* initialize ssd internal layout architecture */
    ssd->ch = g_malloc0(sizeof(struct ssd_channel) * spp->nchs);
    for (int i = 0; i < spp->nchs; i++) {
        ssd_init_ch(&ssd->ch[i], spp);
    }

	/* initialize maptbl */
    ssd_init_maptbl(ssd);
    
	//ssd_init_maptbl_hash(ssd);

    /* initialize rmap */
	ssd_init_rmap(ssd);

	 /* initialize rmap */
    ssd_init_hmb_cache(ssd);

    /* initialize all the lines */
    ssd_init_lines(ssd);

    /* initialize write pointer, this is how we allocate new pages for writes */
    ssd_init_write_pointer(ssd);

//#if defined(HASH_FTL)
	ssd_init_hash_FTL(ssd);
//#endif 

	if(!HASH_FTL) {
		ssd->victim_blk_pq = pqueue_init(BLKS_PER_PL, victim_blk_cmp_pri,
				victim_blk_get_pri, victim_blk_set_pri,
				victim_blk_get_pos, victim_blk_set_pos);
	}


	// hmb_debug("possible caching entries: %u ", (spp->tt_pgs * ssd->addr_bits) / (HMB_CTRL.page_size * 8));	

	printf("ssd init done");
    qemu_thread_create(&ssd->ftl_thread, "FEMU-FTL-Thread", ftl_thread, n,
                       QEMU_THREAD_JOINABLE);
}

static inline bool valid_ppa(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    int ch = ppa->g.ch;
    int lun = ppa->g.lun;
    int pl = ppa->g.pl;
    int blk = ppa->g.blk;
    int pg = ppa->g.pg;
    int sec = ppa->g.sec;

    if (ch >= 0 && ch < spp->nchs && lun >= 0 && lun < spp->luns_per_ch && pl >=
        0 && pl < spp->pls_per_lun && blk >= 0 && blk < spp->blks_per_pl && pg
        >= 0 && pg < spp->pgs_per_blk && sec >= 0 && sec < spp->secs_per_pg)
        return true;

    return false;
}

static inline bool valid_lpn(struct ssd *ssd, uint64_t lpn)
{
    return (lpn < ssd->sp.tt_pgs);
}

static inline bool mapped_ppa(struct ppa *ppa)
{
    return !(ppa->ppa == UNMAPPED_PPA);
}


static inline struct ssd_channel *get_ch(struct ssd *ssd, struct ppa *ppa)
{
    return &(ssd->ch[ppa->g.ch]);
}

struct nand_lun *get_lun(struct ssd *ssd, struct ppa *ppa)
{
    struct ssd_channel *ch = get_ch(ssd, ppa);
    return &(ch->lun[ppa->g.lun]);
}

static inline struct nand_plane *get_pl(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_lun *lun = get_lun(ssd, ppa);
    return &(lun->pl[ppa->g.pl]);
}

struct nand_block *get_blk(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_plane *pl = get_pl(ssd, ppa);
    return &(pl->blk[ppa->g.blk]);
}

static inline struct line *get_line(struct ssd *ssd, struct ppa *ppa)
{
    return &(ssd->lm.lines[ppa->g.blk]);
}

static inline struct nand_page *get_pg(struct ssd *ssd, struct ppa *ppa)
{
	struct nand_block *blk = get_blk(ssd, ppa);
    return &(blk->pg[ppa->g.pg]);
}

uint64_t ssd_advance_status(struct ssd *ssd, struct ppa *ppa, struct
        nand_cmd *ncmd, int hmb_cached)
{
    int c = ncmd->cmd;
    uint64_t cmd_stime = (ncmd->stime == 0) ? \
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : ncmd->stime;
    uint64_t nand_stime;
    struct ssdparams *spp = &ssd->sp;
    struct nand_lun *lun = get_lun(ssd, ppa);
    uint64_t lat = 0;

    switch (c) {
    case NAND_READ:
        /* read: perform NAND cmd first */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        if(hmb_cached == 1)
			lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;
		else
			lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat + spp->mp_rd_lat;
        lat = lun->next_lun_avail_time - cmd_stime;
        break;

    case NAND_WRITE:
        /* write: transfer data through channel first */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        if (ncmd->type == USER_IO) {
            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
        } else {
            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
        }
        lat = lun->next_lun_avail_time - cmd_stime;
        break;
    case NAND_ERASE:
        /* erase: only need to advance NAND status */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->blk_er_lat;

        lat = lun->next_lun_avail_time - cmd_stime;
        break;

    default:
        ftl_err("Unsupported NAND command: 0x%x\n", c);
    }
    return lat;
}

/* update SSD status about one page from PG_VALID -> PG_VALID */
static void mark_page_invalid(struct ssd *ssd, struct ppa *ppa)
{
    struct line_mgmt *lm = &ssd->lm;
    struct ssdparams *spp = &ssd->sp;
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    bool was_full_line = false;
    struct line *line;

	if(HASH_FTL)
		ssd->hash_OOB[ppa2pgidx(ssd, ppa)].lpa = -1;
  	
	/* update corresponding page status */
    pg = get_pg(ssd, ppa);
    ftl_assert(pg->status == PG_VALID);
    pg->status = PG_INVALID;

    /* update corresponding block status */
    blk = get_blk(ssd, ppa);
    ftl_assert(blk->ipc >= 0 && blk->ipc < spp->pgs_per_blk);
    blk->ipc++;
    ftl_assert(blk->vpc > 0 && blk->vpc <= spp->pgs_per_blk);
    blk->vpc--;

	if(!HASH_FTL) {
		/* update corresponding line status */
		line = get_line(ssd, ppa);
		ftl_assert(line->ipc >= 0 && line->ipc < spp->pgs_per_line);
		if (line->vpc == spp->pgs_per_line) {
			ftl_assert(line->ipc == 0);
			was_full_line = true;
		}
		line->ipc++;
		ftl_assert(line->vpc > 0 && line->vpc <= spp->pgs_per_line);
		line->vpc--;	
		
		if (was_full_line) {
			/* move line: "full" -> "victim" */
			QTAILQ_REMOVE(&lm->full_line_list, line, entry);
			lm->full_line_cnt--;
			pqueue_insert(lm->victim_line_pq, line);
			lm->victim_line_cnt++;	
		} else {
			pqueue_remove(lm->victim_line_pq, line); 
			pqueue_insert(lm->victim_line_pq, line);
		}

		
		// full invalid 
		if(line->ipc == PGS_PER_BLK  && line->vpc == 0) {
			pqueue_insert(ssd->victim_blk_pq, line);
			
			pqueue_remove(lm->victim_line_pq, line); 
			lm->victim_line_cnt--;
			ssd->full_invalid_cnt++;
		} 	
	}
}

static void mark_page_valid(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    struct line *line;

    /* update page status */
    pg = get_pg(ssd, ppa);
    ftl_assert(pg->status == PG_FREE);
    pg->status = PG_VALID;

    /* update corresponding block status */
    blk = get_blk(ssd, ppa);
    ftl_assert(blk->vpc >= 0 && blk->vpc < ssd->sp.pgs_per_blk);
    blk->vpc++;

    /* update corresponding line status */
    line = get_line(ssd, ppa);
    ftl_assert(line->vpc >= 0 && line->vpc < ssd->sp.pgs_per_line);
    line->vpc++;
}

void mark_block_free(struct ssd *ssd, struct ppa *ppa)
{
    
	struct ssdparams *spp = &ssd->sp;
    struct nand_block *blk = get_blk(ssd, ppa);
    struct nand_page *pg = NULL;

    for (int i = 0; i < spp->pgs_per_blk; i++) {
        /* reset page status */
        pg = &blk->pg[i];
        ftl_assert(pg->nsecs == spp->secs_per_pg);
        pg->status = PG_FREE;
    }

    /* reset block status */
    ftl_assert(blk->npgs == spp->pgs_per_blk);
    
	blk->ipc = 0;
    blk->vpc = 0;
	blk->wp = 0; 

    blk->erase_cnt++;
	ssd->blk_erase_cnt++; 
}

static void gc_read_page(struct ssd *ssd, struct ppa *ppa)
{
 
    struct ssdparams *spp = &ssd->sp;
	/* advance ssd status, we don't care about how long it takes */
    if (ssd->sp.enable_gc_delay) {
        struct nand_cmd gcr;	
		int hmb_cache_entry = 0;
		uint64_t lpn = get_rmap_ent(ssd, ppa);

        gcr.type = GC_IO;
        gcr.cmd = NAND_READ;
        gcr.stime = 0;

		// chk mapping cached to HMB
		hmb_cache_entry = (lpn) / spp->hmb_lpas_per_blk;

		// chk if lpn is cached to HMB
		if (hmb_cached(ssd, hmb_cache_entry) == 0) { // not mapped to HMB 
			ssd_advance_status(ssd, ppa, &gcr, 0);  // not cahced  (rd_lat *2)
		} else {
			ssd_advance_status(ssd, ppa, &gcr, 1);   // TODO NEED TO CHK HMB CACHE 
		}
    }
}

/* move valid page data (already in DRAM) from victim line to a new page */
static uint64_t gc_write_page_hash(struct ssd *ssd, struct ppa *old_ppa)
{
    struct ppa new_ppa;	
	struct nand_lun *new_lun;

    uint64_t lpn = get_rmap_ent(ssd, old_ppa);
	ftl_assert(valid_lpn(ssd, lpn));
	
	new_ppa.ppa = 0;	
	new_ppa.g.ch = 0;
	new_ppa.g.lun = 0;
	new_ppa.g.pg = (ssd->reserved).wp;
	new_ppa.g.blk = (ssd->reserved).blk_id;
	new_ppa.g.pl = 0;

	new_ppa.hid = old_ppa->hid; 
	new_ppa.ppid = (ssd->reserved).wp; 

	new_ppa.ppa_hash = ppa2pgidx(ssd, &new_ppa);

	ftl_assert(new_ppa.g.pl == 0);
	(ssd->reserved).wp++;
	
	/* update maptbl */
    set_maptbl_ent(ssd, lpn, &new_ppa);
    
	/* update rmap */
    set_rmap_ent(ssd, lpn, &new_ppa);

	ssd->hash_OOB[old_ppa->ppa_hash].lpa = -1;
	mark_page_valid(ssd, &new_ppa);

    /* need to advance the write pointer here */ 
	ssd_advance_block_write_pointer(ssd, &new_ppa);

    if (ssd->sp.enable_gc_delay) {
        struct nand_cmd gcw;
        gcw.type = GC_IO;
        gcw.cmd = NAND_WRITE;
        gcw.stime = 0;
        ssd_advance_status(ssd, &new_ppa, &gcw, 0);
    }

    new_lun = get_lun(ssd, &new_ppa);
    new_lun->gc_endtime = new_lun->next_lun_avail_time;
			
	ssd->hash_OOB[ppa2pgidx(ssd, &(new_ppa))].lpa = lpn;
    return 0;
}


/* move valid page data (already in DRAM) from victim line to a new page */
static uint64_t gc_write_page(struct ssd *ssd, struct ppa *old_ppa)
{
    struct ppa new_ppa;
    struct nand_lun *new_lun;
    uint64_t lpn = get_rmap_ent(ssd, old_ppa);

    ftl_assert(valid_lpn(ssd, lpn));
    new_ppa = get_new_page(ssd);

	/* update maptbl */
    set_maptbl_ent(ssd, lpn, &new_ppa);
    
	/* update rmap */
    set_rmap_ent(ssd, lpn, &new_ppa);

    mark_page_valid(ssd, &new_ppa);

    /* need to advance the write pointer here */
    ssd_advance_write_pointer(ssd);

    if (ssd->sp.enable_gc_delay) {
        struct nand_cmd gcw;
        gcw.type = GC_IO;
        gcw.cmd = NAND_WRITE;
        gcw.stime = 0;
        ssd_advance_status(ssd, &new_ppa, &gcw, 0);
    }

    new_lun = get_lun(ssd, &new_ppa);
    new_lun->gc_endtime = new_lun->next_lun_avail_time;

    return 0;
}

static struct line *select_victim_line(struct ssd *ssd, bool force)
{
    struct line_mgmt *lm = &ssd->lm;
    struct line *victim_line = NULL;

	if(ssd->full_invalid_cnt > 0) {
		victim_line = pqueue_peek(ssd->victim_blk_pq);
		
		if(!victim_line) {
			printf("full vic blk mngmt err");
			return NULL;
		}
		
		pqueue_pop(ssd->victim_blk_pq);
		ssd->full_invalid_cnt--;

		return victim_line;

	} else if(force && (ssd->full_invalid_cnt == 0)) {
		victim_line = pqueue_peek(lm->victim_line_pq);

		if (!victim_line) {
			return NULL;
		}


		pqueue_pop(lm->victim_line_pq);
		lm->victim_line_cnt--;

	} else {
		return NULL; 
	}

	return victim_line;
}

/* here ppa identifies the block we want to clean */
void clean_one_block(struct ssd *ssd, struct ppa *ppa, uint64_t vba)
{
    struct ssdparams *spp = &ssd->sp;
    struct nand_page *pg_iter = NULL;
    struct ppa old_ppa;
	int cnt = 0;
	int lpn = 0; 
	
    for (int pg = 0; pg < spp->pgs_per_blk; pg++) {
        ppa->g.pg = pg;
        pg_iter = get_pg(ssd, ppa);
        /* there shouldn't be any free page in victim blocks */
        ftl_assert(pg_iter->status != PG_FREE);
		lpn = get_rmap_ent(ssd, ppa);

		if (pg_iter->status == PG_VALID) {
            gc_read_page(ssd, ppa);
            /* delay the maptbl update until "write" happens */
            if(!HASH_FTL) 
				gc_write_page(ssd, ppa);
			else {
				old_ppa = get_maptbl_ent(ssd, lpn);
				
				ppa->hid = old_ppa.hid;
				ppa->ppa_hash = old_ppa.ppa_hash; 
				
				gc_write_page_hash(ssd, ppa);
			}

			// chk hmb cached info and makr dirty	
			int hmb_cache_entry = (lpn) / spp->hmb_lpas_per_blk;	
			if (hmb_cached(ssd, hmb_cache_entry) == 1) { // mapping mapped to HMB 
				set_hmb_cache(ssd, hmb_cache_entry, 0); // set HMB cache dirty  
			}
            
			cnt++;  // valid page copy count 
        }
    }

	ssd->num_GCcopy = ssd->num_GCcopy + cnt; 
    ftl_assert(get_blk(ssd, ppa)->vpc == cnt);
}

void mark_line_free(struct ssd *ssd, struct ppa *ppa)
{
    struct line_mgmt *lm = &ssd->lm;
    struct line *line = get_line(ssd, ppa);
    line->ipc = 0;
    line->vpc = 0;
    /* move this line to free line list */
    QTAILQ_INSERT_TAIL(&lm->free_line_list, line, entry);
    lm->free_line_cnt++;
}

int do_gc(struct ssd *ssd, bool force)
{
	struct line *victim_line = NULL;
    struct ssdparams *spp = &ssd->sp;
    
	struct nand_lun *lunp;
    
	int i = 0; 

	victim_line = select_victim_line(ssd, force);
	if (!victim_line) {
		return -1;
	}

	while(should_gc(ssd) && victim_line && i < 2) { 
		i++;
		
		struct ppa ppa;
		int ch, lun;

		ppa.g.blk = victim_line->id;
		//hmb_debug("GC-ing F %d line:%d,ipc=%d,victim=%d,full_inv=%d full=%d,free=%d", force, ppa.g.blk,
		//		victim_line->ipc, ssd->lm.victim_line_cnt, ssd->full_invalid_cnt, ssd->lm.full_line_cnt,
		//		ssd->lm.free_line_cnt);
		
		/* copy back valid data */
		for (ch = 0; ch < spp->nchs; ch++) {
			for (lun = 0; lun < spp->luns_per_ch; lun++) {
				ppa.g.ch = ch;
				ppa.g.lun = lun;
				ppa.g.pl = 0;
				lunp = get_lun(ssd, &ppa);
				clean_one_block(ssd, &ppa, 0);
				mark_block_free(ssd, &ppa);

				if (spp->enable_gc_delay) {
					struct nand_cmd gce;
					gce.type = GC_IO;
					gce.cmd = NAND_ERASE;
					gce.stime = 0;
					ssd_advance_status(ssd, &ppa, &gce, 0);
				}
				lunp->gc_endtime = lunp->next_lun_avail_time;
			}
		}

		/* update line status */
		mark_line_free(ssd, &ppa);
		ssd->free_blk_cnt++; 
		ssd->num_GC++; 

		//if(force && i >= 8) {
		if(force && !should_gc_high(ssd)) {
			force = false; 
		}

		victim_line = select_victim_line(ssd, force);
		if (!victim_line) {
				return -1;
		}
	}
		
	//hmb_debug(" GC # %u    num_valid_copy: %u\n", ssd->num_GC, ssd->num_GCcopy );
	return 0;
}

static uint64_t ssd_read(struct ssd *ssd, NvmeRequest *req)
{
	struct ssdparams *spp = &ssd->sp;
    uint64_t lba = req->slba;
    int nsecs = req->nlb;
    struct ppa ppa;
    uint64_t start_lpn = lba / spp->secs_per_pg;
    
	uint64_t end_lpn = (lba + nsecs) / spp->secs_per_pg;
    uint64_t lpn;	
	
	uint64_t sublat = 0 , tot_lat = 0; //maxlat = 0;
	uint64_t curr_time = 0, end_time = 0, dma_time = 0; // for debugging 

	// HMB cache 
	int hmb_cache_entry = 0;
	int hmb_return = -10;
	

    if (end_lpn >= spp->tt_pgs) {
        ftl_err("start_lpn=%"PRIu64", end_lpn=%"PRIu64", tt_pgs=%d\n", start_lpn, end_lpn, ssd->sp.tt_pgs);
    }

    /* normal IO read path */
    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {	
		ppa = get_maptbl_ent(ssd, lpn);

        if (!mapped_ppa(&ppa) || !valid_ppa(ssd, &ppa)) {
            // not mapped to a ppa
            continue;
        }

		ssd->tot_read++;

		if(HASH_FTL) {
			if(hash_read(ssd, &ppa, lpn) == -1) { 
				hmb_debug("[HASH READ ERROR] ... ");	
			}
		}

        struct nand_cmd srd;
        srd.type = USER_IO;
        srd.cmd = NAND_READ;
        srd.stime = req->stime;
		
		// chk if lpn is cached to HMB
		if (hmb_cached(ssd, hmb_cache_entry) == 0) { // not mapped to HMB 

			// not cahced  (rd_lat *2)
			sublat = ssd_advance_status(ssd, &ppa, &srd, 0);
			
			if(HMB_ENTRIES != 0) {
				hmb_cache_entry = (lpn) / spp->hmb_lpas_per_blk;	
				hmb_return = ReferencePage(ssd, hmb_cache_entry);
				
				// cache the corresponding LPN list to HMB
				if(ssd->nr_hmb_cache > 0) {
					ssd->nr_hmb_cache--; 
					/*if (hmb_return != hmb_cache_entry)
					hmb_debug("HMB cache meta management is not synced"); */

				} else {   // lru evict entry and add
					if(hmb_return < 0)
						hmb_debug("new HMB caching failed for entry %u ", hmb_cache_entry);
					else 
						set_hmb_cache(ssd, hmb_return, 0);
				}
				set_hmb_cache(ssd, hmb_cache_entry, 1); 
				
				// chk dma timing
				/*HmbEntry new_entry;	
				curr_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
				hmb_write(HMB_CTRL.list_addr + sizeof(HmbEntry) * 0, &new_entry, sizeof(HmbEntry));
				end_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

				dma_time = end_time - curr_time; 
				hmb_debug("dma cache write time: %d ", dma_time ); */


			}
		} else {
			ssd->hmb_read_hit++;
			
			// chk dma timing
			/*
			HmbEntry new_entry;	
			curr_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
			hmb_read(HMB_CTRL.list_addr + sizeof(HmbEntry) * 0, &new_entry, sizeof(HmbEntry));
			end_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
			dma_time = end_time - curr_time; 
			hmb_debug("dma cache hit read time: %d ", dma_time );
			*/

			sublat = ssd_advance_status(ssd, &ppa, &srd, 1);  // cahced 	
		}
		
        tot_lat = tot_lat + sublat;
        //maxlat = (sublat > maxlat) ? sublat : maxlat;
    } 

	return tot_lat; //maxlat; //tot_lat;
}

static uint64_t ssd_write(struct ssd *ssd, NvmeRequest *req)
{
	uint64_t lba = req->slba;
	struct ssdparams *spp = &ssd->sp;
	int len = req->nlb;
	uint64_t start_lpn = lba / spp->secs_per_pg;
	uint64_t end_lpn = (lba + len - 1) / spp->secs_per_pg;
	struct ppa ppa;
	uint64_t lpn;
	uint64_t curlat = 0, maxlat = 0;

	if (end_lpn >= spp->tt_pgs) {
		ftl_err("start_lpn=%"PRIu64", end_lpn=%"PRIu64", tt_pgs=%d\n", start_lpn, end_lpn, ssd->sp.tt_pgs);
		//ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", start_lpn, ssd->sp.tt_pgs);
	}

	if(!HASH_FTL) {
		int r;
		//while (should_gc(ssd)) {
		while (should_gc_high(ssd)) {
			/* perform GC here until !should_gc(ssd) */

			r = do_gc(ssd, true);
			if (r == -1)
				break;
		}
	}

	for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
		ssd->tot_write++;
		
		ppa = get_maptbl_ent(ssd, lpn);
		if (mapped_ppa(&ppa)) {
			/* update old page information first */
			mark_page_invalid(ssd, &ppa);
			ssd->tot_update++;
			set_rmap_ent(ssd, INVALID_LPN, &ppa);

			// chk hmb cached info and makr dirty	
			int hmb_cache_entry = (lpn) / spp->hmb_lpas_per_blk;	
			if (hmb_cached(ssd, hmb_cache_entry) == 1) { // mapping mapped to HMB 
				set_hmb_cache(ssd, hmb_cache_entry, 0); // set HMB cache dirty  
			}
		}

		/* get new page to write */	
		if(!HASH_FTL)
			ppa = get_new_page(ssd);
		else {
			ppa = get_new_page_hash(ssd, lpn);
			ssd->hash_OOB[ppa2pgidx(ssd, &ppa)].lpa = lpn;
		}	

		/* update maptbl */
		set_maptbl_ent(ssd, lpn, &ppa);
		/* update rmap */
		set_rmap_ent(ssd, lpn, &ppa);
		mark_page_valid(ssd, &ppa);

		/* need to advance the write pointer here */
		if(!HASH_FTL)
			ssd_advance_write_pointer(ssd);
		else
			ssd_advance_block_write_pointer(ssd, &ppa);


		struct nand_cmd swr;
		swr.type = USER_IO;
		swr.cmd = NAND_WRITE;
		swr.stime = req->stime;
		/* get latency statistics */
		curlat = ssd_advance_status(ssd, &ppa, &swr, 0);
		// tot_lat = tot_lat + curlat;

		maxlat = (curlat > maxlat) ? curlat : maxlat;
	}
   
	return maxlat;
}

static void *ftl_thread(void *arg)
{
    FemuCtrl *n = (FemuCtrl *)arg;
    struct ssd *ssd = n->ssd;
    NvmeRequest *req = NULL;
    uint64_t lat = 0, curr_time = 0;
    int rc;
    int i;

    while (!*(ssd->dataplane_started_ptr)) {
        usleep(100000);
    }

    /* FIXME: not safe, to handle ->to_ftl and ->to_poller gracefully */
    ssd->to_ftl = n->to_ftl;
    ssd->to_poller = n->to_poller;

	while (1) {
		curr_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

		if((curr_time % 900000000) == 0) { 
			hmb_debug("GC: %u GCW: %u TOT WR: %u TOT UP %u TOT R: %u cache_hit: %u", ssd->num_GC, ssd->num_GCcopy, ssd->tot_write, ssd->tot_update, ssd->tot_read, ssd->hmb_read_hit);
			//hmb_debug("free: %u   full: %u vic: %u inv_vic: %u ", ssd->lm.free_line_cnt, ssd->lm.full_line_cnt, ssd->lm.victim_line_cnt, ssd->full_invalid_cnt);
		}

		for (i = 1; i <= n->num_poller; i++) {
			if (!ssd->to_ftl[i] || !femu_ring_count(ssd->to_ftl[i]))
				continue;

			rc = femu_ring_dequeue(ssd->to_ftl[i], (void *)&req, 1);
			if (rc != 1) {
				printf("FEMU: FTL to_ftl dequeue failed\n");
			}

			ftl_assert(req);
			switch (req->is_write) {
				case 1:
					lat = ssd_write(ssd, req);
					break;
				case 0:
					lat = ssd_read(ssd, req);
					break;
				default:
					ftl_err("FTL received unkown request type, ERROR\n");
			}

			req->reqlat = lat;
			req->expire_time += lat;

			rc = femu_ring_enqueue(ssd->to_poller[i], (void *)&req, 1);
			if (rc != 1) {
				ftl_err("FTL to_poller enqueue failed\n");
			}


			if(!HASH_FTL) {
				/* clean one line if needed (in the background) */
				if (should_gc(ssd)) {
					//if (should_gc_high(ssd)) {
					do_gc(ssd, false);
				}
				}
			}	
		}
		return NULL;
}
