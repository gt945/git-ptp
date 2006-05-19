#!/bin/sh
#
# Copyright (c) 2006 Shawn Pearce
#

test_description='Test git-update-ref and basic ref logging'
. ./test-lib.sh

Z=0000000000000000000000000000000000000000
A=1111111111111111111111111111111111111111
B=2222222222222222222222222222222222222222
C=3333333333333333333333333333333333333333
D=4444444444444444444444444444444444444444
E=5555555555555555555555555555555555555555
F=6666666666666666666666666666666666666666
m=refs/heads/master

test_expect_success \
	"create $m" \
	'git-update-ref $m $A &&
	 test $A = $(cat .git/$m)'
test_expect_success \
	"create $m" \
	'git-update-ref $m $B $A &&
	 test $B = $(cat .git/$m)'
rm -f .git/$m

test_expect_success \
	"create $m (by HEAD)" \
	'git-update-ref HEAD $A &&
	 test $A = $(cat .git/$m)'
test_expect_success \
	"create $m (by HEAD)" \
	'git-update-ref HEAD $B $A &&
	 test $B = $(cat .git/$m)'
rm -f .git/$m

test_expect_failure \
	'(not) create HEAD with old sha1' \
	'git-update-ref HEAD $A $B'
test_expect_failure \
	"(not) prior created .git/$m" \
	'test -f .git/$m'
rm -f .git/$m

test_expect_success \
	"create HEAD" \
	'git-update-ref HEAD $A'
test_expect_failure \
	'(not) change HEAD with wrong SHA1' \
	'git-update-ref HEAD $B $Z'
test_expect_failure \
	"(not) changed .git/$m" \
	'test $B = $(cat .git/$m)'
rm -f .git/$m

mkdir -p .git/logs/refs/heads
touch .git/logs/refs/heads/master
test_expect_success \
	"create $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:30" \
	 git-update-ref HEAD $A -m "Initial Creation" &&
	 test $A = $(cat .git/$m)'
test_expect_success \
	"update $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:31" \
	 git-update-ref HEAD $B $A -m "Switch" &&
	 test $B = $(cat .git/$m)'
test_expect_success \
	"set $m (logged by touch)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:41" \
	 git-update-ref HEAD $A &&
	 test $A = $(cat .git/$m)'

cat >expect <<EOF
$Z $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150200 +0000	Initial Creation
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150260 +0000	Switch
$B $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150860 +0000
EOF
test_expect_success \
	"verifying $m's log" \
	'diff expect .git/logs/$m'
rm -rf .git/$m .git/logs expect

test_expect_success \
	'enable core.logAllRefUpdates' \
	'git-repo-config core.logAllRefUpdates true &&
	 test true = $(git-repo-config --bool --get core.logAllRefUpdates)'

test_expect_success \
	"create $m (logged by config)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:32" \
	 git-update-ref HEAD $A -m "Initial Creation" &&
	 test $A = $(cat .git/$m)'
test_expect_success \
	"update $m (logged by config)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:33" \
	 git-update-ref HEAD $B $A -m "Switch" &&
	 test $B = $(cat .git/$m)'
test_expect_success \
	"set $m (logged by config)" \
	'GIT_COMMITTER_DATE="2005-05-26 23:43" \
	 git-update-ref HEAD $A &&
	 test $A = $(cat .git/$m)'

cat >expect <<EOF
$Z $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150320 +0000	Initial Creation
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150380 +0000	Switch
$B $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150980 +0000
EOF
test_expect_success \
	"verifying $m's log" \
	'diff expect .git/logs/$m'
rm -f .git/$m .git/logs/$m expect

git-update-ref $m $D
cat >.git/logs/$m <<EOF
$C $A $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150320 -0500
$A $B $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150380 -0500
$F $Z $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150680 -0500
$Z $E $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> 1117150980 -0500
EOF

ed="Thu, 26 May 2005 18:32:00 -0500"
gd="Thu, 26 May 2005 18:33:00 -0500"
ld="Thu, 26 May 2005 18:43:00 -0500"
test_expect_success \
	'Query "master@May 25 2005" (before history)' \
	'rm -f o e
	 git-rev-parse --verify "master@May 25 2005" >o 2>e &&
	 test $C = $(cat o) &&
	 test "warning: Log .git/logs/$m only goes back to $ed." = "$(cat e)"'
test_expect_success \
	"Query master@2005-05-25 (before history)" \
	'rm -f o e
	 git-rev-parse --verify master@2005-05-25 >o 2>e &&
	 test $C = $(cat o) &&
	 echo test "warning: Log .git/logs/$m only goes back to $ed." = "$(cat e)"'
test_expect_success \
	'Query "master@May 26 2005 23:31:59" (1 second before history)' \
	'rm -f o e
	 git-rev-parse --verify "master@May 26 2005 23:31:59" >o 2>e &&
	 test $C = $(cat o) &&
	 test "warning: Log .git/logs/$m only goes back to $ed." = "$(cat e)"'
test_expect_success \
	'Query "master@May 26 2005 23:32:00" (exactly history start)' \
	'rm -f o e
	 git-rev-parse --verify "master@May 26 2005 23:32:00" >o 2>e &&
	 test $A = $(cat o) &&
	 test "" = "$(cat e)"'
test_expect_success \
	'Query "master@2005-05-26 23:33:01" (middle of history with gap)' \
	'rm -f o e
	 git-rev-parse --verify "master@2005-05-26 23:33:01" >o 2>e &&
	 test $B = $(cat o) &&
	 test "warning: Log .git/logs/$m has gap after $gd." = "$(cat e)"'
test_expect_success \
	'Query "master@2005-05-26 23:33:01" (middle of history)' \
	'rm -f o e
	 git-rev-parse --verify "master@2005-05-26 23:38:00" >o 2>e &&
	 test $Z = $(cat o) &&
	 test "" = "$(cat e)"'
test_expect_success \
	'Query "master@2005-05-26 23:43:00" (exact end of history)' \
	'rm -f o e
	 git-rev-parse --verify "master@2005-05-26 23:43:00" >o 2>e &&
	 test $E = $(cat o) &&
	 test "" = "$(cat e)"'
test_expect_success \
	'Query "master@2005-05-28" (past end of history)' \
	'rm -f o e
	 git-rev-parse --verify "master@2005-05-28" >o 2>e &&
	 test $D = $(cat o) &&
	 test "warning: Log .git/logs/$m unexpectedly ended on $ld." = "$(cat e)"'

test_done
