#!/usr/bin/env bash
# Default values
counter=0
server_port=5683
record=SERVER
version=latest
experiment=none
klee="${KLEE:-/artifact/tools/kleener-coap/build/bin/klee}"
[ -x "$klee" ] || klee=klee

# Help function
function show_help()
{
    printf "Usage: run.sh <options> \n"
    printf "Options are: \n"
    printf "    -e  <experiment> \n"
    printf "    -r  <states to check> [default=SERVER]\n"
    printf "    -V  <version> [default=latest]\n"
    printf "    -p  <port_number> [default=5683]\n"
    printf "    -h  shows this help \n\n"

    printf "experiments are: \n"
    printf "    code, version, type, token-length, mid, type-code, token \n"

    printf "states to check are: \n"
    printf "    SERVER \t {the CoAP server is the SUT} \n"

    printf "versions are: \n"
    printf "    latest \t {LibCoAP 4.3.5} \n"
    printf "    original \t {LibCoAP 4.3.1} \n"
    exit 0
}

# Get options using flags
while getopts e:r:V:p:h flag
do
    case "${flag}" in
        e) experiment=${OPTARG};;
        r) record=${OPTARG};;
        V) version=${OPTARG};;
        p) server_port=${OPTARG};;
        h) show_help;;
        \?) echo "Unknown option -$OPTARG"; exit 1;;
    esac
done

# Setting some Env variables required for KLEE
export SERVER_PORT=$server_port
export STATE_TO_CHECK=$record
export PROTOCOL=CoAP

case $experiment in
    code)                   export KLEE_SYMBOLIC_EXPERIMENT=100;;
    version)                export KLEE_SYMBOLIC_EXPERIMENT=101;;
    type)                   export KLEE_SYMBOLIC_EXPERIMENT=102;;
    token-length)           export KLEE_SYMBOLIC_EXPERIMENT=103;;
    mid)                    export KLEE_SYMBOLIC_EXPERIMENT=104;;
    type-code)              export KLEE_SYMBOLIC_EXPERIMENT=105;;
    token)                  export KLEE_SYMBOLIC_EXPERIMENT=109;;
    *) echo "Unknown experiment: $experiment (see -h)"; exit 1;;
esac
printf "%s (%s)\n" "$experiment" "$KLEE_SYMBOLIC_EXPERIMENT"

case $version in
    latest)   bitcode=libcoap-435-linked.bc;;
    original) bitcode=libcoap-431-linked.bc;;
    *) echo "Unknown version: $version (see -h)"; exit 1;;
esac

while [ -d "$version-$experiment-$record-$counter" ]
do
    ((counter = counter + 1))
done

# DFS finishes each path before the next, since paths share the real loopback sockets
"$klee" -write-smt2s --libc=uclibc --posix-runtime --search=dfs --solver-backend=z3 --max-memory=32000 --max-time=24h --output-dir=$version-$experiment-$record-$counter ./$bitcode
