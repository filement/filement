#!/usr/bin/perl

use strict;
use warnings;

die "Actions not specified" if $#ARGV;

open SORTED, '>actions_sorted.h';
print SORTED "#define ACTIONS ";

my $actions = "actions.out";
if (-f $actions)
{
	open RAW, "<$actions";

	my @actions = ();
	while (<RAW>)
	{
		next if m|^\s*#|; # Skip comments
		s/^\s*(\S*)\s*$/$1/; # Trim whitespaces
		next if !$_;
		push @actions, $_;
	}
	@actions = sort(@actions);

	# Generate actions array
	for (@actions)
	{
		my $length = length;
		my $handler = $_;
		$handler =~ s/\./_/g;
		print SORTED qq|\\\n\t{.name = {.data = "$_", .length = $length}, .handler = &$handler},|;
	}
	print SORTED "\n";

	# Generate function declarations
	for (@actions)
	{
		my $handler = $_;
		$handler =~ s/\./_/g;
		print SORTED "int $handler(const struct http_request *request, struct http_response *restrict response, struct resources *restrict resources, const union json *options);\n"
	}

	close RAW;
}

print SORTED "\n";
close SORTED;
