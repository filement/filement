#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <curl/curl.h>

#include <pthread.h>		// libpthread

#include "types.h"
#include "json.h"
#include "stream.h"
#include "http.h"
#include "http_parse.h"
#include "cache.h"
#include "server.h"
#include "auth.h"
#include "io.h"

#define AUTH_EXPIRE_TIME 1800

#define ID_LENGTH (16 + 16)

static struct dict auth_keys;
static pthread_mutex_t auth_mutex;

bool auth_init(void)
{
	extern struct dict auth_keys;
	extern pthread_mutex_t auth_mutex;

	dict_init(&auth_keys, 256);
	return !pthread_mutex_init(&auth_mutex, 0);
}

bool auth_create(struct resources *restrict resources,struct auth *auth)
{

	extern struct dict auth_keys;
	extern pthread_mutex_t auth_mutex;

	resources->auth_id = 0;
	resources->auth = 0;

	// TODO: delete outdated sessions every once in a while

	// Allocate memory for the session. Initialize it and save session_id in request

	while (true)
	{
		
		// Use current time to provide semi-unique session_id
		auth->time = time(0);

		// Add current session to the sesion dictionary
		pthread_mutex_lock(&auth_mutex);
		bool success = !dict_add(&auth_keys, auth->auth_id, auth);
		pthread_mutex_unlock(&auth_mutex);
		// TODO: free auth->auth_id ?
		if (!success) break;

		resources->auth_id = auth->auth_id;
		resources->auth = auth;

		return true;
	}

	return false;
}

bool auth_use(struct resources *restrict resources, struct string *auth_id)
{
	extern struct dict auth_keys;
	extern pthread_mutex_t auth_mutex;

	resources->auth_id = 0;
	resources->auth = 0;

	pthread_mutex_lock(&auth_mutex);
	struct auth *auth = dict_get(&auth_keys, auth_id);
	if (!auth)
	{
		pthread_mutex_unlock(&auth_mutex);
		return false;
	}

	// Check whether the session has expired
	time_t now = time(0);
	if (auth->time + AUTH_EXPIRE_TIME < now)
	{
		dict_remove(&auth_keys, auth_id);
		pthread_mutex_unlock(&auth_mutex);
		//TODO to check do I need session_item_free like function to the auth
		//session_item_free(item);
		return false;
	}
	else auth->time = now;

	pthread_mutex_unlock(&auth_mutex);

	resources->auth_id = auth_id;
	resources->auth = auth;

	return true;
}
