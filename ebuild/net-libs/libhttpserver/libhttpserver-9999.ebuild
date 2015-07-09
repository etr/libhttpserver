# Copyright 2015 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: $

EAPI="4"

inherit git-r3

FORK="vozhyk-" # use "etr" for original version

DESCRIPTION="C++ library for creating an embedded Rest HTTP server (and more)"
HOMEPAGE="http://github.com/${FORK}/${PN}"
#EGIT_REPO_URI="git://github.com/${FORK}/${PN}.git"
EGIT_REPO_URI="/home/vozhyk/dev/cpp/${PN}"

LICENSE="LGPL-2.1"
SLOT="0"
KEYWORDS="~amd64"
IUSE="debug"

RDEPEND=">=net-libs/libmicrohttpd-0.9.37"
DEPEND="${RDEPEND}"

src_configure() {
    ./bootstrap

    mkdir build
    cd build && ECONF_SOURCE=.. econf \
			    $(use_enable debug) \
			    --disable-doxygen-doc # no useflag for now
    # TODO configure checks for TLS
}

src_compile() {
    cd build && emake
}

src_install() {
    cd build && emake DESTDIR="${D}" install
}
