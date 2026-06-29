# Regression test: the optimizer used to ignore -size units

cd "$TEST"

dd if=/dev/null of=511 bs=1 seek=511 count=0
dd if=/dev/null of=512 bs=1 seek=512 count=0
dd if=/dev/null of=513 bs=1 seek=513 count=0

bfs_diff . -size 1 -size 511c -exec printf '511: %s\n' {} \; \
    -o -size -2 -size 512c -exec printf '512: %s\n' {} \; \
    -o -size +1 -size 513c -exec printf '513: %s\n' {} \;
