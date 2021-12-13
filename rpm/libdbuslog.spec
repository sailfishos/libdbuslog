Name: dbuslog-tools
Version: 1.0.22
Release: 0
Summary: Command line client for libdbuslogserver
License: BSD
URL: https://github.com/sailfishos/libdbuslog
Source: %{name}-%{version}.tar.bz2

%define libglibutil_version 1.0.43

BuildRequires: pkgconfig
BuildRequires: pkgconfig(libglibutil) >= %{libglibutil_version}
BuildRequires: pkgconfig(libdbusaccess)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(gio-2.0)
BuildRequires: pkgconfig(gio-unix-2.0)

%description
Provides command line tool for setting up logging options and dumping
logs to the standard output.

%prep
%setup -q

%build
make %{_smp_mflags} KEEP_SYMBOLS=1 LIBDIR=%{_libdir} release pkgconfig
make %{_smp_mflags} KEEP_SYMBOLS=1 LIBDIR=%{_libdir} -C tools/dbuslog-client release

%check
make -C test test

%install
rm -rf %{buildroot}
make DESTDIR=%{buildroot} LIBDIR=%{_libdir} -C tools/dbuslog-client install
make DESTDIR=%{buildroot} LIBDIR=%{_libdir} -C server install-dev

%files
%defattr(-,root,root,-)
%{_bindir}/dbuslog-client

#############################################################################

%package -n libdbuslogserver-common-devel
Summary: Common development files

%description -n libdbuslogserver-common-devel
This package contains development files shared by libdbuslogserver-dbus-devel
and libdbuslogserver-gio-devel.

%files -n libdbuslogserver-common-devel
%defattr(-,root,root,-)
%{_includedir}/dbuslogserver/*.h

#############################################################################

%package -n libdbuslogserver-dbus
Summary: Library of logging utilities for libdbus based programs
BuildRequires: pkgconfig(dbus-1)
Requires: libglibutil >= %{libglibutil_version}
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description -n libdbuslogserver-dbus
Provides server side for inter-process logging functionality for libdbus
based programs.

%package -n libdbuslogserver-dbus-devel
Summary: Development library for libdbuslogserver-dbus
Requires: libdbuslogserver-common-devel = %{version}
Requires: libdbuslogserver-dbus = %{version}

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
%{_includedir}/dbuslogserver/dbus/*.h

#############################################################################

%package -n libdbuslogserver-gio
Summary: Library of logging utilities for gio based programs
Requires: libglibutil >= %{libglibutil_version}
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description -n libdbuslogserver-gio
Provides server side for inter-process logging functionality for gio
based programs.

%package -n libdbuslogserver-gio-devel
Summary: Development library for libdbuslogserver-gio
Requires: libdbuslogserver-common-devel = %{version}
Requires: libdbuslogserver-gio = %{version}

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
%{_includedir}/dbuslogserver/gio/*.h
