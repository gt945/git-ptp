#!/bin/sh

test_description='some bundle related tests'
. ./test-lib.sh

test_expect_success 'setup' '

	: > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	test_tick &&
	git tag -m tag tag &&
	: > file2 &&
	git add file2 &&
	: > file3 &&
	test_tick &&
	git commit -m second &&
	git add file3 &&
	test_tick &&
	git commit -m third

'

test_expect_success 'tags can be excluded by rev-list options' '

	git bundle create bundle --all --since=7.Apr.2005.15:16:00.-0700 &&
	git ls-remote bundle > output &&
	! grep tag output

'

test_expect_success 'die if bundle file cannot be created' '

	mkdir adir &&
	test_must_fail git bundle create adir --all

'

test_expect_failure 'bundle --stdin' '

	echo master | git bundle create stdin-bundle.bdl --stdin &&
	git ls-remote stdin-bundle.bdl >output &&
	grep master output

'

test_expect_failure 'bundle --stdin <rev-list options>' '

	echo master | git bundle create hybrid-bundle.bdl --stdin tag &&
	git ls-remote hybrid-bundle.bdl >output &&
	grep master output

'

test_expect_success 'empty bundle file is rejected' '

    >empty-bundle && test_must_fail git fetch empty-bundle

'

# This triggers a bug in older versions where the resulting line (with
# --pretty=oneline) was longer than a 1024-char buffer.
test_expect_success 'ridiculously long subject in boundary' '
	: >file4 &&
	test_tick &&
	git add file4 &&
	printf "%01200d\n" 0 | git commit -F - &&
	test_commit fifth &&
	git bundle create long-subject-bundle.bdl HEAD^..HEAD &&
	git bundle list-heads long-subject-bundle.bdl >heads &&
	test -s heads &&
	git fetch long-subject-bundle.bdl &&
	sed -n "/^-/{p;q}" long-subject-bundle.bdl >boundary &&
	grep "^-$_x40 " boundary
'

test_done
