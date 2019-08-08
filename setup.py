#!/bin/python2

import socket, os, sys
from glob import glob as glob

# variables setup
# =============================================================================
dryrun = '-n' in sys.argv
hostname = socket.gethostname()
CD = os.path.dirname( os.path.abspath(__file__) )

# function setup
# =============================================================================
def git_pull():
	# to easy test new features, we can switch git branch based on
	# the name of the vm. if the name has '-branch-' in it, whatever
	# comes after '-branch-' is the branch name.
	# if there's no '-branch-', it defaults to master branch.
	# ex:  storage-4-branch-split-atomo
	# 				the branch will be split-atomo!

	# make a copy of this file so we can check if it changed after updating!
	os.system("cp %s/setup.py /dev/shm/ " % CD)

	# checkout latest tag
	extra = 'git checkout $(git tag | sort | tail -1) ; git branch'

	# reset, pull and checkout
	cmd = '''
		cd %s
		git fetch origin master
		git reset --hard FETCH_HEAD
		git clean -df
		for each in $(git tag) ; do git tag -d $each ; done
		git fetch --prune
		git pull
		%s
		sync
	''' % (CD, extra)
	print cmd
	os.system( cmd )

	# check if this setup.py changed... if so, reboot so we can run the updated one
	cmd = "cd %s ; diff ./setup.py /dev/shm/setup.py" % CD
	if ''.join(os.popen(cmd).readline()).strip():
	    install()

def install():
	os.system( 'cd %s/src && make clean && make install' % os.path.dirname( os.path.abspath(__file__) ) )


# void main()
# =============================================================================
if len(sys.argv) == 1:
	print '='*80
	print '''
		to install files to the system, just run:

			setup.py -i

		use -n to dryrun
		use -p to do a git fetch/reset/pull and make the local copy identical
			   to origin.


	obs: setup.py will copy the files correctly, denpending on
	     what hostname it is.
	'''
	print '='*80
else:
	# use an alternative git branch, based on the vm name
	if "-p" in sys.argv:
		dryrun=True
		git_pull()

	if "-n" in sys.argv:
		dryrun=True

	# install
	if "-i" in sys.argv[1]:
		install()
