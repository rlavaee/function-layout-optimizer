#include <papi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>


#define MAX_EVENT_SIZE 6

int eventsize;
int *events;
long long counters[MAX_EVENT_SIZE];
long long sumcounters[MAX_EVENT_SIZE];

int inst_events[6]=//{PAPI_L1_ICM, PAPI_TOT_INS, PAPI_L2_ICM, PAPI_TLB_IM, 
			{PAPI_BR_UCN,
						PAPI_BR_CN, PAPI_BR_TKN, PAPI_BR_NTK, PAPI_BR_MSP, PAPI_BR_PRC};
//int data_events[eventsize]={PAPI_L1_DCM, PAPI_L2_DCM, PAPI_L2_DCA};
//int accesses_events[eventsize]={PAPI_TOT_INS,PAPI_L2_ICA,PAPI_L2_DCA};
char inst_eventnames[6][7]//={"L1_ICM\0", "L1_ICA\0","L2_ICM\0","TLB_IM\0", 
		 ={"BR_UNC\0",
						"BR_CON\0", "BR_TKN\0", "BR_NTK\0", "BR_MSP\0", "BR_PRC\0"};
//char * data_eventnames[eventsize]={"L1_DCM", "L2_DCM","L2_DCA";
//char * accesses_eventnames[eventsize]={"TOT_INST","L2_ICA","L2_DCA"};

char * ccFileName;


void print_counters(void){
		char count_filename[80];
		//sprintf(count_filename,"hw_cntrs_%d.out",getpid());
		sprintf(count_filename,"branch_cntrs.out");
		FILE * ccFile = fopen(count_filename,"r");
		//FILE * ccFile = NULL;
		int i=0;
		for(;i<eventsize;++i){
				counters[i]=0;
				if(ccFile==NULL)
						sumcounters[i]=0;
				else{
						fscanf(ccFile,"%*s\t%lld\n",&sumcounters[i]);
				}
		}
		PAPI_read_counters(counters,eventsize);
		if(ccFile!=NULL)
				fclose(ccFile);

		ccFile = fopen(count_filename,"w");
		for(i=0;i<eventsize;++i){
				//printf("%lld %lld\n",counters[i], sumcounters[i]);
				long long me = counters[i];
				me += sumcounters[i];
				fprintf(ccFile,"%s\t%lld\n",inst_eventnames[i],me);
		}
		fclose(ccFile);
}

void init_cache_counters(int avail_counters){
		//eventsize=avail_counters;
		eventsize=MAX_EVENT_SIZE;
		atexit(print_counters);
		int retval = PAPI_library_init( PAPI_VER_CURRENT );
		if ( retval != PAPI_VER_CURRENT ) {
				fprintf(stderr,"failed with error%d.\n",retval);
				exit(0);
		}
		PAPI_start_counters(inst_events,eventsize);
}
