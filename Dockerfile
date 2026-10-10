FROM alpine:latest AS builder
RUN apk add build-base cmake libffi-dev git --update-cache
WORKDIR /xuan
COPY . .
RUN make distclean && make

FROM alpine:latest
RUN apk add libstdc++ libffi tini --no-cache
COPY --from=builder /xuan/build/xuan /bin/xuan
COPY --from=builder /xuan/examples /examples
COPY ./docker/entry.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod 755 /usr/local/bin/docker-entrypoint.sh
ENTRYPOINT ["tini", "--", "docker-entrypoint.sh"]
CMD ["run", "/examples/hello_world.js"]
