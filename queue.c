#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <threads.h>

/* -------------------Data Structures ----------------*/

typedef struct Node {
  void *data;
  struct Node *next;
} Node;

typedef struct Queue {
  Node *head;
  Node *tail;
  size_t item_count;
  size_t wait_count;
  size_t visited_count;
  mtx_t mtx;
} Queue;

typedef struct CvNode {
  cnd_t *cv;
  struct CvNode *next;
} CvNode;

typedef struct CvQueue {
  CvNode *head;
  CvNode *tail;
} CvQueue;

static Queue queue;      // Queue instance (private)
static CvQueue cvQueue;  // Queue of conditional variables (private)

/**
 * @brief Initialize the queue
 */
void initQueue(void) {
  // Initialize the queue
  queue.head = NULL;
  queue.tail = NULL;
  queue.item_count = 0;
  queue.wait_count = 0;
  queue.visited_count = 0;
  mtx_init(&queue.mtx, mtx_plain);  // Initialize mutex

  // Initialize the conditional variables queue
  cvQueue.head = NULL;
  cvQueue.tail = NULL;
}

/**
 * @brief Destroy the queue and clean up resources
 */
void destroyQueue(void) {
  // Clean up the data queue
  Node *current = queue.head;
  while (current != NULL) {
    Node *next = current->next;
    free(current);
    current = next;
  }

  mtx_destroy(&queue.mtx);  // Destroy mutex

  // Clean up the conditional variables queue
  CvNode *cv_current = cvQueue.head;
  while (cv_current != NULL) {
    CvNode *cv_next = cv_current->next;
    cnd_destroy(cv_current->cv);  // Destroy conditional variable
    free(cv_current->cv);
    free(cv_current);
    cv_current = cv_next;
  }

  // Reset queue values
  queue.head = NULL;
  queue.tail = NULL;
  queue.item_count = 0;
  queue.wait_count = 0;
  queue.visited_count = 0;
}

/**
 * @brief Check if the conditional variables queue is empty
 * @return True if the queue is empty, false otherwise
 */
bool isCvQueueEmpty(void) {
  return cvQueue.head == NULL;
}

/**
 * @brief Check if the data queue is empty
 * @return True if the queue is empty, false otherwise
 */
bool isQueueEmpty(void) {
  return queue.head == NULL;
}

/**
 * @brief Enqueue an item into the data queue
 * @param item The item to enqueue
 */
void enqueue(void *item) {
  mtx_lock(&queue.mtx);  // Lock the mutex because we are modifying the queue

  bool queue_was_empty = false;
  if (isQueueEmpty()) {
    queue_was_empty = true;
  }

  // Insert item into the data queue
  Node *node = malloc(sizeof(Node));
  node->data = item;
  node->next = NULL;

  if (queue.head == NULL) {  // If the queue is empty
    queue.head = node;
    queue.tail = node;
  } else {  // If the queue is not empty
    queue.tail->next = node;
    queue.tail = node;
  }

  queue.item_count++;
  queue.visited_count++;

  // Signal waiting thread if there are any
  if (queue.wait_count > 0 && queue_was_empty) {
    cnd_signal(cvQueue.head->cv);
  }

  mtx_unlock(&queue.mtx);
}

/**
 * @brief Dequeue an item from the data queue
 * @return The dequeued item
 */
void *dequeue(void) {
  mtx_lock(&queue.mtx);

  if (isCvQueueEmpty() && !isQueueEmpty()) {
    // Dequeue from the data queue
    Node *node = queue.head;
    void *item = node->data;
    queue.head = node->next;
    queue.item_count--;

    if (queue.item_count == 0) {  // If the queue is empty
      queue.tail = NULL;
    }

    free(node);
    mtx_unlock(&queue.mtx);

    return item;
  } else {  // If the queue is empty
    cnd_t *cv = malloc(sizeof(cnd_t));
    cnd_init(cv);

    // Enqueue the conditional variable into the queue
    CvNode *cv_node = malloc(sizeof(CvNode));
    cv_node->cv = cv;
    cv_node->next = NULL;

    if (cvQueue.head == NULL) {
      cvQueue.head = cv_node;
      cvQueue.tail = cv_node;
    } else {
      cvQueue.tail->next = cv_node;
      cvQueue.tail = cv_node;
    }

    queue.wait_count++;
    cnd_wait(cv, &queue.mtx);  // Wait for signal
  }

  // Dequeue from the data queue
  Node *node = queue.head;
  void *item = node->data;
  queue.head = node->next;
  queue.item_count--;

  if (queue.item_count == 0) {  // If the queue is empty
    queue.tail = NULL;
  }

  // Dequeue from the conditional variables queue
  CvNode *cv_node = cvQueue.head;
  cnd_t *cv = cv_node->cv;
  cvQueue.head = cv_node->next;
  cnd_destroy(cv);
  queue.wait_count--;

  if (queue.wait_count > 0) {
    cnd_signal(cvQueue.head->cv);
  }

  free(cv_node);
  free(node);
  mtx_unlock(&queue.mtx);

  return item;
}

/**
 * @brief Try to dequeue an item from the data queue
 * @param item Pointer to store the dequeued item
 * @return True if an item was dequeued successfully, false otherwise
 */
bool tryDequeue(void **item) {
  mtx_lock(&queue.mtx);

  if (queue.item_count == 0) {  // If the queue is empty
    mtx_unlock(&queue.mtx);
    return false;
  }

  // Dequeue from the data queue
  Node *node = queue.head;
  *item = node->data;
  queue.head = node->next;
  queue.item_count--;

  if (queue.item_count == 0) {  // If the queue is empty
    queue.tail = NULL;
  }

  free(node);
  mtx_unlock(&queue.mtx);

  return true;
}

/**
 * @brief Get the number of items in the data queue
 * @return The number of items in the queue
 */
size_t size(void) {
  return queue.item_count;
}

/**
 * @brief Get the number of threads waiting on the queue
 * @return The number of waiting threads
 */
size_t waiting(void) {
  return queue.wait_count;
}

/**
 * @brief Get the number of times the queue has been visited
 * @return The number of visits to the queue
 */
size_t visited(void) {
  return queue.visited_count;
}
