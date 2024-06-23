APP=aesdsocket


all: clean ${APP}

.PHONY: clean
clean:
	rm -f ${APP}

${APP}: ${APP}.c
	${CC} -g -o ${APP} -Wall -Wextra ${APP}.c

