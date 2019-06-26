FROM ferreol/yorick

MAINTAINER Ferréol Soulez <ferreol.soulez@univ-lyon1.fr>

RUN echo http://nl.alpinelinux.org/alpine/edge/testing >> /etc/apk/repositories && \
    apk add --update --no-cache --virtual .build-deps make wget build-base zip && \
    apk add --update  --no-cache  tiff-dev fftw-dev gsl-dev && \
    mkdir -p /opt && cd /opt && \
    wget  http://heasarc.gsfc.nasa.gov/FTP/software/fitsio/c/cfitsio_latest.tar.gz &&\
    tar -xvzf cfitsio_latest.tar.gz && rm cfitsio_latest.tar.gz && mv cfitsio* cfitsio && cd cfitsio && \
    ./configure --prefix=/usr/local && make && make install && \
    cd .. && rm -rf cfitsio && \
    wget https://github.com/emmt/XFFT/archive/master.zip && \
    unzip master.zip && rm master.zip && cd XFFT-master/ &&\
    ./configure --with-fftw2=no --with-fftw2-threads=no && make && make install &&\
    cd .. && rm -rf XFFT-master &&\
    wget https://github.com/emmt/ylib/archive/master.zip && \
    unzip master.zip && rm master.zip && cd ylib-master/ && \
    ./configure && make install &&  cd /opt && rm -rf  ylib-master && \
    wget https://github.com/emmt/Yeti/archive/master.zip && \
    unzip master.zip && rm master.zip && cd Yeti-master &&\
    ./configure --with-fftw=no  --with-regex=yes --with-tiff=yes && \
    make all && make install && cd /opt && rm -rf Yeti-master &&\
    cd /opt/yorick/i-start && ln -s ../i0/yeti.i yeti.i && \
    wget https://github.com/emmt/IPY/archive/master.zip && \
    unzip master.zip && rm master.zip && cd IPY-master && \
    ./configure && make && make install && cd /opt && rm -rf IPY-master && \
    wget https://github.com/emmt/OptimPackLegacy/archive/master.zip && \
    unzip master.zip && rm master.zip && cd OptimPackLegacy-master/ && \
    yorick/configure && make && make install && cd /opt && rm -rf OptimPackLegacy-master && \
    wget https://github.com/emmt/OptimPack/archive/master.zip && \
    unzip master.zip && rm master.zip && cd OptimPack-master/ && \
    yorick/configure && make && make install && cd /opt && rm -rf OptimPack-master && \
    wget https://github.com/emmt/YOIFITS/archive/master.zip  && \
    unzip master.zip && rm master.zip && cd YOIFITS-master && \
    ./configure && make && make install && cd /opt && rm -rf YOIFITS-master && \
    wget https://github.com/emmt/YImage/archive/master.zip  && \
    unzip master.zip && rm master.zip && cd YImage-master && \
    ./configure && make && make install && cd /opt && rm -rf YImage-master && \
    wget https://github.com/emmt/ygsl/archive/master.zip  && \
    unzip master.zip && rm master.zip && cd ygsl-master && \
    ./configure && make && make install && cd /opt && rm -rf ygsl-master && \
    wget https://github.com/emmt/YFITSIO/archive/master.zip  && \
    unzip master.zip && rm master.zip && cd YFITSIO-master && \
    ./configure && make && make install && cd /opt && rm -rf YFITSIO-master && \
    wget https://github.com/emmt/YTotVar/archive/master.zip  && \
    unzip master.zip && rm master.zip && cd YTotVar-master && \
    yorick -batch make.i && make && make install && cd /opt && rm -rf YTotVar-master && \
    apk del  .build-deps  && \
    rm -rf /var/cache/apk/*

