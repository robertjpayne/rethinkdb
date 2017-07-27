FROM debian:jessie
ENV LC_ALL C.UTF-8
RUN apt-get update && apt-get install -y vim nano curl netcat less tcpdump

# Add the RethinkDB repository and public key

ENV RETHINKDB_PACKAGE_VERSION 2.3.6~0jessie

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
   wget \
   build-essential \
   python

RUN apt-get install -y --no-install-recommends \
    libssl-dev \
    libboost-all-dev \
    libv8-dev \
    libncurses5-dev \
    libre2-dev \
    libcurl4-openssl-dev \
    libjemalloc-dev \
    libprotobuf-dev \
    libprotoc-dev \
    zlib1g-dev \
    libidn11-dev \
    nettle-dev \
    libunwind-dev

COPY ./ /rethinkdb-src

RUN cd /rethinkdb-src \
 && ./configure --allow-fetch \
 && make -j8 \
 && cp build/release/rethinkdb /usr/local/bin


VOLUME ["/data"]

WORKDIR /data

CMD ["rethinkdb", "--bind", "all"]

#   process cluster webui
EXPOSE 28015 29015 8080