# bash completion script for bfs

############################################################################
# bfs                                                                      #
# Copyright (C) 2020 Benjamin Mundt <benMundt@ibm.com>                     #
# Copyright (C) 2021 Tavian Barnes <tavianator@tavianator.com>             #
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

_bfs() {
    local cur prev words cword
    _init_completion || return

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
        -perm
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
    local i offset
    for i in "${!words[@]}"; do
        if ((i >= cword)); then
            break
        fi

        case "${words[i]}" in
            -exec|-execdir|-ok|-okdir)
                offset=$((i + 1))
                ;;
            \\\;|+)
                offset=
                ;;
        esac
    done

    if [[ -n "$offset" ]]; then
        _command_offset "$offset"
        COMPREPLY+=($(compgen -W "{} + '\\;'" -- "$cur"))
        return
    fi

    # Completions with 2-word lookbehind
    if ((cword > 1)); then
        case "${words[cword-2]}" in
            -fprintf)
                # -fprintf FORMAT FILE
                #     Like -ls/-print/-print0/-printf, but write to FILE instead of standard
                #     output
                # when -fprintf is prev2, current word is FILE; perform file completion
                _filedir
                return
                ;;
        esac
    fi

    # No completion for numbers, globs, regexes, times, etc.
    if [[ " ${nocomp[@]} " =~ " $prev " ]]; then
        COMPREPLY=()
        return
    fi

    # Complete filenames
    if [[ " ${filecomp[@]} " =~ " $prev " ]]; then
        _filedir
        return
    fi

    # Special completions with 1-word lookbehind
    case "$prev" in
        -D)
            # -D FLAG
            #     Turn on a debugging flag (see -D help)
            COMPREPLY=($(compgen -W 'help cost exec opt rates search stat tree all' -- "$cur"))
            return
            ;;
        -S)
            # -S bfs|dfs|ids|eds
            #     Use breadth-first/depth-first/iterative/exponential deepening search
            #     (default: -S bfs)
            COMPREPLY=($(compgen -W 'bfs dfs ids eds' -- "$cur"))
            return
            ;;
        -fstype)
            # -fstype TYPE
            #     Find files on file systems with the given TYPE
            _fstypes
            return
            ;;
        -gid)
            # -gid [-+]N
            #     Find files owned by group ID N
            _gids
            return
            ;;
        -group)
            # -group NAME
            #     Find files owned by the group NAME
            COMPREPLY=($(compgen -g -- "$cur"))
            return
            ;;
        -uid)
            # -uid [-+]N
            #     Find files owned by auser ID N
            _uids
            return
            ;;
        -user)
            # -user NAME
            #     Find files owned by the user NAME
            COMPREPLY=($(compgen -u -- "$cur"))
            return
            ;;
        -regextype)
            # -regextype TYPE
            #     Use TYPE-flavored regexes (default: posix-basic; see -regextype help)
            COMPREPLY=($(compgen -W 'help posix-basic posix-extended' -- "$cur"))
            return
            ;;
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
    esac

    # Completions with no lookbehind
    if [[ "$cur" == -* ]]; then
        # complete all options
        COMPREPLY=($(compgen -W "${everything[*]}" -- "$cur"))
        return
    fi

    # default completion
    _filedir
    COMPREPLY+=($(compgen -W "- ! , '\\(' '\\)'" -- "$cur"))
} && complete -F _bfs bfs
