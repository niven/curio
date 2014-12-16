#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_BUCKETS 4
#define CACHE_MAX 12

typedef struct counter {
	size_t foo_allocs;
	size_t foo_frees;
	size_t entry_allocs;
	size_t entry_frees;
	size_t free_entry_allocs;
	size_t free_entry_frees;
} counter;

static counter counters;

void print_counters() {
	
	printf("Foo allocs: %lu\n", counters.foo_allocs);
	printf("Foo frees: %lu\n", counters.foo_frees);
	printf("Entry allocs: %lu\n", counters.entry_allocs);
	printf("Entry frees: %lu\n", counters.entry_frees);
	printf("Free entry allocs: %lu\n", counters.free_entry_allocs);
	printf("Free entry frees: %lu\n", counters.free_entry_frees);
	
}

// MurmurHash3 integer finalizer MOD cache buckets
int hash(int i) {

	int h = i;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h % CACHE_BUCKETS;
}

// test item to store
typedef struct foo {
	int b;
} foo;

// doubly linked list of refcount==0 entries in cache
typedef struct free_entry {
	foo* evictable_foo;
	int key;
	struct free_entry* next;
	struct free_entry* prev;
} free_entry;

// bucket entries
typedef struct entry {

	union {
		foo* to_foo;
		free_entry* to_free_entry;
	} ptr;
	int key;
	int refcount;
	struct entry* next;
} entry;


typedef struct cache {
	entry* buckets[CACHE_BUCKETS];
	int num_stored;
	free_entry* free_list;
} cache;

void dump( cache* c ) {
	
	printf("Cache (%d items):\n", c->num_stored);
	for(int i=0; i<CACHE_BUCKETS; i++ ) {
		entry* current = c->buckets[i];
		printf("bucket %d %p\n", i, current);
		while( current != NULL ) {
			printf("\tentry key=%d (foo.b = %d) refcount: %d\n", current->key, current->ptr.to_foo->b, current->refcount );
			current = current->next;
		}
	}

	printf("Free list:\n");
	free_entry* current = c->free_list;
	if( current != NULL ){
		do {
			printf("\tfree entry: key=%d (foo.b=%d) [next=%d, prev=%d]\n", 
			current->key, current->evictable_foo->b, current->next->key, current->prev->key );
			current = current->next;	
		} while( current != c->free_list );
	}
	
}

void clear_cache( cache* c ) {
	
	printf("Clearing the cache\n");
	// free all items in the buckets
	for( int b=0; b<CACHE_BUCKETS; b++ ) {
		entry* current = c->buckets[b];
		while( current != NULL ) {
			// only free actual foos
			if( current->refcount != 0 ) {
				printf("\tfoo %d\n", current->ptr.to_foo->b );
				free( current->ptr.to_foo );
				counters.foo_frees++;
			}
			// free the entry
			printf("\tentry %d\n", current->key );
			entry* next = current->next;
			free( current );
			counters.entry_frees++;
			current = next;
		}
		c->buckets[b] = NULL;
	}
	
	// now all foos in entries are freed, as well as all entries
	// free the free_entry and foos they contain
	free_entry* current = c->free_list;
	// break the chain
	if( current != NULL ) {
		current->prev->next = NULL;
	}
	while( current != NULL ) {
		printf("\tfree entry %d\n", current->key );
		free( current->evictable_foo );
		counters.foo_frees++;
		free_entry* next = current->next;
		free( current );
		counters.free_entry_frees++;
		current = next;
	}
	
	c->num_stored = 0;
	c->free_list = NULL;
}

void add_item( cache* c, foo* f, int key ){

	if( c->num_stored == CACHE_MAX ) {
		printf("Cache full\n");
		// check the free list
		if( c->free_list != NULL ) {
			// take the first thing in the free list
			free_entry* fe = c->free_list;
			// remove it from the free list
			if( c->free_list->next == c->free_list ) { // just single item
				c->free_list = NULL;
			} else {
				// [prev]<->[fe]<->[next]
				//           ^
				//           c->free_list
				fe->prev->next = fe->next;
				fe->next->prev = fe->prev;
				c->free_list = fe->next;
			}
			// now our free list is ok again
			printf("Can evict key %d from free list (it's in bucket %d)\n", fe->key, hash(fe->key) );
			entry* current;
			for(current = c->buckets[ hash(fe->key) ]; current->next->key != fe->key; current = current->next ) {
				printf("Checking key %d (next %d)\n", current->key, current->next->key );
				if( current->key == fe->key ) {
					break;
				}
			}
			printf("Found parent of the free_entry: key %d (next %d)\n", current->key, current->next->key );
			// free that one
			entry* to_be_freed = current->next;
			current->next = to_be_freed->next;
			
			// free the free_entry, the foo it points to and the entry in the bucket
			free( fe->evictable_foo );
			counters.foo_frees++;
			free( fe );
			counters.entry_frees++;
			free( to_be_freed );
			counters.entry_frees++;
			// fall out and carry on with inserting 
		} else {
			printf("Nothing in the free list.\n");
			return;
		}

	}
	int h = hash( key );
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

foo* get_item( cache* c, int key ) {
	
	int h = hash( key );
	for( entry* i = c->buckets[h]; i != NULL; i = i->next ) {
		printf("Get item %d check %d:%d\n", key, h, i->key);
		if( i->key == key ) {
			
			// either a foo, or a pointer to a free_entry
			if( i->refcount == 0 ) {
				// it's one on the free list, means we need to remove it from there
				free_entry* discard = i->ptr.to_free_entry;
				assert( discard != NULL );
				i->ptr.to_foo = discard->evictable_foo; // put it back in the regular entry

				// remove it from the free list
				discard->prev->next = discard->next;
				discard->next->prev = discard->prev;
				
				// if we happen to free the initial entry in the free list, set a new head
				if( c->free_list == discard ) {
					c->free_list = discard->next;
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
	
	return NULL;
	
}

void release_item( cache* c, foo* f, int key ) {

	int h = hash( key );
	for( entry* i = c->buckets[h]; i != NULL; i = i->next ) {
		printf("release %d, looking in bucket %d\n", key, h );
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
				if( c->free_list == NULL ) {
					printf("empty free_list, setting first item\n");
					new_head->next = new_head;
					new_head->prev = new_head;
				} else {
					new_head->next = c->free_list;
					new_head->prev = c->free_list->prev;
					c->free_list->prev = new_head;
					new_head->prev->next = new_head;
				}

				c->free_list = new_head;
			}
			return;
		}
	}
	
	// was not in the cache, just free it
	printf("Item %d was not in the cache, doing a normal free()\n", key);
	counters.foo_frees++;
	free( f );
}




int main() {

	cache store;
	memset( &store.buckets, 0, sizeof(store.buckets) );
	store.num_stored = 0;
	store.free_list = NULL;

	printf("Initial:\n");
	dump( &store );
	print_counters();

	printf("==== Adding keys 1-10 ====\n");
	for(int i=1; i<11; i++) {
		foo* temp = (foo*)malloc( sizeof(foo) );
		counters.foo_allocs++;
		temp->b = i;
		add_item( &store, temp, i );
	}
	dump( &store );
	print_counters();

	clear_cache( &store );
	print_counters();
	dump( &store );

	// checks
	assert( counters.foo_allocs == counters.foo_frees );
	assert( counters.entry_allocs == counters.entry_frees );
	assert( counters.free_entry_allocs == counters.free_entry_frees );

	printf("==== Adding and releasing keys 1-10 ====\n");
	for(int i=1; i<11; i++) {
		foo* temp = (foo*)malloc( sizeof(foo) );
		counters.foo_allocs++;
		temp->b = i;
		add_item( &store, temp, i );
		release_item( &store, temp, i );
	}

	dump( &store );
	clear_cache( &store );
	print_counters();

	printf("==== Adding keys 1-12 (filling the cache), releasing 1-4 ====\n");
	for(int i=1; i<13; i++) {
		foo* temp = (foo*)malloc( sizeof(foo) );
		counters.foo_allocs++;
		temp->b = i;
		add_item( &store, temp, i );
		if( i < 5 ) {
			release_item( &store, temp, i );
		}
	}

	dump( &store );

	printf("==== Retrieving keys 3,4,5,6 (should take them off the free list/and increase refcount) ====\n");
	for( int i=3; i<5; i++) {
		foo* temp = get_item( &store, i );
		assert( temp != NULL );
	}
	dump( &store );

	clear_cache( &store );
	print_counters();

	// checks
	assert( counters.foo_allocs == counters.foo_frees );
	assert( counters.entry_allocs == counters.entry_frees );
	assert( counters.free_entry_allocs == counters.free_entry_frees );

	return 0;
}