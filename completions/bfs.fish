# Completions for the 'bfs' command

set -l debug_flag_comp 'help\t"Print help message" cost\t"Show cost estimates" exec\t"Print executed command details" opt\t"Print optimization details" rates\t"Print predicate success rates" search\t"Trace the filesystem traversal" stat\t"Trace all stat() calls" tree\t"Print the parse tree" all\t"All debug flags at once"'
set -l optimization_comp '0\t"Disable all optimizations" 1\t"Basic logical simplifications" 2\t"-O1, plus dead code elimination and data flow analysis" 3\t"-02, plus re-order expressions to reduce expected cost" 4\t"All optimizations, including aggressive optimizations" fast\t"Same as -O4"'
set -l strategy_comp 'bfs\t"Breadth-first search" dfs\t"Depth-first search" ids\t"Iterative deepening search" eds\t"Exponential deepening search"'
set -l regex_type_comp 'help\t"Print help message" posix-basic\t"POSIX basic regular expressions" posix-extended\t"POSIX extended regular expressions" ed\t"Like ed" emacs\t"Like emacs" grep\t"Like grep" sed\t"Like sed"'
set -l type_comp 'b\t"Block device" c\t"Character device" d\t"Directory" l\t"Symbolic link" p\t"Pipe" f\t"Regular file" s\t"Socket" w\t"Whiteout" D\t"Door"'

# Flags

complete -c bfs -o H -d "Follow symbolic links only on the command line"
complete -c bfs -o L -o follow -d "Follow all symbolic links"
complete -c bfs -o P -d "Never follow symbolic links"
complete -c bfs -o E -d "Use extended regular expressions"
complete -c bfs -o X -d "Filter out files with non-xargs(1)-safe names"
complete -c bfs -o d -o depth -d "Search in post-order"
complete -c bfs -o s -d "Visit directory entries in sorted order"
complete -c bfs -o x -o xdev -d "Don't descend into other mount points"
complete -c bfs -o f -d "Treat specified path as a path to search" -a "(__fish_complete_directories)" -x
complete -c bfs -o D -d "Turn on a debugging flag" -a $debug_flag_comp -x
complete -c bfs -s O -d "Enable specified optimization level" -a $optimization_comp -x
complete -c bfs -o S -d "Choose the search strategy" -a $strategy_comp -x
complete -c bfs -s j -d "Use this many threads" -x

# Operators

complete -c bfs -o not -d "Negate result of expression"
complete -c bfs -o a -o and -d "Result is only true if both previous and next expression are true"
complete -c bfs -o o -o or -d "Result is true if either previous or next expression are true"

# Special forms

complete -c bfs -o exclude -d "Exclude all paths matching the expression from the search" -x

# Options

complete -c bfs -o color -d "Turn colors on"
complete -c bfs -o nocolor -d "Turn colors off"
complete -c bfs -o daystart -d "Measure time relative to the start of today"
complete -c bfs -o files0-from -d "Treat the NUL-separated paths in specified file as starting points for the search" -F
complete -c bfs -o ignore_readdir_race -d "Don't report an error if the file tree is modified during the search"
complete -c bfs -o noignore_readdir_race -d "Report an error if the file tree is modified during the search"
complete -c bfs -o maxdepth -d "Ignore files deeper than specified number" -x
complete -c bfs -o mindepth -d "Ignore files shallower than specified number" -x
complete -c bfs -o mount -d "Exclude mount points"
complete -c bfs -o noerror -d "Ignore any errors that occur during traversal"
complete -c bfs -o nohidden -d "Exclude hidden files and directories"
complete -c bfs -o noleaf -d "Ignored; for compatibility with GNU find"
complete -c bfs -o regextype -d "Use specified flavored regex" -a $regex_type_comp -x
complete -c bfs -o status -d "Display a status bar while searching"
complete -c bfs -o unique -d "Skip any files that have already been seen"
complete -c bfs -o warn -d "Turn on warnings about the command line"
complete -c bfs -o nowarn -d "Turn off warnings about the command line"

# Tests

complete -c bfs -o acl -d "Find files with a non-trivial Access Control List"
complete -c bfs -o amin -d "Find files accessed specified number of minutes ago" -x
complete -c bfs -o Bmin -d "Find files birthed specified number of minutes ago" -x
complete -c bfs -o cmin -d "Find files changed specified number of minutes ago" -x
complete -c bfs -o mmin -d "Find files modified specified number of minutes ago" -x
complete -c bfs -o anewer -d "Find files accessed more recently than specified file was modified" -F
complete -c bfs -o Bnewer -d "Find files birthed more recently than specified file was modified" -F
complete -c bfs -o cnewer -d "Find files changed more recently than specified file was modified" -F
complete -c bfs -o mnewer -d "Find files modified more recently than specified file was modified" -F
complete -c bfs -o asince -d "Find files accessed more recently than specified time" -x
complete -c bfs -o Bsince -d "Find files birthed more recently than specified time" -x
complete -c bfs -o csince -d "Find files changed more recently than specified time" -x
complete -c bfs -o msince -d "Find files modified more recently than specified time" -x
complete -c bfs -o atime -d "Find files accessed specified number of days ago" -x
complete -c bfs -o Btime -d "Find files birthed specified number of days ago" -x
complete -c bfs -o ctime -d "Find files changed specified number of days ago" -x
complete -c bfs -o mtime -d "Find files modified specified number of days ago" -x
complete -c bfs -o capable -d "Find files with capabilities set"
complete -c bfs -o context -d "Find files by SELinux context" -x
complete -c bfs -o depth -d "Find files with specified number of depth" -x
complete -c bfs -o empty -d "Find empty files/directories"
complete -c bfs -o executable -d "Find files the current user can execute"
complete -c bfs -o readable -d "Find files the current user can read"
complete -c bfs -o writable -d "Find files the current user can write"
complete -c bfs -o false -d "Always false"
complete -c bfs -o true -d "Always true"
complete -c bfs -o fstype -d "Find files on file systems with the given type" -a "(__fish_print_filesystems)" -x
complete -c bfs -o gid -d "Find files owned by group ID" -a "(__fish_complete_group_ids)" -x
complete -c bfs -o uid -d "Find files owned by user ID" -a "(__fish_complete_user_ids)" -x
complete -c bfs -o group -d "Find files owned by the group" -a "(__fish_complete_groups)" -x
complete -c bfs -o user -d "Find files owned by the user" -a "(__fish_complete_users)" -x
complete -c bfs -o hidden -d "Find hidden files"
complete -c bfs -o ilname -d "Case-insensitive versions of -lname" -x
complete -c bfs -o iname -d "Case-insensitive versions of -name" -x
complete -c bfs -o ipath -d "Case-insensitive versions of -path" -x
complete -c bfs -o iregex -d "Case-insensitive versions of -regex" -x
complete -c bfs -o iwholename -d "Case-insensitive versions of -wholename" -x
complete -c bfs -o inum -d "Find files with specified inode number" -x
complete -c bfs -o links -d "Find files with specified number of hard links" -x
complete -c bfs -o lname -d "Find symbolic links whose target matches specified glob" -x
complete -c bfs -o name -d "Find files whose name matches specified glob" -x
complete -c bfs -o newer -d "Find files newer than specified file" -F

# handle -newer{a,B,c,m}{a,B,c,m} FILE
for x in {a,B,c,m}
    for y in {a,B,c,m}
        complete -c bfs -o newer$x$y -d "Find files whose $x""time is newer than the $y""time of specified file" -F
    end
end

# handle -newer{a,B,c,m}t TIMESTAMP
for x in {a,B,c,m}
    complete -c bfs -o newer$x"t" -d "Find files whose $x""time is newer than specified timestamp" -x
end

complete -c bfs -o nogroup -d "Find files owned by nonexistent groups"
complete -c bfs -o nouser -d "Find files owned by nonexistent users"
complete -c bfs -o path -o wholename -d "Find files whose entire path matches specified glob" -x
complete -c bfs -o perm -d "Find files with a matching mode" -x
complete -c bfs -o regex -d "Find files whose entire path matches the regular expression" -x
complete -c bfs -o samefile -d "Find hard links to specified file" -F
complete -c bfs -o since -d "Find files modified since specified time" -x
complete -c bfs -o size -d "Find files with the given size" -x
complete -c bfs -o sparse -d "Find files that occupy fewer disk blocks than expected"
complete -c bfs -o type -d "Find files of the given type" -a $type_comp -x
complete -c bfs -o used -d "Find files last accessed specified number of days after they were changed" -x
complete -c bfs -o xattr -d "Find files with extended attributes"
complete -c bfs -o xattrname -d "Find files with the specified extended attribute name" -x
complete -c bfs -o xtype -d "Find files of the given type, following links when -type would not, and vice versa" -a $type_comp -x

# Actions

complete -c bfs -o rm -o delete -d "Delete any found files"
complete -c bfs -o exec -d "Execute a command" -r
complete -c bfs -o ok -d "Prompt the user whether to execute a command" -r
complete -c bfs -o execdir -d "Like -exec, but run the command in the same directory as the found file(s)" -r
complete -c bfs -o okdir -d "Like -ok, but run the command in the same directory as the found file(s)" -r
complete -c bfs -o exit -d "Exit immediately with the given status" -x
complete -c bfs -o fls -d "Like -ls, but write to specified file" -F
complete -c bfs -o fprint -d "Like -print, but write to specified file" -F
complete -c bfs -o fprint0 -d "Like -print0, but write to specified file" -F
complete -c bfs -o fprintf -d "Like -printf, but write to specified file" -F
complete -c bfs -o limit -d "Limit the number of results" -x
complete -c bfs -o ls -d "List files like ls -dils"
complete -c bfs -o print -d "Print the path to the found file"
complete -c bfs -o print0 -d "Like -print, but use the null character as a separator rather than newlines"
complete -c bfs -o printf -d "Print according to a format string" -x
complete -c bfs -o printx -d "Like -print, but escape whitespace and quotation characters"
complete -c bfs -o prune -d "Don't descend into this directory"
complete -c bfs -o quit -d "Quit immediately"
complete -c bfs -o version -l version -d "Print version information"
complete -c bfs -o help -l help -d "Print usage information"
