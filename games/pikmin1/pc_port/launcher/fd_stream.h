#ifndef PIKMIN_LAUNCHER_FD_STREAM_H
#define PIKMIN_LAUNCHER_FD_STREAM_H

// Lectura de una imagen de disco por descriptor de fichero (Android). La usan
// el instalador JNI y el launcher de Android.

#include <cstdint>
#include <istream>
#include <streambuf>
#include <sys/stat.h>
#include <unistd.h>

namespace pikmin {
namespace launcher {

// std::istream sobre un descriptor de fichero. El selector de archivos
// (Storage Access Framework) entrega un descriptor que no se puede reabrir
// por ruta (/proc/self/fd/N falla con "Could not open the disc image"), así
// que el extractor lee de él a través de este búfer: pread() posicionado,
// sin estado compartido con nadie.
class FdStreamBuf : public std::streambuf {
public:
	explicit FdStreamBuf(int fd) : mFd(fd) { setg(mBuffer, mBuffer, mBuffer); }

	std::uint64_t size() const
	{
		struct stat st;
		return fstat(mFd, &st) == 0 ? (std::uint64_t)st.st_size : 0;
	}

protected:
	pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override
	{
		std::int64_t base = 0;
		if (dir == std::ios_base::cur) base = (std::int64_t)mPos - (egptr() - gptr());
		else if (dir == std::ios_base::end) base = (std::int64_t)size();
		return seekpos(pos_type(base + off), std::ios_base::in);
	}
	pos_type seekpos(pos_type pos, std::ios_base::openmode) override
	{
		if ((std::int64_t)pos < 0) return pos_type(off_type(-1));
		mPos = (std::uint64_t)pos;
		setg(mBuffer, mBuffer, mBuffer); // descartar lo leído por adelantado
		return pos;
	}
	int_type underflow() override
	{
		if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
		ssize_t n;
		do n = pread(mFd, mBuffer, sizeof mBuffer, (off_t)mPos);
		while (n < 0 && errno == EINTR);
		if (n <= 0) return traits_type::eof();
		mPos += (std::uint64_t)n;
		setg(mBuffer, mBuffer, mBuffer + n);
		return traits_type::to_int_type(*gptr());
	}
	std::streamsize xsgetn(char* dst, std::streamsize count) override
	{
		std::streamsize done = 0;
		// Lo que quede en el búfer, y el resto directo del fichero.
		const std::streamsize buffered = egptr() - gptr();
		if (buffered > 0) {
			const std::streamsize take = std::min(buffered, count);
			memcpy(dst, gptr(), (size_t)take);
			gbump((int)take);
			done += take;
		}
		while (done < count) {
			ssize_t n;
			do n = pread(mFd, dst + done, (size_t)(count - done), (off_t)mPos);
			while (n < 0 && errno == EINTR);
			if (n <= 0) break;
			mPos += (std::uint64_t)n;
			done += n;
		}
		return done;
	}

private:
	int mFd;
	std::uint64_t mPos = 0; // posición del fichero tras el búfer
	char mBuffer[64 * 1024];
};

class FdStream : public std::istream {
public:
	explicit FdStream(int fd) : std::istream(&mBuf), mBuf(fd) {}
	std::uint64_t size() const { return mBuf.size(); }
private:
	FdStreamBuf mBuf;
};

} // namespace launcher
} // namespace pikmin

#endif // PIKMIN_LAUNCHER_FD_STREAM_H
