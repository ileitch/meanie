#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <pcre.h>
#include <assert.h>
#include <sys/time.h>

#include "util.h"
#include "git.h"
#include "search.h"
#include "common.h"

static pthread_t *threads;
static pthread_cond_t search_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t search_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t done_incr_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t all_done_mutex = PTHREAD_MUTEX_INITIALIZER;

static int num_cores;
static struct timeval begin, end;
static mne_search_ctx *search_contexts;
static mne_search_result **search_results;
static volatile int exiting = 0, threads_complete = 0;
static pcre *re = NULL; /* TODO: volatile? */
static pcre_extra *re_extra = NULL; /* TODO: volatile? */
static int *blob_sizes;
static char **sha1_index, **blob_index;

static void *mne_search(void*);
static void mne_search_ready();
static void mne_search_initialize();
static void mne_search_build_index();
static int mne_search_print_results();
static void mne_search_index_iter(gpointer, gpointer, gpointer);

void mne_search_cleanup() {
  int i;
  for (i = 0; i < num_cores; ++i) {
    pthread_join(threads[i], NULL);
    free(search_results[i]);
  }
    
  free(search_results);
  free(threads);
  free(search_contexts);
  free(sha1_index);
  free(blob_index);
  free(blob_sizes);
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
    int total = mne_search_print_results();
    if (total == MAX_SEARCH_RESULTS_PER_THREAD * num_cores)
      printf("Hit match limit!\n");
    printf("%d matches. ", total);
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
  mne_search_build_index();
  num_cores = mne_detect_logical_cores();

  search_results = malloc(sizeof(mne_search_result*) * num_cores);
  assert(search_results != NULL);

  int i, n;
  for (i = 0; i < num_cores; i++) {
    search_results[i] = malloc(sizeof(mne_search_result) * MAX_SEARCH_RESULTS_PER_THREAD);
    assert(search_results[i] != NULL);
    for (n = 0; n < MAX_SEARCH_RESULTS_PER_THREAD; n++)
      search_results[i][n].fresh = 0;
  }

  threads = malloc(sizeof(pthread_t) * num_cores);
  assert(threads != NULL);

  search_contexts = malloc(sizeof(mne_search_ctx) * num_cores);
  assert(search_contexts != NULL);

  int z, num_blobs = g_hash_table_size(blobs);

  for (z = 0; z < num_cores; z++) {
     search_contexts[z].initial = z;
     search_contexts[z].num_blobs = num_blobs;
     pthread_create(&threads[z], NULL, mne_search, (void *)&search_contexts[z]);
   }
}

static void mne_search_build_index() {
  printf("\nBuilding search index... ");
  int blob_count = g_hash_table_size(blobs);

  blob_index = malloc(sizeof(char*) * blob_count);
  assert(blob_index != NULL);
  
  sha1_index = malloc(sizeof(char*) * blob_count);
  assert(sha1_index != NULL);
  
  blob_sizes = malloc(sizeof(int) * blob_count);
  assert(blob_sizes != NULL);

  mne_indices_ctx ctx;
  ctx.offset = 0;

  g_hash_table_foreach(blobs, mne_search_index_iter, &ctx);
  printf(" ✔\n");
}

static void mne_search_index_iter(gpointer key, gpointer value, gpointer user_data) {
  mne_indices_ctx *ctx = (mne_indices_ctx *)user_data;
  sha1_index[ctx->offset] = (char*)key;
  blob_index[ctx->offset] = (char*)value;
  blob_sizes[ctx->offset] = strlen((char*)value);
  ctx->offset++;
}

static int mne_search_print_results() {
  int i, n, total_results = 0;
  for (i = 0; i < num_cores; i++) {
    for (n = 0; n < MAX_SEARCH_RESULTS_PER_THREAD; n++) {
      mne_search_result result = search_results[i][n];
      if (result.fresh == 0)
        break;

      total_results++;
      char *sha1 = sha1_index[result.sha1_offset];
      char *path = (char*)g_hash_table_lookup(paths, sha1);
      int pad_left = 0, pad_right = 0;

      while (1) {
        if (result.offset - pad_left <= 0)
          break;    
        pad_left++;
        if (pad_left == RESULT_PAD)
          break;
        if (blob_index[result.sha1_offset][result.offset - pad_left] == '\n') {
          pad_left--;
          break;
        }
      }

      while (1) {
        if (pad_right >= blob_sizes[result.sha1_offset])
          break;
        if (blob_index[result.sha1_offset][result.offset + result.length + pad_right] == '\n') {
          break;
        }
        pad_right++;
        if (pad_right == RESULT_PAD)
          break;
      }

      const char **sha1_refs = g_hash_table_lookup(refs, (gpointer)sha1_index[result.sha1_offset]);

      int i;
      for(i = 0 ; i < total_refs; i++) {
        if (sha1_refs[i] == NULL)
          break;
        printf("\033[36m%s\033[0m ", sha1_refs[i]);
      }
      printf("\n\033[1m%s:%d\033[0m\n%.*s\033[1;32m%.*s\033[0m%.*s...\n\n", path, result.offset, pad_left,
        blob_index[result.sha1_offset] + result.offset - pad_left, result.length, blob_index[result.sha1_offset] + result.offset,
        pad_right, blob_index[result.sha1_offset] + result.offset + result.length);
    }
  }

  return total_results;
}

static void *mne_search(void *_ctx) {
  int rc, i, num_results, n, matches[MAX_CAPTURES], offset;
  mne_search_ctx *ctx = (mne_search_ctx *)_ctx;
  mne_search_result *results = search_results[ctx->initial];

  while (1) {
    pthread_mutex_lock(&search_mutex);
    pthread_cond_wait(&search_cond, &search_mutex);
    pthread_mutex_unlock(&search_mutex);

    if (exiting)
      break;

    num_results = 0;

    for (n = ctx->initial; n < ctx->num_blobs; n += num_cores) {
      offset = 0;

      while (1) {
        rc = pcre_exec(re, re_extra, blob_index[n], blob_sizes[n], offset, 0, matches, MAX_CAPTURES);

        if (unlikely(rc == 0)) {
          mne_printf_async("Too many captured substrings in blob %s (> %d).\n", sha1_index[n], MAX_CAPTURES);
          continue;          
        }

        if (rc > 0) {
          for (i = 0; i < rc; ++i) {
            // TODO: These are captured substrings, they should all be part of the same result.
            results[num_results].fresh = 1;
            results[num_results].sha1_offset = n;
            results[num_results].offset = matches[2*i];
            results[num_results].length = matches[2*i+1] - matches[2*i];

            if (unlikely(num_results == MAX_SEARCH_RESULTS_PER_THREAD))
              break;

            offset = matches[2*i] + (matches[2*i+1] - matches[2*i]);
            num_results++;        
          }    
        } else {
          break;
        }

        if (unlikely(num_results == MAX_SEARCH_RESULTS_PER_THREAD))
          break;
      }

      if (unlikely(num_results == MAX_SEARCH_RESULTS_PER_THREAD)) {
        printf("Thread %d has reached maximum result count (%d).\n", ctx->initial, MAX_SEARCH_RESULTS_PER_THREAD);
        break;
      }
    }

    /* Mark the next result as unfresh so the main threads knows how many results we found. */
    if (num_results < MAX_SEARCH_RESULTS_PER_THREAD)
      results[num_results].fresh = 0;
    
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