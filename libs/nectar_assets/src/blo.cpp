#include "nectar_assets/blo.h"

#include "be_writer.h"
#include "reader.h"

namespace nectar {

namespace {

struct Writer {
	BeWriter w;
	std::string error;

	bool fail(const std::string& tag, const std::string& what)
	{
		error = "pane '" + tag + "': " + what;
		return false;
	}

	void align4()
	{
		while (w.b.size() % 4) w.u8(0);
	}

	// P2DStream::getResource: u8 refType (0), u8 largo, bytes. Largo 0 = nulo.
	bool resource(const BloPane& p, const std::string& s, const char* what)
	{
		if (s.size() > 255) return fail(p.tag, std::string(what) + " name is longer than 255 bytes");
		w.u8(0);
		w.u8(std::uint8_t(s.size()));
		w.b.insert(w.b.end(), s.begin(), s.end());
		return true;
	}

	void colour(const BloColour& c)
	{
		w.u8(c.r);
		w.u8(c.g);
		w.u8(c.b);
		w.u8(c.a);
	}

	bool s16(const BloPane& p, int v, const char* what)
	{
		if (v < -32768 || v > 32767) return fail(p.tag, std::string(what) + " does not fit a s16");
		w.u16(std::uint16_t(std::int16_t(v)));
		return true;
	}

	bool pane(const BloPane& p)
	{
		if (p.tag.size() != 4) return fail(p.tag, "tag is not 4 bytes");
		// Tag 0 is never found by P2DPane::search. Only a picture may go untagged: the disc's menu
		// glass is, and pcExtendMenu (drawMenu.cpp) stretches exactly the untagged pictures.
		if (p.tag == std::string(4, '\0') && p.type != BloType::Picture) return fail(p.tag, "tag 0 is never found by P2DPane::search");
		if (p.w <= 0 || p.h <= 0) return fail(p.tag, "empty rectangle (P2DPane::draw skips it)");
		switch (p.type) {
		case BloType::Pane:
		case BloType::Window:
		case BloType::Picture:
		case BloType::TextBox:
			break;
		default:
			return fail(p.tag, "unknown pane type");
		}

		w.u16(std::uint16_t(p.type));
		w.u8(p.visible ? 1 : 0);
		w.u8(0);
		w.b.insert(w.b.end(), p.tag.begin(), p.tag.end());
		if (!s16(p, p.x, "x") || !s16(p, p.y, "y") || !s16(p, p.w, "width") || !s16(p, p.h, "height")) return false;

		if (p.type == BloType::Picture) {
			if (!resource(p, p.texture, "texture") || !resource(p, p.palette, "palette")) return false;
			w.u8(p.binding);
			w.u8(p.flags);
			align4();
		} else if (p.type == BloType::TextBox) {
			if (p.font.empty()) return fail(p.tag, "text box without a font name (P2DFont::loadFont would crash)");
			if (p.text.size() > 32767) return fail(p.tag, "text is longer than a s16");
			if (!resource(p, p.font, "font")) return false;
			colour(p.charColour);
			colour(p.gradColour);
			w.u8(std::uint8_t(p.hBinding | 0x80));
			w.u8(p.vBinding);
			if (!s16(p, p.spacing, "spacing") || !s16(p, p.leading, "leading") || !s16(p, p.fontWidth, "font width")
			    || !s16(p, p.fontHeight, "font height"))
				return false;
			w.u16(std::uint16_t(p.text.size()));
			w.b.insert(w.b.end(), p.text.begin(), p.text.end());
			align4();
		} else if (p.type == BloType::Window) {
			if (p.texture.empty()) return fail(p.tag, "window corner without a texture name (P2DWindow::loadResource would crash)");
			for (const std::string& c : p.otherCorners)
				if (c.empty()) return fail(p.tag, "window corner without a texture name (P2DWindow::loadResource would crash)");
			if (!s16(p, p.winX, "window x") || !s16(p, p.winY, "window y") || !s16(p, p.winW, "window width")
			    || !s16(p, p.winH, "window height"))
				return false;
			if (!resource(p, p.texture, "texture")) return false;
			for (const std::string& c : p.otherCorners)
				if (!resource(p, c, "texture")) return false;
			if (!resource(p, p.palette, "palette")) return false;
			w.u8(p.flags);
			for (const BloColour& c : p.cornerColours) colour(c);
			align4();
		}

		if (!p.children.empty()) {
			w.u16(0x0001);
			w.u16(0);
			if (!list(p.children)) return false;
			w.u16(0x0002);
			w.u16(0);
		}
		return true;
	}

	bool list(const std::vector<BloPane>& panes)
	{
		for (const BloPane& p : panes)
			if (!pane(p)) return false;
		return true;
	}
};

struct Parser {
	Reader r;
	std::string error;

	explicit Parser(const std::vector<std::uint8_t>& b) : r(b.data(), b.size()) { }

	bool fail(const std::string& what)
	{
		error = what + " at byte " + std::to_string(r.pos);
		return false;
	}

	bool resource(std::string& out)
	{
		std::uint8_t ref = r.u8();
		std::uint8_t len = r.u8();
		out.clear();
		if (!len) return r.ok;
		if (ref != 0 && ref != 2) return fail("bad resource refType " + std::to_string(ref) + " (the game aborts: Screen Data is wrong)");
		for (int i = 0; i < len; i++) out.push_back(char(r.u8()));
		return r.ok;
	}

	BloColour colour()
	{
		BloColour c;
		c.r = r.u8();
		c.g = r.u8();
		c.b = r.u8();
		c.a = r.u8();
		return c;
	}

	// P2DStream::align(4): cuenta desde el principio del fichero.
	void align4()
	{
		while (r.ok && r.pos % 4) r.u8();
	}

	// makeHiearachyPanes(parent, ...): `list` son los hijos de `parent`; un
	// 0x0001 sin panel previo en la lista cuelga los hijos del propio padre.
	bool parseList(std::vector<BloPane>& list, int depth)
	{
		if (depth > 64) return fail("pane tree deeper than 64 levels");
		long cur = -1; // índice, no puntero: el vector crece
		while (true) {
			std::uint16_t type = r.u16();
			if (!r.ok) return fail("stream ends before a terminator");
			if (type == 0x0000) return true;
			if (type == 0x0001) {
				r.u16();
				if (!r.ok) return fail("stream ends inside a child marker");
				if (!parseList(cur >= 0 ? list[std::size_t(cur)].children : list, depth + 1)) return false;
				continue;
			}
			if (type == 0x0002) {
				r.u16();
				if (!r.ok) return fail("stream ends inside a child terminator");
				return true;
			}
			if (type < 0x10 || type > 0x13) return fail("unknown record type " + std::to_string(type));

			BloPane p;
			p.type    = BloType(type);
			p.visible = r.u8() != 0;
			r.u8();
			for (int i = 0; i < 4; i++) p.tag.push_back(char(r.u8()));
			p.x = std::int16_t(r.u16());
			p.y = std::int16_t(r.u16());
			p.w = std::int16_t(r.u16());
			p.h = std::int16_t(r.u16());
			if (!r.ok) return fail("stream ends inside a pane header");

			if (p.type == BloType::Picture) {
				if (!resource(p.texture) || !resource(p.palette)) return false;
				p.binding = r.u8();
				p.flags   = r.u8();
				align4();
			} else if (p.type == BloType::TextBox) {
				if (!resource(p.font)) return false;
				p.charColour = colour();
				p.gradColour = colour();
				std::uint8_t h = r.u8();
				p.vBinding     = r.u8();
				if (!(h & 0x80)) return fail("text box without the 0x80 binding bit (the game aborts: blo data is old)");
				p.hBinding   = h & 0x7F;
				p.spacing    = std::int16_t(r.u16());
				p.leading    = std::int16_t(r.u16());
				p.fontWidth  = std::int16_t(r.u16());
				p.fontHeight = std::int16_t(r.u16());
				int len      = std::int16_t(r.u16());
				if (len < 0) return fail("negative text length");
				for (int i = 0; i < len && r.ok; i++) p.text.push_back(char(r.u8()));
				align4();
			} else if (p.type == BloType::Window) {
				p.winX = std::int16_t(r.u16());
				p.winY = std::int16_t(r.u16());
				p.winW = std::int16_t(r.u16());
				p.winH = std::int16_t(r.u16());
				if (!resource(p.texture)) return false;
				for (std::string& c : p.otherCorners)
					if (!resource(c)) return false;
				if (!resource(p.palette)) return false;
				p.flags = r.u8();
				for (BloColour& c : p.cornerColours) c = colour();
				align4();
			}
			if (!r.ok) return fail("stream ends inside a pane body");
			list.push_back(std::move(p));
			cur = long(list.size()) - 1;
		}
	}
};

} // namespace

bool writeBlo(const std::vector<BloPane>& top, std::vector<std::uint8_t>& out, std::string& error)
{
	Writer wr;
	if (!wr.list(top)) {
		error = wr.error;
		return false;
	}
	wr.w.u16(0x0000);
	out = std::move(wr.w.b);
	return true;
}

bool readBlo(const std::vector<std::uint8_t>& bytes, std::vector<BloPane>& out, std::string& error, std::size_t* consumed)
{
	Parser ps(bytes);
	std::vector<BloPane> top;
	if (!ps.parseList(top, 0)) {
		error = ps.error;
		return false;
	}
	if (consumed) *consumed = ps.r.pos;
	out = std::move(top);
	return true;
}

const BloPane* findBloPane(const std::vector<BloPane>& top, const std::string& tag)
{
	for (const BloPane& p : top) {
		if (p.tag == tag) return &p;
		if (const BloPane* c = findBloPane(p.children, tag)) return c;
	}
	return nullptr;
}

} // namespace nectar
