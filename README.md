# meanie

Note: this is very much a work in progress, it WILL kill kittens.

A tool for searching Git repositories using regular expressions. It searches all blobs reachable from HEAD and tags.

It's currently a simple REPL based search, though I'll turn it into a daemon soon and add a MessagePack interface.

### Coolness

* Uses all logical cores during search, without use of locks.
* Uses PCRE with its JIT enabled.

## Build

`make`

## OSX

`export DYLD_LIBRARY_PATH=/path/to/meanie/built/lib/`
`./meanie path/to/git/repo`

## Linux

`export LD_LIBRARY_PATH=/path/to/meanie/built/lib/`
`./meanie path/to/git/repo`

## Ideas

* Stream from disk to support very large/multiple repositories.
* Communicate using MessagePack.
* Walk branches also.
* Restrict search to specific ref.