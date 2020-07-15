#FROM tecra:builder as builder
FROM ubuntu:16.04
#COPY --from=builder /usr/local/bin/tecracoind /usr/local/bin
#COPY --from=builder /usr/local/bin/tecracoin-cli /usr/local/bin
COPY ./tecracoind /usr/local/bin
COPY ./tecracoin-cli /usr/local/bin

RUN mkdir /root/.tecracoin/
# RUN echo "rpcuser=rpcuser"  >> /root/.tecracoin/tecracoin.conf \
#     && echo "rpcpassword=eingei1xa9Uam8iu4enida8gei2aengu" >> /root/.tecracoin/tecracoin.conf \
#     && echo "server=1" >> /root/.tecracoin/tecracoin.conf \
#     && echo "dns=1" >> /root/.tecracoin/tecracoin.conf \
#     && echo "listen=1" >> /root/.tecracoin/tecracoin.conf \
#     && echo "daemon=1" >> /root/.tecracoin/tecracoin.conf

ENTRYPOINT ["/usr/local/bin/tecracoind", "-printtoconsole", "-rpcuser=$RPCUSER", "-rpcpassword=$RPCPASSWORD", "-server", "-rpcallowip=172.0.0.0/8", "-rpcbind=0.0.0.0"]

EXPOSE 2718 2818 2845 9045 2353 6028
