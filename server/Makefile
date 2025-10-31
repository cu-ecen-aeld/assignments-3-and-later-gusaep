APP=aesdsocket


all: clean ${APP}

.PHONY: clean
clean:
	rm -f ${APP}

${APP}: ${APP}.c
	${CC} -D_GNU_SOURCE -g -o ${APP} -Wall -Wextra ${APP}.c -lpthread

