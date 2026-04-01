#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include "hash_functions.h"

#define NUM_WORKERS 11 // Total threads: 11 workers + 1 main = 12 [3]
#define TABLE_SIZE 32768 // Power of 2 for efficient indexing

// Simple Hash Table to store unique hash occurrences
typedef struct Node {
    unsigned char *hash;
    struct Node *next;
} Node;

typedef struct {
    int start, end, chunk_size;
    char **bufs;
    unsigned char **hashes;
} HashArgs;

// Worker: Computes SHA-512 hashes in parallel [4]
void *hash_worker(void *arg) {
    HashArgs *a = (HashArgs *)arg;
    for (int i = a->start; i < a->end; i++) {
        a->hashes[i] = calculate_sha512((unsigned char *)a->bufs[i], a->chunk_size);
    }
    return NULL;
}

void dedupe(char *filename, int chunk_size, char *output) {
    FILE *fp = fopen(filename, "r");
    assert(fp != NULL);
    
    // 1. Determine file size and pre-allocate [Query]
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    int n_hashes = (int)(file_size / chunk_size);
    rewind(fp);

    // Use calloc to ensure pointers are NULL for safe cleanup [5]
    unsigned char **hashes = calloc(n_hashes, sizeof(unsigned char *));
    char *chunk_data = malloc((long)n_hashes * chunk_size);
    char **bufs = malloc(n_hashes * sizeof(char *));
    int *mask = calloc(n_hashes, sizeof(int));
    assert(hashes && chunk_data && bufs && mask);

    for (int i = 0; i < n_hashes; i++)
        bufs[i] = chunk_data + (long)i * chunk_size;

    // Read all data at once to maximize I/O utilization [6]
    fread(chunk_data, chunk_size, n_hashes, fp);
    fclose(fp);

    // 2. Parallel Hashing Phase
    pthread_t threads[NUM_WORKERS];
    HashArgs hargs[NUM_WORKERS];
    int per_thread = (n_hashes + NUM_WORKERS - 1) / NUM_WORKERS;

    for (int t = 0; t < NUM_WORKERS; t++) {
        hargs[t].start = t * per_thread;
        hargs[t].end = (t + 1) * per_thread < n_hashes ? (t + 1) * per_thread : n_hashes;
        hargs[t].chunk_size = chunk_size;
        hargs[t].bufs = bufs;
        hargs[t].hashes = hashes;
        // Robust error checking for thread creation [7, 8]
        int rc = pthread_create(&threads[t], NULL, hash_worker, &hargs[t]);
        assert(rc == 0);
    }

    for (int t = 0; t < NUM_WORKERS; t++) {
        pthread_join(threads[t], NULL);
    }

    // 3. Duplicate Detection via O(N) Hash Table
    Node **table = calloc(TABLE_SIZE, sizeof(Node *));
    int hash_size = size_sha512();

    for (int i = 0; i < n_hashes; i++) {
        // Use the first 8 bytes of the hash as a bucket index
        unsigned long bucket = (*(unsigned long *)hashes[i]) % TABLE_SIZE;
        
        Node *curr = table[bucket];
        int is_duplicate = 0;
        while (curr) {
            if (memcmp(hashes[i], curr->hash, hash_size) == 0) {
                is_duplicate = 1;
                break;
            }
            curr = curr->next;
        }

        if (is_duplicate) {
            mask[i] = 1; // Mark as duplicate [2]
        } else {
            // New unique chunk: insert into table
            Node *new_node = malloc(sizeof(Node));
            assert(new_node != NULL);
            new_node->hash = hashes[i];
            new_node->next = table[bucket];
            table[bucket] = new_node;
        }
    }

    // 4. Output Results
    fp = fopen(output, "w");
    assert(fp != NULL);
    for (int i = 0; i < n_hashes; i++) fprintf(fp, "%d", mask[i]);
    fprintf(fp, "\n");
    fclose(fp);

    // 5. Clean up [5, 9]
    free(chunk_data);
    free(bufs);
    free(mask);
    for (int i = 0; i < TABLE_SIZE; i++) {
        Node *curr = table[i];
        while (curr) {
            Node *tmp = curr;
            curr = curr->next;
            free(tmp); // Free nodes, but hashes[i] is freed below
        }
    }
    free(table);
    for (int i = 0; i < n_hashes; i++) if (hashes[i]) free(hashes[i]);
    free(hashes);
}