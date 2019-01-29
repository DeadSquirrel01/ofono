Name:       ofono
Summary:    Open Source Telephony
Version:    1.21
Release:    1
Group:      Communications/Connectivity Adaptation
License:    GPLv2
URL:        https://git.merproject.org/mer-core/ofono
Source:     %{name}-%{version}.tar.bz2
Requires:   dbus
Requires:   systemd
Requires:   ofono-configs
Requires:   libgrilio >= 1.0.25
Requires:   libglibutil >= 1.0.30
Requires:   mobile-broadband-provider-info
Requires(preun): systemd
Requires(post): systemd
Requires(postun): systemd
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(dbus-glib-1)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(libudev) >= 145
BuildRequires:  pkgconfig(libwspcodec) >= 2.0
BuildRequires:  pkgconfig(libgrilio) >= 1.0.25
BuildRequires:  pkgconfig(libglibutil) >= 1.0.30
BuildRequires:  pkgconfig(libdbuslogserver-dbus)
BuildRequires:  pkgconfig(libmce-glib) >= 1.0.5
BuildRequires:  pkgconfig(mobile-broadband-provider-info)
BuildRequires:  libtool
BuildRequires:  automake
BuildRequires:  autoconf

%description
Telephony stack

%package devel
Summary:    Headers for oFono
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
Development headers and libraries for oFono

%package tests
Summary:    Test Scripts for oFono
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}
Requires:   dbus-python3
Requires:   python3-gobject
Provides:   ofono-test >= 1.0
Obsoletes:  ofono-test < 1.0

%description tests
Scripts for testing oFono and its functionality

%package configs-mer
Summary:    Package to provide default configs for ofono
Group:      Development/Tools
Provides:   ofono-configs

%description configs-mer
This package provides default configs for ofono

%package doc
Summary:   Documentation for %{name}
Group:     Documentation
Requires:  %{name} = %{version}-%{release}

%description doc
Man pages for %{name}.

%prep
%setup -q -n %{name}-%{version}/%{name}

./bootstrap

%build
autoreconf --force --install

%configure --disable-static \
    --enable-test \
    --enable-sailfish-bt \
    --enable-sailfish-debuglog \
    --enable-sailfish-manager \
    --enable-sailfish-provision \
    --enable-sailfish-pushforwarder \
    --enable-sailfish-rilmodem \
    --disable-add-remove-context \
    --disable-isimodem \
    --disable-qmimodem \
    --with-systemdunitdir="/%{_lib}/systemd/system"

make %{_smp_mflags}


%check
# run unit tests
make check

%install
rm -rf %{buildroot}
%make_install

mkdir -p %{buildroot}/%{_sysconfdir}/ofono/push_forwarder.d
mkdir -p %{buildroot}/%{_lib}/systemd/system/network.target.wants
mkdir -p %{buildroot}/var/lib/ofono
ln -s ../ofono.service %{buildroot}/%{_lib}/systemd/system/network.target.wants/ofono.service

mkdir -p %{buildroot}%{_docdir}/%{name}-%{version}
install -m0644 -t %{buildroot}%{_docdir}/%{name}-%{version} \
        ChangeLog AUTHORS README

%preun
if [ "$1" -eq 0 ]; then
systemctl stop ofono.service ||:
fi

%post
systemctl daemon-reload ||:
# Do not restart during update
# We don't want to break anything during update
# New daemon is taken in use after reboot
# systemctl reload-or-try-restart ofono.service ||:

%postun
systemctl daemon-reload ||:

%files
%defattr(-,root,root,-)
%license COPYING
%config %{_sysconfdir}/dbus-1/system.d/*.conf
%{_sbindir}/*
/%{_lib}/systemd/system/network.target.wants/ofono.service
/%{_lib}/systemd/system/ofono.service
%dir %{_sysconfdir}/ofono/
%dir %{_sysconfdir}/ofono/push_forwarder.d
# This file is part of phonesim and not needed with ofono.
%exclude %{_sysconfdir}/ofono/phonesim.conf
%dir %attr(775,radio,radio) /var/lib/ofono

%files devel
%defattr(-,root,root,-)
%{_includedir}/ofono/
%{_libdir}/pkgconfig/ofono.pc

%files tests
%defattr(-,root,root,-)
%{_libdir}/%{name}/test/*

%files configs-mer
%defattr(-,root,root,-)
%config /etc/ofono/ril_subscription.conf

%files doc
%defattr(-,root,root,-)
%{_mandir}/man8/%{name}d.*
%{_docdir}/%{name}-%{version}
