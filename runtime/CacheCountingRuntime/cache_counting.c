#include "papi/papiStdEventDefs.h"
#include "papi/papi.h"
#include <stdio.h>
#include <stdlib.h>

const int eventsize=3;
int *events;
char ** eventnames;
long long counters[eventsize];
long long sumcounters[eventsize];

int inst_events[eventsize]={PAPI_L1_ICM, PAPI_L2_ICM, PAPI_L2_ICA};
int data_events[eventsize]={PAPI_L1_DCM, PAPI_L2_DCM, PAPI_L2_DCA};
int accesses_events[eventsize]={PAPI_TOT_INS,PAPI_L2_ICA,PAPI_L2_DCA};
char * inst_eventnames[eventsize]={"L1_ICM", "L2_ICM","L2_ICA"};
char * data_eventnames[eventsize]={"L1_DCM", "L2_DCM","L2_DCA"};
char * accesses_eventnames[eventsize]={"TOT_INST","L2_ICA","L2_DCA"};

char * ccFileName;


void print_counters(void){
        FILE * ccFile = fopen("cachecount_ro.out","r");
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
	
        ccFile = fopen("cachecount_ro.out","w");
	for(i=0;i<eventsize;++i){
		//printf("%lld %lld\n",counters[i], sumcounters[i]);
        	fprintf(ccFile,"%s\t%lld\n",eventnames[i],counters[i]+sumcounters[i]);
	}
        fclose(ccFile);
}

void init_cache_counters(char inst_or_data){
	switch(inst_or_data){
	case 'i':
		events=inst_events;
		eventnames=inst_eventnames;
		break;
	case 'd':
		events=data_events;
		eventnames=data_eventnames;
		break;
	default:
		break;
		
	}
	atexit(print_counters);
        int retval = PAPI_library_init( PAPI_VER_CURRENT );
        if ( retval != PAPI_VER_CURRENT ) {
           fprintf(stderr,"failed.\n");
           exit(0);
        }
        PAPI_start_counters(events,eventsize);
}
