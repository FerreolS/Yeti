FROM ferreol/yorick

MAINTAINER Ferréol Soulez <ferreol.soulez@univ-lyon1.fr>

RUN echo http://nl.alpinelinux.org/alpine/edge/testing >> /etc/apk/repositories && \
    apk add --update  --no-cache make wget build-base zip rlwrap gsl tiff-dev &&\
    mkdir -p /opt && cd /opt && \
    wget http://www.fftw.org/fftw-2.1.5.tar.gz && tar -xvzf fftw-2.1.5.tar.gz &&\
    rm fftw-2.1.5.tar.gz && cd fftw-2.1.5/ && \
    ./configure --with-openmp --enable-shared && make && make install && \
    cd /opt && rm -rf fftw-2.1.5 && \        
    wget https://github.com/emmt/Yeti/archive/master.zip && \
    unzip master.zip && rm master.zip && cd Yeti-master &&\
    ./configure --with-fftw=yes  --with-regex=yes --with-tiff=yes && \
    make all && make install && cd /opt && rm -rf Yeti-master &&\
    cd /opt/yorick/i-start && ln -s ../i0/yeti.i yeti.i && \
    apk del make wget build-base zip 

