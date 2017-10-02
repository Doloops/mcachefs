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

compare_files $TARGET/file1 $LOCAL/file1
compare_files $TARGET/file2 $LOCAL/file2

echo "New contents of file 1" > $LOCAL/file1
compare_files_different $TARGET/file1 $LOCAL/file1

echo apply_journal > $LOCAL/.mcachefs/action
sleep 1
compare_files $TARGET/file1 $LOCAL/file1

echo "Contents of local :"
ls -lh $LOCAL/*

echo "Another file appears !" > $TARGET/file3

check_file_notexists $LOCAL/file3

echo flush_metadata > $LOCAL/.mcachefs/action
sleep 1

check_file_exists $LOCAL/file3

echo "[OK] All tests OK!"

# cleanup_testing
