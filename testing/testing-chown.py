#!/bin/env python


import os, sys, subprocess, shlex, time

BASEPATH = "/tmp/mcachefs.testing"
MCACHEFS = os.path.dirname( os.path.abspath(__file__) )+'/../src/mcachefs'

def exit(r=0):
	# kill gdb if it's still running!
	p.poll()
	if not p.returncode:
	    p.kill()

	# unmount mcachefs
	os.system("fusermount -u %s/2" % BASEPATH)
	os.system('rm -rf '+BASEPATH+"/1")
	os.system('rm -rf '+BASEPATH+"/2")

	sys.exit(r)


os.system('mkdir -p '+BASEPATH+"/1")
os.system('mkdir -p '+BASEPATH+"/2")

# just make sure mcachefs is not mounted, in case we're running after a failed try
os.system("fusermount -u %s/2" % BASEPATH)

# run mcachefs in gdb so we can see a stack trace in case it fails!
#cmd='''/bin/sh -c "echo -e 'r\\nbt\\n' | gdb --args %s -f -o -s %s/1 %s/2" ''' % (MCACHEFS, BASEPATH, BASEPATH)
cmd='''/bin/sh -c "%s -f -o -s %s/1 %s/2" ''' % (MCACHEFS, BASEPATH, BASEPATH)
p = subprocess.Popen(shlex.split(cmd))

print shlex.split(cmd), p.pid
if not p.pid:
    print "Failed to run %s!" % MCACHEFS
    sys.exit(-1)

# wait a bit mcachefs to mount!
count=0
while( not os.path.exists('%s/2/.mcachefs' % BASEPATH) ):
    count+=1
    time.sleep(1)
    if count>10:
       print "ERROR: mounting filesystem!"
       exit(-1)

# create a file and chown weirdly to test!
os.system('echo teste > %s/2/file' % BASEPATH)
os.system('chown 1000:1000 %s/2/file' % BASEPATH)

os.system('ls -l  %s/2/' % BASEPATH)
stat_info = os.stat('%s/2/file' % BASEPATH)
uid = stat_info.st_uid
gid = stat_info.st_gid
print uid, gid
if uid!=1000:
	print "ERROR: UID no set to 1000... set to ", uid
	exit(-1)

if gid!=1000:
	print "ERROR: GID no set to 1000... set to ", gid
	exit(-1)

os.system('chown :1000 %s/2/file' % BASEPATH)
os.system('ls -l  %s/2/' % BASEPATH)
stat_info = os.stat('%s/2/file' % BASEPATH)
uid = stat_info.st_uid
gid = stat_info.st_gid
print uid, gid
if uid!=1000:
	print "ERROR: UID no set to 1000... set to ", uid
	exit(-1)

if gid!=1000:
	print "ERROR: GID no set to 1000... set to ", gid
	exit(-1)

os.system('cat  %s/2/.mcachefs/journal' % BASEPATH)


# finishing up
time.sleep(3)
l = os.popen( 'pgrep -fa "%s"' % MCACHEFS ).readlines()
print l
if not l:
	print "mcachefs crashed!"

exit()
