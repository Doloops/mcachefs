#!/bin/bash

. testing/testing-common.sh

cleanup_testing

LOCAL=$BASEPATH/local
TARGET=$BASEPATH/target

mkdir -p $LOCAL
mkdir -p $TARGET


run_mcachefs $TARGET $LOCAL

NB_FILES=200

for j in $(seq 1 4 ) ; do

    echo "Creating $NB_FILES files"
    for i in $(seq 1 $NB_FILES) ; do
        # echo "Contents of file $i" > $LOCAL/f${i}i${i}l${i}e$i
        touch $LOCAL/f${i}i${i}l${i}e$i
        if [ $? != 0 ] ; then 
            echo "Could not create content at $i !"
            exit
        fi
    done

    echo "Deleting $NB_FILES files"
    for i in $(seq 1 $NB_FILES) ; do
        rm $LOCAL/f${i}i${i}l${i}e$i
        if [ $? != 0 ] ; then 
            echo "Could not remove content at $i !"
            exit
        fi
    done
done

