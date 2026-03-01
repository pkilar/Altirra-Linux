Name:           altirra
Version:        4.40
Release:        1%{?dist}
Summary:        Atari 8-bit computer emulator (800/XL/XE/5200)
License:        GPL-2.0-or-later
URL:            https://www.virtualdub.org/altirra.html
Source0:        https://github.com/pkilar/Altirra-Linux/archive/refs/tags/v%{version}.tar.gz#/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.20
BuildRequires:  gcc-c++ >= 13
BuildRequires:  ninja-build
BuildRequires:  mads
BuildRequires:  pkg-config
BuildRequires:  git
BuildRequires:  SDL3-devel
BuildRequires:  mesa-libGL-devel
BuildRequires:  zlib-devel
BuildRequires:  libxslt

Requires:       SDL3
Requires:       mesa-libGL
Requires:       zlib

Recommends:     ffmpeg-free

%description
Altirra is a highly accurate Atari 8-bit computer emulator supporting the
Atari 400/800, XL, XE, and 5200 systems. It features cycle-accurate emulation
of all major chips (ANTIC, GTIA, POKEY, PIA), multiple CPU cores (6502, 65C02,
65C816), built-in replacement firmware (AltirraOS), extensive peripheral device
support, and an integrated debugger with disassembler, profiler, and trace viewer.

%prep
%autosetup -n Altirra-Linux-%{version}

%build
%cmake -G Ninja
%cmake_build

%check
cd %{_vpath_builddir}/src
ATTest/attest all

%install
%cmake_install

%files
%license Copying
%doc README.md
%{_bindir}/altirra
%{_datadir}/applications/altirra.desktop
%{_datadir}/icons/hicolor/16x16/apps/altirra.png
%{_datadir}/icons/hicolor/32x32/apps/altirra.png
%{_datadir}/icons/hicolor/48x48/apps/altirra.png
%{_datadir}/pixmaps/altirra.png
%{_datadir}/metainfo/altirra.metainfo.xml
%dir %{_datadir}/altirra
%dir %{_datadir}/altirra/firmware
%dir %{_docdir}/altirra
%{_docdir}/altirra/LICENSE
%{_docdir}/altirra/README.md
%{_docdir}/altirra/html

%changelog
* Sat Mar 01 2026 pkilar <pkilar@users.noreply.github.com> - 4.40-1
- Initial Linux port release
