#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>


void print_array( int* a, int sz ) {

	for( int i=0;i<sz; i++) {
		printf("%d ", a[i] );
	}
	printf("\n");
	
}

void fill_rand( int* array, int size ) {
	srand( time(NULL) );
	for(int i=0; i<size; i++) {
		array[i] = (rand() % 100) - 200;
	}
}

// predicate to sort negative/positive
int predicate( int n ) {

	return n < 0;

}


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


int main(int argc, char** argv) {
	
	int size = 20;
	int arr[size];
	
	fill_rand( arr, size );
	print_array( arr, size );
	
	stable_partition( arr, size, predicate );
	print_array( arr, size );
	
}
