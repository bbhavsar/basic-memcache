CPPFLAGS=-w
LDFLAGS=-g
LDLIBS= -pthread

memcache: threadpool.cc lrucache.cc memcache.cc
	g++ -w -fpermissive -g -o memcache threadpool.cc lrucache.cc memcache.cc $(LDLIBS)

clean:
	rm -f *.o
	rm -f memcache

