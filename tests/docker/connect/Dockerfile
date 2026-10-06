# Kafka Connect with the Debezium Oracle connector, the Oracle JDBC driver and an Avro converter
# (schema registry). Built on demand by `python -m olrt connect`, or by hand from tests/:
#   docker build -t olrt/connect:3.7.0.Final --build-arg DEBEZIUM_VERSION=3.7.0.Final docker/connect
ARG DEBEZIUM_VERSION=3.6.1.Final
FROM quay.io/debezium/connect:${DEBEZIUM_VERSION}

ARG OJDBC_VERSION=23.9.0.25.07
ARG OJDBC_SHA256=f52e9335276cd3999c23bb5a364fa01a628f3468945444d03cb7f92271bca1da
ARG AVRO_VERSION=7.9.0
ARG AVRO_SHA256=f8ad69abc13f0a32ca21e9036ddcd2d2a7a1625ef9b255d9ea93cdb92ca515b2

USER root
# Debezium ships the connector but not the JDBC driver (licence); the OLR adapter needs it for
# the schema and snapshots.
RUN curl -fsSLo /kafka/connect/debezium-connector-oracle/ojdbc11.jar \
      https://repo1.maven.org/maven2/com/oracle/database/jdbc/ojdbc11/${OJDBC_VERSION}/ojdbc11-${OJDBC_VERSION}.jar \
 && echo "${OJDBC_SHA256}  /kafka/connect/debezium-connector-oracle/ojdbc11.jar" | sha256sum -c - \
 && curl -fsSLo /tmp/avro.zip \
      https://hub-downloads.confluent.io/api/plugins/confluentinc/kafka-connect-avro-converter/versions/${AVRO_VERSION}/confluentinc-kafka-connect-avro-converter-${AVRO_VERSION}.zip \
 && echo "${AVRO_SHA256}  /tmp/avro.zip" | sha256sum -c - \
 && mkdir -p /kafka/connect/avro-converter \
 && cd /tmp && unzip -q avro.zip \
 && cp confluentinc-kafka-connect-avro-converter-${AVRO_VERSION}/lib/*.jar /kafka/connect/avro-converter/ \
 && rm -rf /tmp/avro.zip /tmp/confluentinc-kafka-connect-avro-converter-* \
 && chown -R kafka:kafka /kafka/connect/debezium-connector-oracle/ojdbc11.jar /kafka/connect/avro-converter
USER kafka
