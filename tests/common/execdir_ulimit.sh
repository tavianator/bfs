rm -rf scratch/*
mkdir -p scratch/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/q/r/s/t/u/v/w/x/y/z
mkdir -p scratch/a/b/c/d/e/f/g/h/i/j/k/l/m/0/1/2/3/4/5/6/7/8/9/A/B/C

closefrom 4
ulimit -n 13
bfs_diff scratch -execdir echo {} \;
