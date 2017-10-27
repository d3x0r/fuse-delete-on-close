
# Fuse-delete-on-close

Mounts a FUSE file system that is backed by some other directory.
Files that are created/opened in the fuse mount point are automatically 
deleted when all open references are closed.

The file is actually created in a separate directory, and all file operations
are relayed to the files in the storage directory.

## Command line arguments

`fuse_doc <mount point> <storage directory>`


