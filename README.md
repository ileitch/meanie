# meanie

A tool for searching Git repositories with regular expressions. It searches all blobs reachable from HEAD and tags.

It's currently a simple REPL based search, though I'll turn it into a daemon soon and add a MessagePack interface.

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