# bash completion script

_bfs ()
{
  local cword=$COMP_CWORD
  local cur="${COMP_WORDS[$cword]}"
  local prev prev1
  if (($cword > 0)); then
    prev="${COMP_WORDS[$(($cword-1))]}"
  fi
  if (($cword > 1)); then
    prev1="${COMP_WORDS[$(($cword-2))]}"
  fi

  # arguments with a special completion procedure
  local options_special='-exec -execdir -ok -okdir -fprintf -D -S -regextype -fstype -gid -uid -group -user -perm -type -xtype'

  # arguments whose values should not be completed
  # (e.g. because they are numeric, glob, regexp, time, etc.)
  local options_nocomp='-maxdepth -mindepth -amin -Bmin -cmin -mmin -asince -Bsince -csince -msince -atime -Btime -ctime -mtime -ilname -iname -ipath -iregex -iwholename -inum -links -lname -name -path -wholename -regex -since -size -used -printf -newerat -newerBt -newerct -newermt -xattrname'

  # arguments whose value is a filename
  local options_filecomp='-anewer -Bnewer -cnewer -mnewer -newer -samefile -fls -fprint -fprint0 -neweraa -neweraB -newerac -neweram -newerBa -newerBB -newerBc -newerBm -newerca -newercB -newercc -newercm -newerma -newermB -newermc -newermm -f'

  # arguments whose value is a dirname
  local options_dircomp=''

  # options with no value
  local flags='-H -L -P -E -X -d -s -x -O1 -O2 -O3 -not -and -or -exclude -color -nocolor -daystart -depth -follow -ignore_readdir_race -noignore_readdir_race -mount -nohidden -status -unique -warn -nowarn -xdev -acl -capable -empty -executable -readable -writable -false -true -hidden -nogroup -nouser -sparse -xattr -delete -rm -exit -ls -print -print0 -printx -prune -quit -version -help'

  local all_options="${flags} ${options_special} ${options_nocomp} ${options_filecomp} ${options_dircomp}"

  # completions which require parsing the whole command string
  case "${COMP_WORDS[*]}" in
    (*\ -exec\ *|*\ -execdir\ *|*\ -ok\ *|*\ -okdir\ *)
      case "$prev" in
        (-exec|-execdir|-ok|-okdir)
          # complete commands for first word after exec-type option
          COMPREPLY=($(compgen -c -- "$cur"))
          return
        ;;
      esac
      # default completion only after exec-type option
      #TODO: complete whatever command was provided after -exec
      #TODO: detect end of -exec string and continue completing as normal
      COMPREPLY=($(compgen -o default -o bashdefault -- "$cur"))
      return
    ;;
  esac

  # completions with 2-word lookbehind
  case "$prev1" in
    (-fprintf)
      # -fprintf FORMAT FILE
      #     Like -ls/-print/-print0/-printf, but write to FILE instead of standard
      #     output
      # when -fprintf is prev1, current word is FILE; perform file completion
      COMPREPLY=($(compgen -f -- "$cur"))
      return
    ;;
  esac

  # completions with 1-word lookbehind
  case "$prev" in
    (-maxdepth|-mindepth|-[aBcm]min|-[aBcm]since|-[aBcm]time|-ilname|-iname|-ipath|-iregex|-iwholename|-inum|-links|-lname|-name|-path|-wholename|-regex|-since|-size|-used|-printf|-newer[aBcm]t|-xattrname)
      # arguments whose values should not be completed
      # (e.g. because they are numeric, glob, regexp, time, etc.)
      COMPREPLY=()
      return
    ;;
    (-[aBcm]newer|-newer|-samefile|-fls|-fprint|-fprint0|-newer[aBcm][aBcm]|-f)
      # arguments whose value is a filename
      #FIXME: -o filenames should suppress spaces and add trailing / to dirnames
      COMPREPLY=($(compgen -o filenames -f -- "$cur"))
      return
    ;;
    # (-f)
    #   # arguments whose value is a dirname
    #   COMPREPLY=($(compgen -d -- "$cur"))
    #   return
    # ;;
    (-type|-xtype)
      # -type [bcdlpfswD]
      #     Find files of the given type
      # -xtype [bcdlpfswD]
      #     Find files of the given type, following links when -type would not, and
      #     vice versa
      if [[ -n $cur ]] && ! [[ $cur =~ ,$ ]]; then
        cur+=,
      fi
      COMPREPLY=("${cur}"{b,c,d,l,p,f,s,w,D})
      return
    ;;
    (-gid|-uid)
      # -gid [-+]N
      # -uid [-+]N
      #     Find files owned by group/user ID N
      #TODO: list numeric uids/gids
      COMPREPLY=()
      return
    ;;
    (-group)
      # -group NAME
      #     Find files owned by the group NAME
      COMPREPLY=($(compgen -g -- "$cur"))
      return
    ;;
    (-user)
      # -user  NAME
      #     Find files owned by the user NAME
      COMPREPLY=($(compgen -u -- "$cur"))
      return
    ;;
    (-S)
      # -S bfs|dfs|ids|eds
      #     Use breadth-first/depth-first/iterative/exponential deepening search
      #     (default: -S bfs)
      COMPREPLY=($(compgen -W 'bfs dfs ids eds' -- "$cur"))
      return
    ;;
    (-D)
      # -D FLAG
      #     Turn on a debugging flag (see -D help)
      COMPREPLY=($(compgen -W 'help cost exec opt rates search stat tree all' -- "$cur"))
      return
    ;;
    (-regextype)
      # -regextype TYPE
      #     Use TYPE-flavored regexes (default: posix-basic; see -regextype help)
      COMPREPLY=($(compgen -W 'help posix-basic posix-extended' -- "$cur"))
      return
    ;;
    (-fstype)
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

  # completions with no lookbehind
  case "$cur" in
    (-*)
      # complete all options
      COMPREPLY=($(compgen -o default -o bashdefault -W "${all_options}" -- "$cur"))
      return
    ;;
  esac
  
  # default completion
  COMPREPLY=($(compgen -o default -o bashdefault -- "$cur"))
  return
} &&
    complete -F _bfs bfs
