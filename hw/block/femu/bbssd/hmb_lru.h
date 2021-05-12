#include <stdio.h>
#include <stdlib.h>
#include "ftl.h"

int ReferencePage(struct ssd *ssd, unsigned pageNumber);
hmb_Queue* init_hmb_LRU_list( int num_hmb_entries);
hmb_Hash* init_hmb_LRU_hash(int available_FTL_entries);

QNode* newQNode(unsigned pageNumber);
hmb_Queue* createQueue(int numberOfFrames);
int isQueueEmpty(hmb_Queue* queue);
hmb_Hash* createHash(int capacity);
int AreAllFramesFull(hmb_Queue* queue);
void deQueue(hmb_Queue* queue);
int Enqueue(hmb_Queue* queue, hmb_Hash* hash, unsigned pageNumber);

