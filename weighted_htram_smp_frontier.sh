#!/bin/bash
#SBATCH -N 1
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=7
#SBATCH --output=weighted_htram_smp.out
#SBATCH -A bip249
#SBATCH --job-name=weighted_htram_smp
#SBATCH --exclusive
#SBATCH --time=00:10:00      # hh:mm:ss for the job

# module swap PrgEnv-cray PrgEnv-gnu
#mkdir weighted_htram_Smp_projections
export PPN=6
# +commap 0-31:8 +pemap 1-31:8.6
# single node: --network=single_node_vni
#srun --network=single_node_vni ./weighted_htram_smp 62500000 /lustre/orion/bip249/scratch/rrao/1B.csv 100 1 0 0.995 0.05 +ppn $PPN +setcpuaffinity #+traceroot weighted_htram_Smp_projections +logsize 10000000
#srun --network=single_node_vni ./weighted_htram_smp 62500000 1000000000 100 1 1 0.995 0.05 +ppn $PPN +setcpuaffinity #+traceroot weighted_htram_Smp_projections +logsize 10000000
srun --network=single_node_vni ./weighted_htram_smp 999950844 3999803536 100 1 2 0.995 0.05 +ppn $PPN +setcpuaffinity
