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

	printf("[ ");
	for( int i=0;i<sz; i++) {
		printf("%d ", a[i] );
	}
	printf("]");
	
}

void fill_rand( int* array, int size ) {
	srand( clock() );
	for(int i=0; i<size; i++) {
		array[i] = (rand() % 100) - 50;
	}
}

// predicate to sort negative/positive
static uint32_t predicate_checks;
int predicate( int n ) {
	// printf("P(%d)\n", n);
	predicate_checks++;
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

static uint32_t flips;
void flip( int* from, int* to ) {
	// printf("Flipping from %d to %d\n", *from, *to );
	
	if( from == to ) {
		return;
	}
	
	int temp;
	while( from < to ) {
		flips++;
		temp = *from;
		*from = *to;
		*to = temp;
		from++;
		to--;
	}
	
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


/*
This is worse than the algorithm above.
If the partitioning should be something like XXXXXXOOOOOOOOO
We find a subsequence of OOOOOXXX, reverse the O's, reverse the X's and then reverse the whole thing.
Then we have the head of the array with some number of X's and call this function again with the new
start of the array (which will be O's) and then repeat. This means we end up flipping and reflipping
ever larger sets of O's to move them right. It's really bad. Still O(n^2) though.

One useful optimization is: if you flip OOOXX then you know the next call to the function is
s_b_f( 'OOO??????', ... ) so we pass in the mid_offset (start will be 0, mid should start somewhere after 3)
*/
void stable_partition_flip( int* array, int sz, int (*predicate_function)(int), int mid_offset ) {
	
	// print_array( array, sz ); printf("\n");
	// printf("Mid offset: %d\n", mid_offset);
	int start, mid_start, mid = sz, end = -1; 
	
	if( mid_offset == 0 ) {

		// find the first occurence where the predicate is false (if any)
		for( int i=0; i<sz; i++ ) {
			if( !predicate_function( array[i] ) ) {
				start = i;
				break;
			}
		}
		
		mid_start = start+1;
		
	} else {
		start = 0;
		mid_start = mid_offset;
	}
	
	if( start < sz ) {
		// printf("Start: a[%d] = %d\n", start, array[start] );
		// find the point where the predicate is true, if any
		for( int i=mid_start; i<sz; i++ ) {
			// printf("Checking for mid: a[%d] = %d\n", i, array[i]);
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
				stable_partition_flip( array + ok, sz-ok, predicate_function, mid-start+1 );
			}
		} else {
			// printf("All !p\n");
		}
	} else {
		// printf("Nothing to do\n");
	}
	
}


/*
Find a subsequence OOOOXX in the array
*/
inline static void find_subsequence( int* array, int sz, int (*predicate_function)(int), int start, int* mid, int* end ) {
	
	*mid = start + 1;

	while( *mid < sz && !predicate_function( array[*mid] ) ) {
		(*mid)++;
	}
	
	*end = 1 + *mid;
	while( *end < sz && predicate_function( array[*end] ) ) {
		(*end)++;
	}

}

/*

This is essentially the same as the move algorithm except now we flip.

The upside is that predicate_function is called exactly once for each element of the array
*/
void stable_partition_flip2( int* array, int sz, int (*predicate_function)(int) ) {
	
	int start = 0, mid = 0, end = 0;
	int offset = 0;

	// first, skip all the elements at the beginnig that are ok
	for( int i=0; i<sz; i++ ) {
		if( !predicate_function( array[i] ) ) {
			start = i;
			break;
		}
	}

	while( 1 ) {
	
		// find the next sequence, except we can start ahead if we previously moved some !p forward
		find_subsequence( array, sz, predicate_function, start + offset, &mid, &end );
	
		if( mid < sz ) {		

	
			flip( array + start, array + mid - 1 );
			flip( array + mid, array + end - 1 );
			flip( array + start, array + end - 1 );

			offset = mid - start;
			start += end - mid;

		} else {
			break;
		}

	} 
			
	
}

// Damian does up to, mine does including so we -1 the endpoint
inline static void Reverse( int* array, int from, int to ) {
	flip( array + from, array + to - 1 );
}

inline static int Rotate(int* array, int f, int k, int l) {
	// printf("Rotate %d-%d-%d\nBefore: ", f, k, l);
	// print_array( array + f, l-f ); printf("\n");
	Reverse(array, f, k);
	Reverse(array, k, l);
	Reverse(array, f, l);
	// printf("After: "); print_array( array + f, l-f ); printf("\n");
	return f + l - k;
}

int stable_partition_dryski(int* array, int first, int last, int (*predicate_function)(int) ) {

	// print_array( array + first, last-first ); printf("\n");
	int span = last - first;
	
	if( span == 0 ) {
		return first;
	}

	if( span == 1 ) {
		int r = first;
		if( predicate_function( array[first] ) ) {
			r++;
		}
		return r;
	}

	int mid = first + span/2;

	return Rotate(
				array, 
				stable_partition_dryski(array, first, mid, predicate_function), 
				mid, 
				stable_partition_dryski(array, mid, last, predicate_function)
			);
}

static int global_do_verify = 0;

void partition_benchmark( void* params ) {
	uint64_t count = (uint64_t) params;

	int arr[count];
	
	fill_rand( arr, count );
	stable_partition( arr, count, predicate );
	if( global_do_verify && !verify_partition( arr, count, predicate ) ) {
		printf("FAIL!\n");
		print_array( arr, count );
		abort();
	}
}

void partition_benchmark2( void* params ) {
	uint64_t count = (uint64_t) params;

	int arr[count];
	
	fill_rand( arr, count );
	stable_partition_flip( arr, count, predicate, 0 );
	if( global_do_verify && !verify_partition( arr, count, predicate ) ) {
		printf("FAIL!\n");
		print_array( arr, count );
		abort();
	}
}

void partition_benchmark3( void* params ) {
	uint64_t count = (uint64_t) params;

	int arr[count];
	
	fill_rand( arr, count );
	stable_partition_flip2( arr, count, predicate );
	if( global_do_verify && !verify_partition( arr, count, predicate ) ) {
		printf("FAIL!\n");
		print_array( arr, count );
		abort();
	}
}

void partition_benchmark4( void* params ) {
	uint64_t count = (uint64_t) params;

	int arr[count];
	
	fill_rand( arr, count );
	stable_partition_dryski( arr, 0, count, predicate );
	if( global_do_verify && !verify_partition( arr, count, predicate ) ) {
		printf("FAIL!\n");
		print_array( arr, count );
		abort();
	}
}


int main(int argc, char** argv) {
	
	// int bad[] = { -3, -5, 4, 6, -3 };
	// int good[] = { -3, -5, 4, 6, 10 };
	// assert( verify_partition(bad, ARRAY_COUNT(bad), predicate ) == 0 );
	// assert( verify_partition(good, ARRAY_COUNT(good), predicate ) == 1 );
	
	int sz = 10;
	// int arr[] = { -1, 1, -2, 2, -3, 3, -4, 4, -5, 5 };
	int arr[sz];
	fill_rand( arr, sz );
	print_array( arr, sz ); printf("\n");
	flips = 0; predicate_checks = 0;
	stable_partition_dryski( arr, 0, sz, predicate );
	print_array( arr, sz ); printf("\n");
	int oldp = predicate_checks;
	printf("Ok: %d\tFlips: %u\tPreds: %u\n", verify_partition(arr, sz, predicate), flips, predicate_checks-oldp );
	
	// return 0;
	char buf[255];
	printf("N\tmove\t\tflip\t\tflip2\t\tdgryski\n");
	for( uint64_t i=10; i<1000*1000; i*=2) {
		sprintf( buf, "%llu", i );
		benchmark sp_move = run_benchmark( buf, partition_benchmark, (void*)i );
		benchmark sp_flip = run_benchmark( buf, partition_benchmark2, (void*)i );
		benchmark sp_flip2 = run_benchmark( buf, partition_benchmark3, (void*)i );
		benchmark sp_dgryski = run_benchmark( buf, partition_benchmark4, (void*)i );
		printf("%s\t%.10f\t%.10f\t%.10f\t%.10f\n", sp_move.name, sp_move.average_seconds, sp_flip.average_seconds, sp_flip2.average_seconds, sp_dgryski.average_seconds );
	}
	
	
}
