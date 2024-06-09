
# CROSS_COMPILE=""
APP="writer"

writer: clean
	${CROSS_COMPILE}gcc -g -o ${APP} -Wall -Wextra ${APP}.c

.PHONY: clean
clean:
	rm -f ${APP}