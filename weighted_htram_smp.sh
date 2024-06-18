#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=8
#SBATCH --cpus-per-task=8
#SBATCH --output=weighted_htram_smp.out
#SBATCH --partition=cpu
#SBATCH --account=mzu-delta-cpu
#SBATCH --job-name=weighted_htram_smp
#SBATCH --exclusive
#SBATCH --time=00:10:00      # hh:mm:ss for the job

# module swap PrgEnv-cray PrgEnv-gnu
#mkdir weighted_htram_Smp_projections
export PPN=6
# +commap 0-31:8 +pemap 1-31:8.6
srun -n $SLURM_NTASKS ./weighted_htram_smp $((SLURM_NTASKS*PPN)) 10000000 graphs/new_mega_graph.csv 100 1 +ppn $PPN +setcpuaffinity # +traceroot weighted_htram_Smp_projections