#include <glib.h>

#include "git.h"

#define RESULT_PAD 20
#define MAX_SEARCH_RESULTS_PER_THREAD 1000

typedef struct {
	unsigned int id;
} mne_search_ctx;

typedef struct {
	unsigned int offset;
} mne_indices_ctx;

typedef struct {
	unsigned int fresh;
	unsigned int sha1_offset;
	unsigned int offset;
	unsigned int length;
} mne_search_result;

typedef struct {
	mne_git_blob_position *begin;
	mne_git_blob_position *end;
} mne_search_window;

void mne_search_loop();
void mne_search_cleanup();