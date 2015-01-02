#include <stdlib.h> // malloc
#include <stdio.h> // printf
#include <string.h> // memset
#include <assert.h>
#include <time.h> // time() for srand

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
	
	// refcount 0 ones with status=dirty or status=clean
	// these are double linked lists for O(1) add/remove
	entry* available_dirty_entries;
	entry* available_clean_entries; // initially holds the unused items
	
} cache;

static void remove_from_list( entry** list, entry* element ) {
	
	assert( *list );
	assert( element );
	
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
	assert( element );
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
	assert( c );
	
	printf("num b: %d, bsz: %lu\n", CACHE_SIZE, sizeof(c->buckets) );
	memset( c->buckets, 0, sizeof(c->buckets) );
	
	c->available_clean_entries = NULL;
	c->available_dirty_entries = NULL;
	
	// setup the unused list
	for( int i=0; i<CACHE_SIZE; i++ ) {
		insert_into_list( &c->available_clean_entries, &c->entries[i] );
	}
	
	return c;
}

static void free_item( item* i ) {
	
	// TODO(errors): maybe assert here? Could indicate a big to free a NULL item
	if( i == NULL ) {
		return;
	}
	if( i->is_dirty ) {
		printf("Dirty entry needs writing to disk or something: { id = %d, value = %d }\n", i->id, i->value );		
	} else {
		printf("Freeing clean item { id = %d, value = %d }\n", i->id, i->value );
	}

	free( i );
}


static void flush_cache( cache* c ) {

	printf("Flushing all dirty items\n");
	entry* current = NULL;
	if( (current = c->available_dirty_entries) ) {
		do {
			if( current->refcount > 0 ) {
				fprintf( stderr, "\tWarning: refcount not 0 for item ID = %d\n", current->item->id );
			}			
			free_item( current->item );
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
	
	dump_list( c, "Available clean entries", c->available_clean_entries );
	dump_list( c, "Available dirty entries", c->available_dirty_entries );

	printf("###############################\n\n");
	
}

static void release_item( cache* c, item* i ) {

	printf("Releasing item %d\n", i->id );
	assert( i );

	int b = i->id % CACHE_SIZE; // works if IDs are autoinc keys I think, and avoids hashing

	if( c->buckets[b] == NULL ) {
		printf("Item not in cache, freeing\n");
		free_item( i );
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
	
	printf("Item not in cache, freeing.\n");
	free_item( i );
	
}

static void set_entry( entry* e, item* i ) {

	e->item = i;
	e->key = i->id;
	e->refcount = 1;

}
static void add_item( cache* c, item* i ) {
	
	int b = i->id % CACHE_SIZE; // works if IDs are autoinc keys I think, and avoids hashing
	printf("Want to insert { id = %d, value = %d, is_dirty = %s } into bucket %d\n", i->id, i->value, i->is_dirty ? "true" : "false", b);

	// get an available entry
	entry* available_entry = c->available_clean_entries ? c->available_clean_entries : c->available_dirty_entries;
	entry** from_list = c->available_clean_entries ? &c->available_clean_entries : &c->available_dirty_entries;;
	
	if( available_entry ) {
		printf("Recycled an available item (%d)\n", available_entry->item == NULL ? -1 : available_entry->item->id );
		remove_from_list( from_list, available_entry );
		free( available_entry->item );
		set_entry( available_entry, i );
		insert_into_list( &c->buckets[b], available_entry );
	}
	 else {
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
		foo->is_dirty = rand() % 2 == 0;
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
	
	srand( (unsigned int)time(NULL) );

	// test_empty();
	// test_add();
	test_add_release();
	
	char* a =NULL;//malloc(1);
	char* b= NULL;//malloc(1);
	char* c = a ? a : b;
	char** d = &c;
	printf("a=%p, b=%p, c=%p, d=%p\n", a ,b, c, d);
}



