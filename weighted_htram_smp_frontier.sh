#!/bin/bash
#SBATCH -N 4
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=7
#SBATCH --output=weighted_htram_smp_4.out
#SBATCH -A bip249
#SBATCH --job-name=weighted_htram_smp
#SBATCH --exclusive
#SBATCH --time=00:10:00      # hh:mm:ss for the job

# module swap PrgEnv-cray PrgEnv-gnu
# mkdir weighted_htram_Smp_projections
export PPN=6
# +commap 0-31:8 +pemap 1-31:8.6
# single node: --network=single_node_vni
# srun --network=single_node_vni ./weighted_htram_smp 62500000 /lustre/orion/bip249/scratch/rrao/1B.csv 100 1 +ppn 6 +setcpuaffinity #+traceroot weighted_htram_Smp_projections
# srun --network=single_node_vni ./weighted_htram_smp 20000000 320000000 100 1 1 +ppn 6 +setcpuaffinity #+traceroot weighted_htram_Smp_projections
srun ./weighted_htram_smp 62500000 /lustre/orion/bip249/scratch/rrao/1B.csv 100 1 0 0.999 0.05 +ppn $PPN +setcpuaffinity #+traceroot weighted_htram_Smp_projections