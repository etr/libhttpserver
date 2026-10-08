# syntax=docker/dockerfile:1
FROM martenseemann/quic-network-simulator-endpoint@sha256:3b2b9e6fa317da238c8140a7b6d2bf8d4d45a8464b873eecac3ff2a32520b71a AS build
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends clang autoconf automake libtool pkg-config make perl libmicrohttpd-dev libcurl4-openssl-dev
COPY build/tooling/openssl-3.5.9.tar.gz /tmp/provider.tar.gz
RUN echo '603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a  /tmp/provider.tar.gz' | sha256sum -c - && tar -xf /tmp/provider.tar.gz -C /tmp
RUN cd /tmp/openssl-3.5.9 && CC=clang ./Configure --prefix=/opt/provider --libdir=lib no-tests no-shared && make -j4 && make install_sw
COPY . /source
WORKDIR /source
RUN bash bootstrap && mkdir -p build/container && cd build/container && ../../configure --enable-v3-tls --disable-examples CC=clang CXX=clang++ CXXFLAGS='-O1 -g' V3_TLS_CFLAGS=-I/opt/provider/include V3_TLS_LIBS='-L/opt/provider/lib -lssl -lcrypto' && make -C src -j4 libhttpserver_v3core.la && make -C test -j4 quic_interop_endpoint
RUN sha256sum build/container/test/quic_interop_endpoint /opt/provider/lib/libssl.a /opt/provider/lib/libcrypto.a > /provenance.sha256
FROM martenseemann/quic-network-simulator-endpoint@sha256:3b2b9e6fa317da238c8140a7b6d2bf8d4d45a8464b873eecac3ff2a32520b71a
COPY --from=build /source/build/container/test/quic_interop_endpoint /quic_interop_endpoint
COPY --from=build /provenance.sha256 /provenance.sha256
COPY test/interop/quic/run_endpoint.sh /run_endpoint.sh
ENV LIBHTTPSERVER_INTEROP_TEST_MODE=1
ENTRYPOINT ["/run_endpoint.sh"]
