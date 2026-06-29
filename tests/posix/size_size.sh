# Regression test: the optimizer used to ignore -size units

cd "$TEST"

printf '' >./0
printf . >./1

printf '%255s' . >./255
printf '%256s' . >./256
printf '%257s' . >./257

printf '%511s' . >./511
printf '%512s' . >./512
printf '%513s' . >./513

printf '%1023s' . >./1023
printf '%1024s' . >./1024
printf '%1025s' . >./1025

bfs_diff . -type f -size 1 -size 511c -exec printf '511: %s\n' {} \; \
    -o -type f -size -2 -size 512c -exec printf '512: %s\n' {} \; \
    -o -type f -size +1 -size 513c -exec printf '513: %s\n' {} \;
