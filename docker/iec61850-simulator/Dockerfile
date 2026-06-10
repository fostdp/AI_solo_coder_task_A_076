FROM python:3.12-slim

RUN apt-get update && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY scripts/iec61850_simulator.py /app/iec61850_simulator.py

EXPOSE 102 8090

ENTRYPOINT ["python", "/app/iec61850_simulator.py"]
CMD ["--listen-port", "102", "--http-port", "8090"]
