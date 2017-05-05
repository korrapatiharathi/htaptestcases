
#!/bin/bash

# Archive on single file on 4 CMAs and cancel.
# The transfer is slowed down with the dbg-copy-usleep option

#SCRUB=/opt/htap/utils/scrub.sh
. $(dirname $0)/hpss_framework.inc

# Prepare
#TESTPATH=`readlink -f $LHMSC_TEST_LUSTRE_MP/$LHMSC_TEST_LUSTRE_TESTPATH`
TESTPATH="$LHMSC_TEST_LUSTRE_MP/$LHMSC_TEST_LUSTRE_TESTPATH"
if [ -d "$TESTPATH" ] ; then
    rm -rf "$TESTPATH"
fi
mkdir -p $TESTPATH
#make_conf 4dm1cm.conf
make_conf hpss.conf

# Start the manager
start_cmm

# Start the agent
start_cma 1 4 "" "" --dbg-copy-usleep=10000000
wait_cma_bk 1 4

# Create the file, archive it
#rm -f $TESTPATH/ls
#cp /bin/ls $TESTPATH/
TESTFILE="test.tar"
cp -f /mnt/jyc/test/$TESTFILE $TESTPATH/
echo "Archiving"
lfs hsm_archive $TESTPATH/$TESTFILE
echo "Archiving started"

# Wait for transfer to start for 10 seconds.
# The file transfer is split, so we'll have 4 stripes.
LIM=$((`date +%s`+10))
while :
do
    sleep .5
    [ `date +%s` -ge $LIM ] && error "archiving not started (timeout)"
    get_status cmm1
    grep -c "transfer_stripe" $(log cmm1 status) | grep "^4$" && break
done

grep -c archive $(log cmm1 status) | grep "^1$" || error "Must have 1 archive op only"

# Cancel
echo "Cancel"
lfs hsm_cancel $TESTPATH/$TESTFILE
echo "Canceled"

# Wait until it is gone
LIM=$((`date +%s`+10))
while :
do
    sleep .5
    [ `date +%s` -ge $LIM ] && error "archiving not canceled (timeout)"
    get_status cmm1
    grep "archive.*0x" $(log cmm1 status) || break
done

echo "Gone"

get_stats cmm1
get_status cmm1

# Two cancel lines. One for the frontend, and one for the total of all
# frontends.
ecount 2 "\|\s+cancel\s+\|\s+1\s+\|\s+1\s+\|" $(log cmm1 stats)
grep "\b417\b.*\b417\b" $(log cmm1 status) || error "BAD"

# Check it's done
grep "returned to Lustre (op=archive, .*, a_ret=125, ret=0)" $(log cmm1) || error "expected op returned not found"

check_stats cmm1 || error "stat checks failed"
check_status cmm1 || error "status checks failed"

echo "Good"

exit 0
