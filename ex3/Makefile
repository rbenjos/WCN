all: client

client: All_reduce.c
	 gcc All_reduce.c -libverbs -o client --verbose

clean:
	rm -f client

