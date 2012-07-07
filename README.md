# meanie

A tool for searching Git repositories using regular expressions. It searches all blobs reachable from HEAD and tags.

It's currently a REPL based tool, though I'll turn it into a daemon soon and add a MessagePack interface.

### Coolness

* FAST.
* Uses all logical cores during search, without use of locks or CAS (compare-and-swap).
* Uses PCRE with its JIT enabled.

## Build

`make`

## OSX

`export DYLD_LIBRARY_PATH=/path/to/meanie/built/lib/`

`./meanie path/to/git/repo`

## Linux

`export LD_LIBRARY_PATH=/path/to/meanie/built/lib/`

`./meanie path/to/git/repo`

### Ubuntu

`apt-get install build-essential git-core pkg-config libglib2.0 cmake`

## Ideas

* Stream from disk to support very large/multiple repositories.
* Communicate using MessagePack.
* Walk branches also.
* Restrict search to specific ref.