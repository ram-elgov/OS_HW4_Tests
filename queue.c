#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <threads.h>
/* Data Structure */
typedef struct Node {
    void* data;
    struct Node* next;
} Node;

typedef struct Queue {
    Node* head;
    Node* tail;
    size_t itemCount;
    size_t waitCount;
    size_t visitedCount;
    mtx_t mtx;
} Queue;

typedef struct CVNode {
    cnd_t* cv;
    struct CVNode* next;
} CVNode;

typedef struct CVQueue {
    CVNode* head;
    CVNode* tail;
} CVQueue;

static Queue queue; // Queue instance (private)
static CVQueue cvQueue; // Queue of conditional variables (private)

/**
 * @brief Initialize queue
 */
void initQueue(void) {
    queue.head = NULL;
    queue.tail = NULL;
    queue.itemCount = 0;
    queue.waitCount = 0;
    queue.visitedCount = 0;
    mtx_init(&queue.mtx, mtx_plain); // Initialize mutex

    cvQueue.head = NULL;
    cvQueue.tail = NULL;
}

/**
 * @brief Destroy queue
 */
void destroyQueue(void) {
    Node* current = queue.head;
    while (current != NULL) {
        Node* next = current->next;
        free(current);
        current = next;
    }

    mtx_destroy(&queue.mtx); // Destroy mutex

    CVNode* cvCurrent = cvQueue.head;
    while (cvCurrent != NULL) {
        CVNode* cvNext = cvCurrent->next;
        cnd_destroy(cvCurrent->cv); // Destroy conditional variable
        free(cvCurrent->cv);
        free(cvCurrent);
        cvCurrent = cvNext;
    }

    // Reset queue values
    queue.head = NULL;
    queue.tail = NULL;
    queue.itemCount = 0;
    queue.waitCount = 0;
    queue.visitedCount = 0;
}



/**
 * @brief Enqueue item
 * @param item Item to enqueue
 */
void enqueue(void* item) {
    Node* node = malloc(sizeof(Node));
    node->data = item;
    node->next = NULL;

    mtx_lock(&queue.mtx); // Lock mutex because we are modifying the queue
    if (queue.head == NULL) { // If queue is empty
        queue.head = node;
        queue.tail = node;
    } else { // If queue is not empty
        queue.tail->next = node;
        queue.tail = node;
    }
    queue.itemCount++;
    queue.visitedCount++;

    // Signal waiting thread if there are any
    if (queue.waitCount > 0) {
        CVNode* cvNode = cvQueue.head;
        cnd_signal(cvNode->cv);
        if (cvNode->next == NULL) {
            cvQueue.head = NULL;
            cvQueue.tail = NULL;
        } else {
            cvQueue.head = cvNode->next;
        }
        cnd_destroy(cvNode->cv); // Destroy the conditional variable
        free(cvNode->cv);
        free(cvNode);
        queue.waitCount--;
    }

    mtx_unlock(&queue.mtx);
}

/**
 * @brief Dequeue item
 * @return Dequeued item
 */
void* dequeue(void) {
    mtx_lock(&queue.mtx);
    while (queue.itemCount == 0) { // If queue is empty
        cnd_t* cv = malloc(sizeof(cnd_t));
        cnd_init(cv);
        CVNode* cvNode = malloc(sizeof(CVNode));
        cvNode->cv = cv;
        cvNode->next = NULL;

        if (cvQueue.head == NULL) {
            cvQueue.head = cvNode;
            cvQueue.tail = cvNode;
        } else {
            cvQueue.tail->next = cvNode;
            cvQueue.tail = cvNode;
        }

        queue.waitCount++;
        cnd_wait(cv, &queue.mtx); // Wait for signal
    }

    Node* node = queue.head;
    void* item = node->data;
    queue.head = node->next;
    queue.itemCount--;

    if (queue.itemCount == 0) // If queue is empty
        queue.tail = NULL;

    free(node);
    mtx_unlock(&queue.mtx);

    return item;
}

bool tryDequeue(void** item) {
    mtx_lock(&queue.mtx);
    if (queue.itemCount == 0) { // If queue is empty
        mtx_unlock(&queue.mtx);
        return false;
    }

    Node* node = queue.head;
    *item = node->data;
    queue.head = node->next;
    queue.itemCount--;

    if (queue.itemCount == 0) // If queue is empty
        queue.tail = NULL;

    free(node);
    mtx_unlock(&queue.mtx);

    return true;
}

size_t size(void) {
    return queue.itemCount;
}

size_t waiting(void) {
    return queue.waitCount;
}

size_t visited(void) {
    return queue.visitedCount;
}
