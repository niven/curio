#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char bool;

#define false 0
#define true 1

#define CACHE_MEMORY_BYTES 4096

typedef struct counter {
	size_t foo_allocs;
	size_t foo_frees;
	size_t entry_allocs;
	size_t entry_frees;
	size_t free_entry_allocs;
	size_t free_entry_frees;
} counter;

static counter counters;

static void print_counters() {
	
	printf("Foo allocs: %lu\n", counters.foo_allocs);
	printf("Foo frees: %lu\n", counters.foo_frees);
	printf("Entry allocs: %lu\n", counters.entry_allocs);
	printf("Entry frees: %lu\n", counters.entry_frees);
	printf("Free entry allocs: %lu\n", counters.free_entry_allocs);
	printf("Free entry frees: %lu\n", counters.free_entry_frees);
	
}


// test item to store
typedef struct foo {
	size_t b;
	bool is_dirty;
	char padding[256];
} foo;

// doubly linked list of refcount==0 entries in cache
typedef struct free_entry {
	foo* evictable_foo;
	size_t key;
	struct free_entry* next;
	struct free_entry* prev;
} free_entry;

// bucket entries
typedef struct entry {

	union {
		foo* to_foo;
		free_entry* to_free_entry;
	} ptr;
	size_t key;
	size_t refcount;
	struct entry* next;
} entry;

#define BYTES_PER_CACHE_ITEM (sizeof(entry*) + sizeof(entry) + sizeof(free_entry) + sizeof(foo))
#define CACHE_SIZE (CACHE_MEMORY_BYTES / BYTES_PER_CACHE_ITEM)

typedef struct cache {
	entry* buckets[ CACHE_SIZE ];
	size_t num_stored;
	free_entry* free_list;
	free_entry* free_list_dirty;
} cache;


// MurmurHash3 integer finalizer MOD cache buckets
static size_t hash(size_t i) {

	size_t h = i;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h % CACHE_SIZE;
}


static cache* new_cache() {
	
	printf("Bytes per item: %lu, cache mem: %d, cache_size= %lu\n", BYTES_PER_CACHE_ITEM, CACHE_MEMORY_BYTES, CACHE_SIZE);
	
	cache* store = (cache*) malloc( sizeof(cache) );
	memset( store->buckets, 0, sizeof(store->buckets) );
	store->num_stored = 0;
	store->free_list = NULL;
	store->free_list_dirty = NULL;
	return store;
}


static void dump( cache* c ) {
	
	printf("Cache (%lu items): (%p)\n", c->num_stored, c->buckets);
	for(size_t i=0; i<CACHE_SIZE; i++ ) {
		printf("bucket[%lu] = (%p) -> (%p)\n", i, &c->buckets[i], c->buckets[i]);
		entry* current = c->buckets[i];
		while( current != NULL ) {
			foo* current_foo = NULL;
			if( current->refcount == 0 ) {
				current_foo = current->ptr.to_free_entry->evictable_foo;
			} else {
				current_foo = current->ptr.to_foo;
			}
			printf("\tentry key=%lu (foo.b = %lu, dirty: %s) refcount: %lu\n", current->key, current_foo->b, current_foo->is_dirty ? "true" : "false", current->refcount );
			current = current->next;
		}
	}

	printf("Free list (clean):\n");
	free_entry* current = c->free_list;
	if( current != NULL ){
		do {
			printf("\tfree entry: key=%lu (foo.b=%lu dirty=%s) [next=%lu, prev=%lu]\n", 
			current->key, current->evictable_foo->b, current->evictable_foo->is_dirty ? "true" : "false", current->next->key, current->prev->key );
			current = current->next;	
		} while( current != c->free_list );
	}
	
	printf("Free list (dirty):\n");
	current = c->free_list_dirty;
	if( current != NULL ){
		do {
			printf("\tfree entry: key=%lu (foo.b=%lu dirty=%s) [next=%lu, prev=%lu]\n", 
			current->key, current->evictable_foo->b, current->evictable_foo->is_dirty ? "true" : "false", current->next->key, current->prev->key );
			current = current->next;	
		} while( current != c->free_list_dirty );
	}
	
}

static void clear_cache( cache* c ) {
	
	printf("Clearing the cache\n");
	// free all items in the buckets
	for( size_t b=0; b<CACHE_SIZE; b++ ) {
		entry* current = c->buckets[b];
		while( current != NULL ) {
			// only free actual foos
			if( current->refcount != 0 ) {
				printf("\tfoo %lu\n", current->ptr.to_foo->b );
				free( current->ptr.to_foo );
				counters.foo_frees++;
			}
			// free the entry
			printf("\tentry %lu\n", current->key );
			entry* next = current->next;
			free( current );
			counters.entry_frees++;
			current = next;
		}
		c->buckets[b] = NULL;
	}
	
	// now all foos in entries are freed, as well as all entries
	// free the free_entry and foos they contain
	free_entry* free_lists[2] = { c->free_list, c->free_list_dirty };
	for(int i=0; i<2; i++){

		free_entry* current = free_lists[i];
		// break the chain
		if( current != NULL ) {
			current->prev->next = NULL;
		}
		while( current != NULL ) {
			printf("\tfree entry %lu\n", current->key );
			free( current->evictable_foo );
			counters.foo_frees++;
			free_entry* next = current->next;
			free( current );
			counters.free_entry_frees++;
			current = next;
		}
		
	}
	
	c->num_stored = 0;
	c->free_list = NULL;
}

static void evict_item( cache* c, free_entry** free_list ) {
	
	// take the first thing in the free list
	free_entry* fe = *free_list;
	// remove it from the free list
	if( (*free_list)->next == *free_list ) { // just single item
		*free_list = NULL;
	} else {
		// [prev]<->[fe]<->[next]
		//           ^
		//           c->free_list
		fe->prev->next = fe->next;
		fe->next->prev = fe->prev;
		*free_list = fe->next;
	}
	// now our free list is ok again
	size_t b = hash(fe->key);
	printf("Can evict key %lu from free list (it's in bucket %lu)\n", fe->key, b );
	
	// find and remove the entry from the bucket
	entry* entry_to_free = NULL;
	if( c->buckets[b]->key == fe->key ) { // it's the head item
		printf("Removing the entry from the bucket (it was the head)\n");
		entry_to_free = c->buckets[b];
		c->buckets[b] = c->buckets[b]->next; // just move to the next one
	} else {
		entry* current;
		for(current = c->buckets[b]; current->next->key != fe->key; current = current->next ) {
			printf("Checking key %lu (next %lu)\n", current->key, current->next->key );
			if( current->key == fe->key ) {
				break;
			}
			assert( current->next != NULL ); // it has to be in this list
		}
		printf("Found the bucket entry: key %lu (next %lu)\n", current->key, current->next->key );
		entry_to_free = current->next;
		current->next = current->next->next; // skip over it
	}
	assert( entry_to_free != NULL );
	
	// free the free_entry, the foo it points to and the entry in the bucket
	free( fe->evictable_foo );
	counters.foo_frees++;
	free( fe );
	counters.entry_frees++;
	free( entry_to_free );
	counters.free_entry_frees++;
	c->num_stored--;
	
}

static void add_item( cache* c, foo* f, size_t key ) {

	printf("Adding item %lu\n", key);
	if( c->num_stored == CACHE_SIZE ) {
		printf("Cache full\n");
		// check the free list
		
		if( c->free_list != NULL ) {
			printf("Evicting a clean item\n");
			evict_item( c, &c->free_list );
		} else if( c->free_list_dirty != NULL ) {
			printf("Evicting a dirty item\n");
			evict_item( c, &c->free_list_dirty );
		} else {
			printf("Nothing in the free lists.\n");
			return;
		}

	}
	size_t h = hash( key );
	entry* bucket = c->buckets[h];
	
	entry* i = (entry*) malloc( sizeof(entry) );
	counters.entry_allocs++;
	i->ptr.to_foo = f;
	i->refcount = 1;
	i->key = key;
	i->next = NULL;
	
	if( bucket != NULL ) {
		i->next = bucket;
	}
	c->buckets[h] = i;

	c->num_stored++;
}

static foo* get_item( cache* c, size_t key ) {
	
	size_t h = hash( key );
	for( entry* i = c->buckets[h]; i != NULL; i = i->next ) {
		printf("Get item %lu check %lu:%lu\n", key, h, i->key);
		if( i->key == key ) {
			
			// either a foo, or a pointer to a free_entry
			if( i->refcount == 0 ) {
				printf("Reviving item %lu\n", key);
				// it's one on the free list, means we need to remove it from there
				free_entry* discard = i->ptr.to_free_entry;
				assert( discard != NULL );
				i->ptr.to_foo = discard->evictable_foo; // put it back in the regular entry

				// remove it from the free list
				discard->prev->next = discard->next;
				discard->next->prev = discard->prev;
				
				// if we happen to free the initial entry in the free list, set a new head
				// unless this was the last item, then set the list to NULL
				if( c->free_list == discard ) {	
					c->free_list = discard->next == discard ? NULL : discard->next;
				}
				if( c->free_list_dirty == discard ) {	
					c->free_list_dirty = discard->next == discard ? NULL : discard->next;
				}
				
				free( discard );
				discard = NULL;
				counters.free_entry_frees++;
			}
			// regular item, or free_entry inbetween was discarded
			i->refcount++;
			return i->ptr.to_foo;
		}
	}
	
	printf("Item %lu was not in the cache\n", key );
	return NULL;
	
}

static void release_item( cache* c, foo* f, size_t key ) {

	size_t h = hash( key );
	for( entry* i = c->buckets[h]; i != NULL; i = i->next ) {
		printf("release %lu, looking in bucket %lu\n", key, h );
		if( i->key == key ) {
			i->refcount--;
			assert( i->refcount >= 0 );
			// add it to the free list if refcount hits 0
			if( i->refcount == 0 ) {
				free_entry* new_head = (free_entry*) malloc( sizeof(free_entry) );
				counters.free_entry_allocs++;
				new_head->evictable_foo = i->ptr.to_foo; // keep the actual thing we store
				i->ptr.to_free_entry = new_head; // replace it with ref to the free_entry
				new_head->key = key;
				
				free_entry** free_list = new_head->evictable_foo->is_dirty ? &c->free_list_dirty : &c->free_list;
				if( *free_list == NULL ) {
					printf("empty free_list, setting first item\n");
					new_head->next = new_head;
					new_head->prev = new_head;
				} else {
					new_head->next = *free_list;
					new_head->prev = (*free_list)->prev;
					(*free_list)->prev = new_head;
					new_head->prev->next = new_head;
				}

				*free_list = new_head;
			}
			return;
		}
	}
	
	// was not in the cache, just free it
	printf("Item %lu was not in the cache, doing a normal free()\n", key);
	counters.foo_frees++;
	free( f );
}

/********************** TESTS *************************/

static void checks() {
	
	printf( "foo allocs/frees = %lu/%lu\n", counters.foo_allocs, counters.foo_frees);

	assert( counters.foo_allocs == counters.foo_frees );
	assert( counters.entry_allocs == counters.entry_frees );
	assert( counters.free_entry_allocs == counters.free_entry_frees );
	
}

static void test_add() {
	

	cache* store = new_cache();
	dump( store );
	
	printf("==== Adding keys 1-10 ====\n");
	
	for(size_t i=1; i<11; i++) {
		foo* temp = (foo*)malloc( sizeof(foo) );
		counters.foo_allocs++;
		temp->b = i;
		temp->is_dirty = false;
		add_item( store, temp, i );
	}
	dump( store );
	print_counters();

	clear_cache( store );

	checks();
}

static void test_add_release() {
	
	cache* store = new_cache();
	
	printf("==== Adding and releasing keys 1-10 ====\n");
	
	for(size_t i=1; i<11; i++) {
		foo* temp = (foo*)malloc( sizeof(foo) );
		counters.foo_allocs++;
		temp->b = i;
		temp->is_dirty = false;
		add_item( store, temp, i );
		release_item( store, temp, i );
	}
	dump( store );
	print_counters();

	clear_cache( store );

	checks();
	
}

static void test_free_entry_reuse() {

	cache* store = new_cache();
		
	printf("==== Adding keys 1-12 (filling the cache), releasing 1-4 ====\n");
	for(size_t i=1; i<CACHE_SIZE; i++) {
		foo* temp = (foo*)malloc( sizeof(foo) );
		counters.foo_allocs++;
		temp->b = i;
		temp->is_dirty = false;
		add_item( store, temp, i );
		if( i < 5 ) {
			release_item( store, temp, i );
		}
	}

	dump( store );

	printf("==== Retrieving keys 3,4,5,6 (should take them off the free list/and increase refcount) ====\n");
	for(size_t i=3; i<5; i++) {
		foo* temp = get_item( store, i );
		assert( temp != NULL );
	}
	dump( store );

	clear_cache( store );
	print_counters();
	checks();
	
}

static void test_single_add_release_get() {
	
	cache* store = new_cache();
	
	printf("==== Add/Release/Retrieve keys 1 ====\n");
	
	foo* temp = (foo*)malloc( sizeof(foo) );
	counters.foo_allocs++;
	
	size_t payload = 31415, key = 24;
	temp->b = payload;
	temp->is_dirty = false;
	add_item( store, temp, key );
	release_item( store, temp, key );
	temp = NULL;
	temp = get_item( store, key );
	printf("temp->b = %lu\n", temp->b );
	assert( temp != NULL );
	assert( temp->b == payload );
	assert( temp->is_dirty == false );

	clear_cache( store );
	print_counters();
	checks();

}

// evicting an item should also remove it from the bucket,
// which is most tricky when it is the first/only item
static void test_evict_first_item_in_bucket() {

	cache* store = new_cache();
	
	printf("==== Evicting first item in a bucket ====\n");
	
	// fill the cache first
	foo* first_in_bucket = NULL;
	for(size_t i=1; i<CACHE_SIZE; i++) {
		foo* temp = (foo*)malloc( sizeof(foo) );
		counters.foo_allocs++;
		temp->b = i;
		temp->is_dirty = false;
		add_item( store, temp, i );
		if( i == 8 ) {
			first_in_bucket = temp;
		}
	}
	
	// puts it on the free list
	release_item( store, first_in_bucket, 8 );
	
	// add some random new item, should evict 8
	foo* temp = (foo*)malloc( sizeof(foo) );
	counters.foo_allocs++;
	temp->b = 1234;
	temp->is_dirty = false;
	add_item( store, temp , 55 );
	dump( store );
	
	// retrieve should fail now
	foo* not_here = get_item( store, 11 );
	assert( not_here == NULL );
	
	dump( store );

	clear_cache( store );
	print_counters();
	checks();
	
	
}

static void test_evict_middle_item_in_bucket() {

	cache* store = new_cache();
	
	printf("==== Evicting middle item in a bucket ====\n");
	
	// fill the cache first
	foo* middle_in_bucket = NULL;
	for(size_t i=1; i<=CACHE_SIZE; i++) {
		foo* temp = (foo*)malloc( sizeof(foo) );
		counters.foo_allocs++;
		temp->b = i;
		temp->is_dirty = false;
		add_item( store, temp, i );
		if( i == 7 ) {
			middle_in_bucket = temp;
		}
	}
	dump( store );

	// puts it on the free list
	release_item( store, middle_in_bucket, 7 );
	
	// add some random new item, should evict 7
	foo* temp = (foo*)malloc( sizeof(foo) );
	counters.foo_allocs++;
	temp->b = 1234;
	temp->is_dirty = false;
	add_item( store, temp , 55 );

	dump( store );
	
	// retrieve should fail now
	foo* not_here = get_item( store, 7 );
	assert( not_here == NULL );
	
	dump( store );

	clear_cache( store );
	print_counters();
	checks();
	
	
}

static void count_free_entries_by_dirty_clean( cache* store, size_t* clean, size_t* dirty ) {

	for(size_t i=0; i<CACHE_SIZE; i++) {
		entry* e = store->buckets[i];
		while( e != NULL ) {
			foo* current;
			if( e->refcount == 0 ) {
				current = e->ptr.to_free_entry->evictable_foo;
				*dirty += current->is_dirty == true;
				*clean += current->is_dirty == false;
			}
			e = e->next;
		}
	}
	
}

static void test_dirty_items() {
	
	cache* store = new_cache();
	
	printf("==== Testing recycling dirty items first ====\n");
	// fill the cache first
	foo* saved[CACHE_SIZE+1];
	for(size_t i=1; i<=CACHE_SIZE; i++) {
		foo* temp = (foo*)malloc( sizeof(foo) );
		saved[i] = temp;
		counters.foo_allocs++;
		temp->b = i;
		temp->is_dirty = i % 2 == 0; // even items are dirty
		add_item( store, temp, i );
	}
	dump( store );
	
	// release half the items
	for(size_t i=1; i<=CACHE_SIZE/2; i++) {
		release_item( store, saved[i], i );
	}
	dump( store );
	
	// now the free list is 1,2,3,4,5 (2,4 clean and 1,3,5 dirty) (depending on the cache size)
	size_t num_clean_items_in_cache = 0;
	size_t num_dirty_items_in_cache = 0;
	count_free_entries_by_dirty_clean( store, &num_clean_items_in_cache, &num_dirty_items_in_cache);
	printf("Clean items: %lu\n", num_clean_items_in_cache);
	printf("Dirty items: %lu\n", num_dirty_items_in_cache);
	
	// add 2 items (forcing 2 to be evicted) and check if the clean ones were
	foo* replacer1 = (foo*)malloc( sizeof(foo) );
	counters.foo_allocs++;
	replacer1->b = 31415;
	replacer1->is_dirty = false;
	add_item( store, replacer1, 31415 );
	foo* replacer2 = (foo*)malloc( sizeof(foo) );
	counters.foo_allocs++;
	replacer2->b = 21718;
	replacer2->is_dirty = false;
	add_item( store, replacer2, 21718 );
	dump( store );
	
	size_t num_clean_items_in_cache_after = 0;
	size_t num_dirty_items_in_cache_after = 0;
	count_free_entries_by_dirty_clean( store, &num_clean_items_in_cache_after, &num_dirty_items_in_cache_after);
	printf("Clean items: %lu\n", num_clean_items_in_cache_after);
	printf("Dirty items: %lu\n", num_dirty_items_in_cache_after);
	
	assert( num_dirty_items_in_cache_after == num_dirty_items_in_cache );
	assert( num_clean_items_in_cache_after == num_clean_items_in_cache - 2);
	
	clear_cache( store );
	print_counters();
	checks();
	
}

int main() {

	test_evict_middle_item_in_bucket();

	test_evict_first_item_in_bucket();

	test_single_add_release_get();

	test_add();

	test_add_release();

	test_free_entry_reuse();
	
	test_dirty_items();
	
	return 0;
}
