#include <stdlib.h>
#include <sys/types.h>

#include "types.h"

// TODO: make the queue more intuitive

void queue_init(struct queue *restrict queue)
{
	queue->start = 0;
	queue->end = &queue->start;
	queue->length = 0;
}

// WARNING: Deprecated. Use malloc() and queue_init() instead.
struct queue *queue_alloc(void)
{
	struct queue *queue = malloc(sizeof(struct queue));
	if (!queue) return 0;
	queue_init(queue);
	return queue;
}

bool queue_push(struct queue *restrict queue, void *restrict data)
{
	struct queue_item *item = malloc(sizeof(struct queue_item));
	if (!item) return false;
	item->data = data;
	item->next = 0;
	*queue->end = item;
	queue->end = &item->next;
	queue->length += 1;
	return true;
}

void *queue_pop(struct queue *restrict queue)
{
	struct queue_item *item = queue->start;
	if (!item) return 0;
	queue->start = item->next;
	if (!queue->start) queue->end = &queue->start; // set end if this was the last item
	void *data = item->data;
	free(item);
	queue->length -= 1;
	return data;
}

void *queue_remove(struct queue *restrict queue, struct queue_item **item)
{
	struct queue_item *temp = *item;
	*item = temp->next;
	if (!temp->next) queue->end = item; // set end if this was the last item
	void *data = temp->data;
	free(temp);
	queue->length -= 1;
	return data;
}

void queue_term(struct queue *restrict queue)
{
	struct queue_item *item;
	while (item = queue->start)
	{
		queue->start = item->next;
		free(item);
		// TODO: free item->data ?
	}
}

/*#include <stdio.h>

int main(void)
{
	struct queue q;
	size_t i;
	int numbers[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, *result;

	queue_init(&q);

	queue_push(&q, numbers + i++);
	queue_push(&q, numbers + i++);
	queue_push(&q, numbers + i++);

	printf("%d (len=%u)\n", *(int *)queue_pop(&q), q.length);

	queue_push(&q, numbers + i++);
	queue_push(&q, numbers + i++);

	printf("%d (len=%u)\n", *(int *)queue_pop(&q), q.length);
	printf("%d (len=%u)\n", *(int *)queue_pop(&q), q.length);
	printf("%d (len=%u)\n", *(int *)queue_pop(&q), q.length);

	queue_push(&q, numbers + i++);

	printf("%d (len=%u)\n", *(int *)queue_pop(&q), q.length);
	printf("%d (len=%u)\n", *(int *)queue_pop(&q), q.length);

	queue_term(&q);

	return 0;
}*/
