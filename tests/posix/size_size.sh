# Regression test: the optimizer used to ignore -size units

cd "$TEST"

printf '.' >./1

dd if=./1 of=./511 bs=1 seek=510
dd if=./1 of=./512 bs=1 seek=511
dd if=./1 of=./513 bs=1 seek=512

bfs_diff . -type f -size 1 -size 511c -exec printf '511: %s\n' {} \; \
    -o -type f -size -2 -size 512c -exec printf '512: %s\n' {} \; \
    -o -type f -size +1 -size 513c -exec printf '513: %s\n' {} \;
