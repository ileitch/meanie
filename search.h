#include <glib.h>

#define RESULT_PAD 20
#define MAX_CAPTURES 30
#define MAX_SEARCH_RESULTS_PER_THREAD 10000

typedef struct {
	unsigned int initial;
	unsigned int num_blobs;
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

void mne_search_loop();
void mne_search_cleanup();