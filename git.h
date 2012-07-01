#ifndef MEANIE_GIT_H
#define MEANIE_GIT_H

#include <glib.h>
#include <git2.h>

#define MNE_MAX_REF_LENGTH 22
#define MNE_MAX_PATH_LENGTH 256
#define MNE_GIT_TARGET_NOT_COMMIT -1
#define MNE_GIT_OK 0

GHashTable *blobs;
GHashTable *paths;
GHashTable *refs;

typedef struct {
	char *ref_name;
	unsigned long bytes;
	unsigned int total_refs;
	unsigned int distinct_blobs;
} mne_git_walk_context;

void mne_git_cleanup();
void mne_git_load_blobs(const char*);

#endif