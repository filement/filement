#!/bin/bash

# autostart a GUI application
# http://standards.freedesktop.org/autostart-spec/autostart-spec-latest.html

# deb file format
# http://en.wikipedia.org/wiki/Deb_(file_format)
# http://debian-handbook.info/browse/wheezy/packaging-system.html
# http://www.debian.org/doc/manuals/debian-faq/ch-pkg_basics.en.html

# add application to desktop environments
# https://developer.gnome.org/integration-guide/stable/desktop-files.html.en#ex-sample-desktop-file

# add application icon
# https://developer.gnome.org/integration-guide/stable/icons.html.en
# http://standards.freedesktop.org/menu-spec/latest/

# WARNING: Use 217.18.246.203 for deployment since it is old version and has rpm tools.

# http://stackoverflow.com/questions/8003739/is-there-a-way-to-automatically-determine-dependencies-when-setting-up-a-dpkg-co

# http://www.lamolabs.org/blog/164/centos-rpm-tutorial-1/
# http://www.rpm.org/max-rpm/s1-rpm-inside-tags.html
# http://fedoraproject.org/wiki/How_to_create_an_RPM_package#SPEC_file_overview
# http://zenit.senecac.on.ca/wiki/index.php/RPM_Packaging_Process#Setting_up_the_RPM_tree

version=`head -c -1 version`

arch=$(uname -m)
if [ "$arch" = 'i686' ]; then arch='x86'; fi

target="filement_${version}.${arch}"
prefix='/usr/local'

spec='SPECS/filement.spec'

export CC='gcc'
export SUFFIX='.so'

soname()
{
	echo $(objdump -p "$1"|grep SONAME|awk '{print $2}')
}

cwd=$(pwd)

echo 'Generate filement'
rm -rf bin/* &&
mkdir -p "bin/${target}${prefix}" &&
cd "bin/${target}${prefix}" &&
mkdir -p bin lib share/filement &&
cd "$cwd" &&
cp linux/Makefile linux/README "bin/$target" &&
cp "linux/ffmpeg.$arch" "bin/${target}${prefix}/bin/filement_ffmpeg" &&
name=$(soname "lib/libfilement${SUFFIX}") &&
cp -af "lib/$name" "bin/${target}${prefix}/lib/" &&
cp -Lf "lib/$name" "bin/${target}${prefix}/lib/$(readlink lib/$name)" &&
chmod a+rx "bin/${target}${prefix}/lib/$(readlink lib/$name)" &&
cp share/{ca.crt,fonts.conf} "bin/${target}${prefix}/share/filement/"

su -c "chroot --userspec=martin:users $HOME/centos filement/linux/core.sh" &&
$CC -O2 -std=c99 -DRUN_MODE=2 -DPREFIX=\"${prefix}\" -Iinclude/ -Isrc/ -Isrc/lib/ $(pkg-config --cflags gtk+-2.0) 'linux/main-gtk.c' -o "bin/${target}${prefix}/bin/filement-gtk" -Llib/ $(pkg-config --libs gtk+-2.0) -lfilement -Wl,-unresolved-symbols=ignore-in-shared-libs &&
chmod -R u=rwX,go=rX "bin/${target}${prefix}"

# TODO create filement.deb and filement.rpm

# Create deb package.
echo 'Create deb package.'
if [ "$arch" = 'x86_64' ]; then deb_arch='amd64'; else deb_arch='i386'; fi &&
deb_version="${version}-0" &&
cp linux/debian-binary "bin/${target}" &&
sed -e "s/@{VERSION}/$deb_version/" -e "s/@{ARCH}/$deb_arch/" < linux/control.in > "bin/${target}/control" &&
cd "bin/${target}" &&
tar -czf control.tar.gz --numeric-owner --owner 0 --group 0 ./control &&
tar -czf data.tar.gz --numeric-owner --owner 0 --group 0 ./usr &&
ar -cr "../filement_${deb_version}_${deb_arch}.deb" debian-binary control.tar.gz data.tar.gz &&
rm debian-binary control control.tar.gz data.tar.gz &&
cd "$cwd"

# TODO fix dependencies

# Create rpm package.
#echo 'Create rpm package.'
#cd linux/rpm
#sed -e "s|@{VERSION}|$version|" < "${spec}.in" > "$spec"
#rpmbuild -bb "$spec"
#cp "RPMS/x86_64/filement-${version}-1.x86_64.rpm" ../../bin/
cd "$cwd"

echo 'Generate filement-gtk'
mkdir -p "bin/${target}/usr/share" &&
cp -r linux/{applications,icons} "bin/${target}/usr/share/" &&
cp share/{background.png,logo.png} "bin/${target}${prefix}/share/filement/" &&
chmod -R u=rwX,g=rX,o=rX "bin/${target}${prefix}"

# TODO create filement-gtk.deb and filement-gtk.rpm

echo 'Create tar package'
cd bin &&
tar -czf "${target}.tar.gz" --numeric-owner --owner 0 --group 0 "$target" &&
mv "${target}" files &&
cd ..
