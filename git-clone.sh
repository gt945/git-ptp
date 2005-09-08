#!/bin/sh
#
# Copyright (c) 2005, Linus Torvalds
# Copyright (c) 2005, Junio C Hamano
# 
# Clone a repository into a different directory that does not yet exist.

usage() {
	echo >&2 "* git clone [-l [-s]] [-q] [-u <upload-pack>] <repo> <dir>"
	exit 1
}

get_repo_base() {
	(cd "$1" && (cd .git ; pwd)) 2> /dev/null
}

if [ -n "$GIT_SSL_NO_VERIFY" ]; then
    curl_extra_args="-k"
fi

http_fetch () {
	# $1 = Remote, $2 = Local
	curl -nsf $curl_extra_args "$1" >"$2"
}

clone_dumb_http () {
	# $1 - remote, $2 - local
	cd "$2" &&
	clone_tmp='.git/clone-tmp' &&
	mkdir -p "$clone_tmp" || exit 1
	http_fetch "$1/info/refs" "$clone_tmp/refs" &&
	http_fetch "$1/objects/info/packs" "$clone_tmp/packs" || {
		echo >&2 "Cannot get remote repository information.
Perhaps git-update-server-info needs to be run there?"
		exit 1;
	}
	while read type name
	do
		case "$type" in
		P) ;;
		*) continue ;;
		esac &&

		idx=`expr "$name" : '\(.*\)\.pack'`.idx
		http_fetch "$1/objects/pack/$name" ".git/objects/pack/$name" &&
		http_fetch "$1/objects/pack/$idx" ".git/objects/pack/$idx" &&
		git-verify-pack ".git/objects/pack/$idx" || exit 1
	done <"$clone_tmp/packs"

	while read sha1 refname
	do
		name=`expr "$refname" : 'refs/\(.*\)'` &&
		git-http-fetch -v -a -w "$name" "$name" "$1/" || exit 1
	done <"$clone_tmp/refs"
	rm -fr "$clone_tmp"
}

quiet=
use_local=no
local_shared=no
upload_pack=
while
	case "$#,$1" in
	0,*) break ;;
	*,-l|*,--l|*,--lo|*,--loc|*,--loca|*,--local) use_local=yes ;;
        *,-s|*,--s|*,--sh|*,--sha|*,--shar|*,--share|*,--shared) 
          local_shared=yes ;;
	*,-q|*,--quiet) quiet=-q ;;
	1,-u|1,--upload-pack) usage ;;
	*,-u|*,--upload-pack)
		shift
		upload_pack="--exec=$1" ;;
	*,-*) usage ;;
	*) break ;;
	esac
do
	shift
done

# Turn the source into an absolute path if
# it is local
repo="$1"
local=no
if base=$(get_repo_base "$repo"); then
	repo="$base"
	local=yes
fi

dir="$2"
mkdir "$dir" &&
D=$(
	(cd "$dir" && git-init-db && pwd)
) &&
test -d "$D" || usage

# We do local magic only when the user tells us to.
case "$local,$use_local" in
yes,yes)
	( cd "$repo/objects" ) || {
		echo >&2 "-l flag seen but $repo is not local."
		exit 1
	}

	case "$local_shared" in
	no)
	    # See if we can hardlink and drop "l" if not.
	    sample_file=$(cd "$repo" && \
			  find objects -type f -print | sed -e 1q)

	    # objects directory should not be empty since we are cloning!
	    test -f "$repo/$sample_file" || exit

	    l=
	    if ln "$repo/$sample_file" "$D/.git/objects/sample" 2>/dev/null
	    then
		    l=l
	    fi &&
	    rm -f "$D/.git/objects/sample" &&
	    cd "$repo" &&
	    find objects -type f -print |
	    cpio -puamd$l "$D/.git/" || exit 1
	    ;;
	yes)
	    mkdir -p "$D/.git/objects/info"
	    {
		test -f "$repo/objects/info/alternates" &&
		cat "$repo/objects/info/alternates";
		echo "$repo/objects"
	    } >"$D/.git/objects/info/alternates"
	    ;;
	esac

	# Make a duplicate of refs and HEAD pointer
	HEAD=
	if test -f "$repo/HEAD"
	then
		HEAD=HEAD
	fi
	tar Ccf "$repo" - refs $HEAD | tar Cxf "$D/.git" - || exit 1
	;;
*)
	case "$repo" in
	rsync://*)
		rsync $quiet -avz --ignore-existing "$repo/objects/" "$D/.git/objects/" &&
		rsync $quiet -avz --ignore-existing "$repo/refs/" "$D/.git/refs/"
		;;
	http://*)
		clone_dumb_http "$repo" "$D"
		;;
	*)
		cd "$D" && case "$upload_pack" in
		'') git-clone-pack $quiet "$repo" ;;
		*) git-clone-pack $quiet "$upload_pack" "$repo" ;;
		esac
		;;
	esac
	;;
esac

# Update origin.
mkdir -p "$D/.git/remotes/" &&
rm -f "$D/.git/remotes/origin" &&
echo >"$D/.git/remotes/origin" \
"URL: $repo
Pull: master:origin"
