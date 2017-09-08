
version=1.0

src_url=https://github.com/lzfse/lzfse/archive/lzfse-1.0.tar.gz
src_url_sha1=40f156053e34e8725f052d2d1590b6abd318f899

pkg_install-include () {
    mkdir -p "$install_dir/include"
    cp "$src_dir/src/lzfse.h" "$install_dir/include"
}

pkg_install () {
    pkg_copy_src_to_build
    pkg_make install prefix="$install_dir" CXXFLAGS="${CXXFLAGS:-} -O3" LDFLAGS="${LDFLAGS:-} $PTHREAD_LIBS"

    mkdir -p "$install_dir/lib"
    cp "$build_dir/build/bin/liblzfse.a" "$install_dir/lib"
}