# meanie

A tool for searching Git repositories using regular expressions. It searches all blobs reachable from HEAD and tags.

It's currently a simple REPL based search, though I'll turn it into a daemon soon and add a MessagePack interface.

### Coolness

* FAST.
* Uses all logical cores during search, without use of locks or CAS (compare-and-swap).
* Uses PCRE with JIT enabled.

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

## Brain Dump

* Currently memory is allocated on a per-blob basis. These blobs may be scattered about in main memory, inhibiting the CPUs ability to pipeline the blobs. Another approach is to allocate blobs in a contiguous fashion, a Big Blob™. A seperate data structure will hold the begin/end offsets of individual blobs. 