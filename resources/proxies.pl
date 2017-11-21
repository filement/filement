#!/usr/bin/perl

use strict;
use warnings;

my %servers;
my ($port_cloud, $port_proxy, $port_ftp);

if ($0 =~ /production/)
{
	%servers = (
		'sofia.proxy.filement.com' => [426833, 233167],
		'sofia1.proxy.filement.com' => [426834, 233167],
		'oregon.proxy.filement.com' => [327825, -968207],
	);
	$port_cloud = 4081;
	$port_ftp = 4083;
}
else
{
	%servers = (
		'belfastproxy.filement.com' => [545833, -59333],					# Belfast (real location: Dublin [533331, -62489])
		'frankfurtproxy.filement.com' => [501167, 86833],					# Frankfurt (real location: Amsterdam [52.3500,4.9167])
		'proxy.flmntdev.com' => [426833, 233167]							# Sofia
	);
	$port_cloud = 4071;
	$port_ftp = 4073;
}
$port_proxy = 80;

# How to deploy a proxy?
# 0. Copy files.
#    iptables
#    /usr/local/share/filement/ca.crt
#    /etc/filement/test.p12
#    /usr/lib/{libgnutls.so.28,libnettle.so.4,libhogweed.so.2,libgmp.so.10}
#    ../bin/{ftp,cloud}proxy
#    monitor.sh
#    ../src/proxy/proxy_production
# 1. Set permissions.
#    chmod a+x /usr/local/bin/{proxy_production,monitor.sh}
# 2. Edit /etc/rc.local
#    iptables-restore < /etc/iptables
#    nohup monitor.sh > /dev/null &
# 3. sed -e "s/@IP/$ip/g" < /etc/iptables > /tmp/iptables && mv /tmp/iptables /etc/iptables

my $ipv4_mapped = "\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\xff\\xff";

sub address_encode
{
	my @address_string = split(/\./, shift);
	my $string = $ipv4_mapped;
	$string .= sprintf("\\x%02x", $_) for @address_string;
	quotemeta $string;
}

# Get host IP address.
sub ip
{
	my $ip = (split /\n/, qx(host $_[0]))[0];
	chomp $ip;
	$ip =~ s/^.*?(\d+\.\d+\.\d+\.\d+).*$/$1/;
	$ip;
}

my $cloud_list = "\\n";
my $proxies_list = "\\n";
my $ftp_list = "\\n";
my $authorized = "\\n";

# Add the distribute servers as authorized.
#my $address = address_encode(ip('flmntdistribute.cloudapp.net'));
#my $length = length($address);
#$authorized .= qq|\\t{.address\\ =\\ "$address",\\ .hostname\\ =\\ {.data\\ =\\ \\"flmntdistribute.cloudapp.net\\",\\ .length\\ =\\ $length},\\ .rights\\ =\\ RIGHT_PROXY},\\n|;

my (@hosts, $host, $ip);
for $host (keys %servers)
{
	$ip = address_encode(ip($host));
	push @hosts, [$host, $ip];
}
@hosts = sort {$a->[1] cmp $b->[1]} @hosts;

for (@hosts)
{
	($host, $ip) = @$_;

	my $host_length = length($host);
	my $coords = "$servers{$host}->[0],\\ $servers{$host}->[1]";

	$cloud_list .= qq|\\t{.coords\\ =\\ {$coords},\\ .host\\ =\\ {.data\\ =\\ \\"$host\\",\\ .length\\ =\\ $host_length},\\ .port\\ =\\ \\"$port_cloud\\"},\\n|; 

	$proxies_list .= qq|\\t{.coordinates\\ =\\ {$coords},\\ .host\\ =\\ {.data\\ =\\ \\"$host\\",\\ .length\\ =\\ $host_length},\\ .port\\ =\\ $port_proxy},\\n|;

	$ftp_list .= qq|\\t{.coords\\ =\\ {$coords},\\ .host\\ =\\ {.data\\ =\\ \\"$host\\",\\ .length\\ =\\ $host_length},\\ .port\\ =\\ \\"$port_ftp\\"},\\n|; 

	$authorized .= qq|\\t{.address\\ =\\ "$ip",\\ .hostname\\ =\\ {.data\\ =\\ \\"$host\\",\\ .length\\ =\\ $host_length},\\ .port\\ =\\ $port_proxy,\\ .rights\\ =\\ RIGHT_PROXY},\\n|;

	#next if ($host eq 'flmntdev.com');

	#system('scp', 'monitor.sh', "$host:/usr/local/bin/");
	#system('scp', 'iptables', "$host:/etc/"); # WARNING: iptables is just a template file
}

$ipv4_mapped = quotemeta($ipv4_mapped);

# TODO: these commands work correctly only under linux because of \t and \n (probably only GNU sed supports them)
qx(sed -e 's/\@\{CLOUD_LIST\}/$cloud_list/g' < ../src/distribute/cloud.c.in > ../src/distribute/cloud.c);
qx(sed -e 's/\@\{PROXIES_LIST\}/$proxies_list/g' < ../src/distribute/proxies.c.in > ../src/distribute/proxies.c);
qx(sed -e 's/\@\{FTP_LIST\}/$ftp_list/g' < ../src/distribute/ftp.c.in > ../src/distribute/ftp.c);
qx(sed -e 's/\@\{AUTHORIZED\}/$authorized/g' -e 's/\@\{PREFIX_IPv4_MAPPED\}/$ipv4_mapped/g' < ../src/distribute/authorize.c.in > ../src/distribute/authorize.c);

#qx(sed -e s/\@IP/$ip/g < share/iptables.in > share/iptables);
#print "$host...\n";
