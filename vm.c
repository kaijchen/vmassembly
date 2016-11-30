#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUFSIZE 64
#define ARGNUM 8
#define MEMSIZE 0x10000
#define MEMLOW 0x3000
#define MEMHIGH 0xAFFF
#define EVALLIM 1000000
#define INSLIM MEMSIZE

#define MEM	0
#define TLE	1
#define JMP	2
#define CMP	3

static const char* errmsg[4] = {"ACCESS_VIOLATION",
	"TLE", "RUNTIME_ERROR", "CMP_MISSING"};
static int errcode;
static unsigned char mem[MEMSIZE];
static unsigned short ip, ax, bx, cx, dx;
static int flags;
static int flags_unused = 1;
static char *insargs[4 * INSLIM];
static const char **ins[INSLIM];
static int insargsi = 0;
static int insi = 0;
static int evali = 0;

void split(char *buf, char **args)
{
	int n = 0;

	for (char *p = buf, *lp = buf; *p; p++)
		if (*p == ' ') {
			args[n++] = lp;
			lp = p + 1;
			*p = '\0';
		}
	args[n] = '\0';
}

int memcheck(unsigned short addr)
{
	return (MEMLOW <= addr) && (addr < MEMHIGH);
}

unsigned short *memaddr(unsigned short addr)
{
	if (!memcheck(addr)) {
		errcode = MEM;
		return NULL;
	}
	return (unsigned short *)&mem[addr];
}

unsigned short *regaddr(const char *str)
{
	unsigned short *x = NULL;

	if (str[1] != 'X')
		return NULL;
	switch (str[0]) {
	case 'A':
		x = &ax;
		break;
	case 'B':
		x = &bx;
		break;
	case 'C':
		x = &cx;
		break;
	case 'D':
		x = &dx;
		break;
	}
	return x;
}

unsigned short *getaddr(const char *str)
{
	unsigned short addr;
	unsigned short *regp, *valp;
	unsigned int tmp;

	if (str[0] == 'T') {
		if ((regp = regaddr(str + 1)) != NULL) {
			addr = *regp;
		} else {
			sscanf(str + 1, "%x", &tmp);
			addr = tmp;
		}
		if ((valp = memaddr(addr)) == NULL)
			return NULL;
	} else if ((valp = regaddr(str)) == NULL)
		return NULL;
	return valp;
}

int getvalue(const char *str)
{
	unsigned short *valp;
	unsigned short x;
	int tmp;

	if ((valp = getaddr(str))) {
		x = *valp;
	} else if (str[0] == 'T') {
			return -1;
	} else {
		sscanf(str, "%x", &tmp);
		x = tmp;
	}
	return x;
}

int mov(const char **args)
{
	int rv;
	unsigned short *valp;
	unsigned short x;

	if ((valp = getaddr(args[0])) == NULL)
		return -1;
	if ((rv = getvalue(args[1])) == -1)
		return -1;
	x = rv;
	*valp = x;
	return 0;
}

int echo(const char **args)
{
	int rv;
	unsigned short x;

	if ((rv = getvalue(args[0])) == -1)
		return -1;
	x = rv;
	printf("%04X\n", x);
	return 0;
}

int add(const char **args)
{
	int rv;
	unsigned short *valp;
	unsigned short x;

	if ((valp = getaddr(args[0])) == NULL)
		return -1;
	if ((rv = getvalue(args[1])) == -1)
		return -1;
	x = rv;
	*valp += x;
	return 0;
}

int inc(const char **args)
{
	unsigned short *valp;

	if ((valp = getaddr(args[0])) == NULL)
		return -1;
	*valp += 1;
	return 0;
}

int cmp(const char **args)
{
	int rv;
	unsigned short x, y;

	flags_unused = 0;
	if ((rv = getvalue(args[0])) == -1)
		return -1;
	x = rv;
	if ((rv = getvalue(args[1])) == -1)
		return -1;
	y = rv;
	flags = x;
	flags -= y;
	return 0;
}

int jmp(const char **args)
{
	int rv;
	unsigned short x;

	if ((rv = getvalue(args[0])) == -1)
		return -1;
	x = rv;
	if (x < 2 || x > insi) {
		errcode = JMP;
		return -1;
	}
	ip = x - 1;
	return 0;
}

int eval(const char **args)
{
	int jmpflag;

	ip++;
	if (!strcmp(args[0], "MOV")) {
		return mov(args + 1);
	} else if (!strcmp(args[0], "ECHO")) {
		return echo(args + 1);
	} else if (!strcmp(args[0], "ADD")) {
		return add(args + 1);
	} else if (!strcmp(args[0], "INC")) {
		return inc(args + 1);
	} else if (!strcmp(args[0], "CMP")) {
		return cmp(args + 1);
	} else if (args[0][0] == 'J') {
		if (!strcmp(args[0], "JMP")) {
			jmpflag = 1;
		} else if (flags_unused) {
			errcode = CMP;
			return -1;
		} else if (!strcmp(args[0], "JG")) {
			jmpflag = (flags > 0);
		} else if (!strcmp(args[0], "JL")) {
			jmpflag = (flags < 0);
		} else if (!strcmp(args[0], "JE")) {
			jmpflag = (flags == 0);
		} else if (!strcmp(args[0], "JNG")) {
			jmpflag = !(flags > 0);
		} else if (!strcmp(args[0], "JNL")) {
			jmpflag = !(flags < 0);
		} else if (!strcmp(args[0], "JNE")) {
			jmpflag = !(flags == 0);
		}
		if (jmpflag)
			return jmp(args + 1);
		else
			return 0;
	} else if (!strcmp(args[0], "RUN")) {
		return 0;
	} else if (!strcmp(args[0], "STOP")) {
		return 1;
	}
	return -1;
}

int read()
{
	char buf[BUFSIZE];
	char *args[ARGNUM];

	while (fgets(buf, BUFSIZE, stdin)) {
		buf[strlen(buf) - 1] = ' ';
		split(buf, args);
		ins[insi++] = (const char **)&insargs[insargsi];
		for (int i = 0; args[i]; i++)
			insargs[insargsi++] = strdup(args[i]);
		insargs[insargsi++] = NULL;
	}
	return 0;
}

int main()
{
	int rv;

	read();
	for (ip = evali = 0; evali < EVALLIM; evali++)
		if ((rv = eval(ins[ip])))
			break;
	if (evali >= EVALLIM) {
		rv = -1;
		errcode = TLE;
	}
	if (rv == -1)
		puts(errmsg[errcode]);
	return 0;
}
