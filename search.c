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
static mne_search_context *search_contexts;
static volatile int exiting = 0, threads_complete = 0;
static pcre *re = NULL; // TODO: volatile?
static pcre_extra *re_extra = NULL; // TODO: volatile?
static int *blob_sizes;
static char **sha1_index, **blob_index;

static void *mne_search(void*);
static void mne_search_ready();
static void mne_search_initialize();
static void mne_search_build_index();
static void mne_search_print_result(int, int, int);
static void mne_search_index_iter(gpointer, gpointer, gpointer);

void mne_search_cleanup() {
  int i;
  for (i = 0; i < num_cores; ++i)
    pthread_join(threads[i], NULL);

  free(threads);
  free(search_contexts);
  free(sha1_index);
  free(blob_index);
  free(blob_sizes);
}

void mne_search_loop() {
  mne_search_initialize();

  printf("\nType 'exit' to... you know what.\n");
  char *term = NULL;
  const char *error;
  int erroffset;

  while (1) {
    printf("regex: ");
    size_t term_bytes = 1024;
    getline(&term, &term_bytes, stdin);
    term[strlen(term) - 1] = 0; // remove newline.

    if (strncmp(term, "exit", 4) == 0) {
      exiting = 1;
      mne_search_ready();
      break;
    }

    re = pcre_compile(term, 0, &error, &erroffset, NULL);

    if (re == NULL) {
       printf("Regex compilation failed at offset %d: %s\n", erroffset, error);
       continue;
     }

    re_extra = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error);

    if (error != NULL) {
      printf("pcre_study() failed: %s\n", error);
      pcre_free_study(re_extra);
      re_extra = NULL;
      continue;
    }
   
    printf("\n");
    free(term);
    term = NULL;
    gettimeofday(&begin, NULL);
    threads_complete = 0;
    pthread_mutex_unlock(&all_done_mutex);
    pthread_mutex_lock(&all_done_mutex);
    mne_search_ready();
    pthread_mutex_lock(&all_done_mutex);
    gettimeofday(&end, NULL);
    printf("Done, ");
    mne_print_duration(&end, &begin);
    printf(".\n");
  }
}

static void mne_search_initialize() {
  mne_search_build_index();
  num_cores = mne_detect_logical_cores();

  threads = malloc(sizeof(pthread_t) * num_cores);
  assert(threads != NULL);
  search_contexts = malloc(sizeof(mne_search_context) * num_cores);
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
  mne_indices_context conext;
  conext.offset = 0;

  int blob_count = g_hash_table_size(blobs);
  blob_index = malloc(sizeof(char*) * blob_count);
  assert(blob_index != NULL);
  sha1_index = malloc(sizeof(char*) * blob_count);
  assert(sha1_index != NULL);
  blob_sizes = malloc(sizeof(int) * blob_count);
  assert(blob_sizes != NULL);

  g_hash_table_foreach(blobs, mne_search_index_iter, &conext);
  printf("✔\n");
}

static void mne_search_index_iter(gpointer key, gpointer value, gpointer user_data) {
  mne_indices_context *conext = (mne_indices_context *)user_data;
  sha1_index[conext->offset] = (char*)key;
  blob_index[conext->offset] = (char*)value;
  blob_sizes[conext->offset] = strlen((char*)value);
  conext->offset++;
}

static void mne_search_print_result(int index, int offset, int length) {
  char *sha1 = sha1_index[index];
  char *path = (char*)g_hash_table_lookup(paths, sha1);
  int pad_left = 0, pad_right = 0;

  while (1) {
    if (offset - pad_left <= 0)
      break;    
    pad_left++;
    if (pad_left == RESULT_PAD)
      break;
    if (blob_index[index][offset - pad_left] == '\n') {
      pad_left--;
      break;
    }
  }

  while (1) {
    if (pad_right >= blob_sizes[index])
      break;
    if (blob_index[index][offset + length + pad_right] == '\n') {
      break;
    }
    pad_right++;
    if (pad_right == RESULT_PAD)
      break;
  }

  mne_printf_async("%s\n%s:%d\n%.*s\033[1m%.*s\033[0m%.*s...\n\n", sha1_index[index], path, offset, pad_left, blob_index[index] + offset - pad_left, length, blob_index[index] + offset, pad_right, blob_index[index] + offset + length);
}

static void *mne_search(void *arg) {
  int rc, i, n = 0, matches[MAX_MATCHES_PER_BLOB];
  mne_search_context *conext = (mne_search_context *)arg;

  while (1) {
    pthread_mutex_lock(&search_mutex);
    pthread_cond_wait(&search_cond, &search_mutex);
    pthread_mutex_unlock(&search_mutex);

    if (exiting)
      break;

    for (n = conext->initial; n < conext->num_blobs; n += num_cores) {
      rc = pcre_exec(re, re_extra, blob_index[n], blob_sizes[n], 0, 0, matches, MAX_MATCHES_PER_BLOB);

      if (rc == 0)
        mne_printf_async("Too many matches in blob %s\n", sha1_index[n]);

      for (i = 0; i < rc; ++i)
        mne_search_print_result(n, matches[2*i], matches[2*i+1] - matches[2*i]);
    }
    
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