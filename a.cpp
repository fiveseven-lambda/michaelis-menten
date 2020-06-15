#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <unistd.h>
#include <cstdio>
#include <cmath>
#include <vector>

Display *display;
Window window;
Pixmap buffer;
int width, height;
GC winBlackGC, winWhiteGC, bufBlackGC, bufWhiteGC;

template<class T>
struct Vector{
	T x, y;
	Vector(){}
	Vector(const T &x, const T &y): x(x), y(y) {}
	Vector &operator+=(const Vector &v){ x += v.x; y += v.y; return *this; }
	Vector &operator-=(const Vector &v){ x -= v.x; y -= v.y; return *this; }
	Vector &operator*=(const T &k){ x *= k; y *= k; return *this; }
	Vector &operator/=(const T &k){ x /= k; y /= k; return *this; }
	friend Vector operator+(Vector l, const Vector &r){ return l += r; }
	friend Vector operator-(Vector l, const Vector &r){ return l -= r; }
	friend Vector operator*(const T &k, Vector v){ return v *= k; }
	T abs(){ return std::hypot<T>(x, y); }
};

struct Arc{
	short x, y;
	unsigned short r;
	Arc(const short &x, const short &y, const unsigned short &r): x(x), y(y), r(r) {}
	void draw(Display *display, Drawable d, GC gc){
		XDrawArc(display, d, gc, x - r, y - r, r * 2, r * 2, 0, 360 * 64);
	}
	void fill(Display *display, Drawable d, GC gc){
		XFillArc(display, d, gc, x - r, y - r, r * 2, r * 2, 0, 360 * 64);
	}
};

std::vector<XPoint> bezier(Vector<double> p1, Vector<double> p2, Vector<double> p3, double w){
	p1.x *= width;
	p1.y *= height;
	p2.x *= width;
	p2.y *= height;
	p3.x *= width;
	p3.y *= height;
	p1 -= p3;
	p2 -= p3;
	int n = std::max({p1.x - p2.x, p2.x - p1.x, p1.y - p2.y, p2.y - p1.y});
	std::vector<XPoint> ret(n * 2);
	for(int i = 0; i < n; ++i){
		double t = static_cast<double>(i) / (n - 1);
		Vector<double> p = (1 - t) * (1 - t) * p1 + t * t * p2;
		Vector<double> v = (t - 1) * p1 + t * p2;
		v *= w / v.abs();
		ret[i].x = p3.x + p.x - v.y;
		ret[i].y = p3.y + p.y + v.x;
		ret[n * 2 - i - 1].x = p3.x + p.x + v.y;
		ret[n * 2 - i - 1].y = p3.y + p.y - v.x;
	}
	return ret;
}

bool in_polygon(const Vector<short> &p, const std::vector<XPoint> &v){
	bool ret = false;
	for(std::size_t i = 0; i < v.size(); ++i){
		std::size_t j = i + 1;
		if(j == v.size()) j = 0;
		bool tmp = (p.x - v[i].x) * (v[j].y - v[i].y) < (p.y - v[i].y) * (v[j].x - v[i].x);
		if(v[i].y <= p.y && v[j].y > p.y){
			ret ^= tmp;
		}else if(v[i].y > p.y && v[j].y <= p.y){
			ret ^= !tmp;
		}
	}
	return ret;
}

namespace object{
	double scale = 10000;

	namespace node{
		struct Node{
			double am, am_set;
			Vector<double> p;
			Node(const double &am, const Vector<double> &p) : am(am), p(p) {}
			Arc arc(){
				return Arc(p.x * width, p.y * height, am * scale);
			}
		};
		std::vector<Node> nodes;
		std::size_t focused;
		bool moving;
		Vector<short> origin;

		void find(const Vector<short> &);
	}

	namespace edge{
		struct Edge{
			std::vector<std::size_t> from, to;
			double k;
			Vector<double> mid, pre, post;
			Edge(std::size_t from, std::size_t to, double k, Vector<double> mid, Vector<double> pre, Vector<double> post):
				from(1, from), to(1, to), k(k), mid(mid), pre(pre), post(post) {}
		};

		std::vector<Edge> edges;

		std::size_t focused;

		namespace drawing{
			std::size_t from;
			std::size_t from_edge;
			std::vector<XPoint> points;
			std::vector<double> len;
		}

		void find(const Vector<short> &);
	}

	void node::find(const Vector<short> &p){
		focused = nodes.size();
		double min = std::numeric_limits<double>::max();
		for(std::size_t i = 0; i < nodes.size(); ++i){
			Arc arc = nodes[i].arc();
			if((arc.x - p.x) * (arc.x - p.x) + (arc.y - p.y) * (arc.y - p.y) < arc.r * arc.r && min > nodes[i].am){
				focused = i;
				min = nodes[i].am;
			}
		}
		edge::focused = edge::edges.size();
	}
	void edge::find(const Vector<short> &p){
		focused = edges.size();
		double min = std::numeric_limits<double>::max();
		for(std::size_t i = 0; i < edges.size(); ++i){
			if(min > edges[i].k && [&]() -> bool {
				for(std::size_t j : edges[i].from){
					if(in_polygon(p, bezier(edges[i].mid, node::nodes[j].p, edges[i].pre, edges[i].k * scale))){
						return true;
					}
				}
				for(std::size_t j : edges[i].to){
					if(in_polygon(p, bezier(edges[i].mid, node::nodes[j].p, edges[i].post, edges[i].k * scale))){
						return true;
					}
				}
				return false;
			}()){
				focused = i;
				min = edges[i].k;
			}
		}
		node::focused = node::nodes.size();
	}

	void draw_all(){
		XFillRectangle(display, buffer, bufWhiteGC, 0, 0, width, height);
		for(edge::Edge &i : edge::edges){
			for(std::size_t j : i.from){
				std::vector<XPoint> vertices = bezier(i.mid, node::nodes[j].p, i.pre, i.k * scale);
				XFillPolygon(display, buffer, bufBlackGC, &vertices.front(), vertices.size(), Complex, CoordModeOrigin);
			}
			for(std::size_t j : i.to){
				std::vector<XPoint> vertices = bezier(i.mid, node::nodes[j].p, i.post, i.k * scale);
				XFillPolygon(display, buffer, bufBlackGC, &vertices.front(), vertices.size(), Complex, CoordModeOrigin);
			}
		}
		for(node::Node &i : node::nodes){
			Arc arc = i.arc();
			arc.fill(display, buffer, bufWhiteGC);
			arc.draw(display, buffer, bufBlackGC);
		}
		if(node::focused < node::nodes.size()){
			Arc arc = node::nodes[node::focused].arc();
			XDrawRectangle(display, buffer, bufBlackGC, arc.x - arc.r, arc.y - arc.r, arc.r * 2, arc.r * 2);
			arc.fill(display, buffer, bufWhiteGC);
			arc.draw(display, buffer, bufBlackGC);
		}
		if(edge::focused < edge::edges.size()){
			Arc mid(edge::edges[edge::focused].mid.x * width, edge::edges[edge::focused].mid.y * height, 5);
			Arc pre(edge::edges[edge::focused].pre.x * width, edge::edges[edge::focused].pre.y * height, 3);
			Arc post(edge::edges[edge::focused].post.x * width, edge::edges[edge::focused].post.y * height, 3);
			mid.fill(display, buffer, bufWhiteGC);
			mid.draw(display, buffer, bufBlackGC);
			pre.fill(display, buffer, bufWhiteGC);
			pre.draw(display, buffer, bufBlackGC);
			post.fill(display, buffer, bufWhiteGC);
			post.draw(display, buffer, bufBlackGC);
		}
		if(!edge::drawing::points.empty()){
			XDrawLines(display, buffer, bufBlackGC, &edge::drawing::points.front(), edge::drawing::points.size(), CoordModeOrigin);
		}
		XCopyArea(display, buffer, window, winBlackGC, 0, 0, width, height, 0, 0);
	}
};

int main(){
	display = XOpenDisplay(NULL);
	if(display == NULL){
		fputs("error: cannot connect to X server\n", stderr);
		return -1;
	}
	int screen = XDefaultScreen(display);
	Window root = XRootWindow(display, screen);
	width = XDisplayWidth(display, screen);
	height = XDisplayHeight(display, screen);
	unsigned long black = XBlackPixel(display, screen), white = XWhitePixel(display, screen);

	window = XCreateSimpleWindow(display, root, 0, 0, width, height, 1, black, white);

	int depth = XDefaultDepth(display, screen);
	buffer = XCreatePixmap(display, window, width, height, depth);

	XGCValues gcValues;
	gcValues.foreground = black;
	winBlackGC = XCreateGC(display, window, GCForeground, &gcValues);
	bufBlackGC = XCreateGC(display, buffer, GCForeground, &gcValues);
	gcValues.foreground = white;
	winWhiteGC = XCreateGC(display, window, GCForeground, &gcValues);
	bufWhiteGC = XCreateGC(display, buffer, GCForeground, &gcValues);

	constexpr long set_mask = ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
	constexpr long run_mask = StructureNotifyMask | KeyPressMask;

	XSelectInput(display, window, set_mask);
	XMapWindow(display, window);

set:
	for(;;){
		XEvent event;
		XNextEvent(display, &event);
		switch(event.type){
			case Expose:
				XCopyArea(
					display, buffer, window, winBlackGC,
					event.xexpose.x, event.xexpose.y, event.xexpose.width, event.xexpose.height, event.xexpose.x, event.xexpose.y
				);
				continue;
			case ConfigureNotify:
				width = event.xconfigure.width;
				height = event.xconfigure.height;
				XFreePixmap(display, buffer);
				buffer = XCreatePixmap(display, window, width, height, depth);
				break;
			case DestroyNotify:
				XCloseDisplay(display);
				return 0;
			case ButtonPress:
				switch(event.xbutton.button){
					case Button1:
						object::node::find(Vector<short>(event.xbutton.x, event.xbutton.y));
						if(object::node::focused == object::node::nodes.size()){
							object::edge::find(Vector<short>(event.xbutton.x, event.xbutton.y));
							if(object::edge::focused < object::edge::edges.size()){
								object::edge::drawing::from = -1;
								object::edge::drawing::from_edge = object::edge::focused;
								object::edge::drawing::points.push_back(XPoint(event.xbutton.x, event.xbutton.y));
							}
						}else{
							object::edge::drawing::from = object::node::focused;
							object::edge::drawing::points.push_back(XPoint(event.xbutton.x, event.xbutton.y));
						}
						break;
					case Button3:
						object::node::find(Vector<short>(event.xbutton.x, event.xbutton.y));
						if(object::node::focused == object::node::nodes.size()){
							object::node::nodes.push_back(object::node::Node(
								.001,
								Vector<double>(
									static_cast<double>(event.xbutton.x) / width,
									static_cast<double>(event.xbutton.y) / height
								)
							));
							object::node::origin.x = object::node::origin.y = 0;
						}else{
							Arc arc = object::node::nodes[object::node::focused].arc();
							object::node::origin.x = arc.x - event.xbutton.x;
							object::node::origin.y = arc.y - event.xbutton.y;
						}
						object::node::moving = true;
						break;
					case Button4:
						if(object::node::focused < object::node::nodes.size()){
							object::node::nodes[object::node::focused].am += .0001;
						}else if(object::edge::focused < object::edge::edges.size()){
							object::edge::edges[object::edge::focused].k += .0001;
						}
						break;
					case Button5:
						if(object::node::focused < object::node::nodes.size()){
							object::node::nodes[object::node::focused].am -= .0001;
							if(object::node::nodes[object::node::focused].am < 0){
								object::node::nodes[object::node::focused].am = 0;
							}
						}else if(object::edge::focused < object::edge::edges.size()){
							object::edge::edges[object::edge::focused].k -= .0001;
							if(object::edge::edges[object::edge::focused].k < 0){
								object::edge::edges[object::edge::focused].k = 0;
							}
						}
				}
				break;
			case ButtonRelease:
				if(object::node::moving){
					object::node::moving = false;
				}else if(!object::edge::drawing::points.empty()){
					object::node::find(Vector<short>(event.xbutton.x, event.xbutton.y));
					if(object::node::focused == object::node::nodes.size()){
						object::edge::find(Vector<short>(event.xbutton.x, event.xbutton.y));
						if(object::edge::focused < object::edge::edges.size()){
							if(object::edge::drawing::from != -1){
								object::edge::edges[object::edge::focused].from.push_back(object::edge::drawing::from);
							}
						}
					}else{
						if(object::edge::drawing::from == -1){
							object::edge::edges[object::edge::drawing::from_edge].to.push_back(object::node::focused);
						}else if(object::edge::drawing::from != object::node::focused){
							std::size_t i = std::distance(
								object::edge::drawing::len.begin(),
								std::lower_bound(object::edge::drawing::len.begin(), object::edge::drawing::len.end(),
								object::edge::drawing::len.back() / 2)
							);
							Vector<double> line =
								object::node::nodes[object::node::focused].p - object::node::nodes[object::edge::drawing::from].p;
							line *= .2;
							Vector<double> mid(
								static_cast<double>(object::edge::drawing::points[i].x) / width,
								static_cast<double>(object::edge::drawing::points[i].y) / height
							);
							object::edge::focused = object::edge::edges.size();
							object::edge::edges.push_back(
								object::edge::Edge(
									object::edge::drawing::from, object::node::focused, .001, mid, mid - line, mid + line
								)
							);
							object::node::focused = object::node::nodes.size();
						}
					}
					object::edge::drawing::points.clear();
					object::edge::drawing::len.clear();
				}
				break;
			case MotionNotify:
				if(object::node::moving){
					object::node::nodes[object::node::focused].p = Vector<double>(
						static_cast<double>(object::node::origin.x + event.xmotion.x) / width,
						static_cast<double>(object::node::origin.y + event.xmotion.y) / height
					);
				}else if(!object::edge::drawing::points.empty()){
					double x = object::edge::drawing::points.back().x;
					double y = object::edge::drawing::points.back().y;
					object::edge::drawing::points.push_back(XPoint{static_cast<short>(event.xmotion.x), static_cast<short>(event.xmotion.y)});
					double prev = object::edge::drawing::len.empty() ? 0 : object::edge::drawing::len.back();
					object::edge::drawing::len.push_back(prev + hypot(x - event.xmotion.x, y - event.xmotion.y));
				}
				break;
			case KeyPress:
				switch(XLookupKeysym(&event.xkey, 0)){
					case XK_Return:
						XSelectInput(display, window, run_mask);
						goto run;
				}
			default:
				continue;
		}
		object::draw_all();
	}
run:
	for(object::node::Node &i : object::node::nodes) i.am_set = i.am;
	for(;;){
		if(XPending(display)){
			XEvent event;
			XNextEvent(display, &event);
			switch(event.type){
				case DestroyNotify:
					XCloseDisplay(display);
					return 0;
				case ConfigureNotify:
					width = event.xconfigure.width;
					height = event.xconfigure.height;
					break;
				case KeyPress:
					switch(XLookupKeysym(&event.xkey, 0)){
						case XK_Return:
							for(object::node::Node &i : object::node::nodes) i.am = i.am_set;
							XSelectInput(display, window, set_mask);
							XCopyArea(display, buffer, window, winBlackGC, 0, 0, width, height, 0, 0);
							object::draw_all();
							XFlush(display);
							goto set;
					}
			}
		}else{
			for(object::edge::Edge &i : object::edge::edges){
				double tmp = i.k / object::scale;
				for(std::size_t j : i.from) tmp *= object::node::nodes[j].am * object::scale;
				for(std::size_t j : i.from) object::node::nodes[j].am -= tmp;
				for(std::size_t j : i.to) object::node::nodes[j].am += tmp;
			}
			for(std::size_t i = 0; i < object::node::nodes.size(); ++i){
				printf("%f%c", object::node::nodes[i].am, ",\n"[i == object::node::nodes.size() - 1]);
			}
			object::draw_all();
			XFlush(display);
			usleep(1000);
		}
	}
}
