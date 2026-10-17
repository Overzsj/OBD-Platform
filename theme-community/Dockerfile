FROM python:3.12-slim
WORKDIR /app
COPY theme-community/ /app/theme-community/
RUN useradd --uid 10001 --create-home obd && mkdir /data && chown obd /data
USER obd
EXPOSE 5190
VOLUME ["/data"]
CMD ["python","theme-community/server.py","--host","0.0.0.0","--data","/data","--read-only"]
