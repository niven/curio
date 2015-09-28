Stuff that is hard to categorize

Mandelbrot.bf - Generate a Mandelbug in Brainfuck, but faster than the usual implementations ;)

refcount_cache.c - Cache for object that are refcounted (the cache can't free items that are still in use) that caches as much as possible and has O(1) operations for everything (ie, no slow search for an item to evict, no sorting, no nuthin')

stable_partition.c - Take an array and a predicate and partition the array in place keeping the original order of the elements 
