#!@PERL_PATH@

# Copyright (c) 2000, 2018, Oracle and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is also distributed with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have included with MySQL.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

# mysqldumpslow - parse and summarize the MySQL slow query log

use strict;
use Getopt::Long;

# t=time, l=lock time, r=rows
# rw_t=Req_wait_time, pd_t=Pre_dispatch_time, p_t=Parse_time, e_t=Execute_time
# ep_t=Exec_prep_time, ot_t=Open_table_time, mr_t=Mdl_req_time, ir_t=Innodblock_req_time
# oc_t=Order_commit_time, f_t=Flush_time, s_t=Sync_time, c_t=Commit_time, aw_t=Ack_wait_time cqw_t=Commit_queue_wait_time
# at, al, and ar are the corresponding averages
# arw_t, apd_t, and p_t are the corresponding averages

my %opt = (
    s => 'at',
    h => '*',
);

GetOptions(\%opt,
    'v|verbose+',# verbose
    'help+',	# write usage info
    'd|debug+',	# debug
    's=s',	# what to sort by (al, at, ar, c, t, l, r)
    'r!',	# reverse the sort order (largest last instead of first)
    't=i',	# just show the top n queries
    'a!',	# don't abstract all numbers to N and strings to 'S'
    'n=i',	# abstract numbers with at least n digits within names
    'g=s',	# grep: only consider stmts that include this string
    'h=s',	# hostname of db server for *-slow.log filename (can be wildcard)
    'i=s',	# name of server instance (if using mysql.server startup script)
    'l!',	# don't subtract lock time from total time
) or usage("bad option");

$opt{'help'} and usage();

unless (@ARGV) {
    my $defaults   = `my_print_defaults mysqld`;
    my $basedir = ($defaults =~ m/--basedir=(.*)/)[0]
	or die "Can't determine basedir from 'my_print_defaults mysqld' output: $defaults";
    warn "basedir=$basedir\n" if $opt{v};

    my $datadir = ($defaults =~ m/--datadir=(.*)/)[0];
    my $slowlog = ($defaults =~ m/--slow-query-log-file=(.*)/)[0];
    if (!$datadir or $opt{i}) {
	# determine the datadir from the instances section of /etc/my.cnf, if any
	my $instances  = `my_print_defaults instances`;
	die "Can't determine datadir from 'my_print_defaults mysqld' output: $defaults"
	    unless $instances;
	my @instances = ($instances =~ m/^--(\w+)-/mg);
	die "No -i 'instance_name' specified to select among known instances: @instances.\n"
	    unless $opt{i};
	die "Instance '$opt{i}' is unknown (known instances: @instances)\n"
	    unless grep { $_ eq $opt{i} } @instances;
	$datadir = ($instances =~ m/--$opt{i}-datadir=(.*)/)[0]
	    or die "Can't determine --$opt{i}-datadir from 'my_print_defaults instances' output: $instances";
	warn "datadir=$datadir\n" if $opt{v};
    }

    if ( -f $slowlog ) {
        @ARGV = ($slowlog);
        die "Can't find '$slowlog'\n" unless @ARGV;
    } else {
        @ARGV = <$datadir/$opt{h}-slow.log>;
        die "Can't find '$datadir/$opt{h}-slow.log'\n" unless @ARGV;
    }
}

warn "\nReading mysql slow query log from @ARGV\n";

my @pending;
my %stmt;
$/ = ";\n#";		# read entire statements using paragraph mode
while ( defined($_ = shift @pending) or defined($_ = <>) ) {
    warn "[[$_]]\n" if $opt{d};	# show raw paragraph being read

    my @chunks = split /^\/.*Version.*started with[\000-\377]*?Time.*Id.*Command.*Argument.*\n/m;
    if (@chunks > 1) {
	unshift @pending, map { length($_) ? $_ : () } @chunks;
	warn "<<".join(">>\n<<",@chunks).">>" if $opt{d};
	next;
    }

    s/^#? Time: \d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d+(Z|[+-]\d{2}:\d{2}).*\n//;
    my ($user,$host,$dummy,$thread_id) = s/^#? User\@Host:\s+(\S+)\s+\@\s+(\S+)\s+\S+(\s+Id:\s+(\d+))?.*\n// ? ($1,$2,$3,$4) : ('','','','','');

    s/^# Query_time: ([0-9.]+)\s+Lock_time: ([0-9.]+)\s+Rows_sent: ([0-9.]+).*\n//;
    my ($t, $l, $r) = ($1, $2, $3);
    $t -= $l unless $opt{l};
       
    s/^# Req_wait_time: ([0-9.]+)\s+Pre_dispatch_time: ([0-9.]+)\s+Parse_time: ([0-9.]+)\s+Execute_time: ([0-9.]+).*\n//;
    my ($rw_t, $pd_t, $p_t, $e_t) = ($1, $2, $3, $4);

    s/^# Exec_prep_time: ([0-9.]+)\s+Open_table_time: ([0-9.]+)\s+Mdl_req_time: ([0-9.]+)\s+Innodblock_req_time: ([0-9.]+).*\n//; 
    my ($ep_t, $ot_t, $mr_t, $ir_t) = ($1, $2, $3, $4);

    s/^# Order_commit_time: ([0-9.]+)\s+Flush_time: ([0-9.]+)\s+Sync_time: ([0-9.]+)\s+Commit_time: ([0-9.]+)\s+Ack_wait_time: ([0-9.]+)\s+Commit_queue_wait_time: ([0-9.]+).*\n//;
    my ($oc_t, $f_t, $s_t, $c_t, $aw_t, $cqw_t) = ($1, $2, $3, $4, $5, $6);

    # remove fluff that mysqld writes to log when it (re)starts:
    s!^/.*Version.*started with:.*\n!!mg;
    s!^Tcp port: \d+  Unix socket: \S+\n!!mg;
    s!^Time.*Id.*Command.*Argument.*\n!!mg;

    s/^use \w+;\n//;	# not consistently added
    s/^SET timestamp=\d+;\n//;

    s/^[ 	]*\n//mg;	# delete blank lines
    s/^[ 	]*/  /mg;	# normalize leading whitespace
    s/\s*;\s*(#\s*)?$//;	# remove trailing semicolon(+newline-hash)

    next if $opt{g} and !m/$opt{g}/io;

    unless ($opt{a}) {
	s/\b\d+\b/N/g;
	s/\b0x[0-9A-Fa-f]+\b/N/g;
        s/''/'S'/g;
        s/""/"S"/g;
        s/(\\')//g;
        s/(\\")//g;
        s/'[^']+'/'S'/g;
        s/"[^"]+"/"S"/g;
	# -n=8: turn log_20001231 into log_NNNNNNNN
	s/([a-z_]+)(\d{$opt{n},})/$1.('N' x length($2))/ieg if $opt{n};
	# abbreviate massive "in (...)" statements and similar
	s!(([NS],){100,})!sprintf("$2,{repeated %d times}",length($1)/2)!eg;
    }

    my $s = $stmt{$_} ||= { users=>{}, hosts=>{} };
    $s->{c} += 1;
    $s->{t} += $t;
    $s->{l} += $l;
    $s->{r} += $r;
    $s->{rw_t} += $rw_t;
    $s->{pd_t} += $pd_t;
    $s->{p_t} += $p_t;
    $s->{e_t} += $e_t;
    $s->{ep_t} += $ep_t;
    $s->{ot_t} += $ot_t;
    $s->{mr_t} += $mr_t;
    $s->{ir_t} += $ir_t;
    $s->{oc_t} += $oc_t;
    $s->{f_t} += $f_t;
    $s->{s_t} += $s_t;
    $s->{c_t} += $c_t;
    $s->{aw_t} += $aw_t;
    $s->{cqw_t} += $cqw_t;
    $s->{users}->{$user}++ if $user;
    $s->{hosts}->{$host}++ if $host;

    warn "{{$_}}\n\n" if $opt{d};	# show processed statement string
}

foreach (keys %stmt) {
    my $v = $stmt{$_} || die;
    my ($c, $t, $l, $r, $rw_t, $pd_t, $p_t, $e_t) = @{ $v }{qw(c t l r rw_t pd_t p_t e_t)};
    my ($ep_t, $ot_t, $mr_t, $ir_t) = @{ $v }{qw(ep_t ot_t mr_t ir_t)};
    my ($oc_t, $f_t, $s_t, $c_t, $aw_t, $cqw_t) = @{ $v }{qw(oc_t f_t s_t c_t aw_t cqw_t)};
    $v->{at} = $t / $c;
    $v->{al} = $l / $c;
    $v->{ar} = $r / $c;
    $v->{arw_t} = $rw_t / $c;
    $v->{apd_t} = $pd_t / $c;
    $v->{ap_t} = $p_t / $c;
    $v->{ae_t} = $e_t / $c;
    $v->{aep_t} = $ep_t / $c;
    $v->{aot_t} = $ot_t / $c;
    $v->{amr_t} = $mr_t / $c;
    $v->{air_t} = $ir_t / $c;
    $v->{aoc_t} = $oc_t / $c;
    $v->{af_t} = $f_t / $c;
    $v->{as_t} = $s_t / $c;
    $v->{ac_t} = $c_t / $c;
    $v->{aaw_t} = $aw_t / $c;
    $v->{acqw_t} = $cqw_t / $c;
}

my @sorted = sort { $stmt{$b}->{$opt{s}} <=> $stmt{$a}->{$opt{s}} or $b cmp $a } keys %stmt;
@sorted = @sorted[0 .. $opt{t}-1] if $opt{t};
@sorted = reverse @sorted         if $opt{r};

foreach (@sorted) {
    my $v = $stmt{$_} || die;
    my ($c, $t,$at, $l,$al, $r,$ar) = @{ $v }{qw(c t at l al r ar)};
    my ($rw_t, $arw_t,$pd_t,$apd_t,$p_t,$ap_t,$e_t,$ae_t) = @{ $v }{qw(rw_t arw_t pd_t apd_t p_t ap_t e_t ae_t)};
    my ($ep_t, $aep_t,$ot_t,$aot_t,$mr_t,$amr_t,$ir_t,$air_t) = @{ $v }{qw(ep_t aep_t ot_t aot_t mr_t amr_t ir_t air_t)};
    my ($oc_t, $aoc_t,$f_t,$af_t,$s_t,$as_t,$c_t,$ac_t,$aw_t,$aaw_t,$cqw_t,$acqw_t) = @{ $v }{qw(oc_t aoc_t f_t af_t s_t as_t c_t ac_t aw_t aaw_t cqw_t acqw_t)};
    my @users = keys %{$v->{users}};
    my $user  = (@users==1) ? $users[0] : sprintf "%dusers",scalar @users;
    my @hosts = keys %{$v->{hosts}};
    my $host  = (@hosts==1) ? $hosts[0] : sprintf "%dhosts",scalar @hosts;
    printf "Count: %d  Time=%.2fs (%ds)  Lock=%.2fs (%ds)  Rows=%.1f (%d), $user\@$host \n",
        $c, $at,$t, $al,$l, $ar,$r;
    printf "Req_wait_time=%.2fs (%ds)  Pre_dispatch_time=%.2fs (%ds) Parse_time=%.2fs (%ds) Execute_time=%.2fs (%ds) \n",
        $arw_t,$rw_t,$apd_t,$pd_t,$ap_t,$p_t,$ae_t,$e_t;
    printf "Exec_prep_time=%.2fs (%ds)  Open_table_time=%.2fs (%ds)  Mdl_req_time=%.2fs (%ds) Innodblock_req_time=%.2fs (%ds)\n",
        $aep_t,$ep_t,$aot_t,$ot_t,$amr_t,$mr_t,$air_t,$ir_t;
    printf "Order_commit_time=%.2fs (%ds)  Flush_time=%.2fs (%ds) Sync_time=%.2fs (%ds) Commit_time=%.2fs (%ds) Ack_wait_time=%.2fs (%ds) Commit_queue_wait_time=%.2fs (%ds)\n",
        $aoc_t,$oc_t,$af_t,$f_t,$as_t,$s_t,$ac_t,$c_t,$aaw_t,$aw_t,$acqw_t,$cqw_t;
    printf "%s\n\n", $_;
}

sub usage {
    my $str= shift;
    my $text= <<HERE;
Usage: mysqldumpslow [ OPTS... ] [ LOGS... ]

Parse and summarize the MySQL slow query log. Options are

  --verbose    verbose
  --debug      debug
  --help       write this text to standard output

  -v           verbose
  -d           debug
  -s ORDER     what to sort by (al, at, ar, c, l, r, t), 'at' is default
                al: average lock time
                ar: average rows sent
                at: average query time
                 c: count
                 l: lock time
                 r: rows sent
                 t: query time  
  -r           reverse the sort order (largest last instead of first)
  -t NUM       just show the top n queries
  -a           don't abstract all numbers to N and strings to 'S'
  -n NUM       abstract numbers with at least n digits within names
  -g PATTERN   grep: only consider stmts that include this string
  -h HOSTNAME  hostname of db server for *-slow.log filename (can be wildcard),
               default is '*', i.e. match all
  -i NAME      name of server instance (if using mysql.server startup script)
  -l           don't subtract lock time from total time

HERE
    if ($str) {
      print STDERR "ERROR: $str\n\n";
      print STDERR $text;
      exit 1;
    } else {
      print $text;
      exit 0;
    }
}
