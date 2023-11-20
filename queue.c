#include <err.h>
#include <stdlib.h>
#include "queue.h"

queue_t queue_init(void)
{
	return calloc(sizeof(struct queue_s), 1);
}

void queue_free(queue_t q)
{
	struct qent_s *qe;
	while ((qe = queue_dequeue(q))) {
		free(qe->path);
		free(qe);
	}
	free(q);
}

/* add to tail of queue */
int queue_add_tail(queue_t q, char *path)
{
	struct qent_s *qe;

	qe = calloc(1, sizeof(*qe));
	if (!qe) err(1, "in malloc");

	qe->path = path;

	if (q->tail)
		q->tail->next = qe;
	qe->prev = q->tail;
	q->tail = qe;
	if (!q->head)
		q->head = qe;

	return 0;
}

/* add to head of queue */
int queue_add_head(queue_t q, char *path)
{
	struct qent_s *qe;

	qe = calloc(1, sizeof(*qe));
	if (!qe) err(1, "in malloc");

	qe->path = path;

	if (q->head)
		q->head->prev = qe;
	qe->next = q->head;
	q->head = qe;
	if (!q->tail)
		q->tail = qe;

	return 0;
}

/* add whole queue to head of another queue, then free the source queue */
int queue_add_queue_head(queue_t dst, queue_t src)
{
#if 0
	if (src->tail) {
		if (dst->head)
			dst->head->prev = src->tail;
		if (src->tail)
			src->tail->next = dst->head;
		dst->head = src->head;
	}
#endif

	struct qent_s *qe;
	while ((qe = queue_dequeue(src))) {
		queue_add_head(dst, qe->path);
		free(qe);
	}

	src->head = src->tail = NULL;
	queue_free(src);

	return 0;
}

/* pop from queue head */
struct qent_s *queue_dequeue(queue_t q)
{
	struct qent_s *out;
	out = q->head;

	if (q->head) {
		q->head = q->head->next;
		if (q->head)
			q->head->prev = NULL;
	}
	if (!q->head)
		q->tail = NULL;
	
	return out;
}
