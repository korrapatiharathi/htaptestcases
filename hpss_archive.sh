
#!/bin/bash

# Archive one file with 1 agent -- Lustre->HPSS

#  export LHMSC_TEST_LUSTRE_MP=/mnt/lfs
#  export LHMSC_TEST_LUSTRE_TESTPATH=hsm-test
#  export TASCON_TEST_BACKEND_PLUGIN=hpss
#  export TASCON_TEST_BACKEND_PATH=/lhsm-test

# Prepare the source directory
TESTPATH="$LHMSC_TEST_LUSTRE_MP/$LHMSC_TEST_LUSTRE_TESTPATH"

if [ -d "$TESTPATH" ] ; then
    rm -rf "$TESTPATH"
fi

mkdir -p "$TESTPATH"

# Create the source file
cp /bin/ls "$TESTPATH"

# Load in the testing tools
. $(dirname $0)/hpss_framework.inc

# Create a test configuration file
make_conf 4dm1cm.conf

# Start the manager
start_cmm

# Start the agents
start_cma
wait_cma_bk

BKFILE=$($TASN2B -c test.conf -l "$TESTPATH/ls")
## Does not work for hpss
#rm -f "$BKFILE"
echo backend file should be "$BKFILE"

# Archive the source file
echo "Archiving"
lfs hsm_archive --data='{"something": "blah", "somethingelse": "foo",   "bar": 8}' "$TESTPATH/ls"
echo "Archiving started"

wait_hsm_state "$TESTPATH/ls" 0x00000009

echo "File archived"

## This needs some extra hpss commands to enable - maybe hpss_ls
# Ensure it exists, and is in the correct place
#[ -f $BKFILE ] || error "backend file not found"

get_status cmm1
get_stats cmm1

# Check if it's done
ecount 1 "returned to Lustre \(op=archive, .*, a_ret=0, ret=0\)" $(log cmm1)
ecount 1 "copy to object succeeded" $(log cma1)

# Ensure all requests have a name
# This checks for lines like:
#   | Operation       |   Submitted |   Succeeded |   Failed |   Canceled |   Reset |
#   |-----------------+-------------+-------------+----------+------------+---------|

ecount 0 "\(null\)" $(log cmm1 stats)

check_stats cmm1 || error "stat checks failed"
check_status cmm1 || error "status checks failed"

echo "Good"

exit 0
