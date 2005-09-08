#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano.
#

. git-sh-setup || die "Not a git archive."

usage="usage: $0 "'<upstream> [<head>]

Uses output from git-cherry to rebase local commits to the new head of
upstream tree.'

case "$#,$1" in
1,*..*)
    upstream=$(expr "$1" : '\(.*\)\.\.') ours=$(expr "$1" : '.*\.\.\(.*\)$')
    set x "$upstream" "$ours"
    shift ;;
esac

git-update-index --refresh || exit

case "$#" in
1) ours_symbolic=HEAD ;;
2) ours_symbolic="$2" ;;
*) die "$usage" ;;
esac

upstream=`git-rev-parse --verify "$1"` &&
ours=`git-rev-parse --verify "$ours_symbolic"` || exit
different1=$(git-diff-index --name-only --cached "$ours") &&
different2=$(git-diff-index --name-only "$ours") &&
test "$different1$different2" = "" ||
die "Your working tree does not match $ours_symbolic."

git-read-tree -m -u $ours $upstream &&
git-rev-parse --verify "$upstream^0" >"$GIT_DIR/HEAD" || exit

tmp=.rebase-tmp$$
fail=$tmp-fail
trap "rm -rf $tmp-*" 1 2 3 15

>$fail

git-cherry -v $upstream $ours |
while read sign commit msg
do
	case "$sign" in
	-)
		echo >&2 "* Already applied: $msg"
		continue ;;
	esac
	echo >&2 "* Applying: $msg"
	S=`cat "$GIT_DIR/HEAD"` &&
	git-cherry-pick --replay $commit || {
		echo >&2 "* Not applying the patch and continuing."
		echo $commit >>$fail
		git-reset --hard $S
	}
done
if test -s $fail
then
	echo >&2 Some commits could not be rebased, check by hand:
	cat >&2 $fail
	echo >&2 "(the same list of commits are found in $tmp)"
	exit 1
else
	rm -f $fail
fi
