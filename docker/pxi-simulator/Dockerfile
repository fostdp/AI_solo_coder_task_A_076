FROM python:3.12-slim

RUN apt-get update && apt-get install -y --no-install-recommends curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY scripts/pxi_simulator.py /app/pxi_simulator.py

EXPOSE 8091

ENTRYPOINT ["python", "/app/pxi_simulator.py"]
CMD ["--target-ip", "cavitation-monitor", "--target-port", "9000", \
     "--turbines", "6", "--sample-rate", "1000", "--interval-ms", "1", \
     "--inject-port", "8091"]
