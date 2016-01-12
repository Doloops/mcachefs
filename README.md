MCACHEFS OVERVIEW :
-------------------

mcachefs is a simple caching filesystem for Linux using FUSE. It works 
by copying the file that you asked for when the file is opened, and then 
using that copy for all subsequent requests for the file. 

mcachefs needs two filesystems to operate. The target filesystem is the 
slow filesystem you want to cache accesses to, and the backing 
filesystem is where mcachefs can stash stuff which it has copied. The 
backing filesystem should therefore be on local disc, or the whole point 
of the exercise is gone.

mcachefs caches metadata in a dedicated file, the 'metafile', which speeds
up filesystem navigation. Cached metadata is persistent, but one can still
flush away a part or all of the cached structure.

mcachefs handles filesystem modifications asynchronously using a dedicated
'journal' file, and all modifications to the files are made first in the
backing filesystem.

The target goal is to provide an asynchronous access to a filesystem, for
example network drives on a travelling laptop.

INSTALLING :
------------

(No autotools, so no ./configure... Just make and make install for now).

USING :
-------

mcachefs used to look for a config file to determine where the target and
backing filesystems were located, but these days are over.

Now all settings must be provided using command-line arguments. mcachefs
requires at least two arguments : the source and the mountpoint.

Let's say you want to mirror dir /media/input to /media/output.
Just call :
mcachefs /media/input /media/output

Extra fuse-like arguments are : 
* cache :
  the directory where cached files are stored
* metafile :
 the absolute path to store the metadata file (dir structure, file names, ...) in cache
* journal : the absolute path to the journal file
* verbose : the level of verbosity (integer) : 0 enables log, -1 disables it
  (not yet supported)
  
This program wont terminate... If you kill it, the filesystem will unmount in 
a bad way. Don't do that. Use umount instead, or fusermount -u /your/moinpoint

RUN-TIME INTERFACE (VOPS) :
---------------------------

mcachefs exposes various run-time configuration settings in the '.mcachefs' of
the mounted structure :

 * state : current mcachefs state (see below)
 * wrstate : current mcachefs write state (see below)
 * journal : dumps the contents of the journal, id est the modifications made
   to the filesystem and not yet applied to the target.
 * apply_journal : echo apply > [..]/.mcachefs/apply_journal to apply the
   current modifications.
 * drop_journal : echo apply > [..]/.mcachefs/drop_journal to drop the
   current modifications.
 * file_thread_interval : the openned file garbage collector interval, in 
   seconds (default : 1s)
 * file_ttl : number of file_thread_interval seconds before internally closing
   openned files (to spare openned file descriptors)
 * metadata : dumps the contents of the metafile, with the folder hierarchy
   and the hashtree
 * metadata_flush : flushes the contents of the metafile (should apply the
   journal first).
 * timeslices : dumps the currently openned files, sorted by their last usage

mcachefs states are :
 * normal : accessed files are copied to backup if not already done, and
   written files are modified in the backup filesystem.
 * full : backup filesystem is full, read is performed from the target 
   filesystem, and write is forbidden (for the moment at least)
 * handsup : target filesystem may be inaccessible, mcachefs will not access
   it in any way (nor for reading directory contents or accessing a file)
 * quitting : mcachefs is about to quit, so close any openned files...

mcachefs write states are :
 * cache : modifications are written in the backup, and notified in the
   journal. One must apply journal to write back files to the target.
 * flush : files openned write are flushed back to the source after release.
 
 
TODO :
------

A load of things remains to be done :
- real implementation of the write states
- periodical metadata flush/retrieve/merge, based on a ttl
- a merging simulation mechanism when the target has changed regardless of the
  mcachefs modifications.

