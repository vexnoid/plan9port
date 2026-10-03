AUTOLIB(acme)

typedef struct Event Event;
typedef struct Win Win;
typedef struct Winstat Winstat;

#define	EVENTSIZE	256
/* zero before first use; text, arg, loc are malloced */
struct Event
{
	int	c1;
	int	c2;
	int	q0;
	int	q1;
	int	oq0;
	int	oq1;
	int	flag;
	int	nb;
	int	nr;
	char	*text;
	char	*arg;
	char	*loc;
};

struct Win
{
	int id;
	int fromdump;
	CFid *ctl;
	CFid *tag;
	CFid *body;
	CFid *addr;
	CFid *event;
	CFid *data;
	CFid *xdata;
	Channel *c;	/* chan(Event) */
	Win *next;
	Win *prev;

	/* events */
	int nbuf;
	char buf[1024];
	char *bufp;
	jmp_buf jmp;
	Event e2;
	Event e3;
	Event e4;
};

struct Winstat
{
	int	id;
	int	tagnc;
	int	bodync;
	int	isdir;
	int	dirty;
	int	isscratch;
};

Win *newwin(void);
Win *openwin(int, CFid*);
CFsys *acmefsys(void);

int eventfmt(Fmt*);
int pipewinto(Win *w, char *name, int, char *fmt, ...);
int pipetowin(Win *w, char *name, int, char *fmt, ...);
char *sysrun(char*, ...);
int winaddr(Win *w, char *fmt, ...);
int winctl(Win *w, char *fmt, ...);
int windel(Win *w, int sure);
int wincmds(Win *w, char *fmt, ...);
void winstat(char *buf, Winstat *st);
int winfd(Win *w, char *name, int);
char *winmread(Win *w, char *file);
int winname(Win *w, char *fmt, ...);
int winprint(Win *w, char *name, char *fmt, ...);
int winread(Win *w, char *file, void *a, int n);
int winseek(Win *w, char *file, int n, int off);
int winreadaddr(Win *w, uint*);
int winreadevent(Win *w, Event *e);
int winwrite(Win *w, char *file, void *a, int n);
int winwriteevent(Win *w, Event *e);
int winopenfd(Win *w, char *name, int mode);
void windeleteall(void);
void winfree(Win *w);
void winclosefiles(Win *w);
Channel *wineventchan(Win *w);

void *erealloc(void*, uint);
void *emalloc(uint);
char *estrdup(char*);
char *evsmprint(char*, va_list);
