=head1 NAME

Git - Perl interface to the Git version control system

=cut


package Git;

use strict;


BEGIN {

our ($VERSION, @ISA, @EXPORT, @EXPORT_OK);

# Totally unstable API.
$VERSION = '0.01';


=head1 SYNOPSIS

  use Git;

  my $version = Git::command_oneline('version');

  git_cmd_try { Git::command_noisy('update-server-info') }
              '%s failed w/ code %d';

  my $repo = Git->repository (Directory => '/srv/git/cogito.git');


  my @revs = $repo->command('rev-list', '--since=last monday', '--all');

  my ($fh, $c) = $repo->command_pipe('rev-list', '--since=last monday', '--all');
  my $lastrev = <$fh>; chomp $lastrev;
  $repo->command_close_pipe($fh, $c);

  my $lastrev = $repo->command_oneline('rev-list', '--all');

=cut


require Exporter;

@ISA = qw(Exporter);

@EXPORT = qw(git_cmd_try);

# Methods which can be called as standalone functions as well:
@EXPORT_OK = qw(command command_oneline command_pipe command_noisy
                version exec_path hash_object git_cmd_try);


=head1 DESCRIPTION

This module provides Perl scripts easy way to interface the Git version control
system. The modules have an easy and well-tested way to call arbitrary Git
commands; in the future, the interface will also provide specialized methods
for doing easily operations which are not totally trivial to do over
the generic command interface.

While some commands can be executed outside of any context (e.g. 'version'
or 'init-db'), most operations require a repository context, which in practice
means getting an instance of the Git object using the repository() constructor.
(In the future, we will also get a new_repository() constructor.) All commands
called as methods of the object are then executed in the context of the
repository.

TODO: In the future, we might also do

	my $subdir = $repo->subdir('Documentation');
	# Gets called in the subdirectory context:
	$subdir->command('status');

	my $remoterepo = $repo->remote_repository (Name => 'cogito', Branch => 'master');
	$remoterepo ||= Git->remote_repository ('http://git.or.cz/cogito.git/');
	my @refs = $remoterepo->refs();

So far, all functions just die if anything goes wrong. If you don't want that,
make appropriate provisions to catch the possible deaths. Better error recovery
mechanisms will be provided in the future.

Currently, the module merely wraps calls to external Git tools. In the future,
it will provide a much faster way to interact with Git by linking directly
to libgit. This should be completely opaque to the user, though (performance
increate nonwithstanding).

=cut


use Carp qw(carp croak); # but croak is bad - throw instead
use Error qw(:try);

require XSLoader;
XSLoader::load('Git', $VERSION);

}


=head1 CONSTRUCTORS

=over 4

=item repository ( OPTIONS )

=item repository ( DIRECTORY )

=item repository ()

Construct a new repository object.
C<OPTIONS> are passed in a hash like fashion, using key and value pairs.
Possible options are:

B<Repository> - Path to the Git repository.

B<WorkingCopy> - Path to the associated working copy; not strictly required
as many commands will happily crunch on a bare repository.

B<Directory> - Path to the Git working directory in its usual setup. This
is just for convenient setting of both C<Repository> and C<WorkingCopy>
at once: If the directory as a C<.git> subdirectory, C<Repository> is pointed
to the subdirectory and the directory is assumed to be the working copy.
If the directory does not have the subdirectory, C<WorkingCopy> is left
undefined and C<Repository> is pointed to the directory itself.

You should not use both C<Directory> and either of C<Repository> and
C<WorkingCopy> - the results of that are undefined.

Alternatively, a directory path may be passed as a single scalar argument
to the constructor; it is equivalent to setting only the C<Directory> option
field.

Calling the constructor with no options whatsoever is equivalent to
calling it with C<< Directory => '.' >>.

=cut

sub repository {
	my $class = shift;
	my @args = @_;
	my %opts = ();
	my $self;

	if (defined $args[0]) {
		if ($#args % 2 != 1) {
			# Not a hash.
			$#args == 0 or throw Error::Simple("bad usage");
			%opts = ( Directory => $args[0] );
		} else {
			%opts = @args;
		}

		if ($opts{Directory}) {
			-d $opts{Directory} or throw Error::Simple("Directory not found: $!");
			if (-d $opts{Directory}."/.git") {
				# TODO: Might make this more clever
				$opts{WorkingCopy} = $opts{Directory};
				$opts{Repository} = $opts{Directory}."/.git";
			} else {
				$opts{Repository} = $opts{Directory};
			}
			delete $opts{Directory};
		}
	}

	$self = { opts => \%opts };
	bless $self, $class;
}


=back

=head1 METHODS

=over 4

=item command ( COMMAND [, ARGUMENTS... ] )

Execute the given Git C<COMMAND> (specify it without the 'git-'
prefix), optionally with the specified extra C<ARGUMENTS>.

The method can be called without any instance or on a specified Git repository
(in that case the command will be run in the repository context).

In scalar context, it returns all the command output in a single string
(verbatim).

In array context, it returns an array containing lines printed to the
command's stdout (without trailing newlines).

In both cases, the command's stdin and stderr are the same as the caller's.

=cut

sub command {
	my ($fh, $ctx) = command_pipe(@_);

	if (not defined wantarray) {
		# Nothing to pepper the possible exception with.
		_cmd_close($fh, $ctx);

	} elsif (not wantarray) {
		local $/;
		my $text = <$fh>;
		try {
			_cmd_close($fh, $ctx);
		} catch Git::Error::Command with {
			# Pepper with the output:
			my $E = shift;
			$E->{'-outputref'} = \$text;
			throw $E;
		};
		return $text;

	} else {
		my @lines = <$fh>;
		chomp @lines;
		try {
			_cmd_close($fh, $ctx);
		} catch Git::Error::Command with {
			my $E = shift;
			$E->{'-outputref'} = \@lines;
			throw $E;
		};
		return @lines;
	}
}


=item command_oneline ( COMMAND [, ARGUMENTS... ] )

Execute the given C<COMMAND> in the same way as command()
does but always return a scalar string containing the first line
of the command's standard output.

=cut

sub command_oneline {
	my ($fh, $ctx) = command_pipe(@_);

	my $line = <$fh>;
	chomp $line;
	try {
		_cmd_close($fh, $ctx);
	} catch Git::Error::Command with {
		# Pepper with the output:
		my $E = shift;
		$E->{'-outputref'} = \$line;
		throw $E;
	};
	return $line;
}


=item command_pipe ( COMMAND [, ARGUMENTS... ] )

Execute the given C<COMMAND> in the same way as command()
does but return a pipe filehandle from which the command output can be
read.

=cut

sub command_pipe {
	my ($self, $cmd, @args) = _maybe_self(@_);

	$cmd =~ /^[a-z0-9A-Z_-]+$/ or throw Error::Simple("bad command: $cmd");

	my $pid = open(my $fh, "-|");
	if (not defined $pid) {
		throw Error::Simple("open failed: $!");
	} elsif ($pid == 0) {
		_cmd_exec($self, $cmd, @args);
	}
	return wantarray ? ($fh, join(' ', $cmd, @args)) : $fh;
}


=item command_close_pipe ( PIPE [, CTX ] )

Close the C<PIPE> as returned from C<command_pipe()>, checking
whether the command finished successfuly. The optional C<CTX> argument
is required if you want to see the command name in the error message,
and it is the second value returned by C<command_pipe()> when
called in array context. The call idiom is:

       my ($fh, $ctx) = $r->command_pipe('status');
       while (<$fh>) { ... }
       $r->command_close_pipe($fh, $ctx);

Note that you should not rely on whatever actually is in C<CTX>;
currently it is simply the command name but in future the context might
have more complicated structure.

=cut

sub command_close_pipe {
	my ($self, $fh, $ctx) = _maybe_self(@_);
	$ctx ||= '<unknown>';
	_cmd_close($fh, $ctx);
}


=item command_noisy ( COMMAND [, ARGUMENTS... ] )

Execute the given C<COMMAND> in the same way as command() does but do not
capture the command output - the standard output is not redirected and goes
to the standard output of the caller application.

While the method is called command_noisy(), you might want to as well use
it for the most silent Git commands which you know will never pollute your
stdout but you want to avoid the overhead of the pipe setup when calling them.

The function returns only after the command has finished running.

=cut

sub command_noisy {
	my ($self, $cmd, @args) = _maybe_self(@_);

	$cmd =~ /^[a-z0-9A-Z_-]+$/ or throw Error::Simple("bad command: $cmd");

	my $pid = fork;
	if (not defined $pid) {
		throw Error::Simple("fork failed: $!");
	} elsif ($pid == 0) {
		_cmd_exec($self, $cmd, @args);
	}
	if (waitpid($pid, 0) > 0 and $?>>8 != 0) {
		throw Git::Error::Command(join(' ', $cmd, @args), $? >> 8);
	}
}


=item version ()

Return the Git version in use.

Implementation of this function is very fast; no external command calls
are involved.

=cut

# Implemented in Git.xs.


=item exec_path ()

Return path to the git sub-command executables (the same as
C<git --exec-path>). Useful mostly only internally.

Implementation of this function is very fast; no external command calls
are involved.

=cut

# Implemented in Git.xs.


=item hash_object ( FILENAME [, TYPE ] )

=item hash_object ( FILEHANDLE [, TYPE ] )

Compute the SHA1 object id of the given C<FILENAME> (or data waiting in
C<FILEHANDLE>) considering it is of the C<TYPE> object type (C<blob>
(default), C<commit>, C<tree>).

In case of C<FILEHANDLE> passed instead of file name, all the data
available are read and hashed, and the filehandle is automatically
closed. The file handle should be freshly opened - if you have already
read anything from the file handle, the results are undefined (since
this function works directly with the file descriptor and internal
PerlIO buffering might have messed things up).

The method can be called without any instance or on a specified Git repository,
it makes zero difference.

The function returns the SHA1 hash.

Implementation of this function is very fast; no external command calls
are involved.

=cut

# Implemented in Git.xs.



=back

=head1 ERROR HANDLING

All functions are supposed to throw Perl exceptions in case of errors.
See the L<Error> module on how to catch those. Most exceptions are mere
L<Error::Simple> instances.

However, the C<command()>, C<command_oneline()> and C<command_noisy()>
functions suite can throw C<Git::Error::Command> exceptions as well: those are
thrown when the external command returns an error code and contain the error
code as well as access to the captured command's output. The exception class
provides the usual C<stringify> and C<value> (command's exit code) methods and
in addition also a C<cmd_output> method that returns either an array or a
string with the captured command output (depending on the original function
call context; C<command_noisy()> returns C<undef>) and $<cmdline> which
returns the command and its arguments (but without proper quoting).

Note that the C<command_pipe()> function cannot throw this exception since
it has no idea whether the command failed or not. You will only find out
at the time you C<close> the pipe; if you want to have that automated,
use C<command_close_pipe()>, which can throw the exception.

=cut

{
	package Git::Error::Command;

	@Git::Error::Command::ISA = qw(Error);

	sub new {
		my $self = shift;
		my $cmdline = '' . shift;
		my $value = 0 + shift;
		my $outputref = shift;
		my(@args) = ();

		local $Error::Depth = $Error::Depth + 1;

		push(@args, '-cmdline', $cmdline);
		push(@args, '-value', $value);
		push(@args, '-outputref', $outputref);

		$self->SUPER::new(-text => 'command returned error', @args);
	}

	sub stringify {
		my $self = shift;
		my $text = $self->SUPER::stringify;
		$self->cmdline() . ': ' . $text . ': ' . $self->value() . "\n";
	}

	sub cmdline {
		my $self = shift;
		$self->{'-cmdline'};
	}

	sub cmd_output {
		my $self = shift;
		my $ref = $self->{'-outputref'};
		defined $ref or undef;
		if (ref $ref eq 'ARRAY') {
			return @$ref;
		} else { # SCALAR
			return $$ref;
		}
	}
}

=over 4

=item git_cmd_try { CODE } ERRMSG

This magical statement will automatically catch any C<Git::Error::Command>
exceptions thrown by C<CODE> and make your program die with C<ERRMSG>
on its lips; the message will have %s substituted for the command line
and %d for the exit status. This statement is useful mostly for producing
more user-friendly error messages.

In case of no exception caught the statement returns C<CODE>'s return value.

Note that this is the only auto-exported function.

=cut

sub git_cmd_try(&$) {
	my ($code, $errmsg) = @_;
	my @result;
	my $err;
	my $array = wantarray;
	try {
		if ($array) {
			@result = &$code;
		} else {
			$result[0] = &$code;
		}
	} catch Git::Error::Command with {
		my $E = shift;
		$err = $errmsg;
		$err =~ s/\%s/$E->cmdline()/ge;
		$err =~ s/\%d/$E->value()/ge;
		# We can't croak here since Error.pm would mangle
		# that to Error::Simple.
	};
	$err and croak $err;
	return $array ? @result : $result[0];
}


=back

=head1 COPYRIGHT

Copyright 2006 by Petr Baudis E<lt>pasky@suse.czE<gt>.

This module is free software; it may be used, copied, modified
and distributed under the terms of the GNU General Public Licence,
either version 2, or (at your option) any later version.

=cut


# Take raw method argument list and return ($obj, @args) in case
# the method was called upon an instance and (undef, @args) if
# it was called directly.
sub _maybe_self {
	# This breaks inheritance. Oh well.
	ref $_[0] eq 'Git' ? @_ : (undef, @_);
}

# When already in the subprocess, set up the appropriate state
# for the given repository and execute the git command.
sub _cmd_exec {
	my ($self, @args) = @_;
	if ($self) {
		$self->{opts}->{Repository} and $ENV{'GIT_DIR'} = $self->{opts}->{Repository};
		$self->{opts}->{WorkingCopy} and chdir($self->{opts}->{WorkingCopy});
	}
	_execv_git_cmd(@args);
	die "exec failed: $!";
}

# Execute the given Git command ($_[0]) with arguments ($_[1..])
# by searching for it at proper places.
# _execv_git_cmd(), implemented in Git.xs.

# Close pipe to a subprocess.
sub _cmd_close {
	my ($fh, $ctx) = @_;
	if (not close $fh) {
		if ($!) {
			# It's just close, no point in fatalities
			carp "error closing pipe: $!";
		} elsif ($? >> 8) {
			# The caller should pepper this.
			throw Git::Error::Command($ctx, $? >> 8);
		}
		# else we might e.g. closed a live stream; the command
		# dying of SIGPIPE would drive us here.
	}
}


# Trickery for .xs routines: In order to avoid having some horrid
# C code trying to do stuff with undefs and hashes, we gate all
# xs calls through the following and in case we are being ran upon
# an instance call a C part of the gate which will set up the
# environment properly.
sub _call_gate {
	my $xsfunc = shift;
	my ($self, @args) = _maybe_self(@_);

	if (defined $self) {
		# XXX: We ignore the WorkingCopy! To properly support
		# that will require heavy changes in libgit.

		# XXX: And we ignore everything else as well. libgit
		# at least needs to be extended to let us specify
		# the $GIT_DIR instead of looking it up in environment.
		#xs_call_gate($self->{opts}->{Repository});
	}

	# Having to call throw from the C code is a sure path to insanity.
	local $SIG{__DIE__} = sub { throw Error::Simple("@_"); };
	&$xsfunc(@args);
}

sub AUTOLOAD {
	my $xsname;
	our $AUTOLOAD;
	($xsname = $AUTOLOAD) =~ s/.*:://;
	throw Error::Simple("&Git::$xsname not defined") if $xsname =~ /^xs_/;
	$xsname = 'xs_'.$xsname;
	_call_gate(\&$xsname, @_);
}

sub DESTROY { }


1; # Famous last words
