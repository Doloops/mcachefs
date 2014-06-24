#include "mcachefs.h"

/**********************************************************************
 Utility functions
**********************************************************************/

#if 1
hash_t continueHashPartial ( hash_t h, const char* str, int sz )
{
  int cur = 0;
  const char* c;
  unsigned char d;
  for ( c = str ; *c ; c++ )
    {
      d = (unsigned char)(*c);

      h = d + (h<<6) + (h<<16) - h;
      
      cur++;
      if ( cur == sz ) break;
    }
  return h;
}
#else
hash_t continueHashPartial ( hash_t h, const char* str, int sz )
{
  int cur = 0;
  const char* c;
  unsigned char d;
  for ( c = str ; *c ; c++ )
    {
      d = (unsigned char)(*c);

      h += d + d % 2;
      cur++;
      if ( cur == sz ) break;
    }
  return h % 16;
}
#endif

hash_t continueHash ( hash_t h, const char* str )
{
  return continueHashPartial ( h, str, ~((int)0) );
}

hash_t doHashPartial ( const char* str, int sz )
{
  hash_t h = 0x245;
  return continueHashPartial ( h, str, sz );
}

hash_t doHash ( const char* str )
{
  return doHashPartial ( str, ~((int)0) );
}


char *mcachefs_makepath(const char *path, const char *prefix)
{
	  char *newpath;
	  int len;
	
	  len = strlen(path) + strlen(prefix) + 1;
	  newpath = malloc(len + 1);
	  if(newpath == NULL)
	    {
	      Err("  Failed to convert path\n");
	      return NULL;
	    }
	
	  snprintf(newpath, len, "%s%s", prefix, path);
	  return newpath;
}

char *mcachefs_makerealpath(const char *path)
{
	  return mcachefs_makepath(path, mcachefs_target);
}

char *mcachefs_makebackingpath(const char *path)
{
	  return mcachefs_makepath(path, mcachefs_backing);
}

int mcachefs_fileinbacking(const char *path)
{
    char* backingpath;
    struct stat st;
    int res;

    backingpath = mcachefs_makebackingpath(path);    
    res = lstat ( backingpath, &st );
    free ( backingpath );
    return res == 0;
}


int mcachefs_createpath ( const char* prefix, const char* path, int lastIsDir )
{
	char *victim, *current, *tok;
	struct stat sb;
    int prefixfd, tempfd;

    Log ( "Create path prefix='%s', path='%s', lastIsDir=%d\n", prefix, path, lastIsDir );

    prefixfd = open ( prefix, O_RDONLY );
    Log ( "prefixfd = %d\n", prefixfd );
    
    if ( prefixfd == -1 )
      {
        Err ( "Could not get prefix '%s' : error %d:%s\n", prefix, errno, strerror(errno) );
        return -errno;
      }
    
    victim = strdup ( path );
    
    if ( victim == NULL )
      {
        close ( prefixfd );
        return -ENOMEM;
      }
    tok = strtok ( victim, "/" );
    current = tok;
    while ( current )
      {
        Log ( "current='%s'\n", current );
			  tok = strtok(NULL, "/");
			  
			  if ( ! tok && ! lastIsDir )
          {
            Log ( "Skipping '%s' as supposed to be the last one file.\n", current );
            break;
          }      

	      if ( fstatat(prefixfd, current, &sb, 0) < 0)
			    {
			      /*
			       * The path element didn't exist
			       */
			      if ( mkdirat ( prefixfd, current, S_IRWXU) != 0 )
			        {
			          Err ( "Could not mkdirat() : err=%d:%s\n", errno, strerror(errno) );
			        }
			    }

        tempfd = openat ( prefixfd, current, O_RDONLY );
        Log ( "Prefix=%d -> tempfd=%d\n", prefixfd, tempfd );
        close ( prefixfd );
        prefixfd = tempfd;
        current = tok;
      }

    close ( prefixfd );
    free ( victim );
    Log ( "Done.\n" );
    return 0;
}

int mcachefs_createbackingpath(const char* path, int lastIsDir)
{
	  /* Does the path in the backing store exist? */
    Log ( "Creating backing path for '%s'\n", path );
    return mcachefs_createpath ( mcachefs_backing, path, lastIsDir );
}

int mcachefs_backing_createbackingfile ( const char* path, mode_t mode )
{
    int res;
    struct stat st;
    
    res = mcachefs_createbackingpath ( path, 0 );
    if ( res ) return res;
    
    char* backing_path = mcachefs_makebackingpath ( path );
    
    if ( lstat ( backing_path, &st ) == 0 && S_ISREG ( st.st_mode ) )
      {
        Log ( "File already exist !\n" );
        if ( truncate ( backing_path, 0 ) )
          {
            Err ( "Could not truncate existing backing path '%s' err=%d:%s\n", backing_path, errno, strerror(errno) );
            res = -errno;
          }
        free ( backing_path );
        return res;
      }
      
    res = mknod ( backing_path, mode, 0 );
    Log ( "mknod(%s), res=%d\n", backing_path, res );

    free ( backing_path );
    return res;
}

char* mcachefs_split_path ( const char* path, const char** lname )
{
    int i, lastSlash = 0;
    char* dpath;
    for ( i = 0 ; path[i] ; i++ )
      {
        if ( path[i] == '/' )
          {
            lastSlash = i;
            *lname = &(path[i]);
          }
      }
    if ( lastSlash == 0 )
      lastSlash ++;
    Log ( "lastSlash = %d\n", lastSlash );

    dpath = (char*) malloc ( lastSlash + 1 );
    memcpy ( dpath, path, lastSlash );
    dpath[lastSlash] = 0;
        
    (*lname)++;
    Log ( "Path : '%s', dir : '%s', lname='%s'\n", path, dpath, *lname );
    return dpath;  
}

