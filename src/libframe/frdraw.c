#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include <frame.h>

/* clipped one-line background fill */
static
void
fillbg(Frame *f, Point p0, int x1, Image *col)
{
	if(x1 > f->r.max.x)
		x1 = f->r.max.x;
	if(x1 <= p0.x)
		return;
	draw(f->b, Rect(p0.x, p0.y, x1, p0.y+f->font->height), col, nil, p0);
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
	for(nb=0,b=f->box; nb<f->nbox; nb++, b++){
		bcol = (plain && b->bg!=nil)? b->bg : back;
		qt = pt;
		_frcklinewrap(f, &pt, b);
		if(!f->noredraw){
			/* wrapped line: fill with prior box's bg */
			if(pt.y > qt.y)
				fillbg(f, qt, f->r.max.x, pbcol);
			if(b->nrune >= 0){
				tcol = (plain && b->fg!=nil && b->fg->col!=nil)? b->fg->col : text;
				bfont = BOXFONT(f, b);
				_string(f->b, pt, tcol, ZP, bfont, (char*)b->ptr, nil, 1<<24, f->r, bcol, ZP, SoverD);
			}else
				fillbg(f, pt, pt.x+b->wid, bcol);	/* tab/newline box */
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

/*
 * Redrawing a glyph over its own ink darkens its antialiased edges, so
 * clip at r.min.x, the start of the fill, and redraw the overhang of the
 * char after p1, which the fill erased.
 * Per-box colors apply only to the unselected colors, so a selection
 * always paints solid.
 */
Point
frdrawsel0(Frame *f, Point pt, ulong p0, ulong p1, Image *back, Image *text)
{
	Frbox *b, *bx;
	int nb, nr, w, trim, plain;
	Point qt;
	uint p;
	char *ptr;
	Rectangle r;
	Image *tcol, *bcol;
	Font *bfont;

	if(p0 > p1)
		sysfatal("libframe: frdrawsel0 p0=%lud > p1=%lud", p0, p1);

	plain = back==f->cols[BACK] && text==f->cols[TEXT];
	p = 0;
	b = f->box;
	trim = 0;
	r = f->r;
	r.min.x = pt.x;
	for(nb=0; nb<f->nbox && p<p1; nb++){
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
				fillbg(f, qt, f->r.max.x,
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
		fillbg(f, pt, pt.x+w, bcol);
		if(b->nrune >= 0){
			tcol = (plain && b->fg!=nil && b->fg->col!=nil)? b->fg->col : text;
			_string(f->b, pt, tcol, ZP, bfont, ptr, nil, nr, r, bcol, ZP, SoverD);
		}
		pt.x += w;
	    Continue:
		b++;
		p += nr;
	}
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
			r.max.x = pt.x;
			r.min.y = pt.y;
			r.max.y = pt.y+f->font->height;
			bfont = BOXFONT(f, bx);
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
		_frcklinewrap(f, &pt, b);
		if(pt.y > qt.y)
			fillbg(f, qt, f->r.max.x,
				(plain && b[-1].bg!=nil)? b[-1].bg : back);
	}
	return pt;
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

/*
 * Redraw q0 to q1 as frredraw would.  frinsert and frdelete move boxes by
 * copying pixels, which also copies any overhang from the old neighbours.
 * They start at the char before the change, since the overhang of a glyph
 * that was removed or moved lies in the cell to its left.
 */
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

/*
 * Glyphs are drawn clipped to f->r, so the margins are never disturbed by
 * ordinary drawing.  Erase them, then draw the overhang of the boxes at the
 * ends of rows into them.  The ink is in the box's own text colour, never
 * the selection colour.
 */
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
	lr = f->r;
	lr.max.x = lr.min.x;
	lr.min.x -= f->lmargin;
	draw(f->b, lr, f->cols[BACK], nil, ZP);
	rr = f->r;
	rr.min.x = rr.max.x;
	rr.max.x += f->rmargin;
	draw(f->b, rr, f->cols[BACK], nil, ZP);
	h = f->font->height;
	pt = f->r.min;
	for(nb=0,b=f->box; nb<f->nbox; nb++,b++){
		_frcklinewrap(f, &pt, b);
		if(pt.y >= f->r.max.y)
			break;
		if(b->nrune > 0){
			bfont = BOXFONT(f, b);
			tcol = (b->fg!=nil && b->fg->col!=nil)? b->fg->col : f->cols[TEXT];
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

	if(f->ticked==ticked || f->tick==0 || !ptinrect(pt, f->r))
		return;
	pt.x -= f->tickscale;	/* looks best just left of where requested */
	r = Rect(pt.x, pt.y, pt.x+FRTICKW*f->tickscale, pt.y+f->font->height);
	/* can go into left border but not right */
	if(r.max.x > f->r.max.x)
		r.max.x = f->r.max.x;
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

	for(b=f->box,nb=0; nb<f->nbox; nb++, b++){
		_frcklinewrap0(f, &pt, b);
		if(pt.y == f->r.max.y){
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
			if(b->bc == '\n'){
				pt.x = f->r.min.x;
				pt.y+=f->font->height;
			}else
				pt.x += _frnewwid(f, pt, b);
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

/* repaint p0-p1, merge boxes that now match; returns # merged, all within n0-n1 */
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
	pt = _frptofcharnb(f, p0, n0);
	if(n0>0 && f->box[n0-1].nrune>=0 && pt.x-f->box[n0-1].wid>=f->r.min.x){
		--n0;
		pt.x -= f->box[n0].wid;
	}
	nc = n1<f->nbox-1? n1+1 : n1;
	_frclean(f, pt, n0, nc);
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 1);
	return before - f->nbox;
}

/* refresh cached widths of boxes n0 to n1; returns 1 if any changed */
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

/*
 * Box widths changed: rewrap from the top as frinsert would and repaint.
 * Chars pushed off the bottom are dropped; check f->nchars afterwards.
 * The caller has taken the tick down.
 */
static
void
reflow(Frame *f)
{
	Point pt;

	_frdraw(f, f->r.min);
	_frclean(f, f->r.min, 0, f->nbox);
	if(f->p0 > f->nchars)
		f->p0 = f->nchars;
	if(f->p1 > f->nchars)
		f->p1 = f->nchars;
	pt = frptofchar(f, f->nchars);
	f->nlines = (pt.y-f->r.min.y)/f->font->height+(pt.x>f->r.min.x);
	draw(f->b, f->r, f->cols[BACK], nil, ZP);
	frredraw(f);
}

/* only font affects width, not color */
void
frmarkupfg(Frame *f, ulong p0, ulong p1, Frmarkup *fg)
{
	int n0, n1, n, ticked;

	if(p1 > f->nchars)
		p1 = f->nchars;
	if(p0>=p1 || f->b==nil)
		return;
	ticked = f->ticked;
	if(ticked)
		frtick(f, frptofchar(f, f->p0), 0);
	n0 = _frfindbox(f, 0, 0, p0);
	n1 = _frfindbox(f, n0, p0, p1);
	for(n=n0; n<n1; n++)
		f->box[n].fg = fg;
	if(remeasure(f, n0, n1))
		reflow(f);
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

/* redraw every box tagged with markup, after its font or color changed */
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
	if(widchanged)
		reflow(f);
	else{
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
