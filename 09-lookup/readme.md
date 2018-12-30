Description of each file in this folder:

- `forwaring-table.txt` is the original mapping table from ip address to interface id given by professor.

- `prefix_2node.c` is single-bit prefix tree algorithm, which outputs the golden result for advanced algorithms
- `result-step0.txt` is the golden result generated by `prefix_2node.c`
- `prefix_nnode.c` is multiple bits prefix tree algorithm, including leaf pushing, compressed-pointers-tree and compressed-vector-tree. We check the correctness by comparing its output with `result-step0.txt`
- `report.pdf` report of this lab