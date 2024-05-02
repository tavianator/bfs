#!/hint/bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

declare -gA URLS=(
    [chromium]="https://chromium.googlesource.com/chromium/src.git"
    [linux]="https://github.com/torvalds/linux.git"
    [rust]="https://github.com/rust-lang/rust.git"
)

declare -gA TAGS=(
    [chromium]=119.0.6036.2
    [linux]=v6.5
    [rust]=1.72.1
)

COMPLETE_DEFAULT=(linux rust chromium)
EARLY_QUIT_DEFAULT=(chromium)
STAT_DEFAULT=(rust)
PRINT_DEFAULT=(linux)
STRATEGIES_DEFAULT=(rust)
JOBS_DEFAULT=(rust)
EXEC_DEFAULT=(linux)

usage() {
    printf 'Usage: tailfin run %s\n' "${BASH_SOURCE[0]}"
    printf '           [--default] [--<BENCHMARK> [--<BENCHMARK>...]]\n'
    printf '           [--build=...] [--bfs] [--find] [--fd]\n'
    printf '           [--no-clean] [--help]\n\n'

    printf '  --default\n'
    printf '      Run the default set of benchmarks\n\n'

    printf '  --complete[=CORPUS]\n'
    printf '      Complete traversal benchmark.\n'
    printf '      Default corpus is --complete="%s"\n\n' "${COMPLETE_DEFAULT[*]}"

    printf '  --early-quit[=CORPUS]\n'
    printf '      Early quitting benchmark.\n'
    printf '      Default corpus is --early-quit=%s\n\n' "${EARLY_QUIT_DEFAULT[*]}"

    printf '  --stat[=CORPUS]\n'
    printf '      Traversal with stat().\n'
    printf '      Default corpus is --stat=%s\n\n' "${STAT_DEFAULT[*]}"

    printf '  --print[=CORPUS]\n'
    printf '      Path printing benchmark.\n'
    printf '      Default corpus is --print=%s\n\n' "${PRINT_DEFAULT[*]}"

    printf '  --strategies[=CORPUS]\n'
    printf '      Search strategy benchmark.\n'
    printf '      Default corpus is --strategies=%s\n\n' "${STRATEGIES_DEFAULT[*]}"

    printf '  --jobs[=CORPUS]\n'
    printf '      Parallelism benchmark.\n'
    printf '      Default corpus is --jobs=%s\n\n' "${JOBS_DEFAULT[*]}"

    printf '  --exec[=CORPUS]\n'
    printf '      Process spawning benchmark.\n'
    printf '      Default corpus is --exec=%s\n\n' "${EXEC_DEFAULT[*]}"

    printf '  --build=COMMIT\n'
    printf '      Build this bfs commit and benchmark it.  Specify multiple times to\n'
    printf '      compare, e.g. --build=3.0.1 --build=3.0.2\n\n'

    printf '  --bfs[=COMMAND]\n'
    printf '      Benchmark an existing build of bfs\n\n'

    printf '  --find[=COMMAND]\n'
    printf '      Compare against find\n\n'

    printf '  --fd[=COMMAND]\n'
    printf '      Compare against fd\n\n'

    printf '  --no-clean\n'
    printf '      Use any existing corpora as-is\n\n'

    printf '  --help\n'
    printf '      This message\n\n'
}

# Hack to export an array
export_array() {
    local str=$(declare -p "$1" | sed 's/ -a / -ga /')
    unset "$1"
    export "$1=$str"
}

# Hack to import an array
import_array() {
    local cmd="${!1}"
    unset "$1"
    eval "$cmd"
}

# Set up the benchmarks
setup() {
    ROOT=$(realpath -- "$(dirname -- "${BASH_SOURCE[0]}")/..")
    if ! [ "$PWD" -ef "$ROOT" ]; then
        printf 'error: Please run this script from %s\n\n' "$ROOT" >&2
        usage >&2
        exit $EX_USAGE
    fi

    nproc=$(nproc)

    # Options

    CLEAN=1

    BUILD=()
    BFS=()
    FIND=()
    FD=()

    COMPLETE=()
    EARLY_QUIT=()
    STAT=()
    PRINT=()
    STRATEGIES=()
    JOBS=()
    EXEC=()

    for arg; do
        case "$arg" in
            # Flags
            --no-clean)
                CLEAN=0
                ;;
            # bfs commits/tags to benchmark
            --build=*)
                BUILD+=("${arg#*=}")
                BFS+=("bfs-${arg#*=}")
                ;;
            # Utilities to benchmark against
            --bfs)
                BFS+=(bfs)
                ;;
            --bfs=*)
                BFS+=("${arg#*=}")
                ;;
            --find)
                FIND+=(find)
                ;;
            --find=*)
                FIND+=("${arg#*=}")
                ;;
            --fd)
                FD+=(fd)
                ;;
            --fd=*)
                FD+=("${arg#*=}")
                ;;
            # Benchmark groups
            --complete)
                COMPLETE=("${COMPLETE_DEFAULT[@]}")
                ;;
            --complete=*)
                read -ra COMPLETE <<<"${arg#*=}"
                ;;
            --early-quit)
                EARLY_QUIT=("${EARLY_QUIT_DEFAULT[@]}")
                ;;
            --early-quit=*)
                read -ra EARLY_QUIT <<<"${arg#*=}"
                ;;
            --stat)
                STAT=("${STAT_DEFAULT[@]}")
                ;;
            --stat=*)
                read -ra STAT <<<"${arg#*=}"
                ;;
            --print)
                PRINT=("${PRINT_DEFAULT[@]}")
                ;;
            --print=*)
                read -ra PRINT <<<"${arg#*=}"
                ;;
            --strategies)
                STRATEGIES=("${STRATEGIES_DEFAULT[@]}")
                ;;
            --strategies=*)
                read -ra STRATEGIES <<<"${arg#*=}"
                ;;
            --jobs)
                JOBS=("${JOBS_DEFAULT[@]}")
                ;;
            --jobs=*)
                read -ra JOBS <<<"${arg#*=}"
                ;;
            --exec)
                EXEC=("${EXEC_DEFAULT[@]}")
                ;;
            --exec=*)
                read -ra EXEC <<<"${arg#*=}"
                ;;
            --default)
                COMPLETE=("${COMPLETE_DEFAULT[@]}")
                EARLY_QUIT=("${EARLY_QUIT_DEFAULT[@]}")
                STAT=("${STAT_DEFAULT[@]}")
                PRINT=("${PRINT_DEFAULT[@]}")
                STRATEGIES=("${STRATEGIES_DEFAULT[@]}")
                JOBS=("${JOBS_DEFAULT[@]}")
                EXEC=("${EXEC_DEFAULT[@]}")
                ;;
            --help)
                usage
                exit
                ;;
            *)
                printf 'error: Unknown option %q\n\n' "$arg" >&2
                usage >&2
                exit $EX_USAGE
                ;;
        esac
    done

    if ((UID == 0)); then
        max-freq
    fi

    echo "Building bfs ..."
    as-user ./configure --enable-release
    as-user make -s -j"$nproc" all

    as-user mkdir -p bench/corpus

    declare -A cloned=()
    for corpus in "${COMPLETE[@]}" "${EARLY_QUIT[@]}" "${STAT[@]}" "${PRINT[@]}" "${STRATEGIES[@]}" "${JOBS[@]}" "${EXEC[@]}"; do
        if ((cloned["$corpus"])); then
            continue
        fi
        cloned["$corpus"]=1

        dir="bench/corpus/$corpus"
        if ((CLEAN)) || ! [ -e "$dir" ]; then
            as-user ./bench/clone-tree.sh "${URLS[$corpus]}" "${TAGS[$corpus]}" "$dir"{,.git}
        fi
    done

    if ((${#BUILD[@]} > 0)); then
        echo "Creating bfs worktree ..."

        worktree="bench/worktree"
        as-user git worktree add -qd "$worktree"
        defer as-user git worktree remove "$worktree"

        bin="$(realpath -- "$SETUP_DIR")/bin"
        as-user mkdir "$bin"

        for commit in "${BUILD[@]}"; do
            (
                echo "Building bfs $commit ..."
                cd "$worktree"
                as-user git checkout -qd "$commit" --
                if [ -e configure ]; then
                    as-user ./configure --enable-release
                    as-user make -s -j"$nproc"
                else
                    as-user make -s -j"$nproc" release
                fi
                if [ -e ./bin/bfs ]; then
                    as-user cp ./bin/bfs "$bin/bfs-$commit"
                else
                    as-user cp ./bfs "$bin/bfs-$commit"
                fi
                as-user make -s clean
            )
        done

        # $SETUP_DIR contains `:` so it won't work in $PATH
        # Work around this with a symlink
        tmp=$(as-user mktemp)
        as-user ln -sf "$bin" "$tmp"
        defer rm "$tmp"
        export PATH="$tmp:$PATH"
    fi

    export_array BFS
    export_array FIND
    export_array FD

    export_array COMPLETE
    export_array EARLY_QUIT
    export_array STAT
    export_array PRINT
    export_array STRATEGIES
    export_array JOBS
    export_array EXEC

    if ((UID == 0)); then
        turbo-off
    fi

    sync
}

# Runs hyperfine and saves the output
do-hyperfine() {
    local tmp_md="$BENCH_DIR/.bench.md"
    local md="$BENCH_DIR/bench.md"
    local tmp_json="$BENCH_DIR/.bench.json"
    local json="$BENCH_DIR/bench.json"

    if (($# == 0)); then
        printf 'Nothing to do\n\n' | tee -a "$md"
        return 1
    fi

    hyperfine -w2 -M20 --export-markdown="$tmp_md" --export-json="$tmp_json" "$@" &>/dev/tty
    cat "$tmp_md" >>"$md"
    cat "$tmp_json" >>"$json"
    rm "$tmp_md" "$tmp_json"

    printf '\n' | tee -a "$md"
}

# Print the header for a benchmark group
group() {
    printf "## $1\\n\\n" "${@:2}" | tee -a "$BENCH_DIR/bench.md"
}

# Print the header for a benchmark subgroup
subgroup() {
    printf "### $1\\n\\n" "${@:2}" | tee -a "$BENCH_DIR/bench.md"
}

# Print the header for a benchmark sub-subgroup
subsubgroup() {
    printf "#### $1\\n\\n" "${@:2}" | tee -a "$BENCH_DIR/bench.md"
}

# Benchmark the complete traversal of a directory tree
# (without printing anything)
bench-complete-corpus() {
    total=$(./bin/bfs "$2" -printf '.' | wc -c)

    subgroup "%s (%'d files)" "$1" "$total"

    cmds=()
    for bfs in "${BFS[@]}"; do
        cmds+=("$bfs $2 -false")
    done

    for find in "${FIND[@]}"; do
        cmds+=("$find $2 -false")
    done

    for fd in "${FD[@]}"; do
        cmds+=("$fd -u '^$' $2")
    done

    do-hyperfine "${cmds[@]}"
}

# All complete traversal benchmarks
bench-complete() {
    if (($#)); then
        group "Complete traversal"

        for corpus; do
            bench-complete-corpus "$corpus ${TAGS[$corpus]}" "bench/corpus/$corpus"
        done
    fi
}

# Benchmark quiting as soon as a file is seen
bench-early-quit-corpus() {
    dir="$2"
    max_depth=$(./bin/bfs "$dir" -printf '%d\n' | sort -rn | head -n1)

    subgroup '%s (depth %d)' "$1" "$max_depth"

    # Save the list of unique filenames, along with their depth
    UNIQ="$BENCH_DIR/uniq"
    ./bin/bfs "$dir" -printf '%d %f\n' | sort -k2 | uniq -uf1 >"$UNIQ"

    for ((i = 2; i <= max_depth; i *= 2)); do
        subsubgroup 'Depth %d' "$i"

        # Sample random uniquely-named files at depth $i
        export FILES="$BENCH_DIR/uniq-$i"
        sed -n "s/^$i //p" "$UNIQ" | shuf -n20 >"$FILES"
        if ! [ -s "$FILES" ]; then
            continue
        fi

        cmds=()
        for bfs in "${BFS[@]}"; do
            cmds+=("$bfs $dir -name \$(shuf -n1 \$FILES) -print -quit")
        done

        for find in "${FIND[@]}"; do
            cmds+=("$find $dir -name \$(shuf -n1 \$FILES) -print -quit")
        done

        for fd in "${FD[@]}"; do
            cmds+=("$fd -usg1 \$(shuf -n1 \$FILES) $dir")
        done

        do-hyperfine "${cmds[@]}"
    done
}

# All early-quitting benchmarks
bench-early-quit() {
    if (($#)); then
        group "Early termination"

        for corpus; do
            bench-early-quit-corpus "$corpus ${TAGS[$corpus]}" "bench/corpus/$corpus"
        done
    fi
}

# Benchmark traversal with stat()
bench-stat-corpus() {
    total=$(./bin/bfs "$2" -printf '.' | wc -c)

    subgroup "%s (%'d files)" "$1" "$total"

    cmds=()
    for bfs in "${BFS[@]}"; do
        cmds+=("$bfs $2 -size 1024G")
    done

    for find in "${FIND[@]}"; do
        cmds+=("$find $2 -size 1024G")
    done

    for fd in "${FD[@]}"; do
        cmds+=("$fd -u --search-path $2 --size 1024Gi")
    done

    do-hyperfine "${cmds[@]}"
}

# stat() benchmarks
bench-stat() {
    if (($#)); then
        group "Traversal with stat()"

        for corpus; do
            bench-stat-corpus "$corpus ${TAGS[$corpus]}" "bench/corpus/$corpus"
        done
    fi
}

# Benchmark printing paths without colors
bench-print-nocolor() {
    subsubgroup '%s' "$1"

    cmds=()
    for bfs in "${BFS[@]}"; do
        cmds+=("$bfs $2")
    done

    for find in "${FIND[@]}"; do
        cmds+=("$find $2")
    done

    for fd in "${FD[@]}"; do
        cmds+=("$fd -u --search-path $2")
    done

    do-hyperfine "${cmds[@]}"
}

# Benchmark printing paths with colors
bench-print-color() {
    subsubgroup '%s' "$1"

    cmds=()
    for bfs in "${BFS[@]}"; do
        cmds+=("$bfs $2 -color")
    done

    for fd in "${FD[@]}"; do
        cmds+=("$fd -u --search-path $2 --color=always")
    done

    do-hyperfine "${cmds[@]}"
}

# All printing benchmarks
bench-print() {
    if (($#)); then
        group "Printing paths"

        subgroup "Without colors"
        for corpus; do
            bench-print-nocolor "$corpus ${TAGS[$corpus]}" "bench/corpus/$corpus"
        done

        subgroup "With colors"
        for corpus; do
            bench-print-color "$corpus ${TAGS[$corpus]}" "bench/corpus/$corpus"
        done
    fi
}

# Benchmark search strategies
bench-strategies-corpus() {
    subgroup '%s' "$1"

    if ((${#BFS[@]} == 1)); then
        cmds=("$BFS -S "{bfs,dfs,ids,eds}" $2 -false")
        do-hyperfine "${cmds[@]}"
    else
        for S in bfs dfs ids eds; do
            subsubgroup '`-S %s`' "$S"

            cmds=()
            for bfs in "${BFS[@]}"; do
                cmds+=("$bfs -S $S $2 -false")
            done
            do-hyperfine "${cmds[@]}"
        done
    fi
}

# All search strategy benchmarks
bench-strategies() {
    if (($#)); then
        group "Search strategies"

        for corpus; do
            bench-strategies-corpus "$corpus ${TAGS[$corpus]}" "bench/corpus/$corpus"
        done
    fi
}

# Benchmark parallelism
bench-jobs-corpus() {
    subgroup '%s' "$1"

    if ((${#BFS[@]} + ${#FD[@]} == 1)); then
        cmds=()
        for bfs in "${BFS[@]}"; do
            if "$bfs" -j1 -quit &>/dev/null; then
                cmds+=("$bfs -j"{1,2,3,4,6,8,12,16}" $2 -false")
            else
                cmds+=("$bfs $2 -false")
            fi
        done

        for fd in "${FD[@]}"; do
            cmds+=("$fd -j"{1,2,3,4,6,8,12,16}" -u '^$' $2")
        done

        do-hyperfine "${cmds[@]}"
    else
        for j in 1 2 3 4 6 8 12 16; do
            subsubgroup '`-j%d`' $j

            cmds=()
            for bfs in "${BFS[@]}"; do
                if "$bfs" -j1 -quit &>/dev/null; then
                    cmds+=("$bfs -j$j $2 -false")
                elif ((j == 1)); then
                    cmds+=("$bfs $2 -false")
                fi
            done

            for fd in "${FD[@]}"; do
                cmds+=("$fd -j$j -u '^$' $2")
            done

            if ((${#cmds[@]})); then
                do-hyperfine "${cmds[@]}"
            fi
        done
    fi
}

# All parallelism benchmarks
bench-jobs() {
    if (($#)); then
        group "Parallelism"

        for corpus; do
            bench-jobs-corpus "$corpus ${TAGS[$corpus]}" "bench/corpus/$corpus"
        done
    fi
}

# One file/process
bench-exec-single() {
    subsubgroup "One file per process"

    cmds=()
    for cmd in "${BFS[@]}" "${FIND[@]}"; do
        cmds+=("$cmd $1 -maxdepth 2 -exec true -- {} \;")
    done

    for fd in "${FD[@]}"; do
        cmds+=("$fd -u --search-path $1 --max-depth=2 -x true --")
        # Without -j1, fd runs multiple processes in parallel, which is unfair
        cmds+=("$fd -j1 -u --search-path $1 --max-depth=2 -x true --")
    done

    do-hyperfine "${cmds[@]}"
}

# Many files/process
bench-exec-multi() {
    subsubgroup "Many files per process"

    cmds=()
    for cmd in "${BFS[@]}" "${FIND[@]}"; do
        cmds+=("$cmd $1 -exec true -- {} +")
    done

    for fd in "${FD[@]}"; do
        cmds+=("$fd -u --search-path $1 -X true --")
    done

    do-hyperfine "${cmds[@]}"
}

# Many files, same dir
bench-exec-chdir() {
    if ((${#BFS[@]} + ${#FIND[@]} == 0)); then
        return
    fi

    subsubgroup "Spawn in parent directory"

    cmds=()
    for cmd in "${BFS[@]}" "${FIND[@]}"; do
        cmds+=("$cmd $1 -maxdepth 3 -execdir true -- {} +")
    done

    do-hyperfine "${cmds[@]}"
}

# Benchmark process spawning
bench-exec-corpus() {
    subgroup '%s' "$1"

    bench-exec-single "$2"
    bench-exec-multi "$2"
    bench-exec-chdir "$2"
}

# All process spawning benchmarks
bench-exec() {
    if (($#)); then
        group "Process spawning"

        for corpus; do
            bench-exec-corpus "$corpus ${TAGS[$corpus]}" "bench/corpus/$corpus"
        done
    fi
}

# Print benchmarked versions
bench-versions() {
    subgroup "Versions"

    local md="$BENCH_DIR/bench.md"

    printf '```console\n' >>"$md"

    {
        for bfs in "${BFS[@]}"; do
            printf '$ %s --version | head -n1\n' "$bfs"
            "$bfs" --version | head -n1
        done

        for find in "${FIND[@]}"; do
            printf '$ %s --version | head -n1\n' "$find"
            "$find" --version | head -n1
        done

        for fd in "${FD[@]}"; do
            printf '$ %s --version\n' "$fd"
            "$fd" --version
        done
    } | tee -a "$md"

    printf '```' >>"$md"
}

# Print benchmark details
bench-details() {
    group "Details"

    bench-versions
}

# Run all the benchmarks
bench() {
    import_array BFS
    import_array FIND
    import_array FD

    import_array COMPLETE
    import_array EARLY_QUIT
    import_array STAT
    import_array PRINT
    import_array STRATEGIES
    import_array JOBS
    import_array EXEC

    bench-complete "${COMPLETE[@]}"
    bench-early-quit "${EARLY_QUIT[@]}"
    bench-stat "${STAT[@]}"
    bench-print "${PRINT[@]}"
    bench-strategies "${STRATEGIES[@]}"
    bench-jobs "${JOBS[@]}"
    bench-exec "${EXEC[@]}"
    bench-details
}
