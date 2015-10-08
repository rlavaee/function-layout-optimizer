#include <stdio.h>
#include <stdlib.h>
#include <linux/unistd.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <atomic>
FILE * traceFile;

pid_t gettid( void )
{
		return syscall( __NR_gettid );
}


std::atomic<pid_t> prof_th;
volatile bool profiling_switch; 
pthread_t prof_switch_th;
volatile bool flush_trace;
pthread_mutex_t switch_mutex;
extern "C" bool do_exchange(){
	pid_t cur_pid = gettid();
	if(prof_th.load()==cur_pid)
		return true;
	pid_t free_th = -1;
	bool result= prof_th.compare_exchange_strong(free_th,cur_pid);
	//std::cerr << "cur_pid is: " << cur_pid << " and prof_th is: " << prof_th.load() << "\n";
	return result;
}

void * prof_switch_toggle(void *){
	while(true){
		usleep(40000);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = true;
		//fprintf(stderr,"for this period prof_th was: %d\n",prof_th.load());
		prof_th.store(-1);
		pthread_mutex_unlock(&switch_mutex);
		usleep(40000);
		pthread_mutex_lock(&switch_mutex);
		profiling_switch = false;
		flush_trace = true;
		pthread_mutex_unlock(&switch_mutex);

	}
}


extern "C" void  record_function_exec(short FuncNum){
	if(flush_trace){
		fprintf(traceFile,"\n");
		pthread_mutex_lock(&switch_mutex);
		flush_trace = false;
		pthread_mutex_unlock(&switch_mutex);
	}
	if(traceFile && profiling_switch)
		fprintf(traceFile,"%d ",FuncNum);
}

void dumpAtExitHandler(){
	fclose(traceFile);
}

extern "C" int start_call_site_tracing(short _totalFuncs) {
	flush_trace = false;
	profiling_switch = false;
	pthread_create(&prof_switch_th,NULL,prof_switch_toggle, (void *) 0);
		traceFile = fopen("trace.txt","w+");
		atexit(dumpAtExitHandler);
}
