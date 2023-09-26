Name: vhost-user-scmi
Version: 1.0
Release: r1
Summary: vhost user scmi B/E
License: BSD-3-Clause-Clear
Group: vhost-user-scmi
Source0: %{name}-%{version}.tar.gz

BuildRequires: cmake libstd vhost-user-lib
Requires: libstd vhost-user-lib

%description
vhost-user-scmi B/E process, which will emulate a scmi platform, and receive scmi message from F/E, process the message and bypass to kernel space.

%prep
%setup -qn %{name}-%{version}

%build
%cmake

%cmake_build

%install
%cmake_install

%files
%{_bindir}/vhost-user-scmi
