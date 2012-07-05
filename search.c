#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <pcre.h>
#include <assert.h>
#include <sys/time.h>

#include "util.h"
#include "git.h"
#include "search.h"

static pthread_t *threads;
static pthread_cond_t search_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t search_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t done_incr_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t all_done_mutex = PTHREAD_MUTEX_INITIALIZER;

static int num_cores;
static struct timeval begin, end;
static mne_search_ctx *search_contexts;
static mne_search_result **search_results;
static mne_search_window *search_windows;
static volatile int exiting = 0, threads_complete = 0;
static pcre *re = NULL; /* TODO: volatile? */
static pcre_extra *re_extra = NULL; /* TODO: volatile? */

static void *mne_search(void*);
static void mne_search_ready();
static void mne_search_initialize();
// static void mne_search_print_results();

void mne_search_cleanup() {
  int i;
  for (i = 0; i < num_cores; ++i) {
    pthread_join(threads[i], NULL);
    free(search_results[i]);
  }
    
  free(search_windows);
  free(search_results);
  free(threads);
  free(search_contexts);
}

void mne_search_loop() {
  char *term = NULL;
  const char *error;
  int erroffset;

  mne_search_initialize();
  printf("\nType 'exit' to... you know what.\n");

  while (1) {
    printf("regex: ");
    size_t term_bytes = 1024;
    getline(&term, &term_bytes, stdin);
    term[strlen(term) - 1] = 0; /* Remove newline. */

    if (strlen(term) == 0) {
      free(term);
      term = NULL;
      continue;
    }

    if (strncmp(term, "exit", 4) == 0) {
      exiting = 1;
      mne_search_ready();
      free(term);
      term = NULL;
      break;
    }

    re = pcre_compile(term, 0, &error, &erroffset, NULL);

    if (re == NULL) {
       printf("Regex compilation failed at offset %d: %s\n", erroffset, error);
       free(term);
       term = NULL;
       continue;
     }

    re_extra = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error);

    if (error != NULL) {
      printf("pcre_study() failed: %s\n", error);
      pcre_free_study(re_extra);
      re_extra = NULL;
      pcre_free(re);
      free(term);
      term = NULL;
      continue;
    }
   
    printf("\n");
    gettimeofday(&begin, NULL);
    threads_complete = 0;
    pthread_mutex_unlock(&all_done_mutex);
    pthread_mutex_lock(&all_done_mutex);
    mne_search_ready();
    pthread_mutex_lock(&all_done_mutex);
    gettimeofday(&end, NULL);
    // mne_search_print_results();
    printf("Done, ");
    mne_print_duration(&end, &begin);
    printf(".\n");

    free(term);
    term = NULL;
    pcre_free(re);
    if (re_extra != NULL)
      pcre_free_study(re_extra);
  }
}

static void mne_search_initialize() {
  num_cores = mne_detect_logical_cores();
  
  int total_blobs = g_hash_table_size(paths);
  int extra;  
  for (extra = 0; (total_blobs - extra) % num_cores != 0; extra++);
  size_t window_size = (total_blobs - extra) / num_cores;
  size_t last_window_size = window_size + extra;

  search_windows = malloc(sizeof(mne_search_window) * num_cores);
  assert(search_windows != NULL);

  search_results = malloc(sizeof(mne_search_result*) * num_cores);
  assert(search_results != NULL);

  int i, n;
  for (i = 0; i < num_cores; i++) {
    if (i == 0) {
      search_windows[i].begin = blob_index + (i * window_size);
    } else {
      search_windows[i].begin = blob_index + (i * window_size) + 1;
    }

    if (i == num_cores - 1) {
      search_windows[i].end = blob_index + (i * window_size) + last_window_size;  
    } else {
      search_windows[i].end = blob_index + (i * window_size) + window_size;  
    }

    search_results[i] = malloc(sizeof(mne_search_result) * MAX_SEARCH_RESULTS_PER_THREAD);
    assert(search_results[i] != NULL);

    for (n = 0; n < MAX_SEARCH_RESULTS_PER_THREAD; n++)
      search_results[i][n].fresh = 0;
  }

  threads = malloc(sizeof(pthread_t) * num_cores);
  assert(threads != NULL);

  search_contexts = malloc(sizeof(mne_search_ctx) * num_cores);
  assert(search_contexts != NULL);

  int z;
  for (z = 0; z < num_cores; z++) {
     search_contexts[z].id = z;
     pthread_create(&threads[z], NULL, mne_search, (void *)&search_contexts[z]);
   }
}

// static void mne_search_print_results() {
//   int i, n;
//   for (i = 0; i < num_cores; i++) {
//     for (n = 0; n < MAX_SEARCH_RESULTS_PER_THREAD; n++) {
//       mne_search_result result = search_results[i][n];
//       if (result.fresh == 0)
//         break;

//       char *sha1 = sha1_index[result.sha1_offset];
//       char *path = (char*)g_hash_table_lookup(paths, sha1);
//       int pad_left = 0, pad_right = 0;

//       while (1) {
//         if (result.offset - pad_left <= 0)
//           break;    
//         pad_left++;
//         if (pad_left == RESULT_PAD)
//           break;
//         if (blob_index[result.sha1_offset][result.offset - pad_left] == '\n') {
//           pad_left--;
//           break;
//         }
//       }

//       while (1) {
//         if (pad_right >= blob_sizes[result.sha1_offset])
//           break;
//         if (blob_index[result.sha1_offset][result.offset + result.length + pad_right] == '\n') {
//           break;
//         }
//         pad_right++;
//         if (pad_right == RESULT_PAD)
//           break;
//       }

//       const char **sha1_refs = g_hash_table_lookup(refs, (gpointer)sha1_index[result.sha1_offset]);

//       int i;
//       for(i = 0 ; i < total_refs; i++) {
//         if (sha1_refs[i] == NULL)
//           break;
//         printf("\033[36m%s\033[0m ", sha1_refs[i]);
//       }
//       printf("\n\033[1m%s:%d\033[0m\n%.*s\033[1;32m%.*s\033[0m%.*s...\n\n", path, result.offset, pad_left,
//         blob_index[result.sha1_offset] + result.offset - pad_left, result.length, blob_index[result.sha1_offset] + result.offset,
//         pad_right, blob_index[result.sha1_offset] + result.offset + result.length);
//     }
//   }
// }

static void *mne_search(void *_ctx) {
  int rc, i, matches[MAX_SEARCH_RESULTS_PER_THREAD];
  mne_search_ctx *ctx = (mne_search_ctx *)_ctx;
  mne_search_result *results = search_results[ctx->id];
  mne_search_window window = search_windows[ctx->id];

  while (1) {
    pthread_mutex_lock(&search_mutex);
    pthread_cond_wait(&search_cond, &search_mutex);
    pthread_mutex_unlock(&search_mutex);

    if (exiting)
      break;

    unsigned int begin = window.begin->offset;
    unsigned int length = (window.end->offset + window.end->length) - begin;
    
    printf("%ld - %ld\n", window.begin->offset, window.end->offset + window.end->length);

    rc = pcre_exec(re, re_extra, data + begin, length, 0, 0, matches, MAX_SEARCH_RESULTS_PER_THREAD);

    if (rc == 0)
      mne_printf_async("Too many matches in range %ld - %ld (> %d).\n", begin, end, MAX_SEARCH_RESULTS_PER_THREAD);

    if (rc > 0) {
      for (i = 0; i < rc; ++i) {
        printf("%.*s\n", matches[2*i+1] - matches[2*i], data + matches[2*i]);
        // printf(".");
        results[i].fresh = 1;
        // results[i].sha1_offset = n;
        results[i].offset = matches[2*i];
        results[i].length = matches[2*i+1] - matches[2*i];
      }

      rc = 0;
    }

    /* Mark the next result as unfresh so the main threads knows how many results we found. */
    if (rc < MAX_SEARCH_RESULTS_PER_THREAD)
      results[rc].fresh = 0;

    pthread_mutex_lock(&done_incr_mutex);
    threads_complete++;
    if (threads_complete == num_cores)
      pthread_mutex_unlock(&all_done_mutex);
    pthread_mutex_unlock(&done_incr_mutex);
  }

  pthread_exit(NULL);
}

static void mne_search_ready() {
  pthread_mutex_lock(&search_mutex);
  pthread_cond_broadcast(&search_cond);
  pthread_mutex_unlock(&search_mutex);
}