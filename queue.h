#pragma once

struct qent_s {
	struct qent_s *next;
	struct qent_s *prev;
	char *path;
};
struct queue_s {
	struct qent_s *head;
	struct qent_s *tail;
};
typedef struct queue_s* queue_t;

extern queue_t queue_init(void);
extern void queue_free(queue_t q);
extern int queue_add_tail(queue_t q, char *path);
extern int queue_add_head(queue_t q, char *path);
extern int queue_add_queue_head(queue_t dst, queue_t src);
extern struct qent_s *queue_dequeue(queue_t q);
