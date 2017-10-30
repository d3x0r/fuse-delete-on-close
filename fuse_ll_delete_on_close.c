#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>

#ifndef min
#  define min(a,b) (((a)<(b))?(a):(b))
#endif

#define BUILD_TEST

#define DEBUG_OPERATIONS

#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64

#include <fuse.h>
#ifndef __ANDROID__
#include <fuse/fuse_lowlevel.h>
#else
#include <fuse_lowlevel.h>
#endif

struct open_file {
	char *name;
	char *source_name;
   int references;
	struct fuse_entry_param fep;
   struct fuse_file_info ffi;
   int handle;
   //int storage_handle;
	struct open_file *next, **me;
   struct open_file *children;
};

static struct fuse_private_local
{
	struct fuse_chan* mount;
	struct fuse_session *session;
	const char *mount_point;
   const char *source_dir;
	pid_t myself;
	uid_t uid;
	gid_t gid;
	pthread_t thread;
	fuse_ino_t current_ino;
	struct open_file *files;
   int pair[2];
} fpl;

static struct open_file *getFile( struct open_file *root, const char *name, const char *source ) {
	struct open_file *file;
	for( file = fpl.files; file; file = file->next ) {
		if( strcmp( file->name, name ) == 0 )
         break;
	}
	if( !file && source ) {
		file = malloc( sizeof( struct open_file ) );
      file->references = 0;
		file->name = strdup( name );
      file->source_name = strdup( source );
		file->fep.ino = fpl.current_ino++;
      file->fep.generation = 1; // ino's are always unique.
		file->fep.attr.st_dev = 0;
		file->fep.attr.st_ino = file->fep.ino;
		file->fep.attr.st_mode = S_IFREG;
		file->fep.attr.st_nlink = 1;
		file->fep.attr.st_uid = 0;
		file->fep.attr.st_gid = 0;
		file->fep.attr.st_rdev = 0;
		file->fep.attr.st_size = 0;
		file->fep.attr.st_atime = 0;
		file->fep.attr.st_mtime = 0;
		file->fep.attr.st_ctime = 0;
		file->fep.attr.st_blksize = 4096;
		file->fep.attr.st_blocks = 0;
		file->fep.attr_timeout = 0;
		file->fep.entry_timeout = 0;
		file->ffi.flags = 0; // open/release flags
		//file->ffi.writepage =
		file->ffi.fh = -1;
		file->ffi.direct_io = 1;
		file->ffi.keep_cache = 0;
		file->handle = -1;
		if( file->next = fpl.files )
			fpl.files->me = &file->next;
      file->me = &fpl.files;
      fpl.files = file;
	}
   return file;
}

static struct open_file * getFileByIno( struct open_file *root, fuse_ino_t ino ) {
   struct open_file *file;
	for( file = fpl.files; file; file = file->next ) {
		if( file->fep.ino == ino )
         break;
	}
   return file;
}

static void closeFile( struct open_file *file ) {
	file->references--;
	if( !file->references ) {
		close( file->handle );
      unlink( file->source_name );
		file->me[0] = file->next;
		free( file->name );
		free( file->source_name );
      free( file );

	}
}

static int doStat(fuse_ino_t ino, struct stat *attr, double *timeout)
{
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "request attr %ld", ino );
#endif
	memset( attr, 0, sizeof( *attr ) );
	attr->st_dev = 1;
	attr->st_nlink = 1;
	attr->st_ino = ino;
	attr->st_uid = fpl.uid;
	attr->st_gid = fpl.gid;
	attr->st_size = 1234;
	attr->st_blocks = 4321;
	attr->st_blksize = 1;
	if( ino == 1 ) {
		attr->st_mode = S_IFDIR | 0700;
		(*timeout) = 10000.0;
	}
	else {
		attr->st_mode = S_IFREG | 0600;
		(*timeout) = 1.0;
	}
	return 0;
}


static void doc_ll_getattr(fuse_req_t req, fuse_ino_t ino,
                             struct fuse_file_info *fi)
{
	struct stat stbuf;
	double timeout;
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "getattr %d\n", ino );
#endif
	if( ino == 1 )
		stat( fpl.source_dir, &stbuf );
	else {
		struct open_file *file = getFileByIno( fpl.files, ino );
		if( !file ) {
			fuse_reply_err(req, ENOENT);
			return;
		} else {
			stat( file->source_name, &stbuf );
		}
	}
	fuse_reply_attr(req, &stbuf, 10 );
}
static void doc_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	const struct fuse_ctx *ctx = fuse_req_ctx(req);
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "lookup from %d  %d %ld %s\n", ctx->pid, fpl.myself, parent, name );
#endif
	struct open_file *root = fpl.files;
   struct open_file *file = NULL;
	if( parent == 1 ) {
      file = getFile( fpl.files, name, NULL );
	} else {
		root = getFileByIno( fpl.files, parent );
      file = getFile( root, name, NULL );
	}

	if( !file || file->references == 0 ) {
#ifdef DEBUG_OPERATIONS
		fprintf( stderr, "fail?\n" );
#endif
		fuse_reply_err(req, ENOENT);
	} else {
		struct fuse_entry_param e;
#ifdef DEBUG_OPERATIONS
		fprintf( stderr, "lookup ok?\n" );
#endif
		memset(&e, 0, sizeof(e));
		e.ino = file->fep.ino;;
      stat( file->source_name, &e.attr );
		e.attr_timeout = 0;
		e.entry_timeout = 0;
		fuse_reply_entry(req, &e);
	}
}
struct dirbuf {
		  char *p;
		  size_t size;
};
static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
					        fuse_ino_t ino)
{
		  struct stat stbuf;
		  size_t oldsize = b->size;
		  b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
		  b->p = (char *) realloc(b->p, b->size);
		  memset(&stbuf, 0, sizeof(stbuf));
		  stbuf.st_ino = ino;
		  fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
					           b->size);
}

//#define min(x, y) ((x) < (y) ? (x) : (y))
static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
					              off_t off, size_t maxsize)
{
	if (off < bufsize)
	{
		return fuse_reply_buf(req, buf + off,
									 min(bufsize - off, maxsize));
	}
	else
		return fuse_reply_buf(req, NULL, 0);
}


static void doc_ll_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "opendir...\n" );
#endif
	fi->fh = (uintptr_t)malloc( sizeof( struct dirbuf ) );
	memset((void*)fi->fh, 0, sizeof(struct dirbuf));
	fuse_reply_open( req, fi );
}

static void doc_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
					              off_t off, struct fuse_file_info *fi)
{
	struct dirbuf *b = (struct dirbuf*)fi->fh;
	struct open_file *root;
	if( ino == 1 ) root = fpl.files;
   else root = getFileByIno( fpl.files, ino );
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "readdir... %d\n", ino );
#endif
	if( b->size == 0 )
	{
		if (ino != 1)
			fuse_reply_err(req, ENOTDIR);
		else {
			//memset(&b, 0, sizeof(b));
			//dirbuf_add(req, b, ".", 1);
			//dirbuf_add(req, b, "..", 1);
			{
				DIR *d;
				struct dirent *de;
            const char *tmp;
            if( ino == 1 )
					d = opendir( tmp = fpl.source_dir );
				else
					d = opendir( tmp = root->source_name );
#ifdef DEBUG_OPERATIONS
				fprintf( stderr, "dir:%s %p\n", tmp, d );
#endif
				while( (de = readdir( d ) ) ) {
               dirbuf_add( req, b, de->d_name, de->d_ino );
				}
			}
			reply_buf_limited(req, b->p, b->size, off, size);
			//free(b.p);
		}
	}
	else
	{
		reply_buf_limited(req, b->p, b->size, off, size);
	}
}
static void doc_ll_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	free( (void*)fi->fh );
	fuse_reply_none( req );
}

static void doc_ll_create( fuse_req_t req
								 , fuse_ino_t parent
								 , const char*name, mode_t mode
								 , struct fuse_file_info *fi)
{
	char buf[256];
	struct open_file *dir = parent == 1?fpl.files:getFileByIno( fpl.files, parent );
   struct open_file *file;
	struct fuse_entry_param fep;

#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "create a file:%s\n", name );
#endif
   if( dir == fpl.files )
		snprintf( buf, 256, "%s/%s", fpl.source_dir, name );
   else
		snprintf( buf, 256, "%s/%s", dir->source_name, name );
   file = getFile( fpl.files, name, buf );
	file->handle = creat( buf, mode );
   file->references = 1;
	if( file->handle != -1 ) {
		stat( buf, &file->fep.attr );
		file->fep.attr.st_ino = file->fep.ino;
		file->ffi.fh = file->handle;
		fuse_reply_create( req, &file->fep, &file->ffi );
      return;
	}
   fuse_reply_err( req, errno );
}

static void doc_ll_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode) {
	char buf[256];
	struct open_file *dir = parent == 1?fpl.files:getFileByIno( fpl.files, parent );
   struct open_file *file;
	struct fuse_entry_param fep;

#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "create a directory:%s\n", name );
#endif
   if( dir == fpl.files )
		snprintf( buf, 256, "%s/%s", fpl.source_dir, name );
   else
		snprintf( buf, 256, "%s/%s", dir->source_name, name );
	if( !mkdir( buf, mode ) ) {
		file = getFile( fpl.files, name, buf );
		file->handle = -1;
		stat( buf, &file->fep.attr );
		file->fep.attr.st_ino = file->fep.ino;
		file->ffi.fh = file->handle;
		fuse_reply_create( req, &file->fep, &file->ffi );
      return;
	}
   fuse_reply_err( req, errno );
}

static void doc_ll_open(fuse_req_t req, fuse_ino_t ino,
					           struct fuse_file_info *fi)
{
	char buf[256];
   struct open_file *file = getFileByIno( fpl.files, ino );
	struct fuse_entry_param fep;
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "open a file...%d\n", ino );
#endif
	if( file ) {
      file->references++;
      fi->fh = open( file->source_name, fi->flags );
	  	fuse_reply_open( req, fi );
	}
   else
		fuse_reply_err(req, errno);
}

static void doc_ll_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
	struct open_file *file = getFileByIno( fpl.files, ino );
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "Release file: %p %d\n", file, ino );
#endif
	if( file ) {
#ifdef DEBUG_OPERATIONS
		fprintf( stderr, "closing File (and delete?)\n" );
#endif
		closeFile( file );
      fuse_reply_err( req, 0 );
	}
	else
      fuse_reply_err( req, EACCES );
}


static void doc_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
					           off_t off, struct fuse_file_info *fi)
{
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "read called... %d %d\n", size, off );
#endif
	(void) fi;
	//assert(ino == 2);
	//reply_buf_limited(req, doc_str, strlen(doc_str), off, size);
}


static void doc_ll_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size,
					           off_t off, struct fuse_file_info *fi)
{
	struct open_file *file = getFileByIno( fpl.files, ino );
	if( file ) {
		int n;
#ifdef DEBUG_OPERATIONS
		fprintf( stderr, "write called... \n" );
#endif
      lseek( file->handle, off, SEEK_SET );
      n = write( file->handle, buf, size );
		fuse_reply_write( req, n );
	}
	else {
#ifdef DEBUG_OPERATIONS
		fprintf( stderr, "Lost specified file by inode...\n" );
#endif
      fuse_reply_err( req, EACCES );
	}
}

static void doc_ll_falloc( fuse_req_t req
								 , fuse_ino_t ino
								 , int mode
								 , off_t offset
								 , off_t length
								 , struct fuse_file_info *fi)
{
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "falloc....\n" );
#endif
   //fuse_reply_err
}

static void doc_ll_bmap(fuse_req_t req, fuse_ino_t ino, size_t blocksize, uint64_t idx){

#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "bmap? \n" );
#endif
//   fuse_reply_bmap(req, 0);
	fuse_reply_err( req, ENOSYS );
}


static struct fuse_lowlevel_ops ll_oper = {
		  .lookup         = doc_ll_lookup,
		  .getattr        = doc_ll_getattr,
		  .readdir        = doc_ll_readdir,
		  .open           = doc_ll_open,
		  .read           = doc_ll_read,
		  .write          = doc_ll_write,
		  .opendir        = doc_ll_opendir,
        .release        = doc_ll_release,
		  .releasedir     = doc_ll_releasedir,
		  .create         = doc_ll_create,
		  .fallocate      = doc_ll_falloc,
		  .bmap           = doc_ll_bmap,
        .mkdir          = doc_ll_mkdir,
};

static void fpvfs_close( void )
{
	if( fpl.session ) {
		fuse_session_destroy( fpl.session );
		fpl.session = NULL;
	}
	if( fpl.mount ) {
		fuse_unmount( fpl.mount_point, fpl.mount );
		fpl.mount = NULL;
	}
}

static void* sessionLoop( void* thread )
{
	//rintf( stderr, "thread as %d %d\n", getpid(), getpgrp() );
	if( fuse_set_signal_handlers( fpl.session ) != -1 ) {
		int error;
		fuse_session_add_chan( fpl.session, fpl.mount );
		error = fuse_session_loop( fpl.session );
		fprintf( stderr, "Volume has been unmounted" );
		fuse_remove_signal_handlers( fpl.session );
		fuse_session_remove_chan( fpl.mount );
		fpvfs_close();
		//if( fpl.main )
		//	WakeThread( fpl.main );
	}
	return 0;
}

int fpvfs_init( const char * path, const char *source )
{
	struct fuse_args args = { argc: 0, argv: NULL, allocated:0 };
	//fuse_opt_add_arg( &args, "-d" );
	//fuse_opt_add_arg( &args, "-o" );
								 //fuse_opt_add_arg( &args, "idmap=user" );
	//fuse_opt_add_arg( &args, "-o" );
	//fuse_opt_add_arg( &args, "allow_other" )

	//fuse_opt_add_arg( &args, "direct_io" );
	fuse_opt_add_arg( &args, "allow_root" );
	fpl.mount = fuse_mount( path, &args );
	fpl.myself = getpid();
	fpl.uid = getuid();
	fpl.gid = getgid();
	if( fpl.mount ) {
		fpl.mount_point = strdup( path );
		fpl.source_dir = strdup( source );
		fpl.session = fuse_lowlevel_new( &args, &ll_oper, sizeof( ll_oper ), NULL );
		if( fpl.session ) {
			pthread_create( &fpl.thread, NULL, sessionLoop, NULL );
		}else
			fprintf( stderr, "failed to create register fuse lowlevel operations: %s(%d)\n", strerror(errno), errno );
	}

}

static void sigInt( int signal ) {
#ifdef DEBUG_OPERATIONS
	fprintf( stderr, "signal.\n" );
#endif
	fpvfs_close();
   write( fpl.pair[1], "G", 1 );
}

int main(int argc, char *argv[])
{
	char *mount;
	char *store;
	if( argc > 1 )
		mount = argv[1];
   else mount = "./test";
	if( argc > 2 )
		store = argv[2];
   else store = "./test_storage";

   pipe( fpl.pair );
	fpl.current_ino = 100;

	//rintf( stderr, "Started as %d %d\n", getpid(), getpgrp() );
	{
		struct sigaction action;
		action.sa_handler = sigInt;
		sigemptyset(&action.sa_mask);
		action.sa_flags = 0;

		sigaction( SIGINT, &action, NULL );
	}
	atexit( fpvfs_close );

	fpvfs_init( mount, store );
	while( fpl.thread && fpl.mount )
	{
      char buf;
      read( fpl.pair[0], &buf, 1 );
	}
	return 0;
}


