#!/bin/bash

# Archive and cancel. Do not keep a agent around so the request won't be
# performed.

# Load in the testing tools
. $(dirname $0)/hpss_framework.inc

# Prepare
#TESTPATH=`readlink -f $LHMSC_TEST_LUSTRE_MP/$LHMSC_TEST_LUSTRE_TESTPATH`
TESTPATH="$LHMSC_TEST_LUSTRE_MP/$LHMSC_TEST_LUSTRE_TESTPATH"
if [ -d "$TESTPATH" ] ; then
    rm -rf "$TESTPATH"
fi
mkdir -p $TESTPATH

# Create the file, archive it
rm -f $TESTPATH/ls
cp /bin/ls $TESTPATH/

make_conf 4dm1cm.conf

# Start the manager
start_cmm

echo "Archiving"
lfs hsm_archive $TESTPATH/ls
echo "Archiving started"

# Wait for transfer to start for 10 seconds.
LIM=$((`date +%s`+10))
while :
do
    sleep .5
    [ `date +%s` -ge $LIM ] && error "archiving not started (timeout)"
    get_status cmm1
    grep -c archive $(log cmm1 status) | grep "^1$" && break
done

# Cancel
echo "Cancel"
lfs hsm_cancel $TESTPATH/ls
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
