# Define MOZILLA_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine coming from Mozilla. It is GPL'd and should be fast
# on non-x86 architectures (e.g. PowerPC), while the OpenSSL version (default
# choice) has very fast version optimized for i586.
#
# Define NO_OPENSSL environment variable if you do not have OpenSSL. You will
# miss out git-rev-list --merge-order. This also implies MOZILLA_SHA1.
#
# Define NO_CURL if you do not have curl installed.  git-http-pull is not
# built, and you cannot use http:// and https:// transports.
#
# Define PPC_SHA1 environment variable when running make to make use of
# a bundled SHA1 routine optimized for PowerPC.
#
# Define NEEDS_SSL_WITH_CRYPTO if you need -lcrypto with -lssl (Darwin).
# Define NEEDS_LIBICONV if linking with libc is not enough (Darwin).

# Define COLLISION_CHECK below if you believe that SHA1's
# 1461501637330902918203684832716283019655932542976 hashes do not give you
# sufficient guarantee that no collisions between objects will ever happen.

# DEFINES += -DCOLLISION_CHECK

# Define USE_NSEC below if you want git to care about sub-second file mtimes
# and ctimes. Note that you need recent glibc (at least 2.2.4) for this, and
# it will BREAK YOUR LOCAL DIFFS! show-diff and anything using it will likely
# randomly break unless your underlying filesystem supports those sub-second
# times (my ext3 doesn't).

# DEFINES += -DUSE_NSEC

# Define USE_STDEV below if you want git to care about the underlying device
# change being considered an inode change from the update-cache perspective.

# DEFINES += -DUSE_STDEV

GIT_VERSION = 0.99.6

CFLAGS = -g -O2 -Wall
ALL_CFLAGS = $(CFLAGS) $(DEFINES)

prefix = $(HOME)
bindir = $(prefix)/bin
template_dir = $(prefix)/share/git-core/templates/
# DESTDIR=

CC = gcc
AR = ar
INSTALL = install
RPMBUILD = rpmbuild

# sparse is architecture-neutral, which means that we need to tell it
# explicitly what architecture to check for. Fix this up for yours..
SPARSE_FLAGS = -D__BIG_ENDIAN__ -D__powerpc__



### --- END CONFIGURATION SECTION ---



SCRIPTS=git git-merge-one-file-script git-prune-script \
	git-pull-script git-tag-script git-resolve-script git-whatchanged \
	git-fetch-script git-status-script git-commit-script \
	git-log-script git-shortlog git-cvsimport-script git-diff-script \
	git-reset-script git-add-script git-checkout-script git-clone-script \
	gitk git-cherry git-rebase-script git-relink-script git-repack-script \
	git-format-patch-script git-sh-setup-script git-push-script \
	git-branch-script git-parse-remote-script git-verify-tag-script \
	git-ls-remote-script git-rename-script \
	git-request-pull-script git-bisect-script \
	git-applymbox git-applypatch

SCRIPTS += git-count-objects-script
SCRIPTS += git-revert-script
SCRIPTS += git-octopus-script
SCRIPTS += git-archimport-script

# The ones that do not have to link with lcrypto nor lz.
SIMPLE_PROGRAMS = \
	git-get-tar-commit-id git-mailinfo git-mailsplit git-stripspace \
	git-daemon git-var

# ... and all the rest
PROG=   git-update-cache git-diff-files git-init-db git-write-tree \
	git-read-tree git-commit-tree git-cat-file git-fsck-cache \
	git-checkout-cache git-diff-tree git-rev-tree git-ls-files \
	git-ls-tree git-merge-base git-merge-cache \
	git-unpack-file git-export git-diff-cache git-convert-cache \
	git-ssh-push git-ssh-pull git-rev-list git-mktag \
	git-diff-helper git-tar-tree git-local-pull git-hash-object \
	git-apply \
	git-diff-stages git-rev-parse git-patch-id git-pack-objects \
	git-unpack-objects git-verify-pack git-receive-pack git-send-pack \
	git-prune-packed git-fetch-pack git-upload-pack git-clone-pack \
	git-show-index git-peek-remote git-show-branch \
	git-update-server-info git-show-rev-cache git-build-rev-cache \
	$(SIMPLE_PROGRAMS)

ifdef WITH_SEND_EMAIL
SCRIPTS += git-send-email-script
endif

ifndef NO_CURL
	PROG+= git-http-pull
endif

LIB_FILE=libgit.a
LIB_H=cache.h object.h blob.h tree.h commit.h tag.h delta.h epoch.h csum-file.h \
	pack.h pkt-line.h refs.h
LIB_OBJS=read-cache.o sha1_file.o usage.o object.o commit.o tree.o blob.o \
	 tag.o date.o index.o diff-delta.o patch-delta.o entry.o path.o \
	 refs.o csum-file.o pack-check.o pkt-line.o connect.o ident.o \
	 sha1_name.o setup.o

LIB_H += rev-cache.h
LIB_OBJS += rev-cache.o

LIB_H += run-command.h
LIB_OBJS += run-command.o

LIB_H += strbuf.h
LIB_OBJS += strbuf.o

LIB_H += quote.h
LIB_OBJS += quote.o 

LIB_H += diff.h count-delta.h
DIFF_OBJS = diff.o diffcore-rename.o diffcore-pickaxe.o diffcore-pathspec.o \
	diffcore-break.o diffcore-order.o
LIB_OBJS += $(DIFF_OBJS) count-delta.o

LIB_OBJS += gitenv.o
LIB_OBJS += server-info.o

LIBS = $(LIB_FILE)
LIBS += -lz

ifeq ($(shell uname -s),Darwin)
	NEEDS_SSL_WITH_CRYPTO = YesPlease
	NEEDS_LIBICONV = YesPlease
endif

ifndef NO_OPENSSL
	LIB_OBJS += epoch.o
	OPENSSL_LIBSSL=-lssl
else
	DEFINES += '-DNO_OPENSSL'
	MOZILLA_SHA1=1
	OPENSSL_LIBSSL=
endif
ifdef NEEDS_SSL_WITH_CRYPTO
	LIB_4_CRYPTO = -lcrypto -lssl
else
	LIB_4_CRYPTO = -lcrypto
endif
ifdef NEEDS_LIBICONV
	LIB_4_ICONV = -liconv
else
	LIB_4_ICONV =
endif
ifdef MOZILLA_SHA1
	SHA1_HEADER="mozilla-sha1/sha1.h"
	LIB_OBJS += mozilla-sha1/sha1.o
else
	ifdef PPC_SHA1
		SHA1_HEADER="ppc/sha1.h"
		LIB_OBJS += ppc/sha1.o ppc/sha1ppc.o
	else
		SHA1_HEADER=<openssl/sha.h>
		LIBS += $(LIB_4_CRYPTO)
	endif
endif

DEFINES += '-DSHA1_HEADER=$(SHA1_HEADER)'



### Build rules

all: $(PROG)

all:
	$(MAKE) -C templates

%.o: %.c
	$(CC) -o $*.o -c $(ALL_CFLAGS) $<
%.o: %.S
	$(CC) -o $*.o -c $(ALL_CFLAGS) $<

git-%: %.o $(LIB_FILE)
	$(CC) $(ALL_CFLAGS) -o $@ $(filter %.o,$^) $(LIBS)

git-mailinfo : SIMPLE_LIB += $(LIB_4_ICONV)
$(SIMPLE_PROGRAMS) : $(LIB_FILE)
$(SIMPLE_PROGRAMS) : git-% : %.o
	$(CC) $(ALL_CFLAGS) -o $@ $(filter %.o,$^) $(LIB_FILE) $(SIMPLE_LIB)

git-http-pull: pull.o
git-local-pull: pull.o
git-ssh-pull: rsh.o pull.o
git-ssh-push: rsh.o

git-http-pull: LIBS += -lcurl
git-rev-list: LIBS += $(OPENSSL_LIBSSL)

init-db.o: init-db.c
	$(CC) -c $(ALL_CFLAGS) \
		-DDEFAULT_GIT_TEMPLATE_DIR='"$(template_dir)"' $*.c

$(LIB_OBJS): $(LIB_H)
$(patsubst git-%,%.o,$(PROG)): $(LIB_H)
$(DIFF_OBJS): diffcore.h

$(LIB_FILE): $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

doc:
	$(MAKE) -C Documentation all



### Testing rules

test: all
	$(MAKE) -C t/ all

test-date: test-date.c date.o
	$(CC) $(ALL_CFLAGS) -o $@ test-date.c date.o

test-delta: test-delta.c diff-delta.o patch-delta.o
	$(CC) $(ALL_CFLAGS) -o $@ $^

check:
	for i in *.c; do sparse $(ALL_CFLAGS) $(SPARSE_FLAGS) $$i; done



### Installation rules

install: $(PROG) $(SCRIPTS)
	$(INSTALL) -m755 -d $(DESTDIR)$(bindir)
	$(INSTALL) $(PROG) $(SCRIPTS) $(DESTDIR)$(bindir)
	$(INSTALL) git-revert-script $(DESTDIR)$(bindir)/git-cherry-pick-script
	$(MAKE) -C templates install

install-doc:
	$(MAKE) -C Documentation install




### Maintainer's dist rules

git-core.spec: git-core.spec.in Makefile
	sed -e 's/@@VERSION@@/$(GIT_VERSION)/g' < $< > $@

GIT_TARNAME=git-core-$(GIT_VERSION)
dist: git-core.spec git-tar-tree
	./git-tar-tree HEAD $(GIT_TARNAME) > $(GIT_TARNAME).tar
	@mkdir -p $(GIT_TARNAME)
	@cp git-core.spec $(GIT_TARNAME)
	tar rf $(GIT_TARNAME).tar $(GIT_TARNAME)/git-core.spec
	@rm -rf $(GIT_TARNAME)
	gzip -f -9 $(GIT_TARNAME).tar

rpm: dist
	$(RPMBUILD) -ta git-core-$(GIT_VERSION).tar.gz

deb: dist
	rm -rf $(GIT_TARNAME)
	tar zxf $(GIT_TARNAME).tar.gz
	dpkg-source -b $(GIT_TARNAME)
	cd $(GIT_TARNAME) && fakeroot debian/rules binary

### Cleaning rules

clean:
	rm -f *.o mozilla-sha1/*.o ppc/*.o $(PROG) $(LIB_FILE)
	rm -f git-core.spec
	rm -rf $(GIT_TARNAME)
	rm -f $(GIT_TARNAME).tar.gz git-core_$(GIT_VERSION)-*.tar.gz
	rm -f git-core_$(GIT_VERSION)-*.deb git-core_$(GIT_VERSION)-*.dsc
	rm -f git-tk_$(GIT_VERSION)-*.deb
	$(MAKE) -C Documentation/ clean
	$(MAKE) -C templates/ clean
	$(MAKE) -C t/ clean
