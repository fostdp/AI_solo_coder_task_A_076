FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config liblz4-dev curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY CMakeLists.txt .
COPY include/ include/
COPY src/ src/
COPY config/ config/

RUN mkdir build && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release .. \
    && make -j$(nproc)

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    liblz4-1 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /src/build/cavitation_monitor /app/cavitation_monitor
COPY config/ /app/config/

RUN mkdir -p /app/logs

EXPOSE 8080 9000/udp 9090

ENTRYPOINT ["/app/cavitation_monitor"]
CMD ["--config", "/app/config/model_config.json"]
