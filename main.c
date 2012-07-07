#include <pcre.h>
#include <stdio.h>

#include "search.h"
#include "git.h"

int main(int argc, char **argv) {
  int rc;
  pcre_config(PCRE_CONFIG_JIT, &rc);
  if (!rc) {
    printf("ERROR: PCRE not compiled with JIT?\n");
    exit(1);
  }

  mne_git_load_blobs(argv[1]);
  mne_search_loop();
  mne_search_cleanup();
  mne_git_cleanup();

  return 0;
} 