#include "box2f.h"
#include <sstream>

namespace zylann {

StdStringStream &operator<<(StdStringStream &ss, const Box2f &box) {
	ss << "(o:";
	ss << box.position;
	ss << ", s:";
	ss << box.size;
	ss << ")";
	return ss;
}

} // namespace zylann
