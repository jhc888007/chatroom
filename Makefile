objects = mser.out mcli.out mcli1.out

protocfg = `pkg-config --cflags --libs protobuf`

define make_ser
	g++ proto/net_message.pb.cc server/server.cpp -I. -g -pthread -o mser.out $(protocfg)
endef
define make_cli
	g++ client/client.cpp -I. -g -o mcli.out
endef
define make_cli1
	g++ client/client.cpp -I. -g -o mcli1.out
endef

$(objects):
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
