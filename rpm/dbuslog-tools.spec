Name: dbuslog-tools
Version: 1.0.1
Release: 0
Summary: Command line tools for libdbuslogserver
Group: Development/Tools
License: BSD
URL: https://git.merproject.org/mer-core/libdbuslog
Source: %{name}-%{version}.tar.bz2
BuildRequires: pkgconfig(libglibutil)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(gio-2.0)
BuildRequires: pkgconfig(gio-unix-2.0)

%description
Provides command line tool for setting up logging options and pulling
logs from the command line.

%prep
%setup -q

%build
make -C tools/dbuslog-client KEEP_SYMBOLS=1 release

%install
rm -rf %{buildroot}
install -d %{buildroot}/%{_bindir}
install -m 755 tools/dbuslog-client/build/release/dbuslog-client %{buildroot}/%{_bindir}

%files
%defattr(-,root,root,-)
%{_bindir}/dbuslog-client
