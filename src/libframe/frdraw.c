#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>

static
void
fillbg(Frame *f, Rectangle r, Image *col)
{
	if(rectclip(&r, f->r))
		draw(f->b, r, col, nil, r.min);
}

static void
boxlines(Frame *f, Frbox *b, Point pt, int w, int plain, Image *text)
{
	int ht, th, y;

	if(b->fg==nil || b->nrune<0 && b->bc=='\n')	/* newline runs off the edge */
		return;
	th = scalesize(f->display, 1);
	ht = BOXFONT(f, b)->height;
	/* selected text is all one color */
	if(b->fg->underline != nil)
		fillbg(f, Rect(pt.x, pt.y+ht-th, pt.x+w, pt.y+ht), plain? b->fg->underline : text);
	if(b->fg->strikethrough != nil){
		y = pt.y+(ht-th)/2;
		fillbg(f, Rect(pt.x, y, pt.x+w, y+th), plain? b->fg->strikethrough : text);
	}
}

void
_frdrawtext(Frame *f, Point pt, Image *text, Image *back)
{
	Frbox *b;
	int nb, plain;
	Image *tcol, *bcol, *pbcol;
	Font *bfont;
	Point qt;

	plain = back==f->cols[BACK] && text==f->cols[TEXT];
	pbcol = back;
	f->curlineht = f->font->height;
	for(nb=0,b=f->box; nb<f->nbox; nb++, b++){
		bcol = (plain && b->bg!=nil)? b->bg : back;
		qt = pt;
		_frcklinewrap(f, &pt, b);
		if(!f->noredraw){
			/* wrapped line: fill with prior box's bg */
			if(pt.y > qt.y)
				fillbg(f, Rect(qt.x, qt.y, f->r.max.x, pt.y), pbcol);
			if(b->nrune >= 0){
				tcol = (plain && b->fg!=nil && b->fg->col!=nil)? b->fg->col : text;
				bfont = BOXFONT(f, b);
				_string(f->b, pt, tcol, ZP, bfont, (char*)b->ptr, nil, 1<<24, f->r, bcol, ZP, SoverD);
			}else
				fillbg(f, Rect(pt.x, pt.y, pt.x+b->wid, pt.y+f->font->height), bcol);	/* tab/newline box */
			boxlines(f, b, pt, b->wid, plain, text);
		}
		pt.x += b->wid;
		pbcol = bcol;
	}
}

static int
nbytes(char *s0, int nr)
{
	char *s;
	Rune r;

	s = s0;
	while(--nr >= 0)
		s += chartorune(&r, s);
	return s-s0;
}

void
frdrawsel(Frame *f, Point pt, ulong p0, ulong p1, int issel)
{
	Image *back, *text;

	if(f->ticked)
		frtick(f, frptofchar(f, f->p0), 0);

	if(p0 == p1){
		frtick(f, pt, issel);
		return;
	}

	if(issel){
		back = f->cols[HIGH];
		text = f->cols[HTEXT];
	}else{
		back = f->cols[BACK];
		text = f->cols[TEXT];
	}

	frdrawsel0(f, pt, p0, p1, back, text);
}

Point
frdrawsel0(Frame *f, Point pt, ulong p0, ulong p1, Image *back, Image *text)
{
	Frbox *b, *bx;
	int nb, nr, w, trim, plain, lineht, lineend;
	Point qt;
	uint p;
	char *ptr;
	Rectangle r;
	Image *tcol, *bcol;
	Font *bfont;

	if(p0 > p1)
		sysfatal("libframe: frdrawsel0 p0=%lud > p1=%lud", p0, p1);

	/* per-box colors apply only to unselected text, so a selection paints solid */
	plain = back==f->cols[BACK] && text==f->cols[TEXT];
	p = 0;
	b = f->box;
	trim = 0;
	/* clip at the start of the fill, or glyphs darken */
	r = f->r;
	r.min.x = pt.x;
	/* each box is painted at its whole line's height, found by looking ahead */
	lineht = f->font->height;
	lineend = -1;
	for(nb=0; nb<f->nbox && p<p1; nb++){
		if(nb >= lineend){
			f->curlineht = lineht;	/* b may wrap off the line just left */
			lineht = _frlinescan(f, nb, &lineend);
		}
		nr = b->nrune;
		if(nr < 0)
			nr = 1;
		if(p+nr <= p0)
			goto Continue;
		bcol = (plain && b->bg!=nil)? b->bg : back;
		if(p >= p0){
			qt = pt;
			_frcklinewrap(f, &pt, b);
			/* fill in the end of a wrapped line */
			if(pt.y > qt.y){
				fillbg(f, Rect(qt.x, qt.y, f->r.max.x, pt.y),
					(plain && nb>0 && b[-1].bg!=nil)? b[-1].bg : back);
				r.min.x = f->r.min.x;
			}
		}
		ptr = (char*)b->ptr;
		bfont = BOXFONT(f, b);
		if(p < p0){	/* beginning of region: advance into box */
			ptr += nbytes(ptr, p0-p);
			nr -= (p0-p);
			p = p0;
		}
		trim = 0;
		if(p+nr > p1){	/* end of region: trim box */
			nr -= (p+nr)-p1;
			trim = 1;
		}
		if(b->nrune<0 || nr==b->nrune)
			w = b->wid;
		else
			w = stringnwidth(bfont, ptr, nr);
		fillbg(f, Rect(pt.x, pt.y, pt.x+w, pt.y+lineht), bcol);
		if(b->nrune >= 0){
			tcol = (plain && b->fg!=nil && b->fg->col!=nil)? b->fg->col : text;
			_string(f->b, pt, tcol, ZP, bfont, ptr, nil, nr, r, bcol, ZP, SoverD);
		}
		boxlines(f, b, pt, w, plain, text);
		pt.x += w;
	    Continue:
		b++;
		p += nr;
	}
	/* fill erased the overhang of the char after p1 */
	if(p1>p0 && (trim || b<f->box+f->nbox && b->nrune>0)){
		qt = pt;
		if(trim){
			ptr += nbytes(ptr, nr);
			bx = b-1;
		}else{
			_frcklinewrap(f, &qt, b);
			ptr = (char*)b->ptr;
			bx = b;
		}
		if(qt.y == pt.y){
			bfont = BOXFONT(f, bx);
			r.max.x = pt.x;
			r.min.y = pt.y;
			r.max.y = pt.y+bfont->height;
			if(r.max.y > f->r.max.y)
				r.max.y = f->r.max.y;
			if(p1>=f->p0 && p1<f->p1)
				tcol = f->cols[HTEXT];
			else
				tcol = (bx->fg!=nil && bx->fg->col!=nil)? bx->fg->col : f->cols[TEXT];
			_string(f->b, pt, tcol, ZP, bfont, ptr, nil, 1, r, nil, ZP, SoverD);
		}
	}
	/* if this is end of last plain text box on wrapped line, fill to end of line */
	if(p1>p0 &&  b>f->box && b<f->box+f->nbox && b[-1].nrune>0 && !trim){
		qt = pt;
		f->curlineht = lineht;
		_frcklinewrap(f, &pt, b);
		if(pt.y > qt.y)
			fillbg(f, Rect(qt.x, qt.y, f->r.max.x, pt.y),
				(plain && b[-1].bg!=nil)? b[-1].bg : back);
	}
	return pt;
}

void
frredraw(Frame *f)
{
	int ticked;
	Point pt;

	if(f->p0 == f->p1){
		ticked = f->ticked;
		if(ticked)
			frtick(f, frptofchar(f, f->p0), 0);
		frdrawsel0(f, frptofchar(f, 0), 0, f->nchars, f->cols[BACK], f->cols[TEXT]);
		_frfixmargin(f);
		if(ticked)
			frtick(f, frptofchar(f, f->p0), 1);
		return;
	}

	pt = frptofchar(f, 0);
	pt = frdrawsel0(f, pt, 0, f->p0, f->cols[BACK], f->cols[TEXT]);
	pt = frdrawsel0(f, pt, f->p0, f->p1, f->cols[HIGH], f->cols[HTEXT]);
	pt = frdrawsel0(f, pt, f->p1, f->nchars, f->cols[BACK], f->cols[TEXT]);
	_frfixmargin(f);
}

void
_frrepaint(Frame *f, ulong q0, ulong q1)
{
	Point pt;
	ulong p0, p1;

	if(q1 > f->nchars)
		q1 = f->nchars;
	if(q0 >= q1)
		return;
	p0 = f->p0;
	if(p0 < q0)
		p0 = q0;
	if(p0 > q1)
		p0 = q1;
	p1 = f->p1;
	if(p1 < p0)
		p1 = p0;
	if(p1 > q1)
		p1 = q1;
	pt = frptofchar(f, q0);
	pt = frdrawsel0(f, pt, q0, p0, f->cols[BACK], f->cols[TEXT]);
	pt = frdrawsel0(f, pt, p0, p1, f->cols[HIGH], f->cols[HTEXT]);
	frdrawsel0(f, pt, p1, q1, f->cols[BACK], f->cols[TEXT]);
}

void
_frfixmargin(Frame *f)
{
	Frbox *b;
	Point pt;
	Rectangle lr, rr;
	Image *tcol;
	Font *bfont;
	int nb, h, ticked;

	if((f->lmargin<=0 && f->rmargin<=0) || f->b==nil || f->noredraw)
		return;
	ticked = f->ticked;	/* the tick can lie in the left margin */
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 0);
	/* ordinary drawing never touches the margins */
	lr = f->r;
	lr.max.x = lr.min.x;
	lr.min.x -= f->lmargin;
	draw(f->b, lr, f->cols[BACK], nil, ZP);
	rr = f->r;
	rr.min.x = rr.max.x;
	rr.max.x += f->rmargin;
	draw(f->b, rr, f->cols[BACK], nil, ZP);
	f->curlineht = f->font->height;
	pt = f->r.min;
	/* overhang is in the box's own colour, even beside a selection */
	for(nb=0,b=f->box; nb<f->nbox; nb++,b++){
		_frcklinewrap(f, &pt, b);
		if(pt.y >= f->r.max.y)
			break;
		if(b->nrune > 0){
			bfont = BOXFONT(f, b);
			tcol = (b->fg!=nil && b->fg->col!=nil)? b->fg->col : f->cols[TEXT];
			h = bfont->height;
			if(pt.y+h > f->r.max.y)
				h = f->r.max.y-pt.y;
			if(pt.x < f->r.min.x+f->lmargin)
				_string(f->b, pt, tcol, ZP, bfont, (char*)b->ptr, nil, 1<<24,
					Rect(lr.min.x, pt.y, lr.max.x, pt.y+h), nil, ZP, SoverD);
			if(pt.x+b->wid+f->rmargin > f->r.max.x)
				_string(f->b, pt, tcol, ZP, bfont, (char*)b->ptr, nil, 1<<24,
					Rect(rr.min.x, pt.y, rr.max.x, pt.y+h), nil, ZP, SoverD);
		}
		_fradvance(f, &pt, b);
	}
	/* the server flushes only the area a string names, not its overhang */
	draw(f->b, lr, f->b, nil, lr.min);
	draw(f->b, rr, f->b, nil, rr.min);
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 1);
}

static void
_frtick(Frame *f, Point pt, int ticked)
{
	Rectangle r;
	int bn, ht;
	ulong cn;

	if(f->ticked==ticked || f->tick==0 || !ptinrect(pt, f->r))
		return;
	if(ticked){	/* as tall as its line; erasing uses what was saved */
		ht = Dy(_frfindline(f, pt, &bn, &cn));
		if(Dy(f->tick->r) != ht){
			_frtickalloc(f, ht);
			if(f->tick == nil)
				return;
		}
	}
	pt.x -= f->tickscale;	/* looks best just left of where requested */
	r = Rect(pt.x, pt.y, pt.x+Dx(f->tick->r), pt.y+Dy(f->tick->r));
	/* can go into left border but not right */
	if(r.max.x > f->r.max.x)
		r.max.x = f->r.max.x;
	if(r.max.y > f->r.max.y)
		r.max.y = f->r.max.y;
	if(ticked){
		draw(f->tickback, f->tickback->r, f->b, nil, pt);
		draw(f->b, r, f->tick, nil, ZP);
	}else
		draw(f->b, r, f->tickback, nil, ZP);
	f->ticked = ticked;
}

void
frtick(Frame *f, Point pt, int ticked)
{
	if(f->tickscale != scalesize(f->display, 1)) {
		if(f->ticked)
			_frtick(f, pt, 0);
		frinittick(f);
	}
	_frtick(f, pt, ticked);
}

Point
_frdraw(Frame *f, Point pt)
{
	Frbox *b;
	int nb, n;

	f->curlineht = f->font->height;
	for(b=f->box,nb=0; nb<f->nbox; nb++, b++){
		_frcklinewrap0(f, &pt, b);
		if(pt.y >= f->r.max.y){
			f->nchars -= _frstrlen(f, nb);
			_frdelbox(f, nb, f->nbox-1);
			break;
		}
		if(b->nrune > 0){
			n = _frcanfit(f, pt, b);
			if(n == 0)
				break;
			if(n != b->nrune){
				_frsplitbox(f, nb, n);
				b = &f->box[nb];
			}
			pt.x += b->wid;
		}else{
			if(b->bc != '\n')
				_frnewwid(f, pt, b);
			_fradvance(f, &pt, b);
		}
	}
	return pt;
}

int
_frstrlen(Frame *f, int nb)
{
	int n;

	for(n=0; nb<f->nbox; nb++)
		n += NRUNE(&f->box[nb]);
	return n;
}

static
int
redrawrange(Frame *f, ulong p0, ulong p1, int n0, int n1)
{
	Point pt;
	int before, ticked, nc;

	before = f->nbox;
	ticked = f->ticked;
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 0);
	_frrepaint(f, p0>0? p0-1 : 0, p1);
	/* merge boxes that now match */
	pt = _frptofcharnb(f, p0, n0);
	if(n0>0 && f->box[n0-1].nrune>=0 && pt.x-f->box[n0-1].wid>=f->r.min.x){
		--n0;
		pt.x -= f->box[n0].wid;
	}
	nc = n1<f->nbox-1? n1+1 : n1;
	_frclean(f, pt, n0, nc);
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 1);
	/* number of boxes merged away */
	return before - f->nbox;
}

static
int
remeasure(Frame *f, int n0, int n1)
{
	Frbox *b;
	int n, neww, changed;

	changed = 0;
	for(n=n0; n<n1; n++){
		b = &f->box[n];
		if(b->nrune < 0){
			if(b->bc != '\t')
				continue;
			/* a tab's minimum width is the font's space */
			neww = stringwidth(BOXFONT(f, b), " ");
			if(neww != b->minwid)
				changed = 1;
			b->minwid = neww;
			continue;
		}
		neww = stringwidth(BOXFONT(f, b), (char*)b->ptr);
		if(neww != b->wid)
			changed = 1;
		b->wid = neww;
	}
	return changed;
}

void
_frreflow(Frame *f)
{
	/* rewrap as frinsert would; chars pushed off the bottom are dropped */
	_frdraw(f, f->r.min);
	f->curlineht = f->font->height;
	_frclean(f, f->r.min, 0, f->nbox);
	if(f->p0 > f->nchars)
		f->p0 = f->nchars;
	if(f->p1 > f->nchars)
		f->p1 = f->nchars;
	f->nlines = _frcountlines(f);
	/* caller has taken the tick down */
	draw(f->b, f->r, f->cols[BACK], nil, ZP);
	frredraw(f);
}

void
frmarkupfg(Frame *f, ulong p0, ulong p1, Frmarkup *fg)
{
	Frbox *b;
	int n0, n1, n, ht, ticked, relayout;

	if(p1 > f->nchars)
		p1 = f->nchars;
	if(p0>=p1 || f->b==nil)
		return;
	ticked = f->ticked;
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 0);
	n0 = _frfindbox(f, 0, 0, p0);
	n1 = _frfindbox(f, n0, p0, p1);
	/* only the font affects width and line height */
	relayout = 0;
	for(n=n0; n<n1; n++){
		b = &f->box[n];
		ht = BOXFONT(f, b)->height;
		b->fg = fg;
		if(BOXFONT(f, b)->height != ht)
			relayout = 1;
	}
	relayout |= remeasure(f, n0, n1);
	if(relayout)
		_frreflow(f);
	else{
		redrawrange(f, p0, p1, n0, n1);
		_frfixmargin(f);
	}
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 1);
}

void
frmarkupbg(Frame *f, ulong p0, ulong p1, Image *bg)
{
	int n0, n1, n;

	if(p1 > f->nchars)
		p1 = f->nchars;
	if(p0>=p1 || f->b==nil)
		return;
	n0 = _frfindbox(f, 0, 0, p0);
	n1 = _frfindbox(f, n0, p0, p1);
	for(n=n0; n<n1; n++)
		f->box[n].bg = bg;
	redrawrange(f, p0, p1, n0, n1);
	_frfixmargin(f);
}

void
frredrawmarkup(Frame *f, Frmarkup *markup)
{
	int n0, n1, removed, ticked, widchanged;
	ulong p, p0;

	if(f->b == nil)
		return;
	ticked = f->ticked;
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 0);
	widchanged = 0;
	for(n0=0; n0<f->nbox; n0++)
		if(f->box[n0].fg==markup && remeasure(f, n0, n0+1))
			widchanged = 1;
	if(widchanged || _frvarht(f))	/* old heights are gone; can't tell */
		_frreflow(f);
	else{
		/* repaint each run of boxes tagged with markup */
		p = 0;
		n0 = 0;
		while(n0 < f->nbox){
			if(f->box[n0].fg != markup){
				p += NRUNE(&f->box[n0]);
				n0++;
				continue;
			}
			p0 = p;
			n1 = n0;
			while(n1<f->nbox && f->box[n1].fg==markup){
				p += NRUNE(&f->box[n1]);
				n1++;
			}
			removed = redrawrange(f, p0, p, n0, n1);
			n0 = n1 - removed;
		}
		_frfixmargin(f);
	}
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 1);
}
