#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=48
#SBATCH --cpus-per-task=1
#SBATCH --output=sssp_nonsmp.out
#SBATCH --partition=cpu
#SBATCH --account=mzu-delta-cpu
#SBATCH --job-name=sssp_nonsmp
#SBATCH --exclusive
#SBATCH --time=00:10:00      # hh:mm:ss for the job

srun -n $SLURM_NTASKS ./sssp_nonsmp $SLURM_NTASKS 100000 graphs/big_graph.csv 100 1