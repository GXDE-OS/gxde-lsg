build:
	g++ main.cpp -o gxde-lsg --static

install:
	cp -rv gxde-lsg $(DESTDIR)/usr/bin
