#!/usr/bin/perl

# Deploys a new version of Filement.

# location must contain failsafe and a directory named files
# cp filement.dmg /tmp && cd /tmp && 7z x filement.dmg && 7z x 4.hfsx && cd - && cp /tmp/Filement/Filement.app files
# requires: cpan install File::Copy::Recursive

use strict;
use warnings;

use feature ":5.10";

use DBI;
use Digest::SHA;
use File::Copy::Recursive;

use constant SQL_SERVER => 'DBI:mysql:filement_distribute;host=127.0.0.1';
use constant SQL_USERNAME => 'filement';
use constant SQL_PASSWORD => 'parola';

use constant USAGE => "./deploy.sh [platform_id <major|minor|revision|<version>> [location]]\n\tlocation\t\tDirectory that contains the deployment files.\n";

my $db = DBI->connect(SQL_SERVER, SQL_USERNAME, SQL_PASSWORD);
die "Unable to connect to database\n" if !$db;

my %versions;
my @latest = (0, 0, 0);

$" = '.';

my $format = "\t%8s\t%8s\t%16s\t%8s\t%16s\n";

# WARNING: Stupid workaround because of MySQL's lack of first() aggregate function.
my $db_query = $db->prepare('select N.platform_id,os,device,arch,format,version_major,version_minor,revision from versions inner join (select platform_id,max(version_major * 16777216 + version_minor * 4096 + revision) as version from versions group by platform_id) as T on versions.platform_id=T.platform_id and (version_major * 16777216 + version_minor * 4096 + revision)=version right outer join (select platform_id,device,arch,os,format from platforms) as N on versions.platform_id=N.platform_id');
$db_query->execute;
my @db_result = @{$db_query->fetchall_arrayref};
print sprintf("%3s$format", 'id', 'OS', 'device', 'arch', 'format', 'latest version');
foreach my $row (@db_result)
{
	my @version = (0, 0, 0);

	if (defined($row->[7]))
	{
		@version = @$row[5..7];
	}

	# Remember the latest version for each platform and the global latest version.
	@latest = @version if (($version[0] > $latest[0]) || (($version[0] == $latest[0]) && (($version[1] > $latest[1]) || (($version[1] == $latest[1]) && ($version[2] > $latest[2])))));
	$versions{$row->[0]} = [@$row[5, 6, 7], "@$row[1..4]"];

	print sprintf("%3d$format", $row->[0], $row->[1], $row->[2], $row->[3], $row->[4], defined($row->[5]) ? "$row->[5].$row->[6].$row->[7]" : '');
}
$db_query->finish;

exit if ($#ARGV < 0);
print "\n";
die USAGE if !$#ARGV;

my $id = shift @ARGV;
my $type = shift @ARGV;
my $location = shift @ARGV;
#my ($id, $version) = @ARGV;

my @info = @{$versions{$id}};
my @new = @info[0..2];
if ($type eq 'major')
{
	@new = ($new[0] + 1, 0, 0);
}
elsif ($type eq 'minor')
{
	@new = ($new[0], $new[1] + 1, 0);
}
elsif ($type eq 'revision')
{
	$new[2] += 1;
}
else
{
	@new = split /\./, $type;
}

my $hash = Digest::SHA->new('SHA-256');

sub checksum
{
	my ($filename, $prefix) = @_;
	my $sums = '';

	if (-f $filename)
	{
		my $flags = (-x $filename ? 'x' : ' ');
		$hash->addfile($filename);
		$sums .= $hash->hexdigest."\t$flags\t$prefix\n";
	}
	elsif (-d $filename)
	{
		opendir DIR, $filename;
		my @list = readdir(DIR);
		closedir DIR;

		for my $file (@list)
		{
			next if (($file eq '.') || ($file eq '..'));

			my $entry = "$filename/$file";
			$sums .= checksum($entry, ($prefix ? "$prefix/$file" : $file));
		}
	}

	return $sums;
}

if (defined $location)
{
	File::Copy::Recursive::dircopy($location, "/var/www/repository/$info[3]/@new");
	my $link = "/var/www/repository/$info[3]/latest";
	unlink $link if -e $link;
	symlink "@new", $link;

	# Store checksum of each file.
	open SUMS, ">$link/checksums";
	print SUMS checksum("$link/failsafe", '');
	print SUMS checksum("$link/files", '');
	close SUMS;

	$db_query = $db->prepare("insert into versions(platform_id,version_major,version_minor,revision) values($id,$new[0],$new[1],$new[2])");
	$db_query->execute();
	$db_query->finish;

	print "Version @new created\n";
}
else
{
	print "Next version will be @new\n";
}

$db->disconnect;
