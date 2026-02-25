
INSTALL_BASE=/usr/local
#INSTALL_BASE=${HOME}/.local

BINS=acv apply chart plot linearize hflip imageinfo

all:
	echo "make install to install under ${INSTALL_BASE}"

install:
	install -m 755 $(addprefix bin/,$(BINS)) ${INSTALL_BASE}/bin
	install -m 644 man/acv.1 man/chart.1 ${INSTALL_BASE}/share/man/man1

PUBLISH_BASE=../../public/dn-tools
publish:
	install -m 644 Makefile README.md packages.txt requirements.txt ${PUBLISH_BASE}
	mkdir -p ${PUBLISH_BASE}/bin
	install -m 755 $(addprefix bin/,$(BINS)) ${PUBLISH_BASE}/bin
	install -m 644 bin/env.sh ${PUBLISH_BASE}/bin
	mkdir -p ${PUBLISH_BASE}/images
	install -m 644 images/* ${PUBLISH_BASE}/images
	mkdir -p ${PUBLISH_BASE}/man
	install -m 644 man/acv.1 man/chart.1 ${PUBLISH_BASE}/man
	mkdir -p ${PUBLISH_BASE}/examples
	install -m 644 examples/* ${PUBLISH_BASE}/examples
	(cd ${PUBLISH_BASE} && git add .)
