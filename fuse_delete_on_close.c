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

#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64

#include <fuse.h>
#ifndef __ANDROID__
#include <fuse/fuse.h>
#else
#include <fuse.h>
#endif

struct open_file {
	char *name;
   int references;
	//struct fuse_entry_param fep;
   //struct fuse_file_info ffi;
   int handle;
   struct open_file *next, **me;
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
	int current_ino;
   int current_handle;
	struct open_file *files;
   int pair[2];
} fpl;


static struct open_file *getFile( const char *name ) {
	struct open_file *file;
	for( file = fpl.files; file; file = file->next ) {
		if( strcmp( file->name, name ) == 0 )
         break;
	}
	if( !file ) {
		file = malloc( sizeof( struct open_file ) );
      file->references = 1;
		file->name = strdup( name );
      /*
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
      */
		file->handle = -1;
		if( file->next = fpl.files )
			fpl.files->me = &file->next;
      fpl.files = file;
	}
	else
      file->references++;
   return file;
}

static void closeFile( struct open_file *file ) {
	file->references--;
	if( !file->references ) {
		file->me[0] = file->next;

	}
}

static int doStat(uint64_t handle, struct stat *attr, double *timeout)
{
	fprintf( stderr, "request attr %ld", handle );
	memset( attr, 0, sizeof( *attr ) );
	attr->st_dev = 1;
	attr->st_nlink = 1;
	attr->st_ino = 0; // needs to be filled in.
	attr->st_uid = fpl.uid;
	attr->st_gid = fpl.gid;
	attr->st_size = 1234;
	attr->st_blocks = 4321;
	attr->st_blksize = 1;
	if( handle == 1 ) {
		attr->st_mode = S_IFDIR | 0700;
		(*timeout) = 10000.0;
	}
	else {
		attr->st_mode = S_IFREG | 0600;
		(*timeout) = 1.0;
	}
	return 0;
}

static int doc_stat(const char *file, struct statvfs *buf )
{
	buf->f_bsize = 1;
	buf->f_frsize = 1024;
	buf->f_blocks = 1;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_files =0;
	buf->f_ffree = 0;
	buf->f_favail = 0;
	buf->f_fsid = 0;
	buf->f_flag = 0;
   buf->f_namemax = 0;
	fprintf( stderr, "doc_stat %s\n", file );
   /*
		  switch (ino) {
		  case 1:
					 stbuf->st_mode = S_IFDIR | 0755;
					 stbuf->st_nlink = 2;
					 break;
		  case 2:
					 stbuf->st_mode = S_IFREG | 0444;
					 stbuf->st_nlink = 1;
					 stbuf->st_size = strlen(doc_str);
					 break;
		  default:
					 return -1;
		  }
        */
		  return 0;
}

static void doc_getattr(const char *name, struct stat *stat, struct fuse_file_info *fi)
{
	struct stat stbuf;
	double timeout;

	memset(&stbuf, 0, sizeof(stbuf));
	if (doStat(fi->fh, &stbuf, &timeout) == -1)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_attr(req, &stbuf, timeout);
}

#if 0
static void doc_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	const struct fuse_ctx *ctx = fuse_req_ctx(req);
	struct fuse_entry_param e;
	fprintf( stderr, "lookup from %d  %d %ld %s\n", ctx->pid, fpl.myself, parent, name );


	if (parent != 1 ) {
      fprintf( stderr, "fail?\n" );
		fuse_reply_err(req, ENOENT);
	} else {
		double timeout;
      fprintf( stderr, "ok?\n" );
		memset(&e, 0, sizeof(e));
		e.ino = 2;
		doStat(e.ino, &e.attr, &timeout );
		e.attr_timeout = timeout;
		e.entry_timeout = timeout;
		fuse_reply_entry(req, &e);
	}
}
#endif
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


static void doc_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	fi->fh = (uintptr_t)malloc( sizeof( struct dirbuf ) );
	memset((void*)fi->fh, 0, sizeof(struct dirbuf));
	fuse_reply_open( req, fi );
}

static void doc_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
					              off_t off, struct fuse_file_info *fi)
{
	struct dirbuf *b = (struct dirbuf*)fi->fh;
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
				d = opendir( fpl.source_dir );
				while( (de = readdir( d ) ) ) {
               dirbuf_add( req, b, de->d_name, de->d_ino );
				}
			}
			//dirbuf_add(req, &b, doc_name, 2);
			reply_buf_limited(req, b->p, b->size, off, size);
			//free(b.p);
		}
	}
	else
	{
		reply_buf_limited(req, b->p, b->size, off, size);
	}
}
static void doc_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	free( (void*)fi->fh );
	fuse_reply_none( req );
}

static void doc_create( const char*name, mode_t mode
								 , struct fuse_file_info *fi)
{
	char buf[256];
   struct open_file *file;
	struct fuse_entry_param fep;
	fprintf( stderr, "create a file:%s\n", name );
	snprintf( buf, 256, "%s/%s", fpl.source_dir, name );
   file = getFile( name );
	file->handle = creat( buf, mode );
	stat( buf, &file->fep.attr );
   file->fep.attr.st_ino = file->fep.ino;
   file->ffi.fh = file->handle;
   fuse_reply_create( req, &file->fep, &file->ffi );
}


static void doc_open(fuse_req_t req, fuse_ino_t ino,
					           struct fuse_file_info *fi)
{
   fprintf( stderr, "open a file...%d", ino );
	if (ino != 2)
		fuse_reply_err(req, EISDIR);
	else if ((fi->flags & 3) != O_RDONLY)
		fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}
static void doc_read(fuse_req_t req, fuse_ino_t ino, size_t size,
					           off_t off, struct fuse_file_info *fi)
{
   fprintf( stderr, "read called... %d %d\n", size, off );
	(void) fi;
	//assert(ino == 2);
	reply_buf_limited(req, doc_str, strlen(doc_str), off, size);
}


static void doc_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size,
					           off_t off, struct fuse_file_info *fi)
{
	(void) fi;
	//assert(ino == 2);
	reply_buf_limited(req, doc_str, strlen(doc_str), off, size);
}

static void doc_falloc( fuse_req_t req
								 , fuse_ino_t ino
								 , int mode
								 , off_t offset
								 , off_t length
								 , struct fuse_file_info *fi)
{
	fprintf( stderr, "falloc....\n" );
   //fuse_reply_err
}

static void doc_bmap(fuse_req_t req, fuse_ino_t ino, size_t blocksize, uint64_t idx){

   fprintf( stderr, "bmap? \n" );
//   fuse_reply_bmap(req, 0);
	fuse_reply_err( req, ENOSYS );
}



static struct fuse_ops oper = {
	.getattr        = doc_getattr,
	.readdir        = doc_readdir,
	.open           = doc_open,
	.read           = doc_read,
	.write          = doc_write,
	.opendir        = doc_opendir,
	.releasedir     = doc_releasedir,
	.create         = doc_create,
	.fallocate      = doc_falloc,
	.bmap           = doc_bmap,
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
	fprintf( stderr, "thread as %d %d\n", getpid(), getpgrp() );
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
	fuse_opt_add_arg( &args, "-d" );
	//fuse_opt_add_arg( &args, "-o" );
	//fuse_opt_add_arg( &args, "idmap=user" );
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
		}
	}

}

static void sigInt( int signal ) {
   fprintf( stderr, "signal.\n" );
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

	fprintf( stderr, "Started as %d %d\n", getpid(), getpgrp() );
	{
		struct sigaction action;
		action.sa_handler = sigInt;
		sigemptyset(&action.sa_mask);
		action.sa_flags = 0;

		sigaction( SIGINT, &action, NULL );
	}

	fpvfs_init( mount, store );
	while( fpl.mount )
	{
      char buf;
      read( fpl.pair[0], &buf, 1 );
	}
	atexit( fpvfs_close );
	return 0;
}


