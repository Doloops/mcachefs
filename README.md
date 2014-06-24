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

mcachefs looks for a config file to determine where the target and backing
filesystems are located, as well as a debugging verbosity level. mcachefs
looks in the following locations for it's config file (in this order):

    * /etc/mcachefs
    * ~/.mcachefs
    * `pwd`/mcachefs.cfg 

Each line of the config file starts by the prefix of the mount point, plus
the corresponding configuration setting. 

For example, with a mount point prefix set to '/mnt/cache' :

/mnt/cache/backing  /backing
/mnt/cache/target   /
/mnt/cache/metafile /tmp/root.metafile.mcachefs
/mnt/cache/journal  /tmp/root.journal.mcachefs
/mnt/cache/verbose  99

Note that the two columns must be separated by tabs for the config file to
parse properly. This config section above says that we have a cached
filesystem to mount at /mnt/cache. The backing store is /backing, the target
store is /, and we set the paths for the metafile and the journal.

We want to be _very_ verbose. Lower numbers are less verbose. Please note 
that being very verbose might help with debugging mcachefs, but _will_ slow
down the filesystem.

You can now mount the cached filesystem but doing a:

	mcachefs /mnt/cache

This program wont terminate... If you kill it, the filesystem will unmount in 
a bad way. Don't do that. Use umount instead.

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

