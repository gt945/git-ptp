#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#

. git-sh-setup || die "Not a git archive"
	
no_update_info= all_into_one= remove_redundant= local=
while case "$#" in 0) break ;; esac
do
	case "$1" in
	-n)	no_update_info=t ;;
	-a)	all_into_one=t ;;
	-d)	remove_redandant=t ;;
	-l)	local=t ;;
	*)	break ;;
	esac
	shift
done

rm -f .tmp-pack-*
PACKDIR="$GIT_OBJECT_DIRECTORY/pack"

# There will be more repacking strategies to come...
case ",$all_into_one," in
,,)
	rev_list='--unpacked'
	rev_parse='--all'
	pack_objects='--incremental'
	;;
,t,)
	rev_list=
	rev_parse='--all'
	pack_objects=
	# This part is a stop-gap until we have proper pack redundancy
	# checker.
	existing=`cd "$PACKDIR" && \
	    find . -type f \( -name '*.pack' -o -name '*.idx' \) -print`
	;;
esac
if [ "$local" ]; then
	pack_objects="$pack_objects --local"
fi
name=$(git-rev-list --objects $rev_list $(git-rev-parse $rev_parse) |
	git-pack-objects --non-empty $pack_objects .tmp-pack) ||
	exit 1
if [ -z "$name" ]; then
	echo Nothing new to pack.
	exit 0
fi
echo "Pack pack-$name created."

mkdir -p "$PACKDIR" || exit

mv .tmp-pack-$name.pack "$PACKDIR/pack-$name.pack" &&
mv .tmp-pack-$name.idx  "$PACKDIR/pack-$name.idx" ||
exit

if test "$remove_redandant" = t
then
	# We know $existing are all redandant only when
	# all-into-one is used.
	if test "$all_into_one" != '' && test "$existing" != ''
	then
		( cd "$PACKDIR" &&
		  for e in $existing
		  do
			case "$e" in
			./pack-$name.pack | ./pack-$name.idx) ;;
			*)	rm -f $e ;;
			esac
		  done
		)
	fi
fi

case "$no_update_info" in
t) : ;;
*) git-update-server-info ;;
esac
