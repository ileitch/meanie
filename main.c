#include "search.h"
#include "git.h"

int main(int argc, char **argv) {
  mne_git_load_blobs(argv[1]);
  mne_search_loop();
  mne_search_cleanup();
  mne_git_cleanup();

  return 0;
}