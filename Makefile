objects = mser.out mcli.out mcli1.out

define make_ser
	g++ server/server.cpp -I. -g -pthread -o mser.out
endef
define make_cli
	g++ client/client.cpp -I. -g -o mcli.out
endef
define make_cli1
	g++ client/client.cpp -I. -g -o mcli1.out
endef

ser.out:
	make clean
	$(make_ser)
	$(make_cli)
	$(make_cli1)

ser:
	$(make_ser)
cli:
	$(make_cli)
cli1:
	$(make_cli1)

clean:
	rm -rf $(objects)
