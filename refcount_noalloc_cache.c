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
		insert_into_list( &c->available_clean_entries, &c->entries[i] );
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

static void dump_list( cache* c, const char* title, entry* head ) {
	
	printf("%s:\n", title);
	entry* current;
	if( (current = head) ) {
		do {
			if( current->item == NULL ) {
				printf("\tkey %d, refcount %d (no item) [entry %ld]\n", current->key, current->refcount, current - &c->entries[0] );				
			} else {
				printf("\tkey %d, refcount %d, item { id = %d, value = %d, dirty = %s } [entry %ld]\n", current->key, current->refcount, current->item->id, current->item->value, current->item->is_dirty ? "true" : "false", current - &c->entries[0] );				
			}
			current = current->next_entry;
		} while( current != head );		
	}
	
}

static void dump( cache* c ) {
	
	printf("###############################\n");

	char buf[256];
	printf("Cache (size %d)\nBuckets start %p\n", CACHE_SIZE, c->buckets);
	for(int i=0; i<CACHE_SIZE; i++) {
		sprintf( buf, "Bucket[%d]", i );
		dump_list( c, buf, c->buckets[i] );
	}
	
	dump_list( c, "Unused entries", c->unused_entries );
	dump_list( c, "Available clean entries", c->available_clean_entries );
	dump_list( c, "Available dirty entries", c->available_dirty_entries );

	printf("###############################\n\n");
	
}

static void release_item( cache* c, item* i ) {

	printf("Releasing item %d\n", i->id );
	assert( i != NULL );

	int b = i->id % CACHE_SIZE; // works if IDs are autoinc keys I think, and avoids hashing

	if( c->buckets[b] == NULL ) {
		printf("Item not in cache, freeing\n");
		free( i ); // Here one would call free_item( ... ) that does stats & counters and writing thins like dirty items to disk
		return;
	}
	
	entry* current = c->buckets[b];
	do {
		if( current->key == i->id ) { // TODO(performance): yeah, so why not lookup the id from current? would also save space..
			printf("Found item in cache\n");
			assert( current->refcount > 0 );
			current->refcount--;
			if( current->refcount == 0 ) {
				remove_from_list( &c->buckets[b], current );
				entry** available_list = i->is_dirty ? &c->available_dirty_entries : &c->available_clean_entries;
				insert_into_list( available_list, current );				
			}
			return;
		}
		current = current->next_entry;
	} while( current != c->buckets[b] );
	
	printf("Item not in cache, freeing\n");
	free( i ); // Here one would call free_item( ... ) that does stats & counters and writing thins like dirty items to disk
	
}

static void set_entry( entry* e, item* i ) {

	e->item = i;
	e->key = i->id;
	e->refcount = 1;

}
static void add_item( cache* c, item* i ) {
	
	int b = i->id % CACHE_SIZE; // works if IDs are autoinc keys I think, and avoids hashing
	printf("Want to insert { id = %d, value = %d } into bucket %d\n", i->id, i->value, b);
	// get an available entry
	if( c->unused_entries != NULL ) {
		printf("unused entry available\n");
		entry* first_unused_entry = c->unused_entries;
		remove_from_list( &c->unused_entries, first_unused_entry );
		
		set_entry( first_unused_entry, i );
				
		// doesn't really have to be a doubly linked list, but it might as well be
		// and this makes the code MUCH cleaner. (well, could add functions for single list..)
		insert_into_list( &c->buckets[b], first_unused_entry );
		
	} else if( c->available_clean_entries != NULL ) {
		printf("no more unused entries, clean entry available\n");
		entry* some_clean_entry = c->available_clean_entries;
		remove_from_list( &c->available_clean_entries, some_clean_entry );
		// printf("Freeing clean item %d\n", some_clean_entry->item->id );
		free( some_clean_entry->item ); // Here one would call free_item( ... ) that does stats & counters and writing thins like dirty items to disk
		set_entry( some_clean_entry, i );
		insert_into_list( &c->buckets[b], some_clean_entry );

	} else if( c->available_dirty_entries != NULL ) {
		printf("no more unused entries, dirty entry available\n");
		entry* some_dirty_entry = c->available_dirty_entries;
		remove_from_list( &c->available_dirty_entries, some_dirty_entry );
		printf("Freeing dirty item %d\n", some_dirty_entry->item->id );
		free( some_dirty_entry->item ); // Here one would call free_item( ... ) that does stats & counters and writing thins like dirty items to disk
		set_entry( some_dirty_entry, i );
		insert_into_list( &c->buckets[b], some_dirty_entry );


	} else {
		printf("Cache full, not storing item %d\n", i->id );
	}
	
}

/********************** TESTS *****************************/

static void test_empty() {
	
	printf("************** Test new/flush/free ****************\n");
	cache* store = new_cache();
	dump( store );
	flush_cache( store );
	free(store);	
}

static void test_add() {
	
	printf("************** Test adding more items than fit ****************\n");
	cache* store = new_cache();
	for(int i=0; i<CACHE_SIZE*2; i++) {

		item* foo = (item*) malloc( sizeof(item) );
		foo->id = rand() % 128;
		foo->value = i;
		add_item( store, foo );
	}
	
	dump( store );
	
	flush_cache( store );
	free( store );
	
	
}

static void test_add_release() {
	
	printf("************** Test adding/releasing items (so should recycle items) ****************\n");
	cache* store = new_cache();
	for(int i=0; i<CACHE_SIZE * 2; i++) {

		item* foo = (item*) malloc( sizeof(item) );
		foo->id = rand() % 128;
		foo->value = i;
		add_item( store, foo );
		dump( store );
		release_item( store, foo );
		dump( store );
	}
	
	dump( store );
	flush_cache( store );
	
	free(store);
	
	
}

int main() {
	//
	// test_empty();
	// test_add();
	test_add_release();
}



