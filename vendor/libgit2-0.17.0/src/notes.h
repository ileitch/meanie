/*
 * Copyright (C) 2009-2012 the libgit2 contributors
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */
#ifndef INCLUDE_note_h__
#define INCLUDE_note_h__

#include "common.h"

#include "git2/oid.h"

#define GIT_NOTES_DEFAULT_REF "refs/notes/commits"

#define GIT_NOTES_DEFAULT_MSG_ADD \
	"Notes added by 'git_note_create' from libgit2"

#define GIT_NOTES_DEFAULT_MSG_RM \
	"Notes removed by 'git_note_remove' from libgit2"

struct git_note {
	git_oid oid;

	char *message;
};

#endif /* INCLUDE_notes_h__ */
