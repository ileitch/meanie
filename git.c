#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include "util.h"
#include "git.h"

static git_odb *odb;
static git_repository *repo;

static struct timeval begin, end;
static char **ref_names;

static void mne_git_initialize();
static void mne_git_walk_head(mne_git_walk_ctx*);
static void mne_git_cleanup_iter(gpointer, gpointer, gpointer);
static void mne_git_walk_tags(mne_git_walk_ctx*, git_strarray*);
static int mne_git_get_tag_commit_oid(const git_oid**, git_tag*);
static int mne_git_tree_entry_cb(const char*, git_tree_entry*, void*);
static int mne_git_get_tag_tree(git_tree**, git_reference**, const char*);
static void mne_git_walk_tree(git_tree*, git_reference*, mne_git_walk_ctx*);

void mne_git_cleanup() {
  mne_git_cleanup_ctx ctx;
  /* The same sha1 strings are used as keys for all hashes. */
  ctx.free_key = 1;
  g_hash_table_foreach(paths, mne_git_cleanup_iter, &ctx);
  ctx.free_key = 0;
  g_hash_table_foreach(refs, mne_git_cleanup_iter, &ctx);

  int i;
  for (i = 0; i < total_refs; i++)
    free(ref_names[i]);

  free(ref_names);
  free(data);

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

  total_refs = tag_names.count + 1; /* + 1 for HEAD. */
  ref_names = malloc(sizeof(char*) * total_refs);

  mne_git_walk_ctx ctx;
  ctx.ref_index = 0;

  mne_git_walk_head(&ctx);
  mne_git_walk_tags(&ctx, &tag_names);

  git_strarray_free(&tag_names);
  git_repository_free(repo);

  gettimeofday(&end, NULL);

  float mb = data_size / 1048576.0;
  printf("\nLoaded %d blobs (%.2fmb) ", g_hash_table_size(paths), mb);
  mne_print_duration(&end, &begin);
  printf(".\n");
}

static int mne_git_tree_entry_cb(const char *root, git_tree_entry *entry, void *arg) {
  mne_git_walk_ctx *ctx = (mne_git_walk_ctx*)arg;
  git_otype type = git_tree_entry_type(entry);
  
  if (type == GIT_OBJ_BLOB) {
    const git_oid *blob_oid = git_tree_entry_id(entry);
    char *sha1 = malloc(sizeof(char) * GIT_OID_HEXSZ + 1);
    assert(sha1 != NULL);
    git_oid_tostr(sha1, GIT_OID_HEXSZ + 1, blob_oid);

    git_odb_object *blob_odb_object;
    git_odb_read(&blob_odb_object, odb, blob_oid);

    char **sha1_refs;

    if (!g_hash_table_contains(paths, (gpointer)sha1)) {
      total_blobs++;

      size_t blob_size = git_odb_object_size(blob_odb_object);
      data = realloc(data, data_size + blob_size);
      assert(data != NULL);
      memcpy(data + data_size, git_odb_object_data(blob_odb_object), blob_size);
      
      blob_index = realloc(blob_index, sizeof(mne_git_blob_position) * total_blobs);
      assert(blob_index != NULL);

      blob_index[total_blobs-1].offset = data_size;
      blob_index[total_blobs-1].length = blob_size;

      data_size += blob_size;

      assert((strlen(root) + strlen(git_tree_entry_name(entry))) < MNE_MAX_PATH_LENGTH);
      char *path = malloc(sizeof(char) * MNE_MAX_PATH_LENGTH);
      assert(path != NULL);
      strcpy(path, root);
      strcat(path, git_tree_entry_name(entry));

      g_hash_table_insert(paths, (gpointer)sha1, (gpointer)path);

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

      sha1_refs[i] = ctx->ref_name;
      break;
    }
  }

  return GIT_OK;
}

static int mne_git_get_tag_commit_oid(const git_oid **tag_commit_oid, git_tag* tag) {
  git_object *tag_object;
  int err = git_tag_peel(&tag_object, tag);
  mne_check_error("git_tag_peel()", err, __FILE__, __LINE__);

  const git_otype type = git_object_type(tag_object);

  if (type != GIT_OBJ_COMMIT)
    return MNE_GIT_TARGET_NOT_COMMIT;

  *tag_commit_oid = git_object_id(tag_object);
  assert(tag_commit_oid != NULL);

  git_object_free(tag_object);

  return MNE_GIT_OK;
}

static int mne_git_get_tag_tree(git_tree **tag_tree, git_reference **tag_ref, const char *ref_name) {
  int err = git_reference_lookup(tag_ref, repo, ref_name);
  mne_check_error("git_reference_lookup()", err, __FILE__, __LINE__);

  const git_oid *tag_oid = git_reference_oid(*tag_ref);
  assert(tag_oid != NULL);

  git_tag *tag;
  err = git_tag_lookup(&tag, repo, tag_oid);
  const git_oid *tag_commit_oid;

  if (err == GIT_ENOTFOUND) {
    /* Not a tag, must be a commit. */
    tag_commit_oid = tag_oid;
  } else {
    err = mne_git_get_tag_commit_oid(&tag_commit_oid, tag);
    git_tag_free(tag);
    if (err != GIT_OK)
      return err;
  }

  git_commit *tag_commit;
  err = git_commit_lookup(&tag_commit, repo, tag_commit_oid);
  mne_check_error("git_commit_lookup()", err, __FILE__, __LINE__);

  err = git_commit_tree(tag_tree, tag_commit);
  mne_check_error("git_commit_tree()", err, __FILE__, __LINE__);

  git_commit_free(tag_commit);

  return MNE_GIT_OK;
}

static void mne_git_walk_tree(git_tree *tree, git_reference *ref, mne_git_walk_ctx *ctx) {
  int ref_name_len = strlen(git_reference_name(ref));
  ctx->ref_name = malloc(sizeof(char) * (ref_name_len + 1));
  assert(ctx->ref_name != NULL);
  strncpy(ctx->ref_name, git_reference_name(ref), ref_name_len);
  ctx->ref_name[ref_name_len] = 0;

  ref_names[ctx->ref_index] = ctx->ref_name;

  printf(" * %-22s", ctx->ref_name);
  fflush(stdout);
  unsigned int blobs_before = total_blobs;
  git_tree_walk(tree, &mne_git_tree_entry_cb, GIT_TREEWALK_POST, ctx);
  printf(" ✔ +%d\n", total_blobs - blobs_before);
}

static void mne_git_initialize() {
  data = NULL;
  blob_index = NULL;
  data_size = 0;
  total_refs = 0;
  total_blobs = 0;
  paths = g_hash_table_new(g_str_hash, g_str_equal);
  refs = g_hash_table_new(g_str_hash, g_str_equal);  
}

static void mne_git_walk_head(mne_git_walk_ctx *ctx) {
  git_reference *head_ref;

  int err = git_repository_head(&head_ref, repo);
  mne_check_error("git_repository_head()", err, __FILE__, __LINE__);

  const git_oid *head_oid = git_reference_oid(head_ref);

  git_commit *head_commit;
  git_commit_lookup(&head_commit, repo, head_oid);

  git_tree *head_tree;
  git_commit_tree(&head_tree, head_commit);
  git_commit_free(head_commit);

  mne_git_walk_tree(head_tree, head_ref, ctx);

  git_tree_free(head_tree);
  git_reference_free(head_ref);
}

static void mne_git_walk_tags(mne_git_walk_ctx *ctx, git_strarray *tag_names) {
  int i, err;
  for (i = 0; i < tag_names->count; i++) {
    git_tree *tag_tree;
    git_reference *tag_ref;

    if ((err = mne_git_get_tag_tree(&tag_tree, &tag_ref, tag_names->strings[i])) == MNE_GIT_TARGET_NOT_COMMIT) {
      printf(" ! %s does not target a commit? Skipping.\n", git_reference_name(tag_ref));
      continue;
    }

    ctx->ref_index++;
    mne_git_walk_tree(tag_tree, tag_ref, ctx);
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