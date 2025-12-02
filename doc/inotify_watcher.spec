%global debug_package %{nil}

Name:           inotify_watcher
Version:        1.5
Release:        1%{?dist}
Summary:        Inotify watcher service

License:        MIT
URL:            https://github.com/AndreyBarmaley/inotify-watcher
Source0:        %{name}-%{version}.tar.gz
Patch0:         alma8_build.diff
# almalinux8 deps
Requires:       boost1.78-json, spdlog
%description
Inotify watcher service for Linux

%prep
%setup -n %{name}
%patch0 -p1

%build
mkdir -p build
pushd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j 2
popd

%install
install -d -m 0755 -p $RPM_BUILD_ROOT/usr/sbin
install -d -m 0755 -p $RPM_BUILD_ROOT/etc/systemd/system
install -d -m 0755 -p $RPM_BUILD_ROOT/etc/inotify_watcher
install -d -m 0755 -p $RPM_BUILD_ROOT/etc/inotify_watcher/jobs.d
install -d -m 0755 -p $RPM_BUILD_ROOT/etc/rsyslog.d

install -m755 build/inotify_watcher $RPM_BUILD_ROOT/usr/sbin
install -m644 etc/systemd/inotify_watcher.service $RPM_BUILD_ROOT/etc/systemd/system
install -m644 etc/rsyslog/inotify_watcher.conf $RPM_BUILD_ROOT/etc/rsyslog.d
install -m644 config.json $RPM_BUILD_ROOT/etc/inotify_watcher
install -m644 doc/inotify.txt $RPM_BUILD_ROOT/etc/inotify_watcher
sed -i -e 's|/usr/local/bin|/usr/sbin|' $RPM_BUILD_ROOT/etc/systemd/system/inotify_watcher.service
echo "This folder should contain files with the '.job' extension and contain one json::object inside." > $RPM_BUILD_ROOT/etc/inotify_watcher/jobs.d/README

%clean
%{__rm} -rf %{buildroot}

%post
if systemctl is-system-running > /dev/null; then
    # install
    if [ $1 -eq 1 ]; then
        systemctl daemon-reload
        systemctl is-active --quiet rsyslog && systemctl restart rsyslog
        exit 0
    fi
    # upgrade
    if [ $1 -eq 2 ]; then
        systemctl is-active --quiet inotify_watcher && systemctl restart inotify_watcher
        exit 0
    fi
fi

%preun
if systemctl is-system-running > /dev/null; then
    systemctl stop inotify_watcher
fi

%postun
if systemctl is-system-running > /dev/null; then
    systemctl daemon-reload
    systemctl is-active --quiet rsyslog && systemctl restart rsyslog
    exit 0
fi

%doc LICENSE README.md config.json doc/inotify.txt

%files
%defattr(-, root, root, 0755)
%{_sbindir}/inotify_watcher
%{_sysconfdir}/inotify_watcher/jobs.d/README
%{_sysconfdir}/inotify_watcher/inotify.txt
%config(noreplace) %{_sysconfdir}/systemd/system/inotify_watcher.service
%config(noreplace) %{_sysconfdir}/rsyslog.d/inotify_watcher.conf
%config(noreplace) %{_sysconfdir}/inotify_watcher/config.json
