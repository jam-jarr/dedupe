#include "hash_functions.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_WORKERS 11 // Total threads: 11 workers + 1 main = 12 [3]

typedef struct {
    int start, end, chunk_size;
    char **bufs;
    unsigned char **hashes;
} HashArgs;

// Worker: Computes SHA-512 hashes in parallel [4]
void *hash_worker(void *arg) {
    HashArgs *a = (HashArgs *)arg;
    for (int i = a->start; i < a->end; i++) {
        a->hashes[i] =
            calculate_sha512((unsigned char *)a->bufs[i], a->chunk_size);
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
    size_t items_read = fread(chunk_data, chunk_size, n_hashes, fp);
    if (items_read < n_hashes)
        n_hashes = items_read;
    fclose(fp);

    // 2. Parallel Hashing Phase
    pthread_t threads[NUM_WORKERS];
    HashArgs hargs[NUM_WORKERS];
    int per_thread = (n_hashes + NUM_WORKERS - 1) / NUM_WORKERS;

    for (int t = 0; t < NUM_WORKERS; t++) {
        hargs[t].start = t * per_thread;
        hargs[t].end =
            (t + 1) * per_thread < n_hashes ? (t + 1) * per_thread : n_hashes;
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

    free(chunk_data);
    free(bufs);

    // 3. Duplicate Detection via Linear Probing
    int hash_size = size_sha512();
    int table_size = 1;
    while (table_size < 2 * n_hashes)
        table_size *= 2;

    unsigned char **table = calloc(table_size, sizeof(unsigned char *));
    assert(table);

    for (int i = 0; i < n_hashes; i++) {
        unsigned long bucket;
        memcpy(&bucket, hashes[i], sizeof(bucket));
        bucket %= table_size;

        while (table[bucket]) {
            if (memcmp(hashes[i], table[bucket], hash_size) == 0) {
                mask[i] = 1;
                break;
            }
            bucket = (bucket + 1) & (table_size - 1);
        }
        if (!table[bucket]) {
            table[bucket] = hashes[i];
        }
    }

    free(table);

    // 4. Output Results
    // Buffer output to reduce write calls to the OS
    fp = fopen(output, "w");
    assert(fp != NULL);
    char *out_buf = malloc(n_hashes + 1);
    assert(out_buf);
    for (int i = 0; i < n_hashes; i++)
        out_buf[i] = mask[i] ? '1' : '0';
    out_buf[n_hashes] = '\n';
    fwrite(out_buf, 1, n_hashes + 1, fp);
    fclose(fp);
    // no need to free anything past this point
    // the OS will do it for us, and probably faster (less interrupts)
}
