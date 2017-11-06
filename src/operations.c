#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "types.h"
#include "io.h"
#include "operations.h"

// TODO possible race conditions?

struct operation
{
	pthread_cond_t condition;
	unsigned status;
};

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct vector operations;

bool operations_init(void)
{
	return vector_init(&operations, VECTOR_SIZE_BASE);
}

void operations_term(void)
{
	size_t index;
	for(index = 0; index < operations.length; ++index)
		free(vector_get(&operations, index));
	vector_term(&operations);
	pthread_mutex_destroy(&mutex);
}

int operation_start(void)
{
	struct operation *operation = malloc(sizeof(*operation));
	if (!operation) return ERROR_MEMORY;
	pthread_cond_init(&operation->condition, 0); // TODO error check
	operation->status = STATUS_RUNNING;

	// Put the operation in the first free vector slot or as a new element if there are no free slots.

	pthread_mutex_lock(&mutex);

	size_t index;
	for(index = 0; index < operations.length; ++index)
	{
		if (!vector_get(&operations, index))
		{
			vector_get(&operations, index) = operation;
			goto finally;
		}
	}

	// assert(index == operations.length);
	if (!vector_add(&operations, operation))
	{
		pthread_cond_destroy(&operation->condition);
		free(operation);
		return ERROR_MEMORY;
	}

finally:
	pthread_mutex_unlock(&mutex);
	return index;
}

// Returns whether the operation is in progress or has been cancelled.
// Blocks if the operation is paused.
bool operation_progress(unsigned operation_id)
{
	struct operation *operation;
	int status;

	pthread_mutex_lock(&mutex);
	operation = vector_get(&operations, operation_id);

	// If the operation is paused, wait until it is resumed.
	if (operation->status == STATUS_PAUSED)
		pthread_cond_wait(&operation->condition, &mutex);
	status = operation->status;

	pthread_mutex_unlock(&mutex);

	return (status == STATUS_RUNNING);
}

void operation_end(unsigned operation_id)
{
	// Remove the operation.
	struct operation *operation;
	pthread_mutex_lock(&mutex);
	operation = vector_get(&operations, operation_id);
	vector_get(&operations, operation_id) = 0;
	pthread_mutex_unlock(&mutex);

	// Free allocated resources.
	pthread_cond_destroy(&operation->condition);
	free(operation);
}

void operation_pause(unsigned operation_id)
{
	struct operation *operation;
	pthread_mutex_lock(&mutex);
	operation = vector_get(&operations, operation_id);
	if (operation) operation->status = STATUS_PAUSED;
	pthread_mutex_unlock(&mutex);
}

void operation_resume(unsigned operation_id)
{
	struct operation *operation;
	pthread_mutex_lock(&mutex);
	operation = vector_get(&operations, operation_id);
	if (operation)
	{
		operation->status = STATUS_RUNNING;
		pthread_cond_signal(&operation->condition);
	}
	pthread_mutex_unlock(&mutex);
}

void operation_cancel(unsigned operation_id)
{
	struct operation *operation;
	pthread_mutex_lock(&mutex);
	operation = vector_get(&operations, operation_id);
	if (operation)
	{
		int status = operation->status;
		operation->status = STATUS_CANCELLED;
		if (status == STATUS_PAUSED) pthread_cond_signal(&operation->condition);
	}
	pthread_mutex_unlock(&mutex);
}
