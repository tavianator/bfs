# Regression test: don't abort on incomplete UTF-8 sequences
export LC_ALL=$(locale -a | grep -Ei 'utf-?8$' | head -n1)
test -n "$LC_ALL" || skip
! invoke_bfs -samefile $'\xFA\xFA'
