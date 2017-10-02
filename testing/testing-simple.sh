#!/bin/bash

. testing/testing-common.sh

cleanup_testing

LOCAL=$BASEPATH/local
TARGET=$BASEPATH/target

mkdir -p $LOCAL
mkdir -p $TARGET

echo "Contents of file 1" > $TARGET/file1
echo "Contents of file 2" > $TARGET/file2

run_mcachefs $TARGET $LOCAL

echo "[Test] Testing file fetching to cache"

compare_files $TARGET/file1 $LOCAL/file1
compare_files $TARGET/file2 $LOCAL/file2

echo "[Test] Testing file write on local"

echo "New contents of file 1" > $LOCAL/file1
compare_files_different $TARGET/file1 $LOCAL/file1

echo "[Test] Apply journal"

echo apply_journal > $LOCAL/.mcachefs/action
sleep 1
compare_files $TARGET/file1 $LOCAL/file1

echo "Contents of local :"
ls -lh $LOCAL/*

echo "[Test] Another file appears on target"

echo "Another file appears !" > $TARGET/file3

check_file_notexists $LOCAL/file3

echo "[Test] Flush metadata"

echo flush_metadata > $LOCAL/.mcachefs/action
sleep 1

check_file_exists $LOCAL/file3
compare_files $TARGET/file3 $LOCAL/file3

echo "[Test] Adding a local file and folder"

mkdir $LOCAL/newFolder
echo "Yet another file in local cache !" > $LOCAL/newFolder/file4

check_file_exists $LOCAL/newFolder/file4
check_file_notexists $TARGET/newFolder/file4

echo "[Test] Apply journal"

echo apply_journal > $LOCAL/.mcachefs/action
sleep 1

compare_files $TARGET/newFolder/file4 $LOCAL/newFolder/file4

echo "[Test] Removing a local file"

rm $LOCAL/file2

check_file_notexists $LOCAL/file2
check_file_exists $TARGET/file2

echo "[Test] Apply journal"

echo apply_journal > $LOCAL/.mcachefs/action
sleep 1

check_file_notexists $LOCAL/file2
check_file_notexists $TARGET/file2

echo "[OK] All tests OK!"

# cleanup_testing
