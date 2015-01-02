#include <stdlib.h> // malloc
#include <stdio.h> // printf
#include <string.h> // memset
#include <assert.h>

typedef char bool;
#define true 1
#define false 0

#define CACHE_MEMORY_BYTES 256

#define MEMORY_PER_ITEM (sizeof(item) + sizeof(entry) + sizeof(entry*))
#define CACHE_SIZE (int)(CACHE_MEMORY_BYTES/MEMORY_PER_ITEM)

typedef struct item {
	int id;
	
	int value;

	char data[7]; // filler value to make items bigger (but still 8 byte aligned)
	bool is_dirty;

} item;

typedef struct entry {

	struct entry* next_entry;
	struct entry* prev_entry;
	
	int key;	
	int refcount;	

	// TODO: could compress these using union maybe?
	// item will probably have poor alignment, but what can you do. At least at the end it won't cause padding for entries.
	item* item;
} entry;

typedef struct cache {
	
	// buckets for the hash. Can't use the entries themselves since we'd have to use them as starting point for the buckets
	entry* buckets[CACHE_SIZE];
	
	// all the entries we use
	entry entries[CACHE_SIZE];
	
	// unused ones
	// list that is initially every entry
	entry* unused_entries;
	
	// refcount 0 ones with status=dirty or status=clean
	// these are double linked lists for O(1) add/remove
	entry* available_dirty_entries;
	entry* available_clean_entries;
	
} cache;

static void remove_from_list( entry** list, entry* element ) {
	
	assert( *list != NULL );
	assert( element != NULL );
	
	// last element on the list
	if( element->next_entry == element ) {
		*list = NULL;
	} else {
		element->prev_entry->next_entry = element->next_entry;
		element->next_entry->prev_entry = element->prev_entry;
		if( *list == element ) {
			*list = element->next_entry;
		}
	}
	
}

static void insert_into_list( entry** list, entry* element ) {

	// could be NULL
	assert( element != NULL );
	assert( *list != element );
	
	if( *list == NULL ) {
		*list = element;
		element->next_entry = element;
		element->prev_entry = element;
	} else {
		element->next_entry = (*list)->next_entry;
		element->prev_entry = *list;

		(*list)->next_entry->prev_entry = element;
		(*list)->next_entry = element;		
	}
	
}

static cache* new_cache() {
	
	cache* c = (cache*) malloc( sizeof(cache) );
	assert( c != NULL );
	
	printf("num b: %d, bsz: %lu\n", CACHE_SIZE, sizeof(c->buckets) );
	memset( c->buckets, 0, sizeof(c->buckets) );
	
	c->available_clean_entries = NULL;
	c->available_dirty_entries = NULL;
	c->unused_entries = NULL;
	
	// setup the unused list
	for( int i=0; i<CACHE_SIZE; i++ ) {
		insert_into_list( &c->unused_entries, &c->entries[i] );
	}
	
	return c;
}

static void flush_cache( cache* c ) {

	printf("Flushing all dirty items\n");
	entry* current = NULL;
	if( (current = c->available_dirty_entries) ) {
		do {
			printf("Dirty entry needs writing to disk or something: { id = %d, value = %d }\n", current->item->id, current->item->value );
			if( current->refcount > 0 ) {
				printf("\tWarning: refcount not 0 for item ID = %d\n", current->item->id );
			}
		} while( current != c->available_dirty_entries );
	}
	
}

static void dump( cache* c ) {
	
	printf("###############################\n");
	entry* current;
	printf("Cache (size %d)\nBuckets start %p\n", CACHE_SIZE, c->buckets);
	for(int i=0; i<CACHE_SIZE; i++) {
		current = c->buckets[i];
		printf("Bucket[%d]\n", i);
		if( current != NULL ) {
			do {
				printf("\tkey %d, refcount %d, item->value %d [entry %ld]\n", current->key, current->refcount, current->item->value, current - &c->entries[0] );
				current = current->next_entry;
			} while( current != c->buckets[i] );
		}
	}
	
	printf("Unused entries:\n");
	if( (current = c->unused_entries) ) {
		do {
			printf("\tentry %p (entry index %ld)\n", current, (current - &c->entries[0]) );
			current = current->next_entry;
		} while( current != c->unused_entries );		
	}

	printf("Available dirty entries:\n");
	if( (current = c->available_dirty_entries) ) {
		do { 
			printf("\trefcount %d\n", current->refcount );
			current = current->next_entry;
		}
		while( current != c->available_dirty_entries );		
	}
	
	printf("Available clean entries:\n");
	if( (current = c->available_clean_entries) ) {
		do {
			printf("\trefcount %d\n", current->refcount );
			current = current->next_entry;
		}
		while( current != c->available_clean_entries );
	}	

	printf("###############################\n\n");
	
}


static void add_item( cache* c, item* i ) {
	
	int b = i->id % CACHE_SIZE; // works if IDs are autoinc keys I think, and avoids hashing
	printf("Want to insert { id = %d, value = %d } into bucket %d\n", i->id, i->value, b);
	// get an available entry
	if( c->unused_entries != NULL ) {
		printf("unused entry available\n");
		entry* first_unused_entry = c->unused_entries;
		remove_from_list( &c->unused_entries, first_unused_entry );
		
		// set the values on this entry
		first_unused_entry->item = i;
		first_unused_entry->key = i->id;
		first_unused_entry->refcount = 1;
				
		// doesn't really have to be a doubly linked list, but it might as well be
		// and this makes the code MUCH cleaner. (well, could add functions for single list..)
		insert_into_list( &c->buckets[b], first_unused_entry );
		
	} else if( c->available_clean_entries != NULL ) {
		printf("no more unused entries, clean entry available\n");
		abort();
	} else if( c->available_dirty_entries != NULL ) {
		printf("no more unused entries, dirty entry available\n");
		abort();
	} else {
		printf("Cache full, not storing item %d\n", i->id );
	}
	
}

static void test_empty() {
	cache* store = new_cache();
	dump( store );
	free(store);	
}

static void test_add() {
	
	cache* store = new_cache();
	for(int i=0; i<8; i++) {

		item* foo = (item*) malloc( sizeof(item) );
		foo->id = rand() % 128;
		foo->value = i;
		add_item( store, foo );
		dump( store );
	}
	
	dump( store );
	
	free(store);
	
	
}

int main() {
	
	test_empty();
	test_add();
}



