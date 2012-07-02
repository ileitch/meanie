#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include "util.h"
#include "git.h"

static git_odb *odb;
static git_repository *repo;

static int total_refs;
static struct timeval begin, end;
static char **ref_names;

static void mne_git_initialize();
static void mne_git_walk_head(mne_git_walk_context*);
static void mne_git_walk_tags(mne_git_walk_context*, git_strarray*);
static int mne_git_tree_entry_cb(const char*, git_tree_entry*, void*);
static int mne_git_get_tag_tree(git_tree**, git_reference**, const char*);
static void mne_git_walk_tree(git_tree*, git_reference*, mne_git_walk_context*);
static void mne_git_cleanup_iter(gpointer, gpointer, gpointer);

void mne_git_cleanup() {
  mne_git_cleanup_ctx ctx;
  // The same sha1 strings are used as keys for all hashes.
  ctx.free_key = 1;
  g_hash_table_foreach(blobs, mne_git_cleanup_iter, &ctx);
  ctx.free_key = 0;
  g_hash_table_foreach(paths, mne_git_cleanup_iter, &ctx);
  g_hash_table_foreach(refs, mne_git_cleanup_iter, &ctx);

  int i;
  for (i = 0; i < total_refs; i++)
    free(ref_names[i]);

  free(ref_names);

  g_hash_table_destroy(blobs);
  g_hash_table_destroy(paths);
  g_hash_table_destroy(refs);
}

void mne_git_load_blobs(const char *path) {
  mne_git_initialize();

  printf("\nLoading blobs...\n\n");
  gettimeofday(&begin, NULL);

  git_repository_open(&repo, path);
  git_repository_odb(&odb, repo);

  git_strarray tag_names;
  git_tag_list(&tag_names, repo);

  mne_git_walk_context context;
  context.bytes = 0;
  ref_names = malloc(sizeof(char*) * (tag_names.count + 1)); // + 1 for HEAD. 

  mne_git_walk_head(&context);
  mne_git_walk_tags(&context, &tag_names);

  git_strarray_free(&tag_names);
  git_repository_free(repo);
  git_odb_free(odb);

  gettimeofday(&end, NULL);

  int total_blobs = g_hash_table_size(blobs);
  float mb = context.bytes / 1048576.0;
  printf("Loaded %d blobs (%.2fmb) ", total_blobs, mb);
  mne_print_duration(&end, &begin);
  printf(".\n");
}

static int mne_git_tree_entry_cb(const char *root, git_tree_entry *entry, void *arg) {
  mne_git_walk_context *p = (mne_git_walk_context*)arg;
  git_otype type = git_tree_entry_type(entry);
  
  if (type == GIT_OBJ_BLOB) {
    const git_oid *blob_oid = git_tree_entry_id(entry);
    char *sha1 = malloc(sizeof(char) * GIT_OID_HEXSZ+1);
    assert(sha1 != NULL);
    git_oid_tostr(sha1, GIT_OID_HEXSZ+1, blob_oid);

    git_odb_object *blob_odb_object;
    git_odb_read(&blob_odb_object, odb, blob_oid);

    gpointer blob = g_hash_table_lookup(blobs, (gpointer)sha1);

    char **sha1_refs;

    if (blob == NULL) {
      p->distinct_blobs++;
      char *tmp_data = (char*)git_odb_object_data(blob_odb_object);
      int data_len = strlen(tmp_data);
      char *data = malloc(sizeof(char) * data_len+1);
      memcpy(data, tmp_data, data_len);
      data[data_len] = 0;

      assert((strlen(root) + strlen(git_tree_entry_name(entry))) < MNE_MAX_PATH_LENGTH);
      char *path = malloc(sizeof(char) * MNE_MAX_PATH_LENGTH);
      assert(path != NULL);
      strcpy(path, root);
      strcat(path, git_tree_entry_name(entry));

      // TOOD: Check that the blob <-> path mapping is 1-1.
      g_hash_table_insert(paths, (gpointer)sha1, (gpointer)path);
      g_hash_table_insert(blobs, (gpointer)sha1, (gpointer)data);

      p->bytes += (unsigned long)(sizeof(char) * strlen(data));  

      sha1_refs = malloc(sizeof(char*) * total_refs);
      assert(sha1_refs != NULL);
      int i;
      for (i = 0; i < total_refs; i++)
        sha1_refs[i] = NULL;

      g_hash_table_insert(refs, (gpointer)sha1, (gpointer)sha1_refs);
    } else {
      sha1_refs = g_hash_table_lookup(refs, (gpointer)sha1);
      free(sha1);
    }

    git_odb_object_free(blob_odb_object);

    int i;
    for (i = 0; i < total_refs; i++) {
      if (sha1_refs[i] != NULL)
        continue;

      sha1_refs[i] = p->ref_name;
      break;
    }
  }

  return GIT_OK;
}

static int mne_git_get_tag_tree(git_tree **tag_tree, git_reference **tag_ref, const char *ref_name) {
  int err = git_reference_lookup(tag_ref, repo, ref_name);
  mne_check_error("git_reference_lookup()", err, __FILE__, __LINE__);

  const git_oid *tag_oid = git_reference_oid(*tag_ref);
  assert(tag_oid != NULL);

  git_tag *tag;
  err = git_tag_lookup(&tag, repo, tag_oid);
  mne_check_error("git_tag_lookup()", err, __FILE__, __LINE__);

  git_object *tag_object;
  err = git_tag_peel(&tag_object, tag);
  mne_check_error("git_tag_peel()", err, __FILE__, __LINE__);

  const git_otype type = git_object_type(tag_object);

  if (type != GIT_OBJ_COMMIT)
    return MNE_GIT_TARGET_NOT_COMMIT;

  const git_oid *tag_commit_oid = git_object_id(tag_object);
  assert(tag_commit_oid != NULL);

  git_object_free(tag_object);

  git_commit *tag_commit;
  err = git_commit_lookup(&tag_commit, repo, tag_commit_oid);
  mne_check_error("git_commit_lookup()", err, __FILE__, __LINE__);

  err = git_commit_tree(tag_tree, tag_commit);
  mne_check_error("git_commit_tree()", err, __FILE__, __LINE__);

  git_commit_free(tag_commit);

  return MNE_GIT_OK;
}

static void mne_git_walk_tree(git_tree *tree, git_reference *ref, mne_git_walk_context *context) {
  context->distinct_blobs = 0;
  assert(strlen(git_reference_name(ref)) < MNE_MAX_REF_LENGTH);
  context->ref_name = malloc(sizeof(char) * MNE_MAX_REF_LENGTH);
  ref_names[total_refs] = context->ref_name;
  assert(context->ref_name != NULL);
  strncpy(context->ref_name, git_reference_name(ref), MNE_MAX_REF_LENGTH);

  total_refs++;

  printf(" * %-22s", context->ref_name);
  fflush(stdout);
  git_tree_walk(tree, &mne_git_tree_entry_cb, GIT_TREEWALK_POST, context);
  printf("✔ +%d\n", context->distinct_blobs);
}

static void mne_git_initialize() {
  total_refs = 0;
  blobs = g_hash_table_new(g_str_hash, g_str_equal);
  paths = g_hash_table_new(g_str_hash, g_str_equal);
  refs = g_hash_table_new(g_str_hash, g_str_equal);  
}

static void mne_git_walk_head(mne_git_walk_context *context) {
  git_reference *head_ref;
  int err = git_repository_head(&head_ref, repo);

  if (err != 0) {
    printf("git_repository_head() returned %d", err);
    exit(1);
  }

  const git_oid *head_oid = git_reference_oid(head_ref);

  git_commit *head_commit;
  git_commit_lookup(&head_commit, repo, head_oid);

  git_tree *head_tree;
  git_commit_tree(&head_tree, head_commit);
  git_commit_free(head_commit);

  mne_git_walk_tree(head_tree, head_ref, context);

  git_tree_free(head_tree);
  git_reference_free(head_ref);
}

static void mne_git_walk_tags(mne_git_walk_context *context, git_strarray *tag_names) {
  int i, err;
  for (i = 0; i < tag_names->count; i++) {
    git_tree *tag_tree;
    git_reference *tag_ref;

    err = mne_git_get_tag_tree(&tag_tree, &tag_ref, tag_names->strings[i]);
    if (err == MNE_GIT_TARGET_NOT_COMMIT) {
      printf(" ! %s does not target a commit? Skipping.\n", git_reference_name(tag_ref));
      continue;
    }

    mne_git_walk_tree(tag_tree, tag_ref, context);
    git_tree_free(tag_tree);
    git_reference_free(tag_ref);
  }
}

static void mne_git_cleanup_iter(gpointer key, gpointer value, gpointer args) {
  mne_git_cleanup_ctx *ctx = (mne_git_cleanup_ctx*)args;
  if (ctx->free_key)
    free(key);

  free(value);
}