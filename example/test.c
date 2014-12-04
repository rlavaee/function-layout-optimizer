#include "test_util.h"
#include <stdio.h>
#include <sys/time.h>

	int i;
	int n;

int main()
{

	printf("enter loop limit: ");
	scanf("%d",&n);
	struct timeval tv0,tv1;
	gettimeofday(&tv0,NULL);
	for(i=0;i<n;++i){
		printf("%d",get_average(i,i*2,i*3,i*4,i*5,i*6));
		//printf("%d",get_average(10,i*2));
	}
	gettimeofday(&tv1,NULL);	
	long elapsed = (tv1.tv_sec-tv0.tv_sec)*1000 + (tv1.tv_usec-tv0.tv_usec)/1000;
	printf("elapsed time is: %ld\n milliseconds",elapsed);
	return 0;
}
