#!/bin/sh

# http://www.lamolabs.org/blog/164/centos-rpm-tutorial-1/
# http://www.rpm.org/max-rpm/s1-rpm-inside-tags.html
# http://fedoraproject.org/wiki/How_to_create_an_RPM_package#SPEC_file_overview
# http://zenit.senecac.on.ca/wiki/index.php/RPM_Packaging_Process#Setting_up_the_RPM_tree

# http://wings.buffalo.edu/computing/ublinux/HOWTO-rpm.html

# TODO fix dependencies

version=`head -c -1 /filement/version`
spec='SPECS/filement.spec'

cd rpm
sed -e "s|@{VERSION}|$version|" -e "s|@{RPM}|$(pwd)/SOURCES/filement-$version|" < "${spec}.in" > "$spec"
rpmbuild -bb "$spec"
cp "RPMS/x86_64/filement-${version}-1.x86_64.rpm" ../../bin/
cd ..
