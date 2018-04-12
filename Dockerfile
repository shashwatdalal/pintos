FROM ubuntu
MAINTAINER Shashwat Dalal <spd16@ic.ac.uk>

# Install set up tools
RUN apt-get update && \
    DEBIAN_FRONTEND=noninterative \
        apt-get install -y --no-install-recommends \
            curl

# Install useful user programs
RUN apt-get update && \
    DEBIAN_FRONTEND=noninterative \
        apt-get install -y --no-install-recommends \
            coreutils \
	          gcc perl  \
            vim emacs\
            make \
            gcc \
            gdb ddd \
            qemu \
            tmux

# Prepare the Pintos directory
RUN mkdir pintos
COPY . /pintos
WORKDIR pintos/

# Install scripts
RUN install src/utils/* /usr/local/bin
RUN install src/misc/gdb-macros /usr/local/bin
