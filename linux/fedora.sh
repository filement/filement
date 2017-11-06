echo 'Summary: Filement server
Name: filement
Version: 0.12.1-0
Release: 0
Source0: filement.tar.gz
License: GPL
Group: MyJunk
BuildArch: noarch
BuildRoot: %{_tmppath}/%{name}-buildroot
%description
Server that lets you access your computer storage from the Filement infrastructure. See filement.com for details.
%prep
%setup -q
%build
%install
install -m 0755 files/bin/filement /usr/local/bin/filement
install -m 0755 files/bin/filement_ffmpeg /usr/local/bin/filement_ffmpeg
install -m 0755 files/bin/filement-gtk /usr/local/bin/filement-gtk
install -m 0755 files/lib/libfilement_coreutils.so.1 /usr/local/lib/libfilement_coreutils.so.1
install -m 0755 files/lib/libgnutls.so.28 /usr/local/lib/libgnutls.so.28
install -m 0755 files/lib/libfilement.so /usr/local/lib/libfilement.so
install -m 0755 -d /usr/local/share/filement
install -m 0755 files/share/filement/ca.crt /usr/local/share/filement/ca.crt
install -m 0755 files/share/filement/filement.png /usr/local/share/filement/filement.png
install -m 0755 files/share/filement/background.png /usr/local/share/filement/background.png
install -m 0755 files/share/filement/logo.png /usr/local/share/filement/logo.png
install -m 0755 files/share/filement/fonts.conf /usr/local/share/filement/fonts.conf
%clean
rm -rf $RPM_BUILD_ROOT
%post
echo " "
echo "This will display after rpm installs the package!"
%files
%dir /opt/linc
/opt/linc/myscript.sh'
