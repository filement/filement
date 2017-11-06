#!/usr/bin/perl

use strict;
use warnings;

my %clouds = (
	'f1b856649987e83070e58fd6a41eea54' => 'storage.filement.com',
	'fab77d1793fecdf6ca571912462c3708' => 'bg.filement.com',
	'76b5f259aa844553da7013fb2b7cb6d4' => 'bg2.filement.com',
	'a977f0155b4b33d54adc81e795e6e890' => 'hosting2.filement.com',
	'b06ff5e52ef7a23cb0d8420ba08b86f2' => 'hosting1.webconnect.bg',
	'51b29e6cb104052cd00ed2612924f7b4' => 'hosting.webconnect.es',
);

for my $uuid (keys %clouds)
{
	system './configure', '--debug', "--uuid=$uuid";
	system 'make', 'clean', 'pubcloud';
	system 'ssh', $clouds{$uuid}, 'rm', '/root/pubcloud_old';
	system 'ssh', $clouds{$uuid}, 'mv', '/root/pubcloud_debug', '/root/pubcloud_old';
	system 'scp', 'bin/pubcloud', "$clouds{$uuid}:/root/pubcloud_debug";
	system 'ssh', $clouds{$uuid}, 'killall', '-9', 'pubcloud_debug';
	system 'ssh', $clouds{$uuid}, 'killall', '-9', 'pubcloud';
	system 'ssh', $clouds{$uuid}, 'screen', '-d', '-m', '-S', 'pubcloud', 'gdb', '-ex', 'run', '/root/pubcloud_debug';
}
