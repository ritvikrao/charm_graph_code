#!/bin/bash
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --output=graph_serial.out
#SBATCH --partition=cpu
#SBATCH --account=mzu-delta-cpu
#SBATCH --job-name=weighted_htram_smp
#SBATCH --time=00:10:00      # hh:mm:ss for the job

srun -n $SLURM_NTASKS ./graph_serial 100000 graphs/big_graph.csv 100