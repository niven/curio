#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include "benchmark.c"

#define ARRAY_COUNT(a) (sizeof(a)/sizeof(a[0]))

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

int verify_partition( int* array, int sz, int (*predicate_function)(int) ) {
	
	int match = 1;
	int current = 0;
	for( int i=0; i<sz; i++ ) {
		current = predicate_function( array[i] );
		
		if( !match && current ) {
			return 0;
		}
		
		if( match && !current ) {
			match = 0;
		}
	}
	
	return 1;
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

void flip( int* from, int* to ) {
	// printf("Flipping from %d to %d\n", *from, *to );
	
	if( from == to ) {
		return;
	}
	
	int temp;
	while( from < to ) {
		temp = *from;
		*from = *to;
		*to = temp;
		from++;
		to--;
	}
	
}

void stable_partition_flip( int* array, int sz, int (*predicate_function)(int) ) {
	
	// printf("sz: %d\n", sz);
	// print_array( array, sz );
	// fflush( stdout );
	int start, mid = sz, end = -1; 
	
	// find the first occurence where the predicate is false (if any)
	for( int i=0; i<sz; i++ ) {
		if( !predicate_function( array[i] ) ) {
			start = i;
			break;
		}
	}
	
	if( start < sz ) {
		// printf("Start: a[%d] = %d\n", start, array[start] );
		// find the point where the predicate is true, if any
		for( int i=start+1; i<sz; i++ ) {
			if( predicate_function(array[i]) ) {
				mid = i-1;
				break;
			}
		}
		// printf("mid: %d\n", mid);
		if( mid < sz ) {
			// printf("Mid: a[%d] = %d\n", mid, array[mid]);
			// now find where the predicate stop being true
			for( int i=mid+1; i<sz; i++ ) {
				if( !predicate_function(array[i]) ) {
					end = i-1;
					break;
				}
			}
			
			if( end == -1 ) {
				end = sz-1;
			}
			// printf("End: a[%d] = %d\n", end, array[end]);
			
			flip( array + start, array + mid );
			// print_array( array, sz );
			flip( array + mid + 1, array + end );
			// print_array( array, sz );
			flip( array + start, array + end );
			// print_array( array, sz );
			
			// now we moved end-mid elements after start in the right place
			int ok = start + (end-mid);
			// printf("ok up to: a[%d] = %d\n", ok, array[ok] );
			if( ok < sz ) {
				stable_partition_flip( array + ok, sz-ok, predicate_function );
			}
		} else {
			// printf("All !p\n");
		}
	} else {
		// printf("Nothing to do\n");
	}
	
}


void partition_benchmark( void* params ) {
	uint64_t count = (uint64_t) params;

	int arr[count];
	
	fill_rand( arr, count );
	stable_partition( arr, count, predicate );
	if( !verify_partition( arr, count, predicate ) ) {
		printf("FAIL!\n");
		print_array( arr, count );
		abort();
	}
}

void partition_benchmark2( void* params ) {
	uint64_t count = (uint64_t) params;

	int arr[count];
	
	fill_rand( arr, count );
	stable_partition_flip( arr, count, predicate );
	if( !verify_partition( arr, count, predicate ) ) {
		printf("FAIL!\n");
		print_array( arr, count );
		abort();
	}
}


int main(int argc, char** argv) {
	
	int bad[] = { -3, -5, 4, 6, -3 };
	int good[] = { -3, -5, 4, 6, 10 };
	assert( verify_partition(bad, ARRAY_COUNT(bad), predicate ) == 0 );
	assert( verify_partition(good, ARRAY_COUNT(good), predicate ) == 1 );
	
	char buf[255];
	printf("N\tmove\t\tflip\n");
	for( uint64_t i=10; i<1000*1000; i*=2) {
		sprintf( buf, "%llu", i );
		benchmark sp_bad = run_benchmark( buf, partition_benchmark, (void*)i );
		benchmark sp_better = run_benchmark( buf, partition_benchmark2, (void*)i );
		printf("%s\t%.10f\t%.10f\t\n", sp_bad.name, sp_bad.average_seconds, sp_better.average_seconds );
	}
	
	
}
