Name: dbuslog-tools
Version: 1.0.7
Release: 0
Summary: Command line client for libdbuslogserver
Group: Development/Tools
License: BSD
URL: https://git.merproject.org/mer-core/libdbuslog
Source: %{name}-%{version}.tar.bz2
Requires:   libglibutil >= 1.0.9
BuildRequires: pkgconfig(libglibutil)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(gio-2.0)
BuildRequires: pkgconfig(gio-unix-2.0)

%description
Provides command line tool for setting up logging options and dumping
logs to the standard output.

%prep
%setup -q

%build
make -C tools/dbuslog-client KEEP_SYMBOLS=1 release
make -C server KEEP_SYMBOLS=1 dbus-release dbus-pkgconfig
make -C server KEEP_SYMBOLS=1 gio-release gio-pkgconfig

%check
make -C test test

%install
rm -rf %{buildroot}
make -C server dbus-install-dev DESTDIR=%{buildroot}
make -C server gio-install-dev DESTDIR=%{buildroot}
install -d %{buildroot}/%{_bindir}
install -m 755 tools/dbuslog-client/build/release/dbuslog-client %{buildroot}/%{_bindir}

%files
%defattr(-,root,root,-)
%{_bindir}/dbuslog-client

#############################################################################

%package -n libdbuslogserver-dbus
Summary: Library of logging utilities for libdbus based programs
Group: Development/Libraries
BuildRequires: pkgconfig(dbus-1)
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description -n libdbuslogserver-dbus
Provides server side for inter-process logging functionality for libdbus
based programs.

%package -n libdbuslogserver-dbus-devel
Summary: Development library for libdbuslogserver-dbus
Requires: libdbuslogserver-dbus = %{version}
Requires: pkgconfig

%post -n libdbuslogserver-dbus -p /sbin/ldconfig

%postun -n libdbuslogserver-dbus -p /sbin/ldconfig

%description -n libdbuslogserver-dbus-devel
This package contains the development library for libdbuslogserver-dbus.

%files -n libdbuslogserver-dbus
%defattr(-,root,root,-)
%{_libdir}/libdbuslogserver-dbus.so.*

%files -n libdbuslogserver-dbus-devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/libdbuslogserver-dbus.pc
%{_libdir}/libdbuslogserver-dbus.so
%{_includedir}/dbuslogserver-dbus/*.h

#############################################################################

%package -n libdbuslogserver-gio
Summary: Library of logging utilities for gio based programs
Group: Development/Libraries
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description -n libdbuslogserver-gio
Provides server side for inter-process logging functionality for gio
based programs.

%package -n libdbuslogserver-gio-devel
Summary: Development library for libdbuslogserver-gio
Requires: libdbuslogserver-gio = %{version}
Requires: pkgconfig

%post -n libdbuslogserver-gio -p /sbin/ldconfig

%postun -n libdbuslogserver-gio -p /sbin/ldconfig

%description -n libdbuslogserver-gio-devel
This package contains the development library for libdbuslogserver-gio.

%files -n libdbuslogserver-gio
%defattr(-,root,root,-)
%{_libdir}/libdbuslogserver-gio.so.*

%files -n libdbuslogserver-gio-devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/libdbuslogserver-gio.pc
%{_libdir}/libdbuslogserver-gio.so
%{_includedir}/dbuslogserver-gio/*.h
