FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    make \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN make re

EXPOSE 8080

CMD ["./webserv", "conf/default.conf"]
