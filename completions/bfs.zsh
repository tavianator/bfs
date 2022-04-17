#compdef bfs
# Based on standard zsh find completion and bfs bash completion.

local curcontext="$curcontext" state_descr variant default ret=1
local -a state line args alts disp smatch

args=(
    # Flags
    '(-depth)-d[search in post-order (descendents first)]'
    '-D[print diagnostics]:debug option:(cost exec opt rates search stat time tree all help)'
    '-E[use extended regular expressions with -regex/-iregex]'
    '-f[specify file hierarchy to traverse]:path:_directories'
    '-O+[enable query optimisation]:level:(1 2 3)'
    '-s[traverse directories in sorted order]'
    '-X[warn if filename contains characters special to xargs]'
    "-x[don't span filesystems]"
    '(-H -L)-P[never follow symlinks]'
    '(-H -P)-L[follow symlinks]'
    '(-L -P)-H[only follow symlinks when resolving command-line arguments]'
    "-S[select search method]:value:(bfs dfs ids eds)"
    '-f[treat path as path to search]:path:_files -/'

    # Operators
    '*-and'
    '*-not'
    '*-or'
    '*-a' '*-o'

    # Special forms
    '*-exclude[exclude paths matching EXPRESSION from search]'

    # Options
    '(-nocolor)-color[turn on colors]'
    '(-color)-nocolor[turn off colors]'
    '*-daystart[measure times relative to start of today]'
    '(-d)*-depth[search in post-order (descendents first)]'
    '-files0-from[search NUL separated paths from FILE]:file:_files'
    '*-follow[follow all symbolic links (same as -L)]'
    '*-ignore_readdir_race[report an error if bfs detects file tree is modified during search]'
    '*-noignore_readdir_race[do not report an error if bfs detects file tree is modified during search]'
    '*-maxdepth[ignore files deeper than N]:maximum search depth'
    '*-mindepth[ignore files shallower than N]:minimum search depth'
    "*-mount[don't descend into other mount points]"
    '*-nohidden[exclude hidden files]'
    '*-noleaf[ignored, for compatibility with GNU find]'
    '-regextype[type of regex to use, default posix-basic]:regexp syntax:(help posix-basic posix-extended ed emacs grep sed)'
    '*-status[display a status bar while searching]'
    '-unique[skip any files that have already been seen]'
    '*-warn[turn on warnings about the command line]'
    '*-nowarn[turn off warnings about the command line]'
    "*-xdev[don't descend into other mount points]"

    # Tests
    '*-acl[find files with a non-trivial Access Control List]'
    '*-amin[find files accessed N minutes ago]:access time (minutes):'
    '*-anewer[find files accessed more recently than FILE was modified]:file to compare (access time):_files'
    '*-asince[find files accessed more recently than TIME]:time:'
    '*-atime[find files accessed N days ago]:access time (days):->times'

    '*-Bmin[find files birthed N minutes ago]:birth time (minutes):'
    '*-Bnewer[find files birthed more recently than FILE was modified]:file to compare (birth time):_files'
    '*-Bsince[find files birthed more recently than TIME]:time:'
    '*-Btime[find files birthed N days ago]:birth time (days):->times'

    '*-cmin[find files changed N minutes ago]:inode change time (minutes):'
    '*-cnewer[find files changed more recently than FILE was modified]:file to compare (inode change time):_files'
    '*-csince[find files changed more recently than TIME]:time:'
    '*-ctime[find files changed N days ago]:inode change time (days):->times'

    '*-mmin[find files modified N minutes ago]:modification time (minutes):'
    '*-mnewer[find files modified more recently than FILE was modified]:file to compare (modification time):_files'
    '*-msince[find files modified more recently than TIME]:time:'
    '*-mtime[find files modified N days ago]:modification time (days):->times'

    '*-capable[find files with POSIX.1e capabilities set]'
    # -depth without parameters exist above. I don't know how to handle this gracefully
    '*-empty[find empty files/directories]'
    '*-executable[find files the current user can execute]'
    '*-readable[find files the current user can read]'
    '*-writable[find files the current user can write]'
    '*-false[always false]'
    '*-true[always true]'
    '*-fstype[find files on file systems with the given type]:file system type:_file_systems'
    
    '*-gid[find files owned by group ID N]:numeric group ID:'
    '*-group[find files owned by group NAME]:group:_groups'
    '*-uid[find files owned by user ID N]:numeric user ID'
    '*-user[find files owned by user NAME]:user:_users'
    '*-hidden[find hidden files (those beginning with .)]'

    '*-ilname[find symbolic links whose target matches GLOB (case insensitve)]:link pattern to search (case insensitive):'
    '*-iname[find files whose name matches GLOB (case insensitive)]:name pattern to match (case insensitive):'
    '*-inum[find files with inode number N]:inode number:'
    '*-ipath[find files whose entire path matches GLOB (case insenstive)]:path pattern to search (case insensitive):'
    '*-iregex[find files whose entire path matches REGEX (case insenstive)]:regular expression to search (case insensitive):'
    '*-iwholename[find files whose entire path matches GLOB (case insensitive)]:full path pattern to search (case insensitive):'

    '*-links[find files with N hard links]:number of links:'
    '*-lname[find symbolic links whose target matches GLOB]:link pattern to search'
    '*-name[find files whose name matches GLOB]:name pattern'
    '*-newer[find files newer than FILE]:file to compare (modification time):_files'
    '*-newer'{a,B,c,m}{a,B,c,m}'[find files where timestamp 1 is newer than timestamp 2 of reference FILE]:reference file:_files'
    '*-newer'{a,B,c,m}t'[find files where timestamp is newer than timestamp given as parameter]:timestamp:'
    '*-nogroup[find files with nonexistent owning group]'
    '*-nouser[find files with nonexistent owning user]'
    '*-path[find files whose entire path matches GLOB]:path pattern to search:'
    '*-wholename[find files whose entire path matches GLOB]:full path pattern to search:'

    '*-perm[find files with a matching mode]: :_file_modes'
    '*-regex[find files whose entire path matches REGEX]:regular expression to search:'
    '*-samefile[find hard links to FILE]:file to compare inode:_files'
    '*-since[files modified since TIME]:time:'
    '*-size[find files with the given size]:file size (blocks):'
    '*-sparse[find files that occupy fewer disk blocks than expected]'
    '*-type[find files of the given type]:file type:((b\:block\ device c\:character\ device d\:directory p\:named\ pipe f\:normal\ file l\:symbolic\ link s\:socket w\:whiteout D\:Door))'
    '*-used[find files last accessed N days after they were changed]:access after inode change (days)'
    '*-xattr[find files with extended attributes]'
    '*-xattrname[find files with extended attribute NAME]:name:'
    '*-xtype[find files of the given type following links when -type would not, and vice versa]:file type:((b\:block\ device c\:character\ device d\:directory p\:named\ pipe f\:normal\ file l\:symbolic\ link s\:socket w\:whiteout D\:Door))'
    
    # Actions
    '*-delete[delete any found files (-implies -depth)]'
    '*-rm[delete any found files (-implies -depth)]'

    '*-exec[execute a command]:program: _command_names -e:*(\;|+)::program arguments: _normal'
    '*-execdir[execute a command in the same directory as the found files]:program: _command_names -e:*(\;|+)::program arguments: _normal'
    '*-ok[prompt the user whether to execute a command]:program: _command_names -e:*(\;|+)::program arguments: _normal'
    '*-okdir[prompt the user whether to execute a command in the same directory as the found files]:program: _command_names -e:*(\;|+)::program arguments: _normal'
    
    '-exit[exit with status if found, default 0]'
    '*-fls[list files like ls -dils, but write to FILE instead of standard output]:output file:_files'
    '*-fprint[print the path to the found file, but write to FILE instead of standard output]:output file:_files'
    '*-fprint0[print the path to the found file using null character as separator, but write to FILE instead of standard output]:output file:_files'
    '*-fprintf[print according to format string, but write to FILE instead of standard output]:output file:_files:output format'

    '*-ls[list files like ls -dils]'
    '*-print[print the path to the found file]'
    '*-print0[print the path to the found file using null character as separator]'
    '*-printf[print according to format string]:output format'
    '*-printx[like -print but escapes whitespace and quotation marks]'
    "*-prune[don't descend into this directory]"

    '*-quit[quit immediately]'
    '(- *)-help[print usage information]'
    '(-)--help[print usage information]'
    '(- *)-version[print version information]'
    '(-)--version[print version information]'

    '(--help --version)*:other:{_alternative "directories:directory:_files -/" "logic:logic:(, ! \( \) )"}'
)

_arguments -C $args && ret=0

if [[ $state = times ]]; then
    if ! compset -P '[+-]' || [[ -prefix '[0-9]' ]]; then
        compstate[list]+=' packed'
        if zstyle -t ":completion:${curcontext}:senses" verbose; then
            zstyle -s ":completion:${curcontext}:senses" list-separator sep || sep=--
            default=" [default exactly]"
            disp=( "- $sep before" "+ $sep since" )
            smatch=( - + )
        else
            disp=( before exactly since )
            smatch=( - '' + )
        fi
        alts=( "senses:sense${default}:compadd -V times -S '' -d disp -a smatch" )
    fi
    alts+=( "times:${state_descr}:_dates -f d" )
    _alternative $alts && ret=0
fi

return ret
