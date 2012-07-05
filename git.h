#ifndef MEANIE_GIT_H
#define MEANIE_GIT_H

#include <glib.h>
#include <git2.h>

#define MNE_MAX_PATH_LENGTH 256
#define MNE_GIT_TARGET_NOT_COMMIT -1
#define MNE_GIT_OK 0

unsigned int total_blobs;
unsigned int total_refs;
char *data;
size_t data_size;

GHashTable *paths; /* TODO: Needs a beter name. */
GHashTable *refs;

typedef struct {
	unsigned long offset;
	unsigned long length;
} mne_git_blob_position;

mne_git_blob_position *blob_index;

typedef struct {
	char *ref_name;
	unsigned int ref_index;
} mne_git_walk_ctx;

typedef struct {
	int free_key;
} mne_git_cleanup_ctx;

void mne_git_cleanup();
void mne_git_load_blobs(const char*);

#endif