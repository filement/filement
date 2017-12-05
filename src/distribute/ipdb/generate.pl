#!/usr/bin/perl

use strict;
use warnings;

use String::Util 'unquote';

use Text::CSV_XS;

use constant {
	LOC_INDEX => 0,
	LOC_COUNTRY => 1,
	LOC_LATITUDE => 5,
	LOC_LONGITUDE => 6,
	BLOCK_IP_LOW => 0,
	BLOCK_IP_HIGH => 1,
	BLOCK_INDEX => 2,
};

my $csv = Text::CSV_XS->new({binary => 1, eol => $/});

# IP database:
# http://dev.maxmind.com/geoip/legacy/geolite/

# Disable output buffering
$| = 1;

print "Getting locations...";

# Array with coordinates and country for each location
# Multiply coordinates by 10 000 in order to store them as integers
# [latitude * 10_000, longitude * 10_000, country_code]
my @locations;

# Hash that maps country index to each country code
# code => index
my %countries;

open LOCATION, '<location.csv';

# Skip the first two lines of the file
<LOCATION>;
<LOCATION>;

my $index = 0;
while (<LOCATION>)
{
	# Add current location
	$csv->parse($_);
	my @data = $csv->fields;
	#my @data = split(/,/);
	$locations[$data[LOC_INDEX]] = [$data[LOC_LATITUDE] * 10_000, $data[LOC_LONGITUDE] * 10_000, unquote($data[LOC_COUNTRY])];

	# Add current country
	/,"(\w\w)"/;
	$countries{$1} = $index++ if !exists($countries{$1});
}

close LOCATION;

print " done.\nGenerating ipdb.c ...";

my (@block, $location);

open DB, '>ipdb.c';

print DB qq|#include "types.h"\n#include "ipdb.h"\n\n|;

# Create countries array
my %codes = ();
$codes{$countries{$_}} = $_ for keys %countries;
print DB qq|const char *countries[] = {\n|;
print DB qq|\t[$_] = "$codes{$_}",\n| for (sort {$a <=> $b} keys %codes);
print DB qq|};\n\n|;

open BLOCKS, '<blocks.csv';

# Skip the first two lines of the file
<BLOCKS>;
<BLOCKS>;

print DB qq|const struct location locations[] = {\n|;
while (<BLOCKS>)
{
	chomp;
	@block = map {unquote $_} split(/,/);
	$location = $locations[$block[BLOCK_INDEX]];

	print DB qq|{.ip={$block[0],$block[1]},.coords={$location->[0],$location->[1]},.country=$countries{$location->[2]}},\n|;
}
print DB "};\n\nconst size_t locations_count = (sizeof(locations) / sizeof(*locations));\n";

print " done.\nCompiling database...";

close DB;

close BLOCKS;

system qw|gcc -c -O2 -std=c99 -I../../../include/ -I../../lib/ ipdb.c|;

print " done.\n";
