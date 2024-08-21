Name: vhost-user-scmi
Version: 1.0
Release: r1
Summary: vhost user scmi B/E
License: BSD-3-Clause-Clear
Group: vhost-user-scmi
Source0: %{name}-%{version}.tar.gz

BuildRequires: cmake g++ libstd vhost-user-lib safelinux-modules safelinux-modules-uapi-headers
Requires: libstd vhost-user-lib safelinux-modules-uapi-headers

%description
vhost-user-scmi B/E process, which will emulate a scmi platform, and receive scmi message from F/E, process the message and bypass to kernel space.

%prep
%setup -qn %{name}-%{version}

%build
%cmake

%cmake_build

%install
%cmake_install
mkdir -p %{buildroot}%{_unitdir}
mkdir -p %{buildroot}%{_unitdir}/multi-user.target.wants
install -DpZm 0644 vhost-user-scmi.service %{buildroot}%{_unitdir}
pushd %{buildroot}%{_unitdir} && %{__ln_s} -r vhost-user-scmi.service multi-user.target.wants/vhost-user-scmi.service && popd

%post
%systemd_post vhost-user-scmi.service

%preun
%systemd_preun vhost-user-scmi.service

%postun
%systemd_postun_with_restart vhost-user-scmi.service

%files
%{_bindir}/vhost-user-scmi
%{_unitdir}/vhost-user-scmi.service
%{_unitdir}/multi-user.target.wants/vhost-user-scmi.service
