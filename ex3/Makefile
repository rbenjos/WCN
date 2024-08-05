all: client

client: All_reduce.c
	 gcc All_reduce.c -libverbs -o server --verbose

clean:
	rm -f client

