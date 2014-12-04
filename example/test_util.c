#include "test_util.h"
#include "stdio.h"

int not_used_func(){
  int i;
   printf("salam\n");
  return i;
}

__attribute__((cdecl, noinline)) int get_average(int a,int b,int c, int d, int e, int f){
	not_used_func();
	return (a+b+c+d+e+f)/6;
}



