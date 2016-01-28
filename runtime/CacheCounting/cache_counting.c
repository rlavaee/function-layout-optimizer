#include <papi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>


#define EventSetSize 4

long long counters[EventSetSize];

int inst_events[EventSetSize]={PAPI_L1_ICM, PAPI_TOT_INS, PAPI_L2_ICM, PAPI_TLB_IM};
//int data_events[EventSetSize]={PAPI_L1_DCM, PAPI_L2_DCM, PAPI_L2_DCA};
//int accesses_events[EventSetSize]={PAPI_TOT_INS,PAPI_L2_ICA,PAPI_L2_DCA};
char inst_eventnames[EventSetSize][7]={"L1_ICM\0", "L1_ICA\0","L2_ICM\0","TLB_IM\0"};
//char * data_eventnames[EventSetSize]={"L1_DCM", "L2_DCM","L2_DCA";
//char * accesses_eventnames[EventSetSize]={"TOT_INST","L2_ICA","L2_DCA"};

char * ccFileName;

int EventSet=PAPI_NULL;

void handle_error (int retval)
{
		     printf("PAPI error %d: %s\n", retval, PAPI_strerror(retval));
			      exit(1);
}

void print_counters(void){
		char count_filename[80];
		//sprintf(count_filename,"hw_cntrs_%d.out",getpid());
		sprintf(count_filename,"cacheperf.out");
		FILE * ccFile = fopen(count_filename,"r");
		//FILE * ccFile = NULL;
		int i;
		for(i=0 ;i< EventSetSize ;++i){
				if(ccFile==NULL)
						counters[i]=0;
				else{
						fscanf(ccFile,"%*s:\t%lld\n",&counters[i]);
				}
		}
		
		if (PAPI_accum(EventSet, counters) != PAPI_OK)
		     handle_error(1);


		ccFile = fopen(count_filename,"w");
		for(i=0;i<EventSetSize;++i){
				fprintf(ccFile,"%s:\t%lld\n",inst_eventnames[i],counters[i]);
		}
		fclose(ccFile);
}

void init_perf_counters(int avail_counters){
		atexit(print_counters);

		if ( PAPI_library_init( PAPI_VER_CURRENT ) != PAPI_VER_CURRENT ) {
				fprintf(stderr,"PAPI library init error!\n");
				exit(1);
		}
		if (PAPI_thread_init(pthread_self) != PAPI_OK){
				fprintf(stderr,"PAPI thread init error!\n");
		  		exit(1);
			}
		unsigned long int tid;
		 if ((tid = PAPI_thread_id()) == (unsigned long int)-1)
				     handle_error(1);

		 if (PAPI_create_eventset(&EventSet) != PAPI_OK)
				     handle_error(1);
		int i;
		 for(i=0; i< EventSetSize; ++i)
				 if (PAPI_add_event(EventSet, inst_events[i]) != PAPI_OK)
						     handle_error(1);

		 if(PAPI_start(EventSet)!=PAPI_OK)
				 handle_error(1);
}
