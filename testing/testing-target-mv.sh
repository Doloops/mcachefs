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

find $LOCAL

check_file_exists $LOCAL/file1
check_file_exists $LOCAL/file2

compare_files $TARGET/file1 $LOCAL/file1

rm $TARGET/file2

check_file_exists_but_cant_read $LOCAL/file2

compare_files $TARGET/file1 $LOCAL/file1

echo "Contents of file 2" > $TARGET/file2

compare_files $TARGET/file2 $LOCAL/file2

