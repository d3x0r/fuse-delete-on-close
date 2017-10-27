

#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <malloc.h>
#include <errno.h>
#include <string.h>

int main( void ) {
	int file = open( "test/mmap_file", O_RDWR|O_CREAT );
	if( file >= 0 ) {
		void *map = mmap( NULL, 10000, PROT_NONE, MAP_SHARED, file, 0 );
		if( map == MAP_FAILED ) {
			printf( "map failed:%s(%d)", strerror(errno),errno );
			return 1;
		}
		printf( "map is:%p\n", map );
		if( !mmap ) {
			printf( "Failed to open mmap" );
		} else
			memcpy( map, "Hello World\n", 12 );
	}
	else
		printf( "Failed to open file: %s(%d)", strerror(errno), errno );
   return 0;
}
