#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include "benchmark.c"

void print_array( int* a, int sz ) {

	for( int i=0;i<sz; i++) {
		printf("%d ", a[i] );
	}
	printf("\n");
	
}

void fill_rand( int* array, int size ) {
	srand( clock() );
	for(int i=0; i<size; i++) {
		array[i] = (rand() % 100) - 50;
	}
}

// predicate to sort negative/positive
int predicate( int n ) {

	return n < 0;

}

/*

Input: array with integers and a predicate function that returns true/false for elements in the array
this function partitions the array in place based on the predicate function without changing the relative
order of the elements within the partition. (aka, it is stable).

This algorithm is O(n^2) and you should never use it. @dgryksi has a better one in Go.

*/
void stable_partition( int* array, int sz, int (*predicate_function)(int) ) {
	
	int shift_index = 0;
	while( shift_index < sz && predicate_function(array[shift_index]) ) {
		shift_index++;
	}

	int index = shift_index+1;
	while( index < sz ) {

		// find the next number that needs to be shifted left
		if( !predicate_function(array[index]) ) {
			
			index++;
		
		} else {

			int value = array[index];
			int num_elements_to_move = index - shift_index;
			memmove( array + shift_index + 1, array + shift_index, sizeof(int) * num_elements_to_move );
			array[shift_index] = value; // move the element to the hole left at the end
			shift_index++;
			index++;

		}
	}	
}

void partition_benchmark( void* params ) {
	uint64_t count = (uint64_t) params;

	int arr[count];
	
	fill_rand( arr, count );
	stable_partition( arr, count, predicate );
}

int main(int argc, char** argv) {
	
	int size = 20;
	int arr[size];
	
	fill_rand( arr, size );
	print_array( arr, size );
	
	stable_partition( arr, size, predicate );
	print_array( arr, size );
	
	char buf[255];
	for( uint64_t i=10; i<1*1000*1000; i*=1.2) {
		sprintf( buf, "%llu", i );
		benchmark b = run_benchmark( buf, partition_benchmark, (void*)i );
		printf("%s\t%.10f\ts/run\t%llu runs\n", b.name, b.average_seconds, b.runs );
	}
	
	
}
