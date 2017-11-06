# Filement

all:
	$(MAKE) -C src filement libfilement.so

pubcloud:
	$(MAKE) -C src pubcloud

libfilement.so:
	$(MAKE) -C src libfilement.so

filement:
	$(MAKE) -C src filement

check:
	$(MAKE) -C tests check

failsafe:
	$(MAKE) -C src failsafe

register:
	$(MAKE) -C src register

clean:
	$(MAKE) -C src clean
	$(MAKE) -C tests clean

mrproper: clean
	$(MAKE) -C src mrproper
