#include <assert.h>
#include <fcntl.h>
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h> /* sleep() */

#include "types.h"
#include "filement.h"
#include "log.h"

char *magic_path;
extern char *sqlite_path;
const struct string app_revision = {.data = "0", .length = 1};
int is_inited=0;
int is_started=0;
int is_serve=0;
int is_registered=0;

static void *filement_serve_thfun(void *temp)
{
filement_serve();
return 0;
}

static void filement_export_start_server(void)
{
error(logs("Filement export_start_server "), logi(is_inited), logs(" "), logi(is_serve), logs(" "), logi(is_started));
if(!is_inited)
{
	is_inited=1;
	error(logs("Filement filement_init"));
	if (!filement_init()) return ;
	error(logs("Filement filement_init end "));
	is_registered=1;
}
if(!is_serve)
{
	pthread_t filement_serve_th;
	error(logs("Filement serve_th "));
	pthread_create(&filement_serve_th,NULL,filement_serve_thfun,0);
	pthread_detach(filement_serve_th);
	is_serve=1;
	if(is_registered)is_started=1;
}
if(!is_started)
{
	if(is_registered)
	{
	error(logs("Filement serve_start"));
	filement_start();
	is_started=1;
	}
}
}


static int filement_export_check_device(const char *dbpath, const char *mpath)
{
sqlite_path=strdup(dbpath);
magic_path=strdup(mpath);

if(!is_inited)
{
is_inited=1;
if (!filement_init())return 0; //not registered
is_registered=1;
}

return is_registered;//registered

}

static int filement_export_register_device(const char *dbpath, const char *mpath, struct string *email,char *pin,struct string *password,struct string *dev_name)
{
sqlite_path=strdup(dbpath);
magic_path=strdup(mpath);

if(!is_inited)
{
is_inited=1;
filement_init();
}
		if (filement_register(email, dev_name, password))
		{
		is_registered=1;
		filement_export_start_server();
		return 1;
		}
	 
return 0;
}


static int filement_export_init_server(const char *dbpath, const char *mpath)
{

sqlite_path=strdup(dbpath);
magic_path=strdup(mpath);
error(logs("Filement export_init_server "));
if(!is_inited)
{
is_inited=1;
error(logs("Filement filement_init"));
if (!filement_init()) return 0;
error(logs("Filement filement_init end "));
is_registered=1;
}
if(!is_serve)
{
error(logs("Filement serve_th "));
pthread_t filement_serve_th;
pthread_create(&filement_serve_th,NULL,filement_serve_thfun,0);
pthread_detach(filement_serve_th);

is_serve=1;
if(is_registered)is_started=1;
}
return 1;
}

static void filement_export_stop_server(void)
{
if(is_started)
{
filement_stop();
is_started=0;
}
}













//char *filement_test_lib(void);

/*struct string
{
	char *data;
	size_t length;
};

#define string(...) ( \
		(struct {struct string string; size_t _;}) \
		{__VA_ARGS__, sizeof(__VA_ARGS__) - 1} \
	).string*/

int global_pid=0;

/* This is a trivial JNI example where we use a native method
 * to return a new VM String. See the corresponding Java source
 * file located at:
 *
 *   apps/samples/hello-jni/project/src/com/example/hellojni/HelloJni.java
 */
jboolean
Java_com_filement_filement_Filement_checkdevice( JNIEnv* env,
                                                  jobject thiz, jstring jdbpath, jstring jmpath )
{
	close(0);
	close(1);
	close(2);
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	creat("/data/data/com.filement.filement/files/errors_list", 0644);

    const char *dbpath = (*env)->GetStringUTFChars(env, jdbpath, NULL);
    if(dbpath == NULL)return JNI_FALSE;

    const char *mpath = (*env)->GetStringUTFChars(env, jmpath, NULL);
    if(mpath == NULL)return JNI_FALSE;

    if(filement_export_check_device(dbpath,mpath))return JNI_TRUE;

    return JNI_FALSE;
}

void remove_zombies(int sig)
{
    int status;

   waitpid(-1, &status, WNOHANG);

}

int
Java_com_filement_filement_FilementService_startserver( JNIEnv* env,
                                                  jobject thiz, jstring jdbpath, jstring jmpath )
{
	struct sigaction sa;
	sigfillset(&sa.sa_mask);
    sa.sa_handler = remove_zombies;
    sa.sa_flags = 0;
    sigaction(SIGCHLD, &sa, NULL);

int pid=0;
if(global_pid)
{
	kill( global_pid, SIGKILL );
	global_pid=0;
}

pid = fork();
        if ( pid == -1 ) {
            return -1;
        }
if(!pid)
{
    const char *dbpath = (*env)->GetStringUTFChars(env, jdbpath, NULL);
    assert(NULL != dbpath);

    const char *mpath = (*env)->GetStringUTFChars(env, jmpath, NULL);
    assert(NULL != mpath);

    //if(filement_export_start_server())return JNI_TRUE;
    filement_export_start_server();
    return JNI_TRUE;
}
else global_pid=pid;

    return pid;
}

int
Java_com_filement_filement_FilementService_killpid( JNIEnv* env,
                                                  jobject thiz, jint pid )
{
	//if(!pid || !global_pid)return 0;

	//if(pid)kill( pid, SIGKILL );
	if(global_pid)kill( global_pid, SIGKILL );
	global_pid=0;
	return 0;
}

int
Java_com_filement_filement_Filement_killpid( JNIEnv* env,
                                                  jobject thiz, jint pid )
{
	//if(!pid || !global_pid)return 0;

	//if(pid)kill( pid, SIGKILL );
	if(global_pid)kill( global_pid, SIGKILL );
	global_pid=0;
	return 0;
}

jboolean
Java_com_filement_filement_PairActivity_registerdevice( JNIEnv* env,
                                                  jobject thiz, jstring jdbpath, jstring jmpath, jstring jemail, jstring jpassword, jstring jdev_name )
{
    const char *dbpath = (*env)->GetStringUTFChars(env, jdbpath, NULL);
    if(dbpath==NULL)return JNI_FALSE;

    const char *mpath = (*env)->GetStringUTFChars(env, jmpath, NULL);
    if(mpath==NULL)return JNI_FALSE;

    const char *email = (*env)->GetStringUTFChars(env, jemail, NULL);
    if(email==NULL)return JNI_FALSE;


    const char *password = (*env)->GetStringUTFChars(env, jpassword, NULL);
    if(password==NULL)return JNI_FALSE;

    const char *dev_name = (*env)->GetStringUTFChars(env, jdev_name, NULL);
    if(dev_name==NULL)return JNI_FALSE;


    struct string semail = string(strdup(email), strlen(email));
    struct string spassword = string(strdup(password), strlen(password));
    struct string sdev_name = string(strdup(dev_name), strlen(dev_name));

    if(filement_export_register_device(dbpath,mpath,&semail,0,&spassword,&sdev_name))return JNI_TRUE;

    return JNI_FALSE;
}
