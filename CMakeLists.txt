cmake_minimum_required(VERSION 3.1)


add_executable( fuse_doc fuse_delete_on_close.c )
target_link_libraries( fuse_doc fuse pthread )

