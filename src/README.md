# Dedupe

## Optimizations

- Buffered output files
  - Reduces write calls to operating system
  - Small memory overhead
- Use linear probing instead of a linked list
  - Previously, some runs could result in > 30000 bucket collisions
  - Reduces cache misses by reducing random access and increasing locality (enables prefetching)
  - Improve miss rate by dynamically sizing the hash table based on file size
- Misc
  - Stop freeing memory at the end of the program, the OS will do it for us
