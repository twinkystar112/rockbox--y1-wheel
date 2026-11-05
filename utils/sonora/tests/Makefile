# SONORA host tests (no Rockbox needed): make && ./test_sonora
SRC = ../../../apps/sonora
CFLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
         -DSONORA_HOST_TEST -DSONORA_CUE_DIR='"/tmp/sonora-test-cue"' -I$(SRC)
OBJS = $(SRC)/sonora_util.c $(SRC)/sonora_chapters.c $(SRC)/sonora_feed.c $(SRC)/sonora_data.c

test_sonora: test_sonora.c $(OBJS) $(wildcard $(SRC)/*.h)
	$(CC) $(CFLAGS) -o $@ test_sonora.c $(OBJS)

check: test_sonora
	./test_sonora

clean:
	rm -f test_sonora
