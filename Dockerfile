FROM node:22-bookworm-slim AS web
WORKDIR /web
COPY frontend/package.json ./
RUN npm install
COPY frontend/ ./
RUN npm run build

FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake g++ ninja-build ca-certificates libboost-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY backend ./backend
RUN cmake -S . -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build /tmp/build

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libssl3t64 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /tmp/build/bin/pulsenet /usr/local/bin/pulsenet
COPY --from=web /web/dist /opt/pulsenet/web
EXPOSE 8080
VOLUME /data
ENTRYPOINT ["pulsenet"]
CMD ["--serve", "--listen", "0.0.0.0:8080", "--db", "/data/pulsenet.db", "--web", "/opt/pulsenet/web", "--demo"]
