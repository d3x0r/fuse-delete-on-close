#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <pthread.h>


#ifndef min
#  define min(a,b) (((a)<(b))?(a):(b))
#endif

#define BUILD_TEST

#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS 64

#include <fuse.h>
#ifndef __ANDROID__
#include <fuse/fuse_lowlevel.h>
#else
#include <fuse_lowlevel.h>
#endif

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
} fpl;

static const char *doc_str = "Hello World!\n";
static const char *doc_name = "hello";

static int doStat(fuse_ino_t ino, struct stat *attr, double *timeout)
{
	fprintf( stderr, "request attr %ld", ino );
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

static int doc_stat(fuse_ino_t ino, struct stat *stbuf)
{
		  stbuf->st_ino = ino;
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
		  return 0;
}
static void doc_ll_getattr(fuse_req_t req, fuse_ino_t ino,
                             struct fuse_file_info *fi)
{
	struct stat stbuf;
	double timeout;

		  memset(&stbuf, 0, sizeof(stbuf));
		  if (doStat(ino, &stbuf, &timeout) == -1)
					 fuse_reply_err(req, ENOENT);
		  else
					 fuse_reply_attr(req, &stbuf, timeout);
}
static void doc_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	const struct fuse_ctx *ctx = fuse_req_ctx(req);
	struct fuse_entry_param e;
	fprintf( stderr, "lookup from %d  %d %ld %s", ctx->pid, fpl.myself, parent, name );


	if (parent != 1 || strcmp(name, doc_name) != 0)
		fuse_reply_err(req, ENOENT);
	else {
		double timeout;
		memset(&e, 0, sizeof(e));
		e.ino = 2;
		doStat(e.ino, &e.attr, &timeout );
		e.attr_timeout = timeout;
		e.entry_timeout = timeout;
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


static void ll_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	fi->fh = (uintptr_t)malloc( sizeof( struct dirbuf ) );
	memset((void*)fi->fh, 0, sizeof(struct dirbuf));
	fuse_reply_open( req, fi );
}



static void doc_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
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
static void ll_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	free( (void*)fi->fh );
	fuse_reply_none( req );
}


static void doc_ll_open(fuse_req_t req, fuse_ino_t ino,
					           struct fuse_file_info *fi)
{
	if (ino != 2)
		fuse_reply_err(req, EISDIR);
	else if ((fi->flags & 3) != O_RDONLY)
		fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}
static void doc_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
					           off_t off, struct fuse_file_info *fi)
{
		  (void) fi;
		  //assert(ino == 2);
		  reply_buf_limited(req, doc_str, strlen(doc_str), off, size);
}


static void ll_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size,
					           off_t off, struct fuse_file_info *fi)
{
		  (void) fi;
		  //assert(ino == 2);
		  reply_buf_limited(req, doc_str, strlen(doc_str), off, size);
}

static void ll_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	struct stat attr;
	double timeout;
	fprintf( stderr, "request attr %ld", ino );
	doStat( ino, &attr, &timeout );
	fuse_reply_attr( req, &attr, timeout );
}


static struct fuse_lowlevel_ops ll_oper = {
		  .lookup         = doc_ll_lookup,
		  .getattr        = ll_getattr,
		  .readdir        = doc_ll_readdir,
		  .open           = doc_ll_open,
		  .read           = doc_ll_read,
		  .write          = ll_write,
		  .opendir        = ll_opendir,
		  .releasedir     = ll_releasedir,
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
	fprintf( stderr, "thread as %d %d", getpid(), getpgrp() );
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

	fprintf( stderr, "Started as %d %d", getpid(), getpgrp() );

	fpvfs_init( mount, store );
	while( fpl.mount )
	{
		usleep( 100000 );
	}
	atexit( fpvfs_close );
	return 0;
}


