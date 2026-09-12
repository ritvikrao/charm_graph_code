#!/bin/bash
#SBATCH -A mzu-delta-cpu
#SBATCH -p cpu
#SBATCH -N 2
#SBATCH --ntasks-per-node=1
#SBATCH --exclusive
#SBATCH -t 00:30:00
#SBATCH -J sssp-2node
#
# Multi-node correctness gate for sssp_smp.
#
# scripts/verify.sh cannot see one whole class of defect. Inside a process an
# htram send hands the receiver a pointer, so the receiver reads the entire
# allocation whatever the message envelope says; an envelope that under-states
# its payload is therefore invisible at CkNumNodes() == 1 no matter how many
# PEs are used. Only a message that leaves the address space is truncated to
# its declared size. Two processes on two nodes is the smallest configuration
# that exercises that, and this script is that gate.
#
# It relies on htram's receive-side envelope check (on unless the library is
# built with -DHTRAM_NO_ENVELOPE_CHECK), which aborts if an arriving message's
# envelope is shorter than the items it claims to carry. --verify alone is not
# enough: with 32-bit-range distances a truncated item loses only a zero high
# word, so the digests still match. That is exactly how this went unnoticed.
#
# Usage:
#   sbatch scripts/verify_2node.sh     # from the repository root
# Environment:
#   SSSP_BIN   binary to test          (default: ./sssp_smp next to this repo)
#   SSSP_LOGS  where to write logs     (default: ./logs-2node)
#   SSSP_NDEV  +lci_ndevices           (default: 4)
#   SSSP_EXTRA_ARGS solver options added to every run, e.g. "--combine hold".
#              Step 7 runs this gate with each mechanism on as well as off.
#
# --exclusive is required, not a courtesy: without it Slurm hands the task a
# cpuset that need not contain the cores +pemap names, and every PE aborts in
# CmiSetCPUAffinity.
set -uo pipefail
# Under sbatch, $0 is Slurm's private copy of this script in its spool
# directory, so dirname "$0" is not the repository and every run would exec a
# binary that is not there. Submit from the repository root.
if [ -n "${SLURM_SUBMIT_DIR:-}" ]; then
  cd "$SLURM_SUBMIT_DIR" || exit 1
else
  cd "$(dirname "$0")/.." || exit 1
fi

BIN=${SSSP_BIN:-$PWD/sssp_smp}
LOGS=${SSSP_LOGS:-$PWD/logs-2node}
NDEV=${SSSP_NDEV:-4}
read -r -a EXTRA_ARGS <<< "${SSSP_EXTRA_ARGS:-}"
mkdir -p "$LOGS"

# Cray PMI2 allots a job 21 KVS entries; LCI's OFI bootstrap publishes one key
# per rank per device, so two processes are far below that. Free insurance.
export PMI_MAX_KVS_ENTRIES=1000
# Slingshot's CXI provider dies in flow control ("LE resources not recovered")
# once enough processes share a NIC. Two nodes do not reach that, but this is
# the configuration these nodes are known to work under.
export FI_CXI_RX_MATCH_MODE=hybrid

failures=0

# tag ppn pemap args...
run_one() {
  local tag=$1 ppn=$2 map=$3; shift 3
  local log=$LOGS/${tag}.log
  {
    echo "## $tag ppn=$ppn args=$*"
    echo "## LCI_ATTR_PACKET_SIZE=${LCI_ATTR_PACKET_SIZE:-<default>} ndev=$NDEV"
    timeout -s KILL 600 srun -N 2 -n 2 --ntasks-per-node=1 --cpu-bind=none \
        --unbuffered --kill-on-bad-exit=1 \
        "$BIN" "$@" ${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"} \
        +ppn "$ppn" +pemap "$map" +lci_ndevices "$NDEV"
    echo "## exit=$?"
  } > "$log" 2>&1
  # A killed srun leaves the Charm++ processes spinning and holding the nodes,
  # so the next configuration queues behind them and looks like a hang.
  for st in $(squeue -h -s -j "${SLURM_JOB_ID:-0}" -o %i 2>/dev/null | grep -v extern); do
    scancel --signal=KILL "$st" 2>/dev/null
  done
  srun --overlap -N 2 -n 2 pkill -9 -f '[s]ssp_smp' >/dev/null 2>&1
  sleep 2
  printf '%-28s ' "$tag"
  if grep -q "under-sized it" "$log"; then
    echo "FAIL: envelope check fired -- $(grep -m1 -o 'declaring .*' "$log")"
    failures=$((failures + 1))
  elif grep -q "^VERIFY PASS" "$log"; then
    echo "PASS  $(grep -m1 'Compute time' "$log")"
  else
    echo "FAIL: no VERIFY PASS (see $log)"
    failures=$((failures + 1))
  fi
}

# 10k/160k uniform random, the first row of scripts/verify.sh's matrix.
G_RANDOM="10000 160000 1 1 1 0.999 0.005 --verify --timeout 300"
# 2-D mesh: a far longer critical path, so many more rounds of partial flushes.
G_MESH="40000 0 3 0 2 0.999 0.005 --verify --timeout 300"
# Large enough that full buffers and partial ones both occur in quantity.
G_BIG="200000 3200000 1 1 1 0.999 0.005 --verify --timeout 300"
# RMAT: hub traffic, which is what source-side combining folds.
G_RMAT="65536 1048576 1 42 3 0.999 0.005 --verify --timeout 300"

echo "### binary: $BIN"
echo "### nodes:  ${SLURM_JOB_NODELIST:-<none>}"
echo "### extra:  ${SSSP_EXTRA_ARGS:-<none>}"

# Buffer size decides how many sends are partial flushes: at bufSize == BUFSIZE
# nearly every size-triggered send fills its allocation exactly and an
# under-sized envelope has nothing to expose, while small buffers make partial
# flushes the common case. Sweeping it is what makes this gate sensitive.
for bs in 1 7 63 256 1024 2048; do
  run_one "random_bs${bs}" 8 0-7 $G_RANDOM --bufsize $bs
done

# LCI_ATTR_PACKET_SIZE decides whether a message goes eagerly in one packet or
# through the rendezvous path. An htram message is 16 + 24*bufSize bytes, so at
# bufsize 256 (6160 B) these three settings put it on either side of the line.
for ps in 4096 16384 65536; do
  LCI_ATTR_PACKET_SIZE=$ps run_one "random_bs256_ps${ps}" 8 0-7 $G_RANDOM --bufsize 256
done

run_one "mesh_bs63"    8    0-7  $G_MESH --bufsize 63
run_one "mesh_bs2048"  8    0-7  $G_MESH --bufsize 2048
run_one "big_bs256"    15   0-14 $G_BIG  --bufsize 256
run_one "big_bs2048"   15   0-14 $G_BIG  --bufsize 2048
run_one "rmat_bs63"    8    0-7  $G_RMAT --bufsize 63
run_one "rmat_bs2048"  8    0-7  $G_RMAT --bufsize 2048

if [ $failures -ne 0 ]; then
  echo "2-NODE GATE FAILED ($failures)"
  exit 1
fi
echo "2-NODE GATE PASSED"
