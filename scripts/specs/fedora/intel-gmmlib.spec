%global gmmlib_commit_id bda1dcf
%global major_version 19
%global minor_version 3
%global patch_version 2
%global package_release 1
%global api_major_version 11
%global api_minor_version 0
%global api_patch_version 702

Name:		intel-gmmlib
Version:    %{major_version}.%{minor_version}.%{patch_version}
Release:	%{package_release}%{?dist}
Summary:	Intel(R) Graphics Memory Management Library Package

Group:	    System Environment/Libraries
License:	MIT
URL:		https://github.com/intel/gmmlib
Source0:	https://github.com/intel/gmmlib/archive/%{gmmlib_commit_id}.tar.gz

%if 0%{?el7}
BuildRequires: centos-release-scl epel-release
BuildRequires: devtoolset-7-gcc-c++ cmake3
%else
BuildRequires: gcc-c++ cmake
%endif

BuildRequires: make

%description

%package       devel
Summary:       Intel(R) Graphics Memory Management Library development package
Requires:      %{name} = %{version}-%{release}

%description   devel

%prep

%build
echo "==== BUILD ===="
rm -rf *
mkdir gmmlib
tar xzf $RPM_SOURCE_DIR/%{gmmlib_commit_id}.tar.gz -C gmmlib --strip-components=1

mkdir build
cd build

%if 0%{?el7}
echo building for el7: %{el7}
scl enable devtoolset-7 "cmake3 -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TYPE=release \
 -DMAJOR_VERSION=%{major_version} -DMINOR_VERSION=%{minor_version} -DPATCH_VERSION=%{patch_version} \
 -DGMMLIB_API_PATCH_VERSION=%{api_patch_version} \
 -DRUN_TEST_SUITE:BOOL='ON' ../gmmlib"
scl enable devtoolset-7 "make -j`nproc` DESTDIR=$RPM_BUILD_DIR/build/install igfx_gmmumd_dll install"
%else
echo building for Fedora
cmake -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TYPE=release \
 -DMAJOR_VERSION=%{major_version} -DMINOR_VERSION=%{minor_version} -DPATCH_VERSION=%{patch_version} \
 -DGMMLIB_API_PATCH_VERSION=%{api_patch_version} \
 -DRUN_TEST_SUITE:BOOL='ON' ../gmmlib
make -j `nproc` DESTDIR=$RPM_BUILD_DIR/build/install igfx_gmmumd_dll install
%endif

%install
echo "==== INSTALL ===="
cp -ar $RPM_BUILD_DIR/build/install/usr $RPM_BUILD_ROOT
strip $RPM_BUILD_ROOT/usr/lib64/libigdgmm.so
echo "==== DONE ===="

%files
%defattr(-,root,root)
/usr/lib64/libigdgmm.so.%{api_major_version}
/usr/lib64/libigdgmm.so.%{api_major_version}.%{api_minor_version}.%{api_patch_version}

%files devel
%defattr(-,root,root)
/usr/include/igdgmm/*
/usr/lib64/libigdgmm.so
/usr/lib64/pkgconfig/igdgmm.pc
