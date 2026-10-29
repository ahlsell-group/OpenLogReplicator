# iproute2 + iptables for commands in another container's network namespace
# (docker run --network container:<name> --cap-add NET_ADMIN), see olrt.connect.netns
FROM alpine:3.24
RUN apk add --no-cache iproute2 iptables
