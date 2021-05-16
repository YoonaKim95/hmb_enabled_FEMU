#include "hmb_lru.h"
#include "hmb.h"

// A utility function to create a new Queue Node. The queue Node
// will store the given 'pageNumber'
QNode* newQNode(unsigned pageNumber) {
    // Allocate memory and assign 'pageNumber'
    QNode* temp = (QNode*)malloc(sizeof(QNode));
    temp->pageNumber = pageNumber;

    // Initialize prev and next as NULL
    temp->prev = temp->next = NULL;
  
    return temp;
}
  
// A utility function to create an empty Queue.
// The queue can have at most 'numberOfFrames' nodes
hmb_Queue* createQueue(int numberOfFrames)
{
    hmb_Queue* queue = (hmb_Queue*)malloc(sizeof(hmb_Queue));
  
    // The queue is empty
    queue->count = 0;
    queue->front = queue->rear = NULL;
  
    // Number of frames that can be stored in memory
    queue->numberOfFrames = numberOfFrames;
  
    return queue;
}
  
// A utility function to create an empty Hash of given capacity
hmb_Hash* createHash(int capacity)
{
    // Allocate memory for hash
    hmb_Hash* hash = (hmb_Hash*)malloc(sizeof(hmb_Hash));
    hash->capacity = capacity;
  
    // Create an array of pointers for refering queue nodes
    hash->array = (QNode**)malloc(hash->capacity * sizeof(QNode*));
  
    // Initialize all hash entries as empty
    int i;
    for (i = 0; i < hash->capacity; ++i)
        hash->array[i] = NULL;
  
    return hash;
}
  
hmb_Queue* init_hmb_LRU_list(int num_hmb_entries) {
    // Let cache can hold 4 pages
    hmb_Queue* q = createQueue(num_hmb_entries); 
	return q; 
}

hmb_Hash* init_hmb_LRU_hash(int available_FTL_entries) {
    hmb_Hash* hash = createHash(available_FTL_entries);
	return hash;
}
 
int AreAllFramesFull(hmb_Queue* queue)
{
    // printf("queue count: %d   queue noFrames %d \n", queue->count, queue->numberOfFrames);
	return queue->count == queue->numberOfFrames;
}
  
int isQueueEmpty(hmb_Queue* queue)
{
    return queue->rear == NULL;
}
  
void deQueue(hmb_Queue* queue)
{
    if (isQueueEmpty(queue))
        return;
  
    // If this is the only node in list, then change front
    if (queue->front == queue->rear)
        queue->front = NULL;
  
    // Change rear and remove the previous rear
    QNode* temp = queue->rear;
    queue->rear = queue->rear->prev;
  
    if (queue->rear)
        queue->rear->next = NULL;
  
    free(temp);
  
    // decrement the number of full frames by 1
    queue->count--;
}
  
// A function to add a page with given 'pageNumber' to both queue
// and hash
int Enqueue(hmb_Queue* queue, hmb_Hash* hash, unsigned pageNumber)
{
	int victim = -1; 
    // If all frames are full, remove the page at the rear
    if (AreAllFramesFull(queue)) {
        // remove page from hash	
		victim = queue->rear->pageNumber;
        hash->array[queue->rear->pageNumber] = NULL;
        deQueue(queue);
    }
  
    // Create a new node with given page number,
    // And add the new node to the front of queue
    QNode* temp = newQNode(pageNumber);
    temp->next = queue->front;
  
    // If queue is empty, change both front and rear pointers
    if (isQueueEmpty(queue))
        queue->rear = queue->front = temp;
    else // Else change the front
    {
        queue->front->prev = temp;
        queue->front = temp;
    }
  
    // Add page entry to hash also
    hash->array[pageNumber] = temp;
  
    // increment number of full frames
    queue->count++;

	// printf("Enqueue %d  victim %d \n", pageNumber, victim);
	return victim; 
}
 
static inline hmb_Queue *get_queue(struct ssd *ssd) 
{
	return (ssd->hmb_lru_list);
}


static inline hmb_Hash *get_hash(struct ssd *ssd) 
{
	return (ssd->hmb_lru_hash);
}

int ReferencePage(struct ssd *ssd, unsigned pageNumber)
{
	hmb_Queue *queue = NULL; 	
	queue = get_queue(ssd);

	hmb_Hash *hash = NULL;
	hash = get_hash(ssd);
		
	// printf("in ref\n");
	int victim = -1; 
    QNode* reqPage = hash->array[pageNumber];
  
    // the page is not in cache, bring it
    if (reqPage == NULL) {
        victim = Enqueue(queue, hash, pageNumber);
		return victim; 
	}
    // page is there and not at front, change pointer
    else if (reqPage != queue->front) {
        // Unlink rquested page from its current location
        // in queue.
        reqPage->prev->next = reqPage->next;
        if (reqPage->next)
            reqPage->next->prev = reqPage->prev;
  
        // If the requested page is rear, then change rear
        // as this node will be moved to front
        if (reqPage == queue->rear) {
            queue->rear = reqPage->prev;
            queue->rear->next = NULL;
        }
  
        // Put the requested page before current front
        reqPage->next = queue->front;
        reqPage->prev = NULL;
  
        // Change prev of current front
        reqPage->next->prev = reqPage;
  
        // Change front to the requested page
        queue->front = reqPage;
		
		return -3; 
	} else { // already at the front
		return -2; 
	}
}
