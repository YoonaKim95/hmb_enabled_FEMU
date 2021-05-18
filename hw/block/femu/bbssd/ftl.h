#ifndef __FEMU_FTL_H
#define __FEMU_FTL_H

#include "../nvme.h"

#define INVALID_PPA     (~(0ULL))
#define INVALID_LPN     (~(0ULL))
#define UNMAPPED_PPA    (~(0ULL))

#define HASH_FTL 0
//#define HASH_FTL 1            

#define LEFTROTATE(x, c) (((x) << (c)) | ((x) >> (32 - (c))))
	
#define HID_BITS 6
#if(HASH_FTL)
	#define ADDR_BITS 14
	#define CLEAN 0
	#define DIRTY 1 
#else 
	#define ADDR_BITS 32
#endif 


#define SECSZ  512
#define SECS_PER_PAGE 8  /* 4KB */

#define PGS_PER_BLK 256
//#define PGS_PER_BLK 512
#define BLKS_PER_PL 65536 // 32768 * 2  // 512  * 8 * 8  * 2
//spp->blks_per_pl = 256; /* 16GB */
#define PLS_PER_LUN 1
#define LUNS_PER_CH 1
#define NCHS 1


enum {
    NAND_READ =  0,
    NAND_WRITE = 1,
    NAND_ERASE = 2,
/*	
	NAND_READ_LATENCY = 4000,
    NAND_PROG_LATENCY = 20000,
    NAND_ERASE_LATENCY = 200000,  */


    NAND_READ_LATENCY = 0,
    NAND_PROG_LATENCY = 0,
    NAND_ERASE_LATENCY = 0,  
};

enum {
    USER_IO = 0,
    GC_IO = 1,
};

enum {
    SEC_FREE = 0,
    SEC_INVALID = 1,
    SEC_VALID = 2,

    PG_FREE = 0,
    PG_INVALID = 1,
    PG_VALID = 2
};

enum {
    FEMU_ENABLE_GC_DELAY = 1,
    FEMU_DISABLE_GC_DELAY = 2,

    FEMU_ENABLE_DELAY_EMU = 3,
    FEMU_DISABLE_DELAY_EMU = 4,

    FEMU_RESET_ACCT = 5,
    FEMU_ENABLE_LOG = 6,
    FEMU_DISABLE_LOG = 7,
};


#define BLK_BITS    (16)
#define PG_BITS     (16)
#define SEC_BITS    (8)
#define PL_BITS     (8)
#define LUN_BITS    (8)
#define CH_BITS     (7)

/* describe a physical page addr */
struct ppa {
    union {
        struct {
            uint64_t blk : BLK_BITS;
            uint64_t pg  : PG_BITS;
            uint64_t sec : SEC_BITS;
            uint64_t pl  : PL_BITS;
            uint64_t lun : LUN_BITS;
            uint64_t ch  : CH_BITS;
            uint64_t rsv : 1;
        } g;
        uint64_t ppa;
    };

	uint64_t ppa_hash;

	uint32_t hid; 
	int32_t ppid;
};
 
typedef int nand_sec_status_t;

struct nand_page {
    nand_sec_status_t *sec;
    int nsecs;
    int status;
};

struct nand_block {
	uint64_t blk_id;

	struct nand_page *pg;
    int npgs;
    int ipc; /* invalid page count */
    int vpc; /* valid page count */
    int erase_cnt;
    int wp; /* current write pointer */

	bool reserved; // reserved blk
};

struct nand_plane {
    struct nand_block *blk;
    int nblks;
};

struct nand_lun {
    struct nand_plane *pl;
    int npls;
    uint64_t next_lun_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssd_channel {
    struct nand_lun *lun;
    int nluns;
    uint64_t next_ch_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssdparams {
    int secsz;        /* sector size in bytes */
    int secs_per_pg;  /* # of sectors per page */
    int pgs_per_blk;  /* # of NAND pages per block */
    int blks_per_pl;  /* # of blocks per plane */
    int pls_per_lun;  /* # of planes per LUN (Die) */
    int luns_per_ch;  /* # of LUNs per channel */
    int nchs;         /* # of channels in the SSD */

    int mp_rd_lat;    /* NAND mapping read latency in nanoseconds */
    int pg_rd_lat;    /* NAND page read latency in nanoseconds */
    int pg_wr_lat;    /* NAND page program latency in nanoseconds */
    int blk_er_lat;   /* NAND block erase latency in nanoseconds */
    int ch_xfer_lat;  /* channel transfer latency for one page in nanoseconds
                       * this defines the channel bandwith  */

    int gc_thres_blks_high;
    int gc_thres_blks;
    
	double gc_thres_pcent;
    int gc_thres_lines;
    double gc_thres_pcent_high;
    int gc_thres_lines_high;
    bool enable_gc_delay;

    /* below are all calculated values */
    int secs_per_blk; /* # of sectors per block */
    int secs_per_pl;  /* # of sectors per plane */
    int secs_per_lun; /* # of sectors per LUN */
    int secs_per_ch;  /* # of sectors per channel */
    int tt_secs;      /* # of sectors in the SSD */

    int pgs_per_pl;   /* # of pages per plane */
    int pgs_per_lun;  /* # of pages per LUN (Die) */
    int pgs_per_ch;   /* # of pages per channel */
    int tt_pgs;       /* total # of pages in the SSD */

	int hmb_lpas_per_blk;  /* total # of lpas can be cached to a HMB block*/
    
	int blks_per_lun; /* # of blocks per LUN */
    int blks_per_ch;  /* # of blocks per channel */
    int tt_blks;      /* total # of blocks in the SSD */

    int secs_per_line;
    int pgs_per_line;
    int blks_per_line;
    int tt_lines;

    int pls_per_ch;   /* # of planes per channel */
    int tt_pls;       /* total # of planes in the SSD */

    int tt_luns;      /* total # of LUNs in the SSD */
};

typedef struct line {
    int id;  /* line id, the same as corresponding block id */
    int ipc; /* invalid page count in this line */
    int vpc; /* valid page count in this line */
    QTAILQ_ENTRY(line) entry; /* in either {free,victim,full} list */
    /* position in the priority queue for victim lines */
    size_t                  pos;
} line;

/* wp: record next write addr */
struct write_pointer {
    struct line *curline;
    int ch;
    int lun;
    int pg;
    int blk;
    int pl;
};

struct line_mgmt {
    struct line *lines;
    /* free line list, we only need to maintain a list of blk numbers */
    QTAILQ_HEAD(free_line_list, line) free_line_list;
    pqueue_t *victim_line_pq;
    //QTAILQ_HEAD(victim_line_list, line) victim_line_list;
    QTAILQ_HEAD(full_line_list, line) full_line_list;
    int tt_lines;
    int free_line_cnt;
    int victim_line_cnt;
    int full_line_cnt;
};

struct nand_cmd {
    int type;
    int cmd;
    int64_t stime; /* Coperd: request arrival time */
};

typedef struct QNode {
    struct QNode *prev, *next;
    unsigned pageNumber; // the page number stored in this QNode
} QNode;
  
typedef struct hmb_Queue {
    unsigned count; // Number of filled frames
    unsigned numberOfFrames; // total number of frames
    QNode *front, *rear;
} hmb_Queue;
  
typedef struct hmb_Hash {
    int capacity; // how many pages can be there
    QNode** array; // an array of queue nodes
} hmb_Hash;

// #if defined(HASH_FTL)
typedef struct hash_OOB{
	int64_t lpa; 
} H_OOB;

typedef struct virtual_block_table{
	int64_t pba;
	bool state; // CLEAN or DIRTY
} VIRTUAL_BLOCK_TABLE;
// #endif

struct ssd {
	char *ssdname;
	struct ssdparams sp;
	struct ssd_channel *ch;
	struct ppa *maptbl; /* page level mapping table */
	struct ppa_hash *maptbl_hash; /* page level mapping table */

	int *hmb_cache_bm;  /* hmb cache 4kb unit */	
	uint32_t nr_hmb_cache;
	uint32_t addr_bits;

	int num_GC; 
	int num_GCcopy; 
	int blk_erase_cnt; 

    pqueue_t *victim_blk_pq;

	int full_invalid_cnt; 
	int free_blk_cnt;

	// HASH_FTL
	VIRTUAL_BLOCK_TABLE *vbt;
	int64_t num_hid; 
	int64_t num_ppid;
	int64_t hid_secondary; 

	struct nand_block reserved;

	int64_t lpa_sft;
	int64_t num_vbt;
	int64_t shr_read_cnt[4];
	struct nand_block *barray;
	H_OOB *hash_OOB;

	// HMB LRU list 
	hmb_Queue* hmb_lru_list;
	hmb_Hash* hmb_lru_hash;

	uint64_t *rmap;     /* reverse mapptbl, assume it's stored in OOB */

    struct write_pointer *wp_hash;
	
	struct write_pointer wp;
    struct line_mgmt lm;

    /* lockless ring for communication with NVMe IO thread */
    struct rte_ring **to_ftl;
    struct rte_ring **to_poller;
    bool *dataplane_started_ptr;
    QemuThread ftl_thread;
};

void ssd_init(FemuCtrl *n);

int do_gc(struct ssd *ssd, bool force);
uint64_t ppa2pgidx(struct ssd *ssd, struct ppa *ppa);
struct nand_lun *get_lun(struct ssd *ssd, struct ppa *ppa);
struct nand_block *get_blk(struct ssd *ssd, struct ppa *ppa); 
struct ppa get_new_page_hash(struct ssd *ssd, uint64_t lpa);
int hash_read(struct ssd *ssd, struct ppa *ppa, uint64_t lpa);

uint64_t md5_u(uint32_t *initial_msg, size_t initial_len); 

void clean_one_block(struct ssd *ssd, struct ppa *ppa);

void mark_line_free(struct ssd *ssd, struct ppa *ppa);
void mark_block_free(struct ssd *ssd, struct ppa *ppa);
uint64_t ssd_advance_status(struct ssd *ssd, struct ppa *ppa, struct nand_cmd *ncmd, int hmb_cached);


#ifdef FEMU_DEBUG_FTL
#define ftl_debug(fmt, ...) \
    do { printf("[FEMU] FTL-Dbg: " fmt, ## __VA_ARGS__); } while (0)
#else
#define ftl_debug(fmt, ...) \
    do { } while (0)
#endif

#define ftl_err(fmt, ...) \
    do { fprintf(stderr, "[FEMU] FTL-Err: " fmt, ## __VA_ARGS__); } while (0)

#define ftl_log(fmt, ...) \
    do { printf("[FEMU] FTL-Log: " fmt, ## __VA_ARGS__); } while (0)


/* FEMU assert() */
#ifdef FEMU_DEBUG_FTL
#define ftl_assert(expression) assert(expression)
#else
#define ftl_assert(expression)
#endif

#endif
