#include <stdlib.h> // malloc
#include <stdio.h> // printf
#include <string.h> // memset
#include <assert.h>
#include <time.h> // time() for srand

typedef char bool;

#define CACHE_MEMORY_BYTES 256

#define MEMORY_PER_ITEM (sizeof(item) + sizeof(entry) + sizeof(entry*))
// TODO(chris): replace this by num buckets which is a power of 2
#define CACHE_SIZE (int)(CACHE_MEMORY_BYTES/MEMORY_PER_ITEM)

typedef struct item {
	int id;
	
	int value;

	char data[7]; // filler value to make items bigger (but still 8 byte aligned)
	bool is_dirty;

} item;

typedef struct entry {

	// TODO: I think we can union 2 of these
	// TODO: or even change them to entry indices? That would be great (and smaller)
	struct entry* next_bucket_entry;
	struct entry* prev_bucket_entry;

	struct entry* next_list_entry;
	struct entry* prev_list_entry;
	
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

static inline void free_item( item* i ) {
	
	// TODO(errors): maybe assert here? Could indicate a bug to free a NULL item
	if( i == NULL ) {
		return;
	}

	// TODO(chris): assert refcount?

	if( i->is_dirty ) {
		printf("Pretending to write dirty item to disk or something: { id = %d, value = %d }\n", i->id, i->value );		
	} else {
		printf("Freeing clean item { id = %d, value = %d }\n", i->id, i->value );
	}

	free( i );
}

static void remove_from_list( entry** list, entry* element ) {
	
	assert( *list );
	assert( element );
	
	// last element on the list
	if( element->next_list_entry == element ) {
		*list = NULL;
	} else {
		element->prev_list_entry->next_list_entry = element->next_list_entry;
		element->next_list_entry->prev_list_entry = element->prev_list_entry;
		if( *list == element ) {
			*list = element->next_list_entry;
		}
	}
	
}

// remove and return the oldest item from the clean (preferred) or dirty entry list
static entry* get_available_entry( cache* c ) {
	
	entry* target = c->available_clean_entries ? c->available_clean_entries : c->available_dirty_entries;

	// nothing available in either the clean entries or dirty entries list
	if( target == NULL ) {
		return NULL;
	}

	// move to the oldest one
	target = target->prev_list_entry;

	entry** from_list = c->available_clean_entries ? &c->available_clean_entries : &c->available_dirty_entries;;
	remove_from_list( from_list, target );
	free_item( target->item );

	return target;
}

/*
Puts [d] at the start of the list L

[a] <--> [b] <--> [c] <--> [a]
          L

[a] <--> [d] <--> [b] <--> [c] <--> [a]
          L

d next = L
d prev = L prev
L prev = d
L prev next = d

*/
static void insert_into_list( entry** list, entry* element ) {

	// could be NULL
	assert( element );
	assert( *list != element );
	
	if( *list == NULL ) {
		element->next_list_entry = element;
		element->prev_list_entry = element;
		*list = element;
	} else {
		element->next_list_entry = *list;
		element->prev_list_entry = (*list)->prev_list_entry;

		*list = element;

		(*list)->next_list_entry->prev_list_entry = element;
		(*list)->prev_list_entry->next_list_entry = element;
	}

}

/*
Puts [c] at the end of bucket
 bucket -> [a]<-->[b]<-->[a]..

 ins [c]
 [c]-> = (*b) = [a]
 <-[c] = (*b)>prev = [b]
 -><-[c] = [c] means [b]-> = [c]
 <-->[c] = [c] means <-[a] = c

 result:
	bucket -> [a]<-->[b]<-->[c]<-->[a]
*/
static void insert_into_bucket( entry** b, entry* element ) {

	printf("b=%p, *b=%p\n", b, *b);
	if( *b == NULL ) {
		// first item in this bucket
		*b = element;
		element->next_bucket_entry = element;
		element->prev_bucket_entry = element;

	} else {
		// already has items in this bucket, insert into the doubly linked list
		element->next_bucket_entry = *b;
		element->prev_bucket_entry = (*b)->prev_bucket_entry;
	
		element->prev_bucket_entry->next_bucket_entry = element;
		element->next_bucket_entry->prev_bucket_entry = element;
	}
	
}

static cache* new_cache() {
	
	cache* c = (cache*) malloc( sizeof(cache) );
	assert( c );
	
	printf("num buckets: %d\n", CACHE_SIZE );
	memset( c->buckets, 0, sizeof(c->buckets) );

	// clear entries so we never have ones that accidentally have the dirty flag set
	memset( c->entries, 0, sizeof(c->entries) );
	
	c->available_clean_entries = NULL;
	c->available_dirty_entries = NULL;
	
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
			if( current->refcount > 0 ) {
				fprintf( stderr, "\tWarning: refcount not 0 for item ID = %d\n", current->item->id );
			}			
			free_item( current->item );
			current = current->next_list_entry;
		} while( current != c->available_dirty_entries );
	}
	
}

static void print_entry( cache* c, entry* e ) {
	if( e->item == NULL ) {
		printf("\tkey %d, refcount %d (no item) [entry %ld]\n", e->key, e->refcount, e - &c->entries[0] );				
	} else {
		printf("\tkey %d, refcount %d, item { id = %d, value = %d, dirty = %s } [entry %ld]\n", e->key, e->refcount, e->item->id, e->item->value, e->item->is_dirty ? "true" : "false", e - &c->entries[0] );				
	}
	
}

static void dump_list( cache* c, const char* title, entry* head ) {
	
	printf("%s:\n", title);
	entry* current;
	int sentinel = 0;
	if( (current = head) ) {
		do {
			print_entry( c, current );
			current = current->next_list_entry;
		} while( current != head && sentinel++ < 50 );		
	}
	
}

static void dump( cache* c ) {
	
	printf("###############################\n");

	printf("Cache (size %d)\nBuckets start %p\n", CACHE_SIZE, c->buckets);
	for(int i=0; i<CACHE_SIZE; i++) {
		printf( "Bucket[%d]\n", i );
		entry* current;
		int sentinel = 0;
		if( (current = c->buckets[i]) ) {
			do {
				print_entry( c, current );
				current = current->next_bucket_entry;
			} while( current != c->buckets[i] && sentinel++ < 20 );		
		}
	}
	
	dump_list( c, "Available clean entries", c->available_clean_entries );
	dump_list( c, "Available dirty entries", c->available_dirty_entries );

	printf("###############################\n\n");
	
}

static item* get_item( cache* c, int key ) {
	
	// TODO(chris): Maybe enforce power of 2 size cache, that would also mean no mod (which is expensive)
	int b = key % CACHE_SIZE; // works if IDs are autoinc keys I think, and avoids hashing

	entry* current;
	if( (current = c->buckets[b]) ) {
		do {
			if( current->key == key ) {
				printf("Found item in cache\n");
				// remove it from the available list if it was on there
				if( current->refcount == 0 ) {
					entry** from_list = current->item->is_dirty ? &c->available_dirty_entries : &c->available_clean_entries;
					remove_from_list( from_list, current );
				}
				current->refcount++;
				return current->item;
			}
			current = current->next_bucket_entry;
		} while( current != c->buckets[b] );
	}
		
	return NULL;
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
			printf("Found item in cache bucket %d\n", b);
			assert( current->refcount > 0 );
			current->refcount--;
			if( current->refcount == 0 ) {
				// leave it in the bucket so it can be revived later (aka, this is what caches should do ;)
				entry** available_list = i->is_dirty ? &c->available_dirty_entries : &c->available_clean_entries;
				insert_into_list( available_list, current );				
			}
			return;
		}
		current = current->next_bucket_entry;
	} while( current != c->buckets[b] );
	
	printf("Item not in cache, freeing.\n");
	free_item( i );
	
}

static inline void set_entry( entry* e, item* i ) {

	e->item = i;
	e->key = i->id;
	e->refcount = 1;

}

static void add_item( cache* c, item* i ) {
	
	int b = i->id % CACHE_SIZE; // works if IDs are autoinc keys I think, and avoids hashing
	printf("Want to insert { id = %d, value = %d, is_dirty = %s } into bucket %d\n", i->id, i->value, i->is_dirty ? "true" : "false", b);

	// get an available entry
	entry* available_entry = get_available_entry( c );
	
	if( available_entry ) {

		printf("Recycled an available item (%d)\n", available_entry->item == NULL ? -1 : available_entry->item->id );
		set_entry( available_entry, i );
		insert_into_bucket( &c->buckets[b], available_entry );

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
	
	// ensure all the unused entries are pushed: order must be CACHE_SIZE, CACHE_SIZE-1, .., 1, 0
	entry* current = store->available_clean_entries;
	for(int i=CACHE_SIZE-1; i>=0; i--) {
		assert( current - &store->entries[0] == i );
		current = current->next_list_entry;
	}
	
	flush_cache( store );
	free(store);	
}

static void test_add() {
	
	printf("************** Test adding more items than fit ****************\n");
	cache* store = new_cache();
	for(int i=0; i<CACHE_SIZE*2; i++) {

		item* foo = (item*) malloc( sizeof(item) );
		foo->id = i;
		foo->value = rand() % 128;
		add_item( store, foo );
		dump( store );
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
		foo->id = i;
		foo->value = rand() % 128;
		foo->is_dirty = rand() % 2 == 0 ? true : false;
		add_item( store, foo );
		dump( store );
		release_item( store, foo );
		dump( store );
	}
	
	dump( store );
	flush_cache( store );
	
	free(store);	
}

static void test_revive() {
	
	printf("************** Test adding/releasing/getting items (so should revive items) ****************\n");
	cache* store = new_cache();

	item* foos[CACHE_SIZE];
	for(int i=0; i<CACHE_SIZE; i++) {
		foos[i] = (item*) malloc( sizeof(item) );
		foos[i]->id = i;
		foos[i]->value = rand() % 128;
		foos[i]->is_dirty = rand() % 2 == 0;
		add_item( store, foos[i] );
		release_item( store, foos[i] );
	}
	dump( store );
	for(int i=0; i<CACHE_SIZE; i++) {
		item* f = get_item( store, foos[i]->id );
		assert( f );
		assert( f == foos[i] );
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
	// test_revive();

}



