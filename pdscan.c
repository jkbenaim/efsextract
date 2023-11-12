#define _GNU_SOURCE
#include <inttypes.h>
#include <iso646.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "efs.h"
#include "efs_internal.h"
#include "endian.h"
#include "err.h"

#define ARRAY_LENGTH(x) (sizeof(x)/sizeof(*x))

int verbose = 0;

uint16_t getShort(efs_file_t *f)
{
	uint16_t out = 0;
	size_t sz;

	sz = efs_fread(&out, 2, 1, f);
	if (sz != 1)
		errx(1, "couldn't read short");
	out = be16toh(out);
	//printf("getShort: %4x\n", out);
	return out;
}

uint32_t getInt(efs_file_t *f)
{
	uint32_t out = 0;
	out = getShort(f);
	out <<= 16;
	out += getShort(f);
	return out;
}
	
char *getCstring(efs_file_t *f)
{
	char *out = NULL;
	size_t len = 0;
	long oldpos;
	char a = '\0';
	size_t sz;

	oldpos = efs_ftell(f);
	do {
		sz = efs_fread(&a, 1, 1, f);
		if (sz != 1) errx(1, "in getCstring 1");
		if (a) len++;
		//printf("pos: %6lx\t char: '%c'\n", efs_ftell(f), a);
	} while (a);
	efs_fseek(f, oldpos, SEEK_SET);
	out = malloc(len + 1);
	if (!out) errx(1, "in malloc");
	sz = efs_fread(out, len + 1, 1, f);
	if (sz != 1)
		errx(1, "in getCstring 2");
	out[len] = '\0';
	return out;
}

char *getString(efs_file_t *f)
{
	char *out = NULL;
	size_t len = 0;
	size_t sz;
	len = getShort(f);
	out = malloc(len + 1);
	if (!out) err(1, "in malloc");
	if (len) {
		sz = efs_fread(out, len, 1, f);
		if (sz != 1)
			errx(1, "in getString");
	}
	out[len] = '\0';
#if 0
	for (size_t i = 0; i < len; i++) {
		if (out[i] == '\x01') {
			out[i] = '^';
		}
	}
#endif
	return out;
}

char *getTriplet(efs_file_t *f)
{
	char *a1 = getString(f);
	char *a2 = getString(f);
	char *a3 = getString(f);
	char *triplet = NULL;
	int rc;
	rc = asprintf(&triplet, "%s.%s.%s", a1, a2, a3);
	if (rc == -1) err(1, "in asprintf");
	free(a1);
	free(a2);
	free(a3);
	return triplet;
}

char *getMatcher(efs_file_t *f, const char *prefix)
{
	// a matcher is 3 strings and 2 ints
	char *out = NULL;
	int rc;
	char *triplet = getTriplet(f);
	int32_t from = getInt(f);
	int32_t to = getInt(f);
	const char *my_prefix;
	if (!prefix) {
		my_prefix = "";
	} else if (!strcmp(prefix, "replaces ") && (from < 0)) {
		from = -from;
		my_prefix = "follows ";
	} else {
		my_prefix = prefix;
	}
	if (to == 2147483647u) {
		rc = asprintf(&out, "%s'%s' %d maxint", my_prefix, triplet, from);
	} else {
		rc = asprintf(&out, "%s'%s' %d %d", my_prefix, triplet, from, to);
	}
	if (rc == -1) err(1, "in asprintf");
	free(triplet);
	return out;
}

void getRules(efs_file_t *f)
{
	uint16_t rulesCount = getShort(f);
	//printf("\t\trulesCount: %d\n", rulesCount);
	if (rulesCount > 2000) errx(1, "diagnostic abort (too many rules)");
	for (int rule = 0; rule < rulesCount; rule++) {
		char *matcher = getMatcher(f, "replaces ");
		//printf("\t\t\t%s\n", matcher);
		free(matcher);
	}
}

void getMachInfo(efs_file_t *f)
{
	// get machine info
	uint32_t machCount = getInt(f);
	//printf("machCount: %d\n", machCount);
	for (size_t i = 0; i < machCount; i++) {
		char *m = getString(f);
		//printf("\tmach '%s'\n", m);
		free(m);
	}
}

void getPrereqs(efs_file_t *f)
{
	uint16_t prereqSets = getShort(f);
	//printf("\t\tprereq sets: %d\n", prereqSets);
	for (int set = 0; set < prereqSets; set++) {
		uint16_t prereqsCount = getShort(f);
		//printf("\t\tprereqs: %d (\n", prereqsCount);
		for (int a = 0; a < prereqsCount; a++) {
			char *matcher = getMatcher(f, NULL);
			//printf("\t\t\t%s\n", matcher);
			free(matcher);
		}
		//printf("\t\t)\n");
	}
}

void getAttrs(efs_file_t *f, const char *prefix)
{
	uint32_t attrs = getInt(f);
	//printf("%sattrs: %d\n", prefix, attrs);
	for (size_t i = 0; i < attrs; i++) {
		char *attr = getString(f);
		//printf("%s\t'%s'\n", prefix, attr);
		free(attr);
	}
}

void getUpdates(efs_file_t *f)
{
	uint16_t updatesCount = getShort(f);
	//printf("\t\tupdatesCount: %d\n", updatesCount);
	for (int i = 0; i < updatesCount; i++) {
		char *matcher = getMatcher(f, "updates ");
		//printf("\t\t\t%s\n", matcher);
		free(matcher);
	}
}

int pdscan(efs_file_t *f)
{
	size_t sz;
	char buf[999] = {0,};

#define FMT "%-30s"

	// product
	sz = efs_fread(buf, 2, 1, f);
	if (sz != 1) {
		//errx(1, "while reading header");
		return 1;
	}
	if ((buf[0] != 'p') || (buf[1] != 'd')) {
		//errx(1, "bad file format");
		return 1;
	}
	char *prodId = getCstring(f);
	//printf("prodId: %s\n", prodId);
	uint16_t magic = getShort(f);
	//printf("magic: %04x %s\n", magic, (magic==1988)?"(ok)":"(BAD)");
	uint16_t noOfProds = getShort(f);
	//printf("noOfProds: %04x %s\n", noOfProds, (noOfProds>=1)?"(ok)":"(BAD)");

	for (unsigned prodNum = 0; prodNum < noOfProds; prodNum++) {

	// root
	uint16_t prodMagic = getShort(f);
	//printf("prodMagic: %04x %s\n", prodMagic, (prodMagic==1987)?"(ok)":"(BAD)");
	uint16_t prodFormat = getShort(f);
	//printf("prodFormat: %04x\n", prodFormat);
	switch (prodFormat) {
	case 5 ... 9:
		break;
	default:
		//errx(1, "bad prodFormat: %d not between 5 and 9 inclusive", prodFormat);
		return 1;
	}

	char *shortName = getString(f);
	//printf("shortName: '%s'\n", shortName);
	char *longName = getString(f);
	//printf("longName:  '%s'\n", longName);
	uint16_t prodFlags = getShort(f);
	//printf("prodFlags: %04x\n", prodFlags);
	if (prodFormat >= 5) {
		time_t prodDateTime = getInt(f);
		//printf("datetime: %s", ctime(&prodDateTime));
	}

	if (prodFormat >= 5) {
		char *prodIdk = getString(f);
		//printf("prodIdk: '%s'\n", prodIdk);
		free(prodIdk);
	}

	if (prodFormat == 7) {
		getMachInfo(f);
	}

	if (prodFormat >= 8) {
		getAttrs(f, "");
	}

	char *line = NULL;
	int rc;
	rc = asprintf(&line, "%s", shortName);
	if (rc == -1) err(1, "in asprintf");
	printf("   " FMT "  %s\n", line, longName);
	free(line);
	line = NULL;

	uint16_t imageCount = getShort(f);
	//printf("imageCount: %04x\n", imageCount);

	for (int image = 0; image < imageCount; image++) {
		//printf("product #%d image #%d:\n", prodNum, image);
		uint16_t imageFlags = getShort(f);
		//printf("\timageFlags: %04x\n", imageFlags);
		char *imageName = getString(f);
		//printf("\timageName: '%s'\n", imageName);
		char *imageId = getString(f);
		//printf("\timageId: '%s'\n", imageId);
		uint16_t imageFormat = getShort(f);
		//printf("\timageFormat: %04x\n", imageFormat);

		uint16_t imageOrder = 0;
		if (prodFormat >= 5) {
			imageOrder = getShort(f);
		}
		//printf("\timageOrder: %04x (%u)\n", imageOrder, imageOrder);

		uint32_t imageVersion = getInt(f);
		//printf("\timageVersion: %u\n", imageVersion);

		if (prodFormat == 5) {
			uint32_t a = getInt(f);
			uint32_t b = getInt(f);
			if (a || b) {
				printf("a: %08x\n", a);
				printf("b: %08x\n", b);
				errx(1, "diagnostic abort (has a or b)");
			}
		}

		char *derivedFrom = getString(f);
		if (strlen(derivedFrom)) {
			//printf("\tderivedFrom: '%s'\n", derivedFrom);
		}
		free(derivedFrom);
		if (prodFormat >= 8) {
			getAttrs(f, "\t");
		}
		char *line = NULL;
		int rc;
		rc = asprintf(&line, "%s.%s", shortName, imageName);
		if (rc == -1) err(1, "in asprintf");
		printf("   " FMT "  %s\n", line, imageId);
		free(line);
		line = NULL;

		uint16_t subsysCount = getShort(f);
		//printf("\tsubsysCount: %04x\n", subsysCount);

		for(int subsys = 0; subsys < subsysCount; subsys++) {
			//printf("\tsubsys #%d:\n", subsys);
			uint16_t subsysFlags = getShort(f);
			//printf("\t\tsubsysFlags: %04x\n", subsysFlags);
			if (subsysFlags & 0x0002) {
				//printf("\t\tdefault\n");
			}
			if (subsysFlags & 0x0400) {
				//printf("\t\tpatch\n");
			}
			if (~subsysFlags & 0x0800) {
				//printf("\t\tminiroot\n");
			}
			if (subsysFlags & 0x8000) {
				//printf("\t\toverlays (see 'b' attribute)\n");
			}
			char *subsysName = getString(f);
			//printf("\t\tsubsysName: '%s'\n", subsysName);
			char *subsysId = getString(f);
			//printf("\t\tsubsysId: '%s'\n", subsysId);
			char *line = NULL;
			int rc;
			rc = asprintf(&line, "%s.%s.%s", shortName, imageName, subsysName);
			if (rc == -1) err(1, "in asprintf");
			printf("   " FMT "  %s\n", line, subsysId);
			free(line);
			line = NULL;
			char *subsysExpr = getString(f);
			//printf("\t\tsubsysExpr: '%s'\n", subsysExpr);
			time_t subsysInstallDate = getInt(f);
			if (subsysFlags & 0x0080) {
				//printf("\t\tsubsysInstallDate: %s", ctime(&subsysInstallDate));
			}

			getRules(f);
			getPrereqs(f);
			free(subsysName);
			free(subsysId);
			free(subsysExpr);
			if (prodFormat >= 5) {
				char *altName = getString(f);
				//printf("\t\taltName: '%s'\n", altName);
				free(altName);
			}
			if (prodFormat >= 6) {
				//printf("\t\tincompats:\n");
				getRules(f);
			}
			if (prodFormat >= 8) {
				getAttrs(f, "\t\t");
			}
			if (prodFormat >= 9) {
				getUpdates(f);
			}
		}

		free(imageName);
		free(imageId);
	}

	free(shortName);
	free(longName);
	printf("\n");
	} // end of foreach(prod)
	free(prodId);

	return 0;
}

