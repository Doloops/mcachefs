MCACHEFS=src/mcachefs

BASEPATH=/tmp/mcachefs.testing

function cleanup_testing {
    echo "Cleanup testing !"
    killall -9 mcachefs
    
    
    for i in $(grep ^mcachefs /proc/mounts | cut -d " " -f 2) ; do
        echo "At $i"
        if [ ! -z $i ] ; then
            echo "Unmounting previously mounted $i"
            fusermount -u $i
        fi
    done
    rm -rf /tmp/mcachefs
    rm -rf $BASEPATH
    mkdir -p $BASEPATH
}    

function run_mcachefs() {
    TARGET=$1
    LOCAL=$2
    
    echo "Mounting $TARGET $LOCAL"
    $MCACHEFS -f $TARGET $LOCAL 2> $BASEPATH/log &
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
