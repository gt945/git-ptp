#!/bin/sh
##
## applypatch takes four file arguments, and uses those to
## apply the unpacked patch (surprise surprise) that they
## represent to the current tree.
##
## The arguments are:
##	$1 - file with commit message
##	$2 - file with the actual patch
##	$3 - "info" file with Author, email and subject
##	$4 - optional file containing signoff to add
##
. git-sh-setup || die "Not a git archive."

final=.dotest/final-commit
##
## If this file exists, we ask before applying
##
query_apply=.dotest/.query_apply

## We do not munge the first line of the commit message too much
## if this file exists.
keep_subject=.dotest/.keep_subject


MSGFILE=$1
PATCHFILE=$2
INFO=$3
SIGNOFF=$4
EDIT=${VISUAL:-${EDITOR:-vi}}

export GIT_AUTHOR_NAME="$(sed -n '/^Author/ s/Author: //p' .dotest/info)"
export GIT_AUTHOR_EMAIL="$(sed -n '/^Email/ s/Email: //p' .dotest/info)"
export GIT_AUTHOR_DATE="$(sed -n '/^Date/ s/Date: //p' .dotest/info)"
export SUBJECT="$(sed -n '/^Subject/ s/Subject: //p' .dotest/info)"

if test '' != "$SIGNOFF"
then
	if test -f "$SIGNOFF"
	then
		SIGNOFF=`cat "$SIGNOFF"` || exit
	elif case "$SIGNOFF" in yes | true | me | please) : ;; *) false ;; esac
	then
		SIGNOFF=`git-var GIT_COMMITTER_IDENT | sed -e '
				s/>.*/>/
				s/^/Signed-off-by: /'
		`
	else
		SIGNOFF=
	fi
	if test '' != "$SIGNOFF"
	then
		LAST_SIGNED_OFF_BY=`
			sed -ne '/^Signed-off-by: /p' "$MSGFILE" |
			tail -n 1
		`
		test "$LAST_SIGNED_OFF_BY" = "$SIGNOFF" ||
		echo "$SIGNOFF" >>"$MSGFILE"
	fi
fi

patch_header=
test -f "$keep_subject" || patch_header='[PATCH] '

{
	echo "$patch_header$SUBJECT"
	if test -s "$MSGFILE"
	then
		echo
		cat "$MSGFILE"
	fi
} >"$final"

interactive=yes
test -f "$query_apply" || interactive=no

while [ "$interactive" = yes ]; do
	echo "Commit Body is:"
	echo "--------------------------"
	cat "$final"
	echo "--------------------------"
	echo -n "Apply? [y]es/[n]o/[e]dit/[a]ccept all "
	read reply
	case "$reply" in
		y|Y) interactive=no;;
		n|N) exit 2;;	# special value to tell dotest to keep going
		e|E) "$EDIT" "$final";;
		a|A) rm -f "$query_apply"
		     interactive=no ;;
	esac
done

if test -x "$GIT_DIR"/hooks/applypatch-msg
then
	"$GIT_DIR"/hooks/applypatch-msg "$final" || exit
fi

echo
echo Applying "'$SUBJECT'"
echo

git-apply --index "$PATCHFILE" || exit 1

if test -x "$GIT_DIR"/hooks/pre-applypatch
then
	"$GIT_DIR"/hooks/pre-applypatch || exit
fi

tree=$(git-write-tree) || exit 1
echo Wrote tree $tree
commit=$(git-commit-tree $tree -p $(cat "$GIT_DIR"/HEAD) < "$final") || exit 1
echo Committed: $commit
echo $commit > "$GIT_DIR"/HEAD

if test -x "$GIT_DIR"/hooks/post-applypatch
then
	"$GIT_DIR"/hooks/post-applypatch
fi
