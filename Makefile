LDLIBS= -pthread

memcache: threadpool.cc lrucache.cc memcache.cc
	g++ -g -o memcache threadpool.cc lrucache.cc memcache.cc $(LDLIBS)

clean:
	rm -f *.o
	rm -f memcache

