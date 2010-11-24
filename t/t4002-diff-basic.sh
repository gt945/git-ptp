#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test diff raw-output.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree-m-3way.sh

cat >.test-plain-OA <<\EOF
:000000 100644 0000000000000000000000000000000000000000 ccba72ad3888a3520b39efcf780b9ee64167535d A	AA
:000000 100644 0000000000000000000000000000000000000000 7e426fb079479fd67f6d81f984e4ec649a44bc25 A	AN
:100644 000000 bcc68ef997017466d5c9094bcf7692295f588c9a 0000000000000000000000000000000000000000 D	DD
:000000 040000 0000000000000000000000000000000000000000 6d50f65d3bdab91c63444294d38f08aeff328e42 A	DF
:100644 000000 141c1f1642328e4bc46a7d801a71da392e66791e 0000000000000000000000000000000000000000 D	DM
:100644 000000 35abde1506ddf806572ff4d407bd06885d0f8ee9 0000000000000000000000000000000000000000 D	DN
:000000 100644 0000000000000000000000000000000000000000 1d41122ebdd7a640f29d3c9cc4f9d70094374762 A	LL
:100644 100644 03f24c8c4700babccfd28b654e7e8eac402ad6cd 103d9f89b50b9aad03054b579be5e7aa665f2d57 M	MD
:100644 100644 b258508afb7ceb449981bd9d63d2d3e971bf8d34 b431b272d829ff3aa4d1a5085f4394ab4d3305b6 M	MM
:100644 100644 bd084b0c27c7b6cc34f11d6d0509a29be3caf970 a716d58de4a570e0038f5c307bd8db34daea021f M	MN
:100644 100644 40c959f984c8b89a2b02520d17f00d717f024397 2ac547ae9614a00d1b28275de608131f7a0e259f M	SS
:100644 100644 4ac13458899ab908ef3b1128fa378daefc88d356 4c86f9a85fbc5e6804ee2e17a797538fbe785bca M	TT
:040000 040000 7d670fdcdb9929f6c7dac196ff78689cd1c566a1 5e5f22072bb39f6e12cf663a57cb634c76eefb49 M	Z
EOF

cat >.test-recursive-OA <<\EOF
:000000 100644 0000000000000000000000000000000000000000 ccba72ad3888a3520b39efcf780b9ee64167535d A	AA
:000000 100644 0000000000000000000000000000000000000000 7e426fb079479fd67f6d81f984e4ec649a44bc25 A	AN
:100644 000000 bcc68ef997017466d5c9094bcf7692295f588c9a 0000000000000000000000000000000000000000 D	DD
:000000 100644 0000000000000000000000000000000000000000 68a6d8b91da11045cf4aa3a5ab9f2a781c701249 A	DF/DF
:100644 000000 141c1f1642328e4bc46a7d801a71da392e66791e 0000000000000000000000000000000000000000 D	DM
:100644 000000 35abde1506ddf806572ff4d407bd06885d0f8ee9 0000000000000000000000000000000000000000 D	DN
:000000 100644 0000000000000000000000000000000000000000 1d41122ebdd7a640f29d3c9cc4f9d70094374762 A	LL
:100644 100644 03f24c8c4700babccfd28b654e7e8eac402ad6cd 103d9f89b50b9aad03054b579be5e7aa665f2d57 M	MD
:100644 100644 b258508afb7ceb449981bd9d63d2d3e971bf8d34 b431b272d829ff3aa4d1a5085f4394ab4d3305b6 M	MM
:100644 100644 bd084b0c27c7b6cc34f11d6d0509a29be3caf970 a716d58de4a570e0038f5c307bd8db34daea021f M	MN
:100644 100644 40c959f984c8b89a2b02520d17f00d717f024397 2ac547ae9614a00d1b28275de608131f7a0e259f M	SS
:100644 100644 4ac13458899ab908ef3b1128fa378daefc88d356 4c86f9a85fbc5e6804ee2e17a797538fbe785bca M	TT
:000000 100644 0000000000000000000000000000000000000000 8acb8e9750e3f644bf323fcf3d338849db106c77 A	Z/AA
:000000 100644 0000000000000000000000000000000000000000 087494262084cefee7ed484d20c8dc0580791272 A	Z/AN
:100644 000000 879007efae624d2b1307214b24a956f0a8d686a8 0000000000000000000000000000000000000000 D	Z/DD
:100644 000000 9b541b2275c06e3a7b13f28badf5294e2ae63df4 0000000000000000000000000000000000000000 D	Z/DM
:100644 000000 beb5d38c55283d280685ea21a0e50cfcc0ca064a 0000000000000000000000000000000000000000 D	Z/DN
:100644 100644 d41fda41b7ec4de46b43cb7ea42a45001ae393d5 a79ac3be9377639e1c7d1edf1ae1b3a5f0ccd8a9 M	Z/MD
:100644 100644 4ca22bae2527d3d9e1676498a0fba3b355bd1278 61422ba9c2c873416061a88cd40a59a35b576474 M	Z/MM
:100644 100644 b16d7b25b869f2beb124efa53467d8a1550ad694 a5c544c21cfcb07eb80a4d89a5b7d1570002edfd M	Z/MN
EOF
cat >.test-plain-OB <<\EOF
:000000 100644 0000000000000000000000000000000000000000 6aa2b5335b16431a0ef71e5c0a28be69183cf6a2 A	AA
:100644 000000 bcc68ef997017466d5c9094bcf7692295f588c9a 0000000000000000000000000000000000000000 D	DD
:000000 100644 0000000000000000000000000000000000000000 71420ab81e254145d26d6fc0cddee64c1acd4787 A	DF
:100644 100644 141c1f1642328e4bc46a7d801a71da392e66791e 3c4d8de5fbad08572bab8e10eef8dbb264cf0231 M	DM
:000000 100644 0000000000000000000000000000000000000000 1d41122ebdd7a640f29d3c9cc4f9d70094374762 A	LL
:100644 000000 03f24c8c4700babccfd28b654e7e8eac402ad6cd 0000000000000000000000000000000000000000 D	MD
:100644 100644 b258508afb7ceb449981bd9d63d2d3e971bf8d34 19989d4559aae417fedee240ccf2ba315ea4dc2b M	MM
:000000 100644 0000000000000000000000000000000000000000 15885881ea69115351c09b38371f0348a3fb8c67 A	NA
:100644 000000 a4e179e4291e5536a5e1c82e091052772d2c5a93 0000000000000000000000000000000000000000 D	ND
:100644 100644 c8f25781e8f1792e3e40b74225e20553041b5226 cdb9a8c3da571502ac30225e9c17beccb8387983 M	NM
:100644 100644 40c959f984c8b89a2b02520d17f00d717f024397 2ac547ae9614a00d1b28275de608131f7a0e259f M	SS
:100644 100644 4ac13458899ab908ef3b1128fa378daefc88d356 c4e4a12231b9fa79a0053cb6077fcb21bb5b135a M	TT
:040000 040000 7d670fdcdb9929f6c7dac196ff78689cd1c566a1 1ba523955d5160681af65cb776411f574c1e8155 M	Z
EOF
cat >.test-recursive-OB <<\EOF
:000000 100644 0000000000000000000000000000000000000000 6aa2b5335b16431a0ef71e5c0a28be69183cf6a2 A	AA
:100644 000000 bcc68ef997017466d5c9094bcf7692295f588c9a 0000000000000000000000000000000000000000 D	DD
:000000 100644 0000000000000000000000000000000000000000 71420ab81e254145d26d6fc0cddee64c1acd4787 A	DF
:100644 100644 141c1f1642328e4bc46a7d801a71da392e66791e 3c4d8de5fbad08572bab8e10eef8dbb264cf0231 M	DM
:000000 100644 0000000000000000000000000000000000000000 1d41122ebdd7a640f29d3c9cc4f9d70094374762 A	LL
:100644 000000 03f24c8c4700babccfd28b654e7e8eac402ad6cd 0000000000000000000000000000000000000000 D	MD
:100644 100644 b258508afb7ceb449981bd9d63d2d3e971bf8d34 19989d4559aae417fedee240ccf2ba315ea4dc2b M	MM
:000000 100644 0000000000000000000000000000000000000000 15885881ea69115351c09b38371f0348a3fb8c67 A	NA
:100644 000000 a4e179e4291e5536a5e1c82e091052772d2c5a93 0000000000000000000000000000000000000000 D	ND
:100644 100644 c8f25781e8f1792e3e40b74225e20553041b5226 cdb9a8c3da571502ac30225e9c17beccb8387983 M	NM
:100644 100644 40c959f984c8b89a2b02520d17f00d717f024397 2ac547ae9614a00d1b28275de608131f7a0e259f M	SS
:100644 100644 4ac13458899ab908ef3b1128fa378daefc88d356 c4e4a12231b9fa79a0053cb6077fcb21bb5b135a M	TT
:000000 100644 0000000000000000000000000000000000000000 6c0b99286d0bce551ac4a7b3dff8b706edff3715 A	Z/AA
:100644 000000 879007efae624d2b1307214b24a956f0a8d686a8 0000000000000000000000000000000000000000 D	Z/DD
:100644 100644 9b541b2275c06e3a7b13f28badf5294e2ae63df4 d77371d15817fcaa57eeec27f770c505ba974ec1 M	Z/DM
:100644 000000 d41fda41b7ec4de46b43cb7ea42a45001ae393d5 0000000000000000000000000000000000000000 D	Z/MD
:100644 100644 4ca22bae2527d3d9e1676498a0fba3b355bd1278 697aad7715a1e7306ca76290a3dd4208fbaeddfa M	Z/MM
:000000 100644 0000000000000000000000000000000000000000 d12979c22fff69c59ca9409e7a8fe3ee25eaee80 A	Z/NA
:100644 000000 a18393c636b98e9bd7296b8b437ea4992b72440c 0000000000000000000000000000000000000000 D	Z/ND
:100644 100644 3fdbe17fd013303a2e981e1ca1c6cd6e72789087 7e09d6a3a14bd630913e8c75693cea32157b606d M	Z/NM
EOF
cat >.test-plain-AB <<\EOF
:100644 100644 ccba72ad3888a3520b39efcf780b9ee64167535d 6aa2b5335b16431a0ef71e5c0a28be69183cf6a2 M	AA
:100644 000000 7e426fb079479fd67f6d81f984e4ec649a44bc25 0000000000000000000000000000000000000000 D	AN
:000000 100644 0000000000000000000000000000000000000000 71420ab81e254145d26d6fc0cddee64c1acd4787 A	DF
:040000 000000 6d50f65d3bdab91c63444294d38f08aeff328e42 0000000000000000000000000000000000000000 D	DF
:000000 100644 0000000000000000000000000000000000000000 3c4d8de5fbad08572bab8e10eef8dbb264cf0231 A	DM
:000000 100644 0000000000000000000000000000000000000000 35abde1506ddf806572ff4d407bd06885d0f8ee9 A	DN
:100644 000000 103d9f89b50b9aad03054b579be5e7aa665f2d57 0000000000000000000000000000000000000000 D	MD
:100644 100644 b431b272d829ff3aa4d1a5085f4394ab4d3305b6 19989d4559aae417fedee240ccf2ba315ea4dc2b M	MM
:100644 100644 a716d58de4a570e0038f5c307bd8db34daea021f bd084b0c27c7b6cc34f11d6d0509a29be3caf970 M	MN
:000000 100644 0000000000000000000000000000000000000000 15885881ea69115351c09b38371f0348a3fb8c67 A	NA
:100644 000000 a4e179e4291e5536a5e1c82e091052772d2c5a93 0000000000000000000000000000000000000000 D	ND
:100644 100644 c8f25781e8f1792e3e40b74225e20553041b5226 cdb9a8c3da571502ac30225e9c17beccb8387983 M	NM
:100644 100644 4c86f9a85fbc5e6804ee2e17a797538fbe785bca c4e4a12231b9fa79a0053cb6077fcb21bb5b135a M	TT
:040000 040000 5e5f22072bb39f6e12cf663a57cb634c76eefb49 1ba523955d5160681af65cb776411f574c1e8155 M	Z
EOF
cat >.test-recursive-AB <<\EOF
:100644 100644 ccba72ad3888a3520b39efcf780b9ee64167535d 6aa2b5335b16431a0ef71e5c0a28be69183cf6a2 M	AA
:100644 000000 7e426fb079479fd67f6d81f984e4ec649a44bc25 0000000000000000000000000000000000000000 D	AN
:000000 100644 0000000000000000000000000000000000000000 71420ab81e254145d26d6fc0cddee64c1acd4787 A	DF
:100644 000000 68a6d8b91da11045cf4aa3a5ab9f2a781c701249 0000000000000000000000000000000000000000 D	DF/DF
:000000 100644 0000000000000000000000000000000000000000 3c4d8de5fbad08572bab8e10eef8dbb264cf0231 A	DM
:000000 100644 0000000000000000000000000000000000000000 35abde1506ddf806572ff4d407bd06885d0f8ee9 A	DN
:100644 000000 103d9f89b50b9aad03054b579be5e7aa665f2d57 0000000000000000000000000000000000000000 D	MD
:100644 100644 b431b272d829ff3aa4d1a5085f4394ab4d3305b6 19989d4559aae417fedee240ccf2ba315ea4dc2b M	MM
:100644 100644 a716d58de4a570e0038f5c307bd8db34daea021f bd084b0c27c7b6cc34f11d6d0509a29be3caf970 M	MN
:000000 100644 0000000000000000000000000000000000000000 15885881ea69115351c09b38371f0348a3fb8c67 A	NA
:100644 000000 a4e179e4291e5536a5e1c82e091052772d2c5a93 0000000000000000000000000000000000000000 D	ND
:100644 100644 c8f25781e8f1792e3e40b74225e20553041b5226 cdb9a8c3da571502ac30225e9c17beccb8387983 M	NM
:100644 100644 4c86f9a85fbc5e6804ee2e17a797538fbe785bca c4e4a12231b9fa79a0053cb6077fcb21bb5b135a M	TT
:100644 100644 8acb8e9750e3f644bf323fcf3d338849db106c77 6c0b99286d0bce551ac4a7b3dff8b706edff3715 M	Z/AA
:100644 000000 087494262084cefee7ed484d20c8dc0580791272 0000000000000000000000000000000000000000 D	Z/AN
:000000 100644 0000000000000000000000000000000000000000 d77371d15817fcaa57eeec27f770c505ba974ec1 A	Z/DM
:000000 100644 0000000000000000000000000000000000000000 beb5d38c55283d280685ea21a0e50cfcc0ca064a A	Z/DN
:100644 000000 a79ac3be9377639e1c7d1edf1ae1b3a5f0ccd8a9 0000000000000000000000000000000000000000 D	Z/MD
:100644 100644 61422ba9c2c873416061a88cd40a59a35b576474 697aad7715a1e7306ca76290a3dd4208fbaeddfa M	Z/MM
:100644 100644 a5c544c21cfcb07eb80a4d89a5b7d1570002edfd b16d7b25b869f2beb124efa53467d8a1550ad694 M	Z/MN
:000000 100644 0000000000000000000000000000000000000000 d12979c22fff69c59ca9409e7a8fe3ee25eaee80 A	Z/NA
:100644 000000 a18393c636b98e9bd7296b8b437ea4992b72440c 0000000000000000000000000000000000000000 D	Z/ND
:100644 100644 3fdbe17fd013303a2e981e1ca1c6cd6e72789087 7e09d6a3a14bd630913e8c75693cea32157b606d M	Z/NM
EOF

x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
x40="$x40$x40$x40$x40$x40$x40$x40$x40"
z40='0000000000000000000000000000000000000000'
cmp_diff_files_output () {
    # diff-files never reports additions.  Also it does not fill in the
    # object ID for the changed files because it wants you to look at the
    # filesystem.
    sed <"$2" >.test-tmp \
	-e '/^:000000 /d;s/'$x40'\( [MCRNDU][0-9]*\)	/'$z40'\1	/' &&
    test_cmp "$1" .test-tmp
}

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree $tree_O $tree_A >.test-a &&
     cmp -s .test-a .test-plain-OA'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree -r $tree_O $tree_A >.test-a &&
     cmp -s .test-a .test-recursive-OA'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree $tree_O $tree_B >.test-a &&
     cmp -s .test-a .test-plain-OB'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree -r $tree_O $tree_B >.test-a &&
     cmp -s .test-a .test-recursive-OB'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree $tree_A $tree_B >.test-a &&
     cmp -s .test-a .test-plain-AB'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree -r $tree_A $tree_B >.test-a &&
     cmp -s .test-a .test-recursive-AB'

test_expect_success \
    'diff-tree --stdin of known trees.' \
    'echo $tree_A $tree_B | git diff-tree --stdin > .test-a &&
     echo $tree_A $tree_B > .test-plain-ABx &&
     cat .test-plain-AB >> .test-plain-ABx &&
     cmp -s .test-a .test-plain-ABx'

test_expect_success \
    'diff-tree --stdin of known trees.' \
    'echo $tree_A $tree_B | git diff-tree -r --stdin > .test-a &&
     echo $tree_A $tree_B > .test-recursive-ABx &&
     cat .test-recursive-AB >> .test-recursive-ABx &&
     cmp -s .test-a .test-recursive-ABx'

test_expect_success \
    'diff-cache O with A in cache' \
    'git read-tree $tree_A &&
     git diff-index --cached $tree_O >.test-a &&
     cmp -s .test-a .test-recursive-OA'

test_expect_success \
    'diff-cache O with B in cache' \
    'git read-tree $tree_B &&
     git diff-index --cached $tree_O >.test-a &&
     cmp -s .test-a .test-recursive-OB'

test_expect_success \
    'diff-cache A with B in cache' \
    'git read-tree $tree_B &&
     git diff-index --cached $tree_A >.test-a &&
     cmp -s .test-a .test-recursive-AB'

test_expect_success \
    'diff-files with O in cache and A checked out' \
    'rm -fr Z [A-Z][A-Z] &&
     git read-tree $tree_A &&
     git checkout-index -f -a &&
     git read-tree --reset $tree_O &&
     test_must_fail git update-index --refresh -q &&
     git diff-files >.test-a &&
     cmp_diff_files_output .test-a .test-recursive-OA'

test_expect_success \
    'diff-files with O in cache and B checked out' \
    'rm -fr Z [A-Z][A-Z] &&
     git read-tree $tree_B &&
     git checkout-index -f -a &&
     git read-tree --reset $tree_O &&
     test_must_fail git update-index --refresh -q &&
     git diff-files >.test-a &&
     cmp_diff_files_output .test-a .test-recursive-OB'

test_expect_success \
    'diff-files with A in cache and B checked out' \
    'rm -fr Z [A-Z][A-Z] &&
     git read-tree $tree_B &&
     git checkout-index -f -a &&
     git read-tree --reset $tree_A &&
     test_must_fail git update-index --refresh -q &&
     git diff-files >.test-a &&
     cmp_diff_files_output .test-a .test-recursive-AB'

################################################################
# Now we have established the baseline, we do not have to
# rely on individual object ID values that much.

test_expect_success \
    'diff-tree O A == diff-tree -R A O' \
    'git diff-tree $tree_O $tree_A >.test-a &&
    git diff-tree -R $tree_A $tree_O >.test-b &&
    cmp -s .test-a .test-b'

test_expect_success \
    'diff-tree -r O A == diff-tree -r -R A O' \
    'git diff-tree -r $tree_O $tree_A >.test-a &&
    git diff-tree -r -R $tree_A $tree_O >.test-b &&
    cmp -s .test-a .test-b'

test_expect_success \
    'diff-tree B A == diff-tree -R A B' \
    'git diff-tree $tree_B $tree_A >.test-a &&
    git diff-tree -R $tree_A $tree_B >.test-b &&
    cmp -s .test-a .test-b'

test_expect_success \
    'diff-tree -r B A == diff-tree -r -R A B' \
    'git diff-tree -r $tree_B $tree_A >.test-a &&
    git diff-tree -r -R $tree_A $tree_B >.test-b &&
    cmp -s .test-a .test-b'

test_expect_success \
    'diff can read from stdin' \
    'test_must_fail git diff --no-index -- MN - < NN |
        grep -v "^index" | sed "s#/-#/NN#" >.test-a &&
    test_must_fail git diff --no-index -- MN NN |
        grep -v "^index" >.test-b &&
    test_cmp .test-a .test-b'

test_done
