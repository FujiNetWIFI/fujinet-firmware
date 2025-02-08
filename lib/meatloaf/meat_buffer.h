#ifndef MEATLOAF_BUFFER
#define MEATLOAF_BUFFER

#include <memory>
#include <fstream>

#include "meatloaf.h"

#include "../../include/debug.h"

namespace Meat
{
    /********************************************************
     * C++ Input MFile buffer
     ********************************************************/

    struct my_char_traits : public std::char_traits<char> 
    {
        typedef std::char_traits<char>::int_type int_type;
        static constexpr int_type nda() noexcept 
        {
            return static_cast<int>(_MEAT_NO_DATA_AVAIL);
        }
    };

    template <class charT, class traits = my_char_traits>
    class mfilebuf : public std::basic_filebuf<charT, traits>
    {
        std::unique_ptr<MStream> mstream;
        std::unique_ptr<MFile> mfile;

        static const size_t gbuffer_size = 2048;
        static const size_t pbuffer_size = 512;
        char *gbuffer;
        char *pbuffer;

        std::streampos currBuffStart = 0;
        std::streampos currBuffEnd;

    public:
        typedef charT char_type; // 1
        typedef traits traits_type;
        typedef typename traits_type::int_type int_type;
        typedef typename traits_type::off_type off_type;
        typedef typename traits_type::pos_type pos_type;

        mfilebuf()
        {
            gbuffer = new char[gbuffer_size+1];
            pbuffer = new char[pbuffer_size+1];
        };

        ~mfilebuf()
        {
            if (pbuffer != nullptr)
                delete[] pbuffer;

            if (gbuffer != nullptr)
                delete[] gbuffer;

            close();
        }

        std::filebuf *doOpen(std::ios_base::openmode mode)
        {
            // Debug_println("In filebuf open pre reset mistream");
            mstream.reset(mfile->getSourceStream(mode));

            if (mstream->isOpen())
            {
                // Debug_println("In filebuf open success!");
                if (mode == std::ios_base::in) {
                    // initialize get buffer using gbuffer_size
                    this->setg(gbuffer, gbuffer, gbuffer);
                }
                else if (mode == std::ios_base::out) {
                    this->setp(pbuffer, pbuffer + pbuffer_size);
                }
                return this;
            }
            else
                return nullptr;
        }

        std::filebuf *open(const char *filename, std::ios_base::openmode mode)
        {
            // Debug_println("In filebuf open");
            mfile.reset(MFSOwner::File(filename));
            return doOpen(mode);
        };

        std::filebuf *open(const std::string &filename, std::ios_base::openmode mode)
        {
            // Debug_println("In filebuf open");
            mfile.reset(MFSOwner::File(filename));
            return doOpen(mode);
        };

        virtual bool close()
        {
            if (is_open())
            {
                // Debug_printv("closing in filebuf\n");
                sync();
                mstream->close();
                return true;
            }
            return false;
        }

        bool is_open() const
        {
            Debug_printv("is_open");

            if (mstream == nullptr)
                return false;
            else
                return mstream->isOpen();
        }

        /**
         *  @brief  Fetches more data from the controlled sequence.
         *  @return  The first character from the <em>pending sequence</em>.
         *
         *  Informally, this function is called when the input buffer is
         *  exhausted (or does not exist, as buffering need not actually be
         *  done).  If a buffer exists, it is @a refilled.  In either case, the
         *  next available character is returned, or @c traits::eof() to
         *  indicate a null pending sequence.
         *
         *  For a formal definition of the pending sequence, see a good text
         *  such as Langer & Kreft, or [27.5.2.4.3]/7-14.
         *
         *  A functioning input streambuf can be created by overriding only
         *  this function (no buffer area will be used).  For an example, see
         *  https://gcc.gnu.org/onlinedocs/libstdc++/manual/streambufs.html
         *
         *  @note  Base class version does nothing, returns eof().
         */

        // https://newbedev.com/how-to-write-custom-input-stream-in-c

        // let's get a byte relative to current egptr
        int operator[](int index)
        {
            // 1. let's check if our index is within current buffer POINTERS
            // gptr = current character (get pointer)
            // egptr = one past end of get area
            if (this->gptr() == this->egptr())
                underflow();

            if (this->gptr() < this->egptr())
            {
                Debug_printf("%d char is within gptr-egptr range", index); // or - send the char across IEC to our C64
                return std::char_traits<char>::to_int_type(this->gptr()[index]);
            }
            else
            {
                Debug_printf("Index out of current buffer %d", this->gptr() - this->egptr());

                return my_char_traits::nda();
            }
        }

        int underflow() override
        {

            if (!is_open())
            {
                return std::char_traits<char>::eof();
            }
            else if (this->gptr() == this->egptr())
            {
                // the next statement assumes "size" characters were produced (if
                // no more characters are available, size == 0.
                // auto buffer = reader->read();

                int readCount = mstream->read((uint8_t *)gbuffer, gbuffer_size);

                Debug_printv("meat buffer underflow, readCount=%d", readCount);

                // beg, curr, end <=> eback, gptr, egptr
                if (readCount == _MEAT_NO_DATA_AVAIL)
                {
                    // if gptr >= egptr - sgetc will call underflow again:
                    //                   gptr     egptr
                    Debug_printv("meat underflow received _MEAT_NO_DATA_AVAIL!");
                    //this->setg(gbuffer, gbuffer, gbuffer); 

                    return my_char_traits::nda();
                }
                else if (readCount <= 0)
                {
                    Debug_printv("--mfilebuf read error or EOF, RC=%d!", readCount);
                    //this->setg(gbuffer, gbuffer, gbuffer);
                    return std::char_traits<char>::eof();
                }
                else
                {
                    currBuffEnd = mstream->position();
                    currBuffStart = (uint32_t)currBuffEnd - readCount; // this is where our buffer data starts

                    // Debug_printv("--mfilebuf underflow, read bytes=%d--", readCount);
                    // beg, curr, end <=> eback, gptr, egptr
                    this->setg(gbuffer, gbuffer, gbuffer + readCount);
                }
            }
            // eback = beginning of get area
            // gptr = current character (get pointer)
            // egptr = one past end of get area

            return this->gptr() == this->egptr()
                       ? std::char_traits<char>::eof()
                       : std::char_traits<char>::to_int_type(*this->gptr());
        };

        /**
         *  @brief  Consumes data from the buffer; writes to the
         *          controlled sequence.
         *  @param  __c  An additional character to consume.
         *  @return  eof() to indicate failure, something else (usually
         *           @a __c, or not_eof())
         *
         *  Informally, this function is called when the output buffer
         *  is full (or does not exist, as buffering need not actually
         *  be done).  If a buffer exists, it is @a consumed, with
         *  <em>some effect</em> on the controlled sequence.
         *  (Typically, the buffer is written out to the sequence
         *  verbatim.)  In either case, the character @a c is also
         *  written out, if @a __c is not @c eof().
         *
         *  For a formal definition of this function, see a good text
         *  such as Langer & Kreft, or [27.5.2.4.5]/3-7.
         *
         *  A functioning output streambuf can be created by overriding only
         *  this function (no buffer area will be used).
         *
         *  @note  Base class version does nothing, returns eof().
         */

        // // https://newbedev.com/how-to-write-custom-input-stream-in-c

        int overflow(int ch = traits_type::eof()) override
        {

            Debug_printv("in overflow");
            //  pptr =  Returns the pointer to the current character (put pointer) in the put area.
            //  pbase = Returns the pointer to the beginning ("base") of the put area.
            //  epptr = Returns the pointer one past the end of the put area.

            if (!is_open())
            {
                return EOF;
            }

            char *end = this->pptr();

            Debug_printv("before write call, ch=%d [%c], buffer contains:%d", ch, (char)ch, this->pptr() - this->pbase());

            if (ch != EOF)
            {
                *end++ = ch;
                this->pbump(1);
            }

            Debug_printv("%d bytes in buffer will be written", end - this->pbase());

            const uint8_t *pBase = (uint8_t *)this->pbase();

            if (mstream->write(pBase, end - this->pbase()) == 0)
            {
                ch = EOF;
            }
            else if (ch == EOF)
            {
                ch = 0;
            }
            this->setp(pbuffer, pbuffer + pbuffer_size);

            return ch;
        };

        std::streampos seekposforce(std::streampos __pos, std::ios_base::openmode __mode = std::ios_base::in)
        {
            std::streampos __ret = std::streampos(off_type(-1));

            if (mstream->seek(__pos))
            {
                __ret = std::streampos(off_type(__pos));
                this->setg(gbuffer, gbuffer, gbuffer);
                this->setp(pbuffer, pbuffer + pbuffer_size);
            }

            return __ret;
        }

        std::streampos seekpos(std::streampos __pos, std::ios_base::openmode __mode = std::ios_base::in) override
        {
            std::streampos __ret = std::streampos(off_type(-1));

            // pbase - pbuffer start
            // pptr - current pbuffer position
            // epptr - pbuffer end

            // ebackk - gbuffer start
            // gptr - current gbuffer position
            // egptr - gbuffer end

            Debug_printv("meat buffer seekpos called, newPos=%d buffer=[%d,%d]", (int)__pos, (int)currBuffStart, (int)currBuffEnd);

            if (__pos >= currBuffStart && __pos < currBuffEnd)
            {
                Debug_printv("Seek within cache, lucky!");

                // we're seeking within existing buffer, so let's reuse

                // !!!
                // NOTE - THIS PIECE OF CODE HAS TO BE THROUGHLY TESTED!!!!
                // !!!

                std::streampos delta = __pos - currBuffStart;
                // TODO - check if eback == gbuffer!!!
                // TODO - check if pbase == pbuffer!!!
                this->setg(this->eback(), gbuffer + delta, this->egptr());
                this->setp(this->pbase(), pbuffer + delta);
            }
            else if (mstream->seek(__pos))
            {
                Debug_printv("Seek missed the cache, read required!");
                // the seek op isn't within existing buffer, so we need to actually
                // call seek on stream and force underflow/overflow

                //__ret.state(_M_state_cur);
                __ret = std::streampos(off_type(__pos));

                // not sure if this is ok, but is supposed to cause underflow
                // underflow will set it to:
                // setg(gbuffer, gbuffer, gbuffer + readCount);
                //         begin    next     end
                this->setg(gbuffer, gbuffer, gbuffer);

                // not sure if this is ok, but is supposed to cause overflow and prepare a clean buffer for writing
                // that's how overflow does it after writing all:
                // write(pbase, pptr-pbase)
                // setp(pbuffer, pbuffer+pbuffer_size);
                //         begin    end
                this->setp(pbuffer, pbuffer + pbuffer_size);
            }

            return __ret;
        }

        /**
         *  @brief  Synchronizes the buffer arrays with the controlled sequences.
         *  @return  -1 on failure.
         *
         *  Each derived class provides its own appropriate behavior,
         *  including the definition of @a failure.
         *  @note  Base class version does nothing, returns zero.
         *
         * sync: Called on flush, should output any characters in the buffer to the sink.
         * If you never call setp (so there's no buffer), you're always in sync, and this
         * can be a no-op. overflow or uflow can call this one, or both can call some
         * separate function. (About the only difference between sync and uflow is that
         * uflow will only be called if there is a buffer, and it will never be called
         * if the buffer is empty. sync will be called if the client code flushes the stream.)
         */
        int sync() override
        {
            // Debug_printv("in wrapper sync");

            if (this->pptr() == this->pbase())
            {
                return 0;
            }
            else
            {
                const uint8_t *buffer = (uint8_t *)this->pbase();

                // pptr =  Returns the pointer to the current character (put pointer) in the put area.
                // pbase = Returns the pointer to the beginning ("base") of the put area.
                // epptr = Returns the pointer one past the end of the put area.

                // Debug_printv("before write call, mostream!=null[%d]", mostream!=nullptr);
                auto result = mstream->write(buffer, this->pptr() - this->pbase());
                // Debug_printv("%d bytes left in buffer written to sink, rc=%d", pptr()-pbase(), result);

                this->setp(pbuffer, pbuffer + pbuffer_size);

                return (result != 0) ? 0 : -1;
            }
        };
    };

    // [27.8.1.11] Template class basic_fstream
    /**
     *  @brief  Controlling input and output for files.
     *  @ingroup io
     *
     *  @tparam _CharT  Type of character stream.
     *  @tparam _Traits  Traits for character type, defaults to
     *                   char_traits<_CharT>.
     *
     *  This class supports reading from and writing to named files, using
     *  the inherited functions from std::basic_iostream.  To control the
     *  associated sequence, an instance of std::basic_filebuf is used, which
     *  this page refers to as @c sb.
     */
    template <typename _CharT, typename _Traits = std::char_traits<_CharT>>
    class basic_fstream : public std::basic_iostream<_CharT, _Traits>
    {
    public:
        // Types:
        typedef _CharT char_type;
        typedef _Traits traits_type;
        typedef typename basic_fstream::traits_type::int_type int_type;
        typedef typename basic_fstream::traits_type::pos_type pos_type;
        typedef typename basic_fstream::traits_type::off_type off_type;

        // Non-standard types:
        typedef mfilebuf<char_type, traits_type> __filebuf_type;
        typedef std::basic_ios<char_type, traits_type> __ios_type;
        typedef std::basic_iostream<char_type, traits_type> __iostream_type;

    private:
        __filebuf_type _M_filebuf;

    public:
        // Constructors/destructor:
        /**
         *  @brief  Default constructor.
         *
         *  Initializes @c sb using its default constructor, and passes
         *  @c &sb to the base class initializer.  Does not open any files
         *  (you haven't given it a filename to open).
         */
        basic_fstream() : __iostream_type(), _M_filebuf()
        {
            this->init(&_M_filebuf);
        }

        /**
         *  @brief  Create an input/output file stream.
         *  @param  __s  Null terminated string specifying the filename.
         *  @param  __mode  Open file in specified mode (see std::ios_base).
         */
        explicit basic_fstream(const char *__s,
                               std::ios_base::openmode __mode = std::ios_base::in)
            : __iostream_type(0), _M_filebuf()
        {
            this->init(&_M_filebuf);
            this->open(__s, __mode);
        }

        explicit basic_fstream(MFile *fi,
                               std::ios_base::openmode __mode = std::ios_base::in)
            : __iostream_type(0), _M_filebuf()
        {
            this->init(&_M_filebuf);
            this->open(fi->url.c_str(), __mode);
        }

#if __cplusplus >= 201103L
        /**
         *  @brief  Create an input/output file stream.
         *  @param  __s  Null terminated string specifying the filename.
         *  @param  __mode  Open file in specified mode (see std::ios_base).
         */
        explicit basic_fstream(const std::string &__s, std::ios_base::openmode __mode = std::ios_base::in) : __iostream_type(0), _M_filebuf()
        {
            this->init(&_M_filebuf);
            this->open(__s, __mode);
        }

#if __cplusplus >= 201703L && __cplusplus < 202002L
        /**
         *  @param  Create an input/output file stream.
         *  @param  __s  filesystem::path specifying the filename.
         *  @param  __mode  Open file in specified mode (see std::ios_base).
         */
        template <typename _Path, typename _Require = _If_fs_path<_Path>>
        basic_fstream(const _Path &__s,
                      ios_base::openmode __mode = ios_base::in | ios_base::out)
            : basic_fstream(__s.c_str(), __mode)
        {
        }
#endif // C++17

        basic_fstream(const basic_fstream &) = delete;

        // basic_fstream(basic_fstream &&__rhs) : __iostream_type(std::move(__rhs)), _M_filebuf(std::move(__rhs._M_filebuf))
        // {
        //     __iostream_type::set_rdbuf(&_M_filebuf);
        // }
#endif

        /**
         *  @brief  The destructor does nothing.
         *
         *  The file is closed by the filebuf object, not the formatting
         *  stream.
         */
        ~basic_fstream()
        {
            _M_filebuf.close();
        }

#if __cplusplus >= 201103L
        // 27.8.3.2 Assign and swap:

        basic_fstream &operator=(const basic_fstream &) = delete;

        basic_fstream &operator=(basic_fstream &&__rhs)
        {
            __iostream_type::operator=(std::move(__rhs));
            _M_filebuf = std::move(__rhs._M_filebuf);
            return *this;
        }

        void swap(basic_fstream &__rhs)
        {
            __iostream_type::swap(__rhs);
            _M_filebuf.swap(__rhs._M_filebuf);
        }
#endif

        // Members:
        /**
         *  @brief  Accessing the underlying buffer.
         *  @return  The current basic_filebuf buffer.
         *
         *  This hides both signatures of std::basic_ios::rdbuf().
         */
        __filebuf_type *
        rdbuf() const
        {
            return const_cast<__filebuf_type *>(&_M_filebuf);
        }

        /**
         *  @brief  Wrapper to test for an open file.
         *  @return  @c rdbuf()->is_open()
         */
        bool is_open()
        {
            Debug_printv("is_open");
            return _M_filebuf.is_open();
        }

        // _GLIBCXX_RESOLVE_LIB_DEFECTS
        // 365. Lack of const-qualification in clause 27
        bool is_open() const
        {
            Debug_printv("is_open const");
            return _M_filebuf.is_open();
        }

        /**
         *  @brief  Opens an external file.
         *  @param  __s  The name of the file.
         *  @param  __mode  The open mode flags.
         *
         *  Calls @c std::basic_filebuf::open(__s,__mode).  If that
         *  function fails, @c failbit is set in the stream's error state.
         */
        void open(const char *__s, std::ios_base::openmode __mode = std::ios_base::in)
        {
            if (!_M_filebuf.open(__s, __mode))
                this->setstate(std::ios_base::failbit);
            else
                // _GLIBCXX_RESOLVE_LIB_DEFECTS
                // 409. Closing an fstream should clear error state
                this->clear();
        }

#if __cplusplus >= 201103L
        /**
         *  @brief  Opens an external file.
         *  @param  __s  The name of the file.
         *  @param  __mode  The open mode flags.
         *
         *  Calls @c std::basic_filebuf::open(__s,__mode).  If that
         *  function fails, @c failbit is set in the stream's error state.
         */
        void open(const std::string &__s, std::ios_base::openmode __mode = std::ios_base::in)
        {
            if (!_M_filebuf.open(__s, __mode))
                this->setstate(std::ios_base::failbit);
            else
                // _GLIBCXX_RESOLVE_LIB_DEFECTS
                // 409. Closing an fstream should clear error state
                this->clear();
        }

#if __cplusplus >= 201703L && __cplusplus < 202002L
        /**
         *  @brief  Opens an external file.
         *  @param  __s  The name of the file, as a filesystem::path.
         *  @param  __mode  The open mode flags.
         *
         *  Calls @c std::basic_filebuf::open(__s,__mode).  If that
         *  function fails, @c failbit is set in the stream's error state.
         */
        template <typename _Path>
        _If_fs_path<_Path, void>
        open(const _Path &__s,
             ios_base::openmode __mode = ios_base::in | ios_base::out)
        {
            open(__s.c_str(), __mode);
        }
#endif // C++17
#endif // C++11

        /**
         *  @brief  Close the file.
         *
         *  Calls @c std::basic_filebuf::close().  If that function
         *  fails, @c failbit is set in the stream's error state.
         */
        void close()
        {
            if (!_M_filebuf.close())
                this->setstate(std::ios_base::failbit);
        }

        U8Char getUtf8()
        {
            U8Char codePoint(this);
            return codePoint;
        }

        char getPetscii()
        {
            return getUtf8().toPetscii();
        }

        void putPetsciiAsUtf8(char c)
        {
            U8Char wide = U8Char(c);
            (*this) << wide.toUtf8();
        }

        bool nda() const {
            return (this->rdstate() & ndabit) != 0;
        }

        basic_fstream<_CharT, _Traits> &checkNda()
        {
            // first naive implementation...

            /*
            istream->good(); // rdstate == 0
            istream->bad(); // rdstate() & badbit) != 0
            istream->eof(); // rdstate() & eofbit) != 0
            istream->fail(); // >rdstate() & (badbit | failbit)) != 0
            istream->rdstate();
            istream->setstate();
            _S_goodbit 		= 0,
            _S_badbit 		= 1L << 0,
            _S_eofbit 		= 1L << 1,
            _S_failbit		= 1L << 2,
            _S_ios_iostate_end = 1L << 16,
            _S_ios_iostate_max = __INT_MAX__,
            _S_ios_iostate_min = ~__INT_MAX__
            */

            const int_type __cb = this->rdbuf()->sgetc(); // check next char, don't advance
            if(traits_type::eq_int_type(__cb, my_char_traits::nda())) {
                // it's NDA - ok, so let's set a proper mark on this stream
                this->rdbuf()->sbumpc(); // skip nda and... 
                this->setstate((std::ios_base::iostate)ndabit); // set state to nda
            } 
            return *this;
        }
    };

    typedef basic_fstream<char> iostream;

    // iostream& operator>>(iostream& is, U8Char& c) {
    //     //U8Char codePoint(this);
    //     c = U8Char(&is);
    //     return is;
    // }

    // https://stdcxx.apache.org/doc/stdlibug/39-3.html
    // https://cplusplus.com/reference/istream/iostream/
}

#endif /* MEATLOAF_BUFFER */
