#include "test_util.h"
#include "stdio.h"

int not_used_func(){
  int i;
  for(i=0;i<1000;++i)
    printf("salam\n");
  return i;
}

int get_average(int a,int b){
	return (a+b)/2;
}



