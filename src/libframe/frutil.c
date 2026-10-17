#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>

int
_frcanfit(Frame *f, Point pt, Frbox *b)
{
	int left, w, nr;
	uchar *p;
	Rune r;
	Font *bfont;

	left = f->r.max.x-pt.x;
	if(b->nrune < 0)
		return b->minwid <= left;
	if(left >= b->wid)
		return b->nrune;
	bfont = BOXFONT(f, b);
	for(nr=0,p=b->ptr; *p; p+=w,nr++){
		r = *p;
		if(r < Runeself)
			w = 1;
		else
			w = chartorune(&r, (char*)p);
		left -= stringnwidth(bfont, (char*)p, 1);
		if(left < 0)
			return nr;
	}
	drawerror(f->display, "_frcanfit can't");
	return 0;
}

static
int
wraps(Frame *f, Point p, Frbox *b)
{
	return (b->nrune<0? b->minwid : b->wid) > f->r.max.x-p.x;
}

static
void
nextline(Frame *f, Point *p)
{
	p->x = f->r.min.x;
	/* step down by the height growline has accumulated */
	p->y += f->curlineht;
	f->curlineht = f->font->height;
}

static
void
growline(Frame *f, Frbox *b)
{
	if(BOXFONT(f, b)->height > f->curlineht)
		f->curlineht = BOXFONT(f, b)->height;
}

void
_frcklinewrap(Frame *f, Point *p, Frbox *b)
{
	if(wraps(f, *p, b))
		nextline(f, p);
	growline(f, b);
}

void
_frcklinewrap0(Frame *f, Point *p, Frbox *b)
{
	if(_frcanfit(f, *p, b) == 0)
		nextline(f, p);
	growline(f, b);
}

void
_fradvance(Frame *f, Point *p, Frbox *b)
{
	if(b->nrune<0 && b->bc=='\n')
		nextline(f, p);
	else
		p->x += b->wid;
}

int
_frnewwid(Frame *f, Point pt, Frbox *b)
{
	b->wid = _frnewwid0(f, pt, b);
	return b->wid;
}

int
_frnewwid0(Frame *f, Point pt, Frbox *b)
{
	int c, x;

	c = f->r.max.x;
	x = pt.x;
	if(b->nrune>=0 || b->bc!='\t')
		return b->wid;
	if(x+b->minwid > c)
		x = pt.x = f->r.min.x;
	x += f->maxtab;
	x -= (x-f->r.min.x)%f->maxtab;
	if(x-pt.x<b->minwid || x>c)
		x = pt.x+b->minwid;
	return x-pt.x;
}

int
frrunestep(Frame *f, Point *p, Rune r, Frmarkup *fg)
{
	Frbox b;
	Font *bfont;
	int y, wrapped;

	b.fg = fg;
	b.bg = nil;
	bfont = BOXFONT(f, &b);
	if(r=='\t' || r=='\n'){
		b.bc = r;
		b.nrune = -1;
		b.wid = 5000;
		b.minwid = r=='\n'? 0 : stringwidth(bfont, " ");
	}else{
		b.nrune = 1;
		b.wid = runestringnwidth(bfont, &r, 1);
	}
	f->curlineht = f->font->height;
	y = p->y;
	_frcklinewrap(f, p, &b);
	wrapped = p->y > y;
	if(r == '\t')
		_frnewwid(f, *p, &b);
	_fradvance(f, p, &b);
	return wrapped;	/* r moved to the next row */
}

void
_frclean(Frame *f, Point pt, int n0, int n1)	/* look for mergeable boxes */
{
	Frbox *b;
	int nb, c;

	c = f->r.max.x;
	for(nb=n0; nb<n1-1; nb++){
		b = &f->box[nb];
		_frcklinewrap(f, &pt, b);
		while(b[0].nrune>=0 && nb<n1-1 && b[1].nrune>=0 && b[0].fg==b[1].fg
		     && b[0].bg==b[1].bg && pt.x+b[0].wid+b[1].wid<c){
			_frmergebox(f, nb);
			n1--;
			b = &f->box[nb];
		}
		_fradvance(f, &pt, &f->box[nb]);
	}
	for(; nb<f->nbox; nb++){
		b = &f->box[nb];
		_frcklinewrap(f, &pt, b);
		_fradvance(f, &pt, &f->box[nb]);
	}
	f->lastlinefull = 0;
	if(pt.y >= f->r.max.y)
		f->lastlinefull = 1;
}

int
_frlinescan(Frame *f, int bn, int *nbnp)
{
	Frbox *b;
	Point pt;
	int h;

	h = f->font->height;
	pt = f->r.min;
	for(b=&f->box[bn]; bn<f->nbox; bn++,b++){
		if(pt.x>f->r.min.x && wraps(f, pt, b))
			break;
		if(BOXFONT(f, b)->height > h)
			h = BOXFONT(f, b)->height;
		if(b->nrune<0 && b->bc=='\n'){
			bn++;
			break;
		}
		pt.x += b->nrune<0? _frnewwid0(f, pt, b) : b->wid;
	}
	/* bn is the box starting the next line */
	*nbnp = bn;
	return h;
}

Rectangle
_frfindline(Frame *f, Point pt, int *bnp, ulong *cnp)
{
	Rectangle r;
	int bn, nbn, i;
	ulong cn;

	/* the line holding pt.y, or the empty one below the text */
	r = f->r;
	cn = 0;
	for(bn=0; bn<f->nbox; bn=nbn){
		r.max.y = r.min.y+_frlinescan(f, bn, &nbn);
		if(pt.y < r.max.y)
			break;
		for(i=bn; i<nbn; i++)
			cn += NRUNE(&f->box[i]);
		r.min.y = r.max.y;
	}
	if(bn >= f->nbox)
		r.max.y = r.min.y+f->font->height;
	/* its first box and the number of chars before it */
	*bnp = bn;
	*cnp = cn;
	return r;
}

int
_frvarht(Frame *f)
{
	int nb;

	/* lines are all one height unless some box is taller than the frame's font */
	for(nb=0; nb<f->nbox; nb++)
		if(BOXFONT(f, &f->box[nb])->height > f->font->height)
			return 1;
	return 0;
}

int
_frcountlines(Frame *f)
{
	int bn, nl;

	for(nl=0,bn=0; bn<f->nbox; nl++)
		_frlinescan(f, bn, &bn);
	return nl;
}
