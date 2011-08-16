.PHONE: all

all: xmlpatch.1.gz examples.html

xmlpatch.1.gz: xmlpatch.1
	gzip < $< > $@

examples.html: $(wildcard ../t/*/*)
	./examples.sh
