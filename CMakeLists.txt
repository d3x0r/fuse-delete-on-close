cmake_minimum_required(VERSION 3.1)

OPTION( ENABLE_TESTS "Enable building tests" OFF )

#add_executable( fuse_doc fuse_delete_on_close.c )
#target_link_libraries( fuse_doc fuse pthread )

add_executable( fuse_doc_ll fuse_ll_delete_on_close.c )
target_link_libraries( fuse_doc_ll fuse pthread )

if( ENABLE_TESTS )
	add_subdirectory( tests )
endif()
