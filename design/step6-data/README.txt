Step 6 measurements. See design/scale-free-diagnosis.md for what these are
and how to regenerate them; the notes cite these files.

1node/  2^20 vertices, 16 edges/vertex, 16 PEs, one exclusive Delta cpu node,
        +setcpuaffinity, median of five. Slurm 22006448 on cn046.
2node/  the same, H4 only, on two nodes. Slurm 22006452 on cn055 and cn117.
        H4 is about the cost of a controller round, and a round is a
        reduction and a broadcast, so one node answers an easier question.

Run logs are not kept -- 736 MB of them, and everything the notes cite is
in the summaries here.
