#include <cstddef>
#include <iostream>
#include <limits>
#include <cstring>
#include <cstdint>
#include <exception>
#include <vector>
#include <string>

namespace nbon {

class LogicError: public std::exception {
public:
	const char *what() const noexcept override {
		return "NBON logic error";
	}
};

class ParseError: public std::exception {
public:
	ParseError(const char *str) {
		str_ = "NBON parse error: ";
		str_ += str;
	}

	const char *what() const noexcept override {
		return str_.c_str();
	}

private:
	std::string str_;
};

class Writer;

class ObjectWriter {
public:
	explicit ObjectWriter(std::ostream *os): os_(os) {}

	Writer key(const char *key);

private:
	std::ostream *os_;
};

class Writer {
public:
	Writer() = default;
	explicit Writer(std::ostream *os): os_(os) {}

	void writeTrue() {
		checkReady();

		*os_ << 'T';
	}

	void writeFalse() {
		checkReady();

		*os_ << 'F';
	}

	void writeBool(bool b) {
		checkReady();

		b ? writeTrue() : writeFalse();
	}

	void writeNull() {
		checkReady();

		*os_ << 'N';
	}

	void writeString(const char *str) {
		checkReady();

		*os_ << 'S' << str << '\0';
	}

	void writeFloat(float f) {
		checkReady();

		static_assert(sizeof(float) == 4);
		static_assert(sizeof(std::uint32_t) == 4);
		std::uint32_t n;
		std::memcpy(&n, &f, 4);

		*os_ << 'f';
		*os_ << (char)((n & 0x000000ffu) >> 0);
		*os_ << (char)((n & 0x0000ff00u) >> 8);
		*os_ << (char)((n & 0x00ff0000u) >> 16);
		*os_ << (char)((n & 0xff000000u) >> 24);
	}

	void writeDouble(double d) {
		checkReady();

		static_assert(sizeof(double) == 8);
		static_assert(sizeof(std::uint64_t) == 8);
		std::uint64_t n;
		std::memcpy(&n, &d, 8);

		*os_ << 'd';
		*os_ << (char)((n & 0x00000000000000ffull) >> 0);
		*os_ << (char)((n & 0x000000000000ff00ull) >> 8);
		*os_ << (char)((n & 0x0000000000ff0000ull) >> 16);
		*os_ << (char)((n & 0x00000000ff000000ull) >> 24);
		*os_ << (char)((n & 0x000000ff00000000ull) >> 32);
		*os_ << (char)((n & 0x0000ff0000000000ull) >> 40);
		*os_ << (char)((n & 0x00ff000000000000ull) >> 48);
		*os_ << (char)((n & 0xff00000000000000ull) >> 56);
	}

	void writeBinary(const void *data, std::size_t length) {
		checkReady();

		*os_ << 'B';
		writeLEB128((uint64_t)length);
		os_->write((const char *)data, length);
	}

	void writeInt(int64_t num) {
		checkReady();

		if (num == std::numeric_limits<int64_t>::min()) {
			*os_ << '-';
			writeLEB128((uint64_t)std::numeric_limits<int64_t>::max() + (uint64_t)1);
		} else if (num < 0) {
			*os_ << '-';
			writeLEB128(-num);
		} else if (num <= 9) {
			*os_ << (char)('0' + num);
		} else {
			*os_ << '+';
			writeLEB128(num);
		}
	}

	void writeUInt(uint64_t num) {
		checkReady();

		if (num <= 9) {
			*os_ << (char)('0' + num);
		} else {
			*os_ << '+';
			writeLEB128(num);
		}
	}

	template<typename Func>
	void writeArray(Func func) {
		checkReady();

		*os_ << '[';
		ready_ = false;
		func(Writer(os_));
		ready_ = true;
		*os_ << ']';
	}

	template<typename Func>
	void writeObject(Func func) {
		checkReady();

		*os_ << '{';
		ready_ = false;
		func(ObjectWriter(os_));
		ready_ = true;
		*os_ << '}';
	}

private:
	void writeLEB128(uint64_t num) {
		do {
			unsigned char hi = (unsigned char)(num > 0x7f ? 0x80 : 0);
			*os_ << (char)(hi | (unsigned char)(num & 0x7f));
			num >>= 7;
		} while (num != 0);
	}

	void checkReady() {
		if (!ready_) {
			throw LogicError();
		}
	}

	std::ostream *os_;
	bool ready_ = true;
};

inline Writer ObjectWriter::key(const char *key) {
	*os_ << key;
	*os_ << '\0';
	return Writer(os_);
}

enum class Type {
	BOOL,
	NIL,
	STRING,
	BINARY,
	FLOAT,
	DOUBLE,
	INT,
	UINT,
	ARRAY,
	OBJECT,
};

class Reader;

class ObjectReader {
public:
	explicit ObjectReader(std::istream *os): is_(os) {}

	bool hasNext();
	Reader next(std::string &key);

	template<typename Func>
	void all(Func func);

private:
	std::istream *is_;
};

class ArrayReader {
public:
	explicit ArrayReader(std::istream *os): is_(os) {}

	bool hasNext();
	Reader next();

	template<typename Func>
	void all(Func func);

private:
	std::istream *is_;
};

class Reader {
public:
	Reader() = default;
	explicit Reader(std::istream *is): is_(is) {}

	bool hasNext() {
		return is_->peek() != EOF;
	}

	Type getType() {
		checkReady();

		int ch = is_->peek();
		if (ch == EOF) {
			throw ParseError("Unexpected EOF");
		}

		if (ch == 'T' || ch == 'F') {
			return Type::BOOL;
		} else if (ch == 'N') {
			return Type::NIL;
		} else if (ch == 'f') {
			return Type::FLOAT;
		} else if (ch == 'd') {
			return Type::DOUBLE;
		} else if (ch == 'S') {
			return Type::STRING;
		} else if (ch == 'B') {
			return Type::BINARY;
		} else if (ch == '+' || (ch >= '0' && ch <= '9')) {
			return Type::UINT;
		} else if (ch == '-') {
			return Type::INT;
		} else if (ch == '[') {
			return Type::ARRAY;
		} else if (ch == '{') {
			return Type::OBJECT;
		} else {
			throw ParseError("Unexpected character");
		}
	}

	bool getBool() {
		checkReady();

		int ch = is_->get();
		if (ch == 'T') {
			return true;
		} else if (ch == 'F') {
			return false;
		} else {
			throw ParseError("getBool: Expected 'T' or 'F'");
		}
	}

	void getNil() {
		checkReady();

		if (is_->get() != 'N') {
			throw ParseError("skipNil: Expected 'N'");
		}
	}

	void getString(std::string &s) {
		checkReady();

		if (is_->get() != 'S') {
			throw ParseError("getString: Expected 'S'");
		}

		s.clear();
		int ch;
		while ((ch = next())) {
			s += ch;
		}
	}

	std::string getString() {
		std::string str;
		getString(str);
		return str;
	}

	void skipString() {
		checkReady();

		if (is_->get() != 'S') {
			throw ParseError("skipString: Expected 'S'");
		}

		while (next());
	}

	void getBinary(std::vector<unsigned char> &bin) {
		checkReady();

		if (is_->get() != 'B') {
			throw ParseError("getString: Expected 'B'");
		}

		size_t size = (size_t)nextLEB128();

		bin.clear();
		while (bin.size() < size) {
			bin.push_back(next());
		}
	}

	std::vector<unsigned char> getBinary() {
		std::vector<unsigned char> bin;
		getBinary(bin);
		return bin;
	}

	void skipBinary() {
		checkReady();

		if (is_->get() != 'B') {
			throw ParseError("skipBinary: Expected 'B'");
		}

		size_t size = (size_t)nextLEB128();
		while (size > 0) {
			next();
			size -= 1;
		}
	}

	float getFloat() {
		checkReady();

		static_assert(sizeof(float) == 4);
		static_assert(sizeof(std::uint32_t) == 4);

		if (is_->get() != 'f') {
			throw ParseError("nextFloat: Expected 'f'");
		}

		uint32_t n = 0;
		n |= (uint32_t)(unsigned char)next() << 0;
		n |= (uint32_t)(unsigned char)next() << 8;
		n |= (uint32_t)(unsigned char)next() << 16;
		n |= (uint32_t)(unsigned char)next() << 24;

		float f;
		std::memcpy(&f, &n, 4);
		return f;
	}

	double getDouble() {
		checkReady();

		static_assert(sizeof(double) == 8);
		static_assert(sizeof(std::uint64_t) == 8);

		if (is_->get() != 'd') {
			throw ParseError("nextDouble: Expected 'd'");
		}

		uint64_t n = 0;
		n |= (uint64_t)(unsigned char)next() << 0;
		n |= (uint64_t)(unsigned char)next() << 8;
		n |= (uint64_t)(unsigned char)next() << 16;
		n |= (uint64_t)(unsigned char)next() << 24;
		n |= (uint64_t)(unsigned char)next() << 32;
		n |= (uint64_t)(unsigned char)next() << 40;
		n |= (uint64_t)(unsigned char)next() << 48;
		n |= (uint64_t)(unsigned char)next() << 56;

		double d;
		std::memcpy(&d, &n, 8);
		return d;
	}

	int64_t getInt() {
		checkReady();

		char ch = next();
		if (ch >= '0' && ch <= '9') {
			return ch - '0';
		} else if (ch == '+') {
			return nextLEB128();
		} else if (ch == '-') {
			return -nextLEB128();
		} else {
			throw ParseError("getInt: Expected '0'-'9', '+' or '-'");
		}
	}

	uint64_t getUInt() {
		checkReady();

		char ch = next();
		if (ch >= '0' && ch <= '9') {
			return ch - '0';
		} else if (ch == '+') {
			return nextLEB128();
		} else {
			throw ParseError("getInt: Expected '0'-'9' or '+'");
		}
	}

	template<typename Func>
	void getArray(Func func) {
		checkReady();

		char ch = next();
		if (ch != '[') {
			throw ParseError("getArray: Expected '['");
		}

		ready_ = false;
		ArrayReader arr(is_);
		func(arr);
		ready_ = true;

		ch = next();
		if (ch != ']') {
			throw ParseError("getArray: Expected ']'");
		}
	}

	template<typename Func>
	void readArray(Func func) {
		getArray([&](ArrayReader arr) {
			arr.all(func);
		});
	}

	template<typename Func>
	void getObject(Func func) {
		checkReady();

		char ch = next();
		if (ch != '{') {
			throw ParseError("getObject: Expected '{'");
		}

		ready_ = false;
		ObjectReader obj(is_);
		func(obj);
		ready_ = true;

		ch = next();
		if (ch != '}') {
			throw ParseError("getObject: Expected '}'");
		}
	}

	template<typename Func>
	void readObject(Func func) {
		getObject([&](ObjectReader obj) {
			obj.all(func);
		});
	}

	void skip() {
		switch (getType()) {
		case Type::BOOL:
			getBool();
			break;
		case Type::NIL:
			getNil();
			break;
		case Type::STRING:
			skipString();
			break;
		case Type::BINARY:
			skipBinary();
			break;
		case Type::FLOAT:
			getFloat();
			break;
		case Type::DOUBLE:
			getDouble();
			break;
		case Type::INT:
			getInt();
			break;
		case Type::UINT:
			getUInt();
			break;
		case Type::ARRAY:
			readArray([](Reader r) {
				r.skip();
			});
			break;
		case Type::OBJECT:
			readObject([](std::string &, Reader r) {
				r.skip();
			});
			break;
		}
	}

private:
	char next() {
		int ch = is_->get();
		if (ch == EOF) {
			throw ParseError("Unexpected EOF");
		}

		return (char)ch;
	}

	uint64_t nextLEB128() {
		uint64_t num = 0;
		uint64_t shift = 0;
		unsigned char ch;
		do {
			ch = (unsigned char)next();
			num |= (uint64_t)(ch & 0x7f) << shift;
			shift += 7;
		} while (ch >= 0x80);
		return num;
	}

	void checkReady() {
		if (!ready_) {
			throw LogicError();
		}
	}

	std::istream *is_;
	bool ready_ = true;
};

inline bool ArrayReader::hasNext() {
	int ret = is_->peek();
	return ret != ']' && ret != EOF;
}

inline Reader ArrayReader::next() {
	return Reader(is_);
}

template<typename Func>
inline void ArrayReader::all(Func func) {
	while (hasNext()) {
		auto val = next();
		func(val);
	}
}

inline bool ObjectReader::hasNext() {
	int ret = is_->peek();
	return ret != '}' && ret != EOF;
}

inline Reader ObjectReader::next(std::string &key) {
	key.clear();
	while (true) {
		int ch = is_->get();
		if (ch == EOF) {
			throw ParseError("ObjectReader::next: Unexpected EOF");
		} else if (ch == 0) {
			break;
		}

		key += (char)ch;
	}

	return Reader(is_);
}

template<typename Func>
inline void ObjectReader::all(Func func) {
	std::string key;
	while (hasNext()) {
		auto val = next(key);
		func(key, val);
	}
}

}
