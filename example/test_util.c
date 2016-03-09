#include "test_util.h"
#include "stdio.h"



int __attribute__((noinline)) used_func(){
  int i;
   printf("salam\n");
  return i;
}

int get_average(int a,int b,int c, int d, int e, int f){
	return (used_func()+b+c+d+e+f)/6;
}



