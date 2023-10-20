#!/hint/bash

# Copyright © Tavian Barnes <tavianator@tavianator.com>
# SPDX-License-Identifier: 0BSD

## Argument parsing

# Print usage information
usage() {
    local pad=$(printf "%*s" ${#0} "")
    color cat <<EOF
Usage: ${GRN}$0${RST} [${BLU}--bfs${RST}=${MAG}path/to/bfs${RST}] [${BLU}--sudo${RST}[=${BLD}COMMAND${RST}]] [${BLU}--stop${RST}]
       $pad [${BLU}--no-clean${RST}] [${BLU}--update${RST}] [${BLU}--verbose${RST}[=${BLD}LEVEL${RST}]] [${BLU}--help${RST}]
       $pad [${BLU}--posix${RST}] [${BLU}--bsd${RST}] [${BLU}--gnu${RST}] [${BLU}--all${RST}] [${BLD}TEST${RST} [${BLD}TEST${RST} ...]]

  ${BLU}--bfs${RST}=${MAG}path/to/bfs${RST}
      Set the path to the bfs executable to test (default: ${MAG}./bin/bfs${RST})

  ${BLU}--sudo${RST}[=${BLD}COMMAND${RST}]
      Run tests that require root using ${GRN}sudo${RST} or the given ${BLD}COMMAND${RST}

  ${BLU}--stop${RST}
      Stop when the first error occurs

  ${BLU}--no-clean${RST}
      Keep the test directories around after the run

  ${BLU}--update${RST}
      Update the expected outputs for the test cases

  ${BLU}--verbose${RST}=${BLD}commands${RST}
      Log the commands that get executed
  ${BLU}--verbose${RST}=${BLD}errors${RST}
      Don't redirect standard error
  ${BLU}--verbose${RST}=${BLD}skipped${RST}
      Log which tests get skipped
  ${BLU}--verbose${RST}=${BLD}tests${RST}
      Log all tests that get run
  ${BLU}--verbose${RST}
      Log everything

  ${BLU}--help${RST}
      This message

  ${BLU}--posix${RST}, ${BLU}--bsd${RST}, ${BLU}--gnu${RST}, ${BLU}--all${RST}
      Choose which test cases to run (default: ${BLU}--all${RST})

  ${BLD}TEST${RST}
      Select individual test cases to run (e.g. ${BLD}posix/basic${RST}, ${BLD}"*exec*"${RST}, ...)
EOF
}

# Parse the command line
parse_args() {
    PATTERNS=()
    SUDO=()
    STOP=0
    CLEAN=1
    UPDATE=0
    VERBOSE_COMMANDS=0
    VERBOSE_ERRORS=0
    VERBOSE_SKIPPED=0
    VERBOSE_TESTS=0

    for arg; do
        case "$arg" in
            --bfs=*)
                BFS="${arg#*=}"
                ;;
            --posix)
                PATTERNS+=("posix/*")
                ;;
            --bsd)
                PATTERNS+=("posix/*" "common/*" "bsd/*")
                ;;
            --gnu)
                PATTERNS+=("posix/*" "common/*" "gnu/*")
                ;;
            --all)
                PATTERNS+=("*")
                ;;
            --sudo)
                SUDO=(sudo)
                ;;
            --sudo=*)
                read -a SUDO <<<"${arg#*=}"
                ;;
            --stop)
                STOP=1
                ;;
            --no-clean|--noclean)
                CLEAN=0
                ;;
            --update)
                UPDATE=1
                ;;
            --verbose=commands)
                VERBOSE_COMMANDS=1
                ;;
            --verbose=errors)
                VERBOSE_ERRORS=1
                ;;
            --verbose=skipped)
                VERBOSE_SKIPPED=1
                ;;
            --verbose=tests)
                VERBOSE_TESTS=1
                ;;
            --verbose)
                VERBOSE_COMMANDS=1
                VERBOSE_ERRORS=1
                VERBOSE_SKIPPED=1
                VERBOSE_TESTS=1
                ;;
            --help)
                usage
                exit 0
                ;;
            -*)
                color printf "${RED}error:${RST} Unrecognized option '%s'.\n\n" "$arg" >&2
                usage >&2
                exit 1
                ;;
            *)
                PATTERNS+=("$arg")
                ;;
        esac
    done

    # Try to resolve the path to $BFS before we cd, while also supporting
    # --bfs="./bin/bfs -S ids"
    read -a BFS <<<"${BFS:-$BIN/bfs}"
    BFS[0]=$(_realpath "$(command -v "${BFS[0]}")")

    if ((${#PATTERNS[@]} == 0)); then
        PATTERNS=("*")
    fi

    TEST_CASES=()
    ALL_TESTS=($(cd "$TESTS" && quote {posix,common,bsd,gnu,bfs}/*.sh))
    for TEST in "${ALL_TESTS[@]}"; do
        TEST="${TEST%.sh}"
        for PATTERN in "${PATTERNS[@]}"; do
            if [[ $TEST == $PATTERN ]]; then
                TEST_CASES+=("$TEST")
                break
            fi
        done
    done

    if ((${#TEST_CASES[@]} == 0)); then
        color printf "${RED}error:${RST} No tests matched" >&2
        color printf " ${BLD}%s${RST}" "${PATTERNS[@]}" >&2
        color printf ".\n\n" >&2
        usage >&2
        exit 1
    fi
}
