#!/bin/sh

test_description='git-rev-list --pretty=format test'

. ./test-lib.sh

test_tick
test_expect_success 'setup' '
touch foo && git-add foo && git-commit -m "added foo" &&
  echo changed >foo && git-commit -a -m "changed foo"
'

# usage: test_format name format_string <expected_output
test_format() {
	cat >expect.$1
	test_expect_success "format $1" "
git-rev-list --pretty=format:$2 master >output.$1 &&
git-diff expect.$1 output.$1
"
}

test_format hash %H%n%h <<'EOF'
commit 131a310eb913d107dd3c09a65d1651175898735d
131a310eb913d107dd3c09a65d1651175898735d
131a310
commit 86c75cfd708a0e5868dc876ed5b8bb66c80b4873
86c75cfd708a0e5868dc876ed5b8bb66c80b4873
86c75cf
EOF

test_format tree %T%n%t <<'EOF'
commit 131a310eb913d107dd3c09a65d1651175898735d
fe722612f26da5064c32ca3843aa154bdb0b08a0
fe72261
commit 86c75cfd708a0e5868dc876ed5b8bb66c80b4873
4d5fcadc293a348e88f777dc0920f11e7d71441c
4d5fcad
EOF

test_format parents %P%n%p <<'EOF'
commit 131a310eb913d107dd3c09a65d1651175898735d
86c75cfd708a0e5868dc876ed5b8bb66c80b4873 
86c75cf 
commit 86c75cfd708a0e5868dc876ed5b8bb66c80b4873
86c75cf 
86c75cf 
EOF

# we don't test relative here
test_format author %an%n%ae%n%ad%n%aD%n%at <<'EOF'
commit 131a310eb913d107dd3c09a65d1651175898735d
A U Thor
author@example.com
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
commit 86c75cfd708a0e5868dc876ed5b8bb66c80b4873
A U Thor
author@example.com
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
EOF

test_format committer %cn%n%ce%n%cd%n%cD%n%ct <<'EOF'
commit 131a310eb913d107dd3c09a65d1651175898735d
C O Mitter
committer@example.com
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
commit 86c75cfd708a0e5868dc876ed5b8bb66c80b4873
C O Mitter
committer@example.com
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
EOF

test_format encoding %e <<'EOF'
commit 131a310eb913d107dd3c09a65d1651175898735d
<unknown>
commit 86c75cfd708a0e5868dc876ed5b8bb66c80b4873
<unknown>
EOF

test_format subject %s <<'EOF'
commit 131a310eb913d107dd3c09a65d1651175898735d
changed foo
commit 86c75cfd708a0e5868dc876ed5b8bb66c80b4873
added foo
EOF

test_format body %b <<'EOF'
commit 131a310eb913d107dd3c09a65d1651175898735d
<unknown>
commit 86c75cfd708a0e5868dc876ed5b8bb66c80b4873
<unknown>
EOF

test_format colors %Credfoo%Cgreenbar%Cbluebaz%Cresetxyzzy <<'EOF'
commit 131a310eb913d107dd3c09a65d1651175898735d
[31mfoo[32mbar[34mbaz[mxyzzy
commit 86c75cfd708a0e5868dc876ed5b8bb66c80b4873
[31mfoo[32mbar[34mbaz[mxyzzy
EOF

test_done
