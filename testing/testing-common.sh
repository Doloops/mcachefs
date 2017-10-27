MCACHEFS_SRC=src/mcachefs

MCACHEFS=mcachefs-testing

if [ -r $MCACHEFS ] ; then
    rm $MCACHEFS
fi

ln -s $MCACHEFS_SRC $MCACHEFS

BASEPATH=/tmp/mcachefs.testing
METAFILE=$BASEPATH/metafile
JOURNAL=$BASEPATH/journal
CACHE=$BASEPATH/cache

function cleanup_testing {
    echo "Cleanup testing !"
    killall -9 $MCACHEFS
    
    
    for i in $(grep ^$MCACHEFS /proc/mounts | cut -d " " -f 2) ; do
        echo "At $i"
        if [ ! -z $i ] ; then
            echo "Unmounting previously mounted $i"
            fusermount -u $i
        fi
    done
#    rm -rf /tmp/mcachefs
    rm -rf $BASEPATH
    mkdir -p $BASEPATH
    mkdir -p $CACHE
}    

function run_mcachefs() {
    TARGET=$1
    LOCAL=$2
    
    echo "Mounting $TARGET $LOCAL"
    ./$MCACHEFS -f $TARGET $LOCAL -o metafile=$METAFILE,journal=$JOURNAL,cache=$CACHE/ 2> $BASEPATH/log &
    echo "[INFO] Mounting $TARGET $LOCAL Done !"
    sleep 1
}

function compare_files() {
    FILE1=$1
    FILE2=$2
    if [ ! -r $FILE1 ] ; then
        echo [ERR] $FILE1 not readable !
        exit 1
    fi
    if [ ! -r $FILE2 ] ; then
        echo [ERR] $FILE2 not readable !
        exit 1
    fi
    MD1=$(md5sum $FILE1 | cut -d " " -f 1)
    MD2=$(md5sum $FILE2 | cut -d " " -f 1)
    if [ $MD1 != $MD2 ] ; then
        echo [ERR] Files $FILE1 and $FILE2 have different md5 : $MD1 and $MD2
        exit 1
    fi
    echo [OK] Files $FILE1 and $FILE2 are the same
}

function compare_files_different() {
    FILE1=$1
    FILE2=$2
    if [ ! -r $FILE1 ] ; then
        echo [ERR] $FILE1 not readable !
        exit 1
    fi
    if [ ! -r $FILE2 ] ; then
        echo [ERR] $FILE2 not readable !
        exit 1
    fi
    MD1=$(md5sum $FILE1 | cut -d " " -f 1)
    MD2=$(md5sum $FILE2 | cut -d " " -f 1)
    if [ $MD1 == $MD2 ] ; then
        echo [ERR] Files $FILE1 and $FILE2 have same md5 : $MD1 and $MD2
        exit 1
    fi
    echo [OK] Files $FILE1 and $FILE2 are different
}

function check_file_notexists() {
    FILE=$1
    if [ -r "$FILE" ] ; then
        echo "[ERR] $FILE should not exist !"
        exit 1
    else
        echo "[OK] $FILE does not exist."
    fi
}

function check_file_exists() {
    FILE=$1
    if [ -r "$FILE" ] ; then
        echo "[OK] $FILE does exist."
    else
        echo "[ERR] $FILE should exist !"
        exit 1
    fi
}

function check_file_exists_but_cant_read() {
    FILE=$1
    check_file_exists "$FILE"
    cat "$FILE" > /dev/null
    RES=$?
    if [ $RES -eq 1 ] ; then
         echo "[OK] $FILE could not be read. (result=$RES)"
    else
        echo "[ERR] $FILE could be read !"
        exit 1
    fi    
    
}
