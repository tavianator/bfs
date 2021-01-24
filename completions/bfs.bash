#!/bin/bash

############################################################################
# bfs                                                                      #
# Copyright (C) 2020 Benjamin Mundt <benMundt@ibm.com>                     #
# Copyright (C) 2020 Tavian Barnes <tavianator@tavianator.com>             #
#                                                                          #
# Permission to use, copy, modify, and/or distribute this software for any #
# purpose with or without fee is hereby granted.                           #
#                                                                          #
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES #
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF         #
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR  #
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES   #
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN    #
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF  #
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.           #
############################################################################

# bash completion script for bfs

_bfs() {
    local cword="$COMP_CWORD"
    local cur="${COMP_WORDS[cword]}"
    local prev prev2
    if ((cword > 0)); then
        prev="${COMP_WORDS[cword-1]}"
    fi
    if ((cword > 1)); then
        prev2="${COMP_WORDS[cword-2]}"
    fi

    # Options with a special completion procedure
    local special=(
        -D
        -S
        -exec
        -execdir
        -fprintf
        -fstype
        -gid
        -group
        -ok
        -okdir
        -perm
        -regextype
        -type
        -uid
        -user
        -xtype
    )

    # Options whose values should not be completed
    # (e.g. because they are numeric, glob, regexp, time, etc.)
    local nocomp=(
        -{a,B,c,m}{min,since,time}
        -ilname
        -iname
        -inum
        -ipath
        -iregex
        -iwholename
        -links
        -lname
        -maxdepth
        -mindepth
        -name
        -newer{a,B,c,m}t
        -path
        -printf
        -regex
        -since
        -size
        -used
        -wholename
        -xattrname
    )

    # Options whose value is a filename
    local filecomp=(
        -{a,B,c,m}newer
        -f
        -fls
        -fprint
        -fprint0
        -newer
        -newer{a,B,c,m}{a,B,c,m}
        -samefile
    )

    local operators=(
        -a
        -and
        -exclude
        -not
        -o
        -or
    )

    # Flags that take no arguments
    local nullary_flags=(
        -E
        -H
        -L
        -O{0,1,2,3,4,fast}
        -P
        -X
        -d
        -x
    )

    # Options that take no arguments
    local nullary_options=(
        -color
        -daystart
        -depth
        -follow
        -ignore_readdir_race
        -maxdepth
        -mindepth
        -mount
        -nocolor
        -noignore_readdir_race
        -noleaf
        -nowarn
        -status
        -unique
        -warn
        -xdev
    )

    # Tests that take no arguments
    local nullary_tests=(
        -capable
        -empty
        -executable
        -false
        -hidden
        -nogroup
        -nohidden
        -nouser
        -readable
        -sparse
        -true
        -writable
        -xattr
        -xattrname
    )

    # Actions that take no arguments
    local nullary_actions=(
        --help
        --version
        -delete
        -exit
        -help
        -ls
        -print
        -print0
        -printx
        -prune
        -quit
        -rm
        -version
    )

    local everything=(
        "${special[@]}"
        "${nocomp[@]}"
        "${filecomp[@]}"
        "${operators[@]}"
        "${nullary_flags[@]}"
        "${nullary_options[@]}"
        "${nullary_tests[@]}"
        "${nullary_actions[@]}"
    )

    # Completing -exec requires matching the whole command line
    local iscmd isarg word
    for word in "${COMP_WORDS[@]::cword}"; do
        case "$word" in
            -exec|-execdir|-ok|-okdir)
                iscmd=y
                ;;
            \\\;|+)
                if [[ -n "$iscmd" ]] || [[ -n "$isarg" ]]; then
                    iscmd=
                    isarg=
                fi
                ;;
            *)
                if [[ -n "$iscmd" ]]; then
                    iscmd=
                    isarg=y
                fi
                ;;
        esac
    done

    if [[ -n "$iscmd" ]]; then
        COMPREPLY=($(compgen -c -- "$cur"))
        return
    elif [[ -n "$isarg" ]]; then
        COMPREPLY=($(compgen -o default -o bashdefault -W "{} + \\\\;" -- "$cur"))
        return
    fi

    # Completions with 2-word lookbehind
    case "$prev2" in
        -fprintf)
            # -fprintf FORMAT FILE
            #     Like -ls/-print/-print0/-printf, but write to FILE instead of standard
            #     output
            # when -fprintf is prev2, current word is FILE; perform file completion
            COMPREPLY=($(compgen -f -- "$cur"))
            return
            ;;
    esac

    # No completion for numbers, globs, regexes, times, etc.
    if [[ " ${nocomp[@]} " =~ " $prev " ]]; then
        COMPREPLY=()
        return
    fi

    # Complete filenames
    if [[ " ${filecomp[@]} " =~ " $prev " ]]; then
        COMPREPLY=($(compgen -o filenames -f -- "$cur"))
        return
    fi

    # Other completions with 1-word lookbehind
    case "$prev" in
        -type|-xtype)
            # -type [bcdlpfswD]
            #     Find files of the given type
            # -xtype [bcdlpfswD]
            #     Find files of the given type, following links when -type would not, and
            #     vice versa
            COMPREPLY=()
            if [[ -n $cur ]] && ! [[ $cur =~ ,$ ]]; then
                COMPREPLY+=("$cur")
                cur+=,
            fi
            COMPREPLY+=("$cur"{b,c,d,l,p,f,s,w,D})
            return
            ;;
        -gid|-uid)
            # -gid [-+]N
            # -uid [-+]N
            #     Find files owned by group/user ID N
            # TODO: list numeric uids/gids
            COMPREPLY=()
            return
            ;;
        -group)
            # -group NAME
            #     Find files owned by the group NAME
            COMPREPLY=($(compgen -g -- "$cur"))
            return
            ;;
        -user)
            # -user  NAME
            #     Find files owned by the user NAME
            COMPREPLY=($(compgen -u -- "$cur"))
            return
            ;;
        -S)
            # -S bfs|dfs|ids|eds
            #     Use breadth-first/depth-first/iterative/exponential deepening search
            #     (default: -S bfs)
            COMPREPLY=($(compgen -W 'bfs dfs ids eds' -- "$cur"))
            return
            ;;
        -D)
            # -D FLAG
            #     Turn on a debugging flag (see -D help)
            COMPREPLY=($(compgen -W 'help cost exec opt rates search stat tree all' -- "$cur"))
            return
            ;;
        -regextype)
            # -regextype TYPE
            #     Use TYPE-flavored regexes (default: posix-basic; see -regextype help)
            COMPREPLY=($(compgen -W 'help posix-basic posix-extended' -- "$cur"))
            return
            ;;
        -fstype)
            # -fstype TYPE
            #     Find files on file systems with the given TYPE
            #TODO: parse the mount table for a list of mounted filesystem types
            COMPREPLY=()
            return
            ;;
        (-perm)
            # -perm [-]MODE
            #     Find files with a matching mode
            # sample syntax:
            #   -perm 777
            #   -perm 507
            #   -perm u+rw
            #   -perm og-rx
            if [[ -z "$cur" ]]; then
                # initial completion
                COMPREPLY=(0 1 2 3 4 5 6 7 u g o)
                return
            elif [[ "$cur" =~ [rwx][rwx][rwx]$ ]] || [[ "$cur" =~ [0-7][0-7][0-7]$ ]]; then
                # final completion (filled in all possible mode bits)
                COMPREPLY=("$cur")
                return
            elif [[ "$cur" =~ [0-7]$ ]]; then
                # intermediate completion, octal mode specifier
                COMPREPLY=("$cur"{0,1,2,3,4,5,6,7})
                return
            elif [[ "$cur" =~ ^[ugo]*[+-][rwx]*$ ]]; then
                # intermediate completion, symbolic mode specifier
                COMPREPLY=()
                [[ "$cur" =~ [rwx]$ ]] && COMPREPLY+=("${cur}")
                [[ "$cur" =~ [+-][wx]*r ]] || COMPREPLY+=("${cur}r")
                [[ "$cur" =~ [+-][rx]*w ]] || COMPREPLY+=("${cur}w")
                [[ "$cur" =~ [+-][rw]*x ]] || COMPREPLY+=("${cur}x")
                return 0
            elif [[ "$cur" =~ ^[ugo] ]]; then
                # intermediate completion, symbolic group specifier
                COMPREPLY=(+ -)
                [[ "$cur" =~ ^[go]*u ]] || COMPREPLY+=("${cur}u")
                [[ "$cur" =~ ^[uo]*g ]] || COMPREPLY+=("${cur}g")
                [[ "$cur" =~ ^[ug]*o ]] || COMPREPLY+=("${cur}o")
                return 0
            fi
            COMPREPLY=()
            return
            ;;
    esac

    # Completions with no lookbehind
    if [[ "$cur" == -* ]]; then
        # complete all options
        COMPREPLY=($(compgen -o default -o bashdefault -W "${everything[*]}" -- "$cur"))
        return
    fi

    # default completion
    COMPREPLY=($(compgen -o default -o bashdefault -f -W "${everything[*]} ! ," -- "$cur"))
    return
} && complete -F _bfs bfs
