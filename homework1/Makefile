ifndef CONTIKI
  ${error CONTIKI not defined! Do not forget to source /upb/groups/fg-ccs/public/share/nes/2018w/env.sh}
endif

ifndef TARGET
TARGET=sky
endif

all: protothreads.sky

upload: protothreads.upload

multiple_threads: multiple_threads.c
	gcc -g -o multiple_threads multiple_threads.c

single_thread: single_thread.c
	gcc -g -o single_thread single_thread.c

simulation:
	java -jar $(CONTIKI)/tools/cooja/dist/cooja.jar -contiki=$(CONTIKI)

CONTIKI_WITH_IPV4 = 1
CONTIKI_WITH_RIME = 1
include $(CONTIKI)/Makefile.include
