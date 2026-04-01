#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#include "hash_functions.h"

#define NUM_THREADS 11

int compare_hashes(unsigned char *a, unsigned char *b, int n) {
	for(int i=0; i < n; i++)
		if(a[i] != b[i])
			return 0;
	return 1;
}

// thread arg structs
typedef struct { int start, end, chunk_size; char **bufs; unsigned char **hashes; } HashArgs;
typedef struct { int start, end, n_hashes, hash_size; unsigned char **hashes; int *mask; } CompareArgs;

// worker: hash each chunk in the assigned range
void *hash_worker(void *arg) {
	HashArgs *a = (HashArgs *)arg;
	for (int i = a->start; i < a->end; i++)
		a->hashes[i] = calculate_sha512((unsigned char *)a->bufs[i], a->chunk_size);
	return NULL;
}

// worker: check assigned rows for duplicates (interleaved for load balance)
void *compare_worker(void *arg) {
	CompareArgs *a = (CompareArgs *)arg;
	for (int i = a->start; i < a->n_hashes; i += NUM_THREADS)
		for (int j = i+1; j < a->n_hashes; j++)
			if (compare_hashes(a->hashes[i], a->hashes[j], a->hash_size)) {
				a->mask[j] = 1;
				break;
			}
	return NULL;
}

// Function name: dedupe
// Description:   Computes a hash for each chunk of the input file, and the obtained hashes
//                to each other to determine the number of unique chunks in the file
void dedupe(char *filename, int chunk_size, char *output) {
	FILE *fp;
	int hash_size = size_sha512(), n_hashes = 0;

	// pre-count chunks from file size, then allocate everything at once
	fp = fopen(filename, "r");
	assert(fp != NULL);
	fseek(fp, 0, SEEK_END);
	n_hashes = (int)(ftell(fp) / chunk_size);
	rewind(fp);

	unsigned char **hashes  = (unsigned char **) malloc(n_hashes * sizeof(unsigned char *));
	char *chunk_data        = (char *) malloc((long)n_hashes * chunk_size);
	char **bufs             = (char **) malloc(n_hashes * sizeof(char *));
	for (int i = 0; i < n_hashes; i++)
		bufs[i] = chunk_data + (long)i * chunk_size;

	// read all chunks in one call
	fread(chunk_data, chunk_size, n_hashes, fp);
	fclose(fp);

	int n_threads = n_hashes < NUM_THREADS ? n_hashes : NUM_THREADS;
	int chunk     = n_threads > 0 ? (n_hashes + n_threads - 1) / n_threads : 1;
	pthread_t threads[NUM_THREADS];

	// hash all chunks in parallel
	if (n_threads > 0) {
		HashArgs hargs[NUM_THREADS];
		for (int t = 0; t < n_threads; t++) {
			hargs[t].start      = t * chunk;
			hargs[t].end        = (t+1)*chunk < n_hashes ? (t+1)*chunk : n_hashes;
			hargs[t].chunk_size = chunk_size;
			hargs[t].bufs       = bufs;
			hargs[t].hashes     = hashes;
			pthread_create(&threads[t], NULL, hash_worker, &hargs[t]);
		}
		for (int t = 0; t < n_threads; t++)
			pthread_join(threads[t], NULL);
	}
	free(chunk_data);
	free(bufs);

	int mask[n_hashes];
	for(int i=0; i < n_hashes; i++)
		mask[i] = 0;

	// compare hashes in parallel
	if (n_threads > 0) {
		CompareArgs cargs[NUM_THREADS];
		for (int t = 0; t < n_threads; t++) {
			cargs[t].start     = t; // thread t handles rows t, t+NUM_THREADS, t+2*NUM_THREADS, ...
			cargs[t].end       = (t+1)*chunk < n_hashes ? (t+1)*chunk : n_hashes;
			cargs[t].n_hashes  = n_hashes;
			cargs[t].hash_size = hash_size;
			cargs[t].hashes    = hashes;
			cargs[t].mask      = mask;
			pthread_create(&threads[t], NULL, compare_worker, &cargs[t]);
		}
		for (int t = 0; t < n_threads; t++)
			pthread_join(threads[t], NULL);
	}

	// print results
	fp = fopen(output, "w");
	assert(fp != NULL);
	for(int i=0; i < n_hashes; i++)
		fprintf(fp, "%d", mask[i]);
	fprintf(fp, "\n");
	fclose(fp);

	// release stuff
	for(int i=0; i < n_hashes; i++)
		free(hashes[i]);
	free(hashes);
}
