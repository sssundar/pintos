#include <stdio.h>
#include "/home/cs124/CS124/Curiosity/src/lib/inttypes.h"

/*! Returns the current thread's recent_cpu value. */
// 2^31-1 = 2147483647
int test_get_recent_cpu(int load_avg, int nice) {
	int recent_cpu = 5;
	int f = 2*2*2*2*2*2*2*2*2*2*2*2*2*2;
		
	recent_cpu = (2*(load_avg)/(2*load_avg + 1))*(recent_cpu * f)/f + f * nice;
    return recent_cpu/f;
}

void test1(void){
	int result;
	result = test_get_recent_cpu(145408, 5);
	printf("result is %d\n", result);
}

void test2(void){
	int result;
	result = test_get_recent_cpu(163840, 5);
	printf("result is %d\n", result);
}

void test3(void){
	int result;
	result = test_get_recent_cpu(1073741823, 0);
	printf("result is %d\n", result);
}
void test4(void){
	int result;
	result = test_get_recent_cpu(536870912, 0);
	printf("result is %d\n", result);
}

void test5(void){
	int result;
	result = test_get_recent_cpu(2147483647, 0);
	printf("result is %d\n", result);
}

int main(void) {
	test1();
	test2();
	test3();
	test4();
	test5();

	return 0;
}
