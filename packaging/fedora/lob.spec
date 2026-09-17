# Fedora / COPR package for Lob.
#
# Fedora rather than any other non-Arch target because it is the one that
# actually carries the dependencies: Lob needs KDE Frameworks 6.19+ and
# layer-shell-qt 6.6+, and most stable distributions are well behind both.
# Fedora 42 and later have them.

Name:           lob
Version:        0.2
Release:        1%{?dist}
Summary:        Choose which browser opens each link

License:        MIT
URL:            https://github.com/puco/lob
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  gettext
BuildRequires:  extra-cmake-modules
# Defines %{_userunitdir}. Without it the macro survives into %files unexpanded
# and rpm rejects the path for not starting with "/".
BuildRequires:  systemd-rpm-macros
# tests/CMakeLists.txt requires dbus-run-session at configure time, so this is
# a build dependency even though nothing links against D-Bus tooling.
BuildRequires:  dbus-daemon

BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6QuickControls2)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6Network)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Test)

BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6I18n)
BuildRequires:  cmake(KF6Config)
BuildRequires:  cmake(KF6Service)
BuildRequires:  cmake(KF6KIO)
BuildRequires:  cmake(KF6DBusAddons)
BuildRequires:  cmake(KF6WindowSystem)
BuildRequires:  cmake(KF6IconThemes)
BuildRequires:  cmake(KF6ColorScheme)
BuildRequires:  cmake(KF6Crash)
BuildRequires:  cmake(KF6Notifications)
BuildRequires:  cmake(KF6StatusNotifierItem)
BuildRequires:  cmake(LayerShellQt)

# Needed to run the tests, not to compile: %check starts the real binary, which
# loads the picker as QML and sets the org.kde.desktop style. Without these the
# component fails to load, initialize() returns false, and every test that
# starts the application fails -- while the build itself succeeds, which is the
# confusing way round to find out.
BuildRequires:  kf6-kirigami
BuildRequires:  kf6-qqc2-desktop-style

# The picker is QML and the desktop style is resolved at runtime, so neither is
# visible to the automatic dependency generator.
Requires:       kf6-kirigami
Requires:       kf6-qqc2-desktop-style
Requires:       qt6-qtdeclarative

# Wayland only in any meaningful sense: the overlay is a layer-shell surface.
# It runs on X11 without the exclusive keyboard grab, which is why this is a
# recommendation of the session rather than a hard requirement.
Recommends:     plasma-workspace

%description
Lob registers as the system handler for http and https links and decides where
each one should go, instead of sending everything to a single default browser.

Links that match a rule open straight away, after a brief bar that can be
interrupted to choose something else. Anything unrecognised shows a
keyboard-driven picker listing installed browsers and their profiles, with an
option to remember the choice for that host.

Lob handles http and https only. It does not claim mail links, local files or
PDFs, and refuses URLs carrying embedded credentials.

%prep
%autosetup

%build
%cmake -GNinja -DBUILD_TESTING=ON
%cmake_build

%install
%cmake_install
# No %find_lang, because no translation ships yet: ki18n_install installs
# nothing, %find_lang writes an empty list, and rpm refuses an empty -f file.
# The first po/<lang>/lob.po to land needs `%find_lang %{name}` here and
# `-f %{name}.lang` on %files below. po/README.md says so too, since that is
# where whoever adds one will be looking.

%check
# The GUI tests need a compositor, which no build root has. Everything else
# runs headless; tests/run-under-compositor.sh covers the rest in CI.
%ctest

%files
%license LICENSE
%doc README.md
%{_bindir}/lob
%{_datadir}/applications/io.github.puco.lob.desktop
%{_datadir}/dbus-1/services/io.github.puco.lob.service
%{_datadir}/icons/hicolor/scalable/apps/io.github.puco.lob.svg
%{_metainfodir}/io.github.puco.lob.metainfo.xml
%{_userunitdir}/lob.service

%changelog
* Thu Sep 17 2026 puco <puco@users.noreply.github.com> - 0.2-1
- Initial Fedora package
