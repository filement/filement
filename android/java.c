#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "types.h"
#include "filement.h"
#include "log.h"

static bool initialized = false;
static bool registered = false;
static bool serving = false;
static bool started = false;

extern char *sqlite_path;

jboolean Java_com_filement_filement_Filement_checkdevice(JNIEnv* env, jobject thiz, jstring jdbpath, jstring jmpath)
{
	debug(logs("java checkdevice"));

	if (!initialized)
	{
		// Initialize database path.
		const char *dbpath = (*env)->GetStringUTFChars(env, jdbpath, NULL);
		if (!dbpath)
		{
			error(logs("Failed to get database path."));
			return JNI_FALSE;
		}
		sqlite_path = strdup(dbpath);
		if (!sqlite_path)
		{
			error(logs("Failed to store database path."));
			return JNI_FALSE;
		}

		close(0);
		close(1);
		close(2);
		open("/dev/null", O_RDONLY);
		open("/dev/null", O_WRONLY);
		creat("/mnt/sdcard/filement2", 0644);

		initialized = true;
		registered = filement_init();
	}

	return registered ? JNI_TRUE : JNI_FALSE;
}

static void *filement_main(void *_)
{
	debug(logs("java main"));
	filement_serve();
	return 0;
}

int Java_com_filement_filement_FilementService_startserver(JNIEnv* env, jobject thiz, jstring jdbpath, jstring jmpath)
{
	debug(logs("java startserver"));

	if (!serving)
	{
		serving = true;
		started = true;
		pthread_t thread_id;
		pthread_create(&thread_id, 0, filement_main, 0); // TODO what if this fails
		pthread_detach(thread_id);
	}
	else if (!started)
	{
		started = true;
		filement_start();
	}

	return 1; // return value is passed to killpid()
}

int Java_com_filement_filement_FilementService_killpid(JNIEnv* env, jobject thiz, jint pid)
{
	debug(logs("java service kill"));
	started = false;
	filement_stop();
	return 0;
}

int Java_com_filement_filement_Filement_killpid(JNIEnv* env, jobject thiz, jint pid)
{
	debug(logs("java kill"));
	started = false;
	filement_stop();
	return 0;
}

jboolean Java_com_filement_filement_PairActivity_registerdevice(JNIEnv* env, jobject thiz, jstring jdbpath, jstring jmpath, jstring jemail, jstring jpassword, jstring jdev_name)
{
	debug(logs("java register"));

	const char *email = (*env)->GetStringUTFChars(env, jemail, NULL);
	if (!email)
		return JNI_FALSE;

	const char *password = (*env)->GetStringUTFChars(env, jpassword, NULL);
	if (!password)
		return JNI_FALSE;

	const char *dev_name = (*env)->GetStringUTFChars(env, jdev_name, NULL);
	if (!dev_name)
		return JNI_FALSE;

	struct string semail = string((char *)email, strlen(email));
	struct string spassword = string((char *)password, strlen(password));
	struct string sdev_name = string((char *)dev_name, strlen(dev_name));

	registered = filement_register(&semail, &sdev_name, &spassword);

	if (registered)
		Java_com_filement_filement_FilementService_startserver(env, thiz, jdbpath, jmpath);

	return registered ? JNI_TRUE : JNI_FALSE;
}
