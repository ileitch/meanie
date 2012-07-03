#include <glib.h>

#define RESULT_PAD 20
#define MAX_MATCHES_PER_BLOB 30

typedef struct {
	unsigned int initial;
	unsigned int num_blobs;
} mne_search_ctx;

typedef struct {
	unsigned int offset;
} mne_indices_ctx;

void mne_search_loop();
void mne_search_cleanup();