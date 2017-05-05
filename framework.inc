
# Framework for testing
#
# This must be sourced

if [ -e Makefile ]; then
    # The Makefile is present, so we're running the testsuite inside a
    # development machine. Use the binaries we just compiled.
    TASCMM=../tas_cmm/tas_cmm
    TASCMA=../tas_cma/tas_cma
    TASCMCTL=../utils/tascmctl
    TASIMPORT=../utils/tascm_import
    TASN2B=../utils/tasn2b
    TASRESTD=../utils/tasrestd.py
    CONFIGS=configs

    export PYTHONPATH=`pwd`/../utils
    TAS_CMA_LIBS=../tas_cma/.libs/
    export LD_LIBRARY_PATH=`pwd`/../tas_cma/.libs/
else
    # Independent test suite which has been installed. Use the
    # installed binaries in the path, but leave the option to
    # override them if needed.
    TASCMM=${TASCMM:-tas_cmm}
    TASCMA=${TASCMA:-tas_cma}
    TASCMCTL=${TASCMCTL:-tascmctl}
    TASIMPORT=${TASIMPORT:-tascm_import}
    TASN2B=${TASN2B:-tasn2b -c test.conf}
    TASRESTD=${TASRESTD:-tasrestd.py}
    CONFIGS=$(dirname $0)/configs
    TAS_CMA_LIBS=${TAS_CMA_LIBS:-/usr/lib*/}
fi

# Set the pool names, unless already set by the tester
LHMSC_TEST_POOL1=${LHMSC_TEST_POOL1:-tascm_test_sdd}
LHMSC_TEST_POOL2=${LHMSC_TEST_POOL2:-tascm_test_hdd}

# Set the default backend location, unless already set by the user
TASCON_TEST_LOCATION=${TASCON_TEST_LOCATION:-fidhash}

# If G_DEBUG is not set, for glib to segfault on warnings.
G_DEBUG=${G_DEBUG:-fatal-warnings}

function error {
    # Dump script stack
    echo "Error at:"
    local frame=0
    while caller $frame; do
        ((frame++));
    done

    echo $1
    exit 1
}

# Returns the stats log name for a CMM or CMA
# arg 1: the CMM/CMA name (such as cma1/cmm1)
# Output sample: archive-cmm1.log         (without a 2nd argument)
# Output sample: archive-cmm1-status.log  (with a 2nd argument)
function log {
    local ext
 if [ -z "$2" ]; then
        ext=""
    else
        ext="-$2"
    fi

    echo $(basename $0 .sh)-$1$ext.log
}

# Build a configuration file given the user parameters
# arg 1 = source configuration in ${CONFIGS}
# arg 2 = destination config file (default: test.conf)
function make_conf {
    local dest=${2:-test.conf}

    sed -e "s~@LHMSC_TEST_LUSTRE_MP@~$LHMSC_TEST_LUSTRE_MP~" \
         -e "s~@TASCON_TEST_BACKEND_PLUGIN@~$TASCON_TEST_BACKEND_PLUGIN~" \
         -e "s~@TASCON_TEST_BACKEND_PATH@~$TASCON_TEST_BACKEND_PATH~" \
         -e "s~@TASCON_TEST_LOCATION@~$TASCON_TEST_LOCATION~" \
         ${CONFIGS}/$1 > $dest
}

# Wait for a frontend to be active on a CMA
# arg 1 = CMA (default: cma1)
# arg 2 = frontend name (default: fe1)
# arg 3 = CMM (default: cmm1)
function wait_cma_fe {
    local cmaname=${1:-cma1}
    local fename=${2:-fe1}
    local cmmname=${3:-cmm1}

    LIM=$((`date +%s`+10))
    while :
    do
        [ `date +%s` -ge $LIM ] && error "CMA ${cmaname} not declared/started"
        grep -c "frontend $fename has been declared on CMA $cmaname" $(log $cmmname) | grep --quiet "^1$" && break
        sleep .5
    done
}

# Wait for a backend to be active on a CMA. Wait 10 seconds plus 1
# second per extra CMA.
# arg 1 = first CMA number (default: 1)
# arg 2 = last CMA number (default: 1)
# arg 3 = basename for the cma (default: cma)
# arg 4 = backend name (default: default)
# arg 5 = CMM (default: cmm1)
# arg 6 = how many similar backends to expect
function wait_cma_bk {
    local first=${1:-1}
    local last=${2:-1}
    local cmabase=${3:-cma}
    local bkname=${4:-default}
    local cmmname=${5:-cmm1}
    local howmany=${6:-1}

    LIM=$((`date +%s`+10+last-first))
 # Several CMA. In that case the cmaname is just the base
    # name. eg "cma", which will generate cma1, cma2, cma3, ...
    for i in `seq $first $last`;
    do
        while :
        do
            [ `date +%s` -ge $LIM ] && error "CMA $cmabase$i not declared/started"
            grep -c ": backend declared (op=declare_backend, backend=$bkname, cma=$cmabase$i)" $(log $cmmname) | grep --quiet "^${howmany}$" && break
            sleep .5
        done
    done
}

# Wait for a file to reach a given HSM state
# arg 1 = full file name
# arg 2 = HSM state (such as 0x00000001)
# arg 3 = timeout in seconds, defaults to 10 seconds
function wait_hsm_state {
    local timeout=${3:-10}

    # Poll state for $timeout seconds
    LIM=$((`date +%s` + timeout))
    while :
    do
        [[ `date +%s` -ge $LIM ]] && error "HSM state for file \"$1\" isn't $2"
        lfs hsm_state $1 | grep --quiet $2 && break
        sleep .5
    done
}

# Wait for a frontend to be in a given state (running, pausing, paused)
# arg 1 = state
# arg 2 = CMM (default: cmm1)
# arg 3 = frontend name (default: fe1)
function wait_frontend_state {
    local cmmname=${2:-cmm1}
    local fename=${3:-fe1}

    LIM=$((`date +%s`+10))
    while :
    do
        [ `date +%s` -ge $LIM ] && error "frontend not in desired state ($1)"
        get_status ${cmmname}
        grep -c "\b$1\b" $(log ${cmmname} status) | grep --quiet "^1$" && break
        sleep .5
    done
}

# Wait for a string in a log file
# arg 1 = string
# arg 2 = log file name (e.g. "$(log cmm1)")
# arg 3 = timeout in seconds, defaults to 10 seconds
# arg 4 = number of times the string must appears, defaults to any number
# arg 5 = sleep time between iterations, defaults to 0.5s
function wait_for_string
{
    local timeout=${3:-10}
    local howmany=${4:--1}
    local sleeptime=${5:-.5}

    if [[ $howmany -eq -1 ]]; then
        LIM=$((`date +%s` + timeout))
        while :
        do
            [ `date +%s` -ge $LIM ] && error "string '$1' not found in $2"
            grep --quiet "$1" $2 && break
            sleep $sleeptime
        done
    else
        LIM=$((`date +%s` + timeout))
        while :
        do
            [ `date +%s` -ge $LIM ] && error "string '$1' not found in $2"
            grep -c "$1" $2 | grep --quiet "^${howmany}$" && break
            sleep $sleeptime
        done
    fi
}

# Start a CMM
# arg 1 = config file (defaults is test.conf)
# arg 2 = CMM name (default = cmm1)
#
# Create a file name for a JSON dump file (tascmctl --dump=...)
function start_cmm {

    local conf=${1:-test.conf}
    local cmmname=${2:-cmm1}

    if [ "x$WITH_VALGRIND" = "x1" ]; then
        G_SLICE=always-malloc valgrind --gen-suppressions=all --suppressions=valgrind.supp --log-file=valgrind-$(basename $0)-${cmmname}.log --leak-check=full --show-leak-kinds=all --track-fds=yes $TASCMM -c ${conf} -i ${cmmname} -L debug -l $(log $cmmname) ${*:3} &
    else
        $TASCMM -c ${conf} -i ${cmmname} -L debug -l $(log $cmmname) ${*:3} &
    fi

    last_cmm_pid=$!

    LIM=$((`date +%s`+10))
    while :
    do
        [ `date +%s` -ge $LIM ] && error "frontend ${cmmname} not started"
        grep -c "Manager.*${cmmname}.*started" $(log $cmmname) | grep --quiet "^1$" && break
        sleep .5
    done

    # Create JSON file for dumping stuff:
    #   JSONFILE_cmm1 = archive-striped-cmm1-json.log
    eval JSONFILE_${cmmname}="$(basename $0 .sh)-${cmmname}-json.log"
    rm -f $JSONFILE_${cmmname}
}
# Start a CMA asynchronously. Use wait_cma to make sure it is started.
# arg 1 = config file (defaults is test.conf)
# arg 2 = CMA name (default = cma1)
function start_cma_no_wait {
    local conf=${1:-test.conf}
    local cmaname=${2:-cma1}

    if [ "x$WITH_VALGRIND" = "x1" ]; then
        G_SLICE=always-malloc valgrind --gen-suppressions=all --suppressions=valgrind.supp --log-file=valgrind-$(basename $0)-${cmaname}.log --leak-check=full --show-leak-kinds=all --track-fds=yes $TASCMA -c ${conf} -i ${cmaname} -l $(log $cmaname) -L debug ${*:3} &
    else
        $TASCMA -c ${conf} -i ${cmaname} -l $(log $cmaname) -L debug ${*:3} &
    fi

    # Create JSON file for dumping stuff:
    #   JSONFILE_cma1 = archive-striped-cma1-json.log
    eval JSONFILE_${cmaname}="$(basename $0 .sh)-${cmaname}-json.log"
    rm -f $JSONFILE_${cmaname}
}

# Wait for a cma to be started
# arg 1 = CMA name (default = cma1)
function wait_cma
{
    local cmaname=${1:-cma1}

    LIM=$((`date +%s`+10))
    while :
    do
        [ `date +%s` -ge $LIM ] && error "agent ${cmaname} not started"
        grep -c "Agent.*${cmaname}.*started" $(log $cmaname) | grep --quiet "^1$" && break
        sleep .5
    done
}
# Start one or more CMAs and wait for them to be ready
# arg 1 and 2 = index of the CMAs (default: 1 1)
# arg 3 = basename for CMA (default = cma)
# arg 4 = config file (defaults: test.conf)
# arg 5+ = extra CMA parameters
function start_cma {
    local first=${1:-1}
    local last=${2:-1}
    local cmabase=${3:-cma}
    local conf=${4:-test.conf}

    for i in `seq $first $last`;
    do
        start_cma_no_wait "$conf" "$cmabase$i" ${@:5}
    done

    for i in `seq $first $last`;
    do
        wait_cma $cmabase$i
    done
}

# Start the TAS REST daemon
# arg 1 = config file (defaults is test.conf)
#
# Skip the test if flask_restful is not installed.
function start_tasrestd {

    [ $LHMSC_RESTFUL -eq 0 ] && return

    local conf=${1:-test.conf}

    $TASRESTD -c ${conf} >& tasrestd.out &

    LIM=$((`date +%s`+10))
    while :
    do
        [ `date +%s` -ge $LIM ] && error "tasrestd not started"
        grep "Restarting with reloader" tasrestd.out && break
        grep --quiet "ImportError: No module named flask_restful" tasrestd.out && exit 77
        sleep .5
    done
}
# Check the statistics for a node. Call the test script check_stat.py.
function check_stats {
    local node=${1:-cmm1}
    local conf=${2:-test.conf}

    $(dirname $0)/check_stats.py -c ${conf} check ${node}

    return $?
}

# Check the status for a CMM. Call the test script check_status.py.
function check_status {
    local node=${1:-cmm1}
    local conf=${2:-test.conf}

    $(dirname $0)/check_status.py -c ${conf} check ${node}

    return $?
}

# Send signal to CMM and CMA
function signal_connector {
    if [ "x$WITH_VALGRIND" = "x1" ]; then
        killall -q --signal $1 memcheck-amd64-linux
    else
        killall -q --signal $1 tas_cmm
        killall -q --signal $1 tas_cma lt-tas_cma
    fi
}

# Send signal to CMM
# arg 1 = signal name (KILL, HUP, ...)
function signal_connector_cmm {
    if [ "x$WITH_VALGRIND" = "x1" ]; then
        pkill -$1 -f "^valgrind.*tas_cmm"
        #killall -q --signal $1 -r "memcheck-amd64-linux.*tas_cmm"
    else
        killall -q --signal $1 tas_cmm lt-tas_cmm
    fi
}
# Kill the CMM and wait for it
function stop_connector_cmm {
    if [ "x$WITH_VALGRIND" = "x1" ]; then
        kill $last_cmm_pid
        while ps -p $last_cmm_pid > /dev/null; do sleep 1; done;
    else
        killall -q --wait tas_cmm
    fi
}

# Kill the CMM / CMAs and wait for them
function stop_connector {
    pkill -f "python.*tasrestd.py"
    if [ "x$WITH_VALGRIND" = "x1" ]; then
        killall -q --wait memcheck-amd64-linux
    else
        killall -q --wait tas_cmm
        killall -q --wait tas_cma lt-tas_cma
    fi
    killall -q --wait stap
}
trap stop_connector EXIT

# Retrieve the statistics from one node
#  $1 = node (default: cmm1)
#  $2 = config (default: test.conf)
function get_stats {
    local node=${1:-cmm1}
    local conf=${2:-test.conf}

    rm -f $(log $node stats)
    $TASCMCTL -c $conf stats $node > $(log $node stats)
}

# Retrieve the status from one node
#  $1 = node (default: cmm1)
#  $2 = config (default: test.conf)
function get_status {
    local node=${1:-cmm1}
    local conf=${2:-test.conf}

    rm -f $(log $node status)
    $TASCMCTL -c $conf status $node > $(log $node status)
}

# Check the priority of a request using the resftful api.
# arg 1 = priority
# arg 2 = filename
function check_prio_rest {
    $CURL ${URL}/status/cmm1 || error "curl fail in check_prio_rest"

python - << EOF
import json, base64
x=json.load(open('$(log tasrestd)'))
for wr in x['queued_work']:
    if wr['op'] == 'archive':
        if base64.b64decode(wr['filename']) == '$2':
            assert(wr['priority'] == int($1))
            break
else:
    # File not found
    assert()
EOF

    [ $? -eq 0 ] || error "invalid prio (expected $1)"
}

# Check that the retcode in the JSON returned by the REST API has the
# given errno value
# arg 1 = value
function retcode_rest {
    python -c "import json; x=json.load(open('$(log tasrestd)')); assert(x['retcode']==$1)" || error "retcode_rest for value $1 failed"
}

# Get the UUID from a file, reading the extended attribute
# Returns a string similar to "b80fe92f-8bd4-4ea1-8949-4dde3a4f6b0f"
function get_uuid {
    echo $(getfattr -n trusted.tascon.uuid $1 | sed -rn 's/trusted.tascon.uuid="(.*)"/\1/p')
}

# Add a probe. It runs in the background. It can be called more than
# once. Each probe has its own log file.
PROBENUM=0
function addprobe()
{
    # Valgrind + systemtap doesn't work. Skip the test.
    if [ "x$WITH_VALGRIND" = "x1" ]; then
       exit 77
    fi

    PROBENUM=$((PROBENUM + 1))
    local LOG=$(log stap-$PROBENUM)

    stap -v -g -e "$1" >& $LOG &

    # Poll for 20 seconds to give time to compile
    LIM=$((`date +%s`+20))
    while :
    do
        [ `date +%s` -ge $LIM ] && error "stap probe not running"
        grep --quiet "Pass 5: starting run" $LOG && break
        grep -i error $LOG && exit "error found in probe"
        sleep .5
    done
}
# Count the number of regex occurence in a file and bail out if the
# count is not right
# arg 1 = count
# arg 2 = grep regex
# arg 3 = filename(s)
function ecount()
{
    local C=$(egrep "$2" ${@:3} | wc -l)

    [ $C -eq $1 ] || error "counted $C, expected $1 for \"$2\" in ${@:3}"
}

# Ensure the various environment variables are set
${LHMSC_TEST_LUSTRE_MP+"false"} && error "LHMSC_TEST_LUSTRE_MP not set or empty"
${LHMSC_TEST_LUSTRE_TESTPATH+"false"} && error "LHMSC_TEST_LUSTRE_TESTPATH not set or empty"
${TASCON_TEST_BACKEND_PLUGIN+"false"} && error "TASCON_TEST_BACKEND_PLUGIN not set or empty"
${TASCON_TEST_BACKEND_PATH+"false"} && error "TASCON_TEST_BACKEND_PATH not set or empty"

[ -d $LHMSC_TEST_LUSTRE_MP ] || error "Path in LHMSC_TEST_LUSTRE_MP doesn't exist"

BACKEND_DIR=$TASCON_TEST_BACKEND_PATH
[ -d $BACKEND_DIR ] || error "directory $TASCON_TEST_BACKEND_PATH doesn't exist"

CURL="curl --silent --fail --output $(log tasrestd)"
URL="http://0.0.0.0:6207"
LHMSC_RESTFUL=${LHMSC_RESTFUL:-0}

# Cleanup on startup - Remove remnants of previous tests
killall -q --wait memcheck-amd64-linux
killall -q --wait tas_cmm
killall -q --wait tas_cma lt-tas_cma
killall -q --wait stap
pkill -f "python.*tasrestd.py"
rm -f $(basename $0 .sh)-cmm*.log
rm -f $(basename $0 .sh)-cma*.log
rm -f /tmp/fooevent
rm -f test.conf
rm -f tasrestd.out $(log tasrestd)
