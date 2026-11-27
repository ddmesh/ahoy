//-----------------------------------------------------------------------------
// 2026 Ahoy, https://ahoydtu.de
// Creative Commons - https://creativecommons.org/licenses/by-nc-sa/4.0/deed
//-----------------------------------------------------------------------------

#ifndef __JSON_STREAM_H__
#define __JSON_STREAM_H__

#include <functional>
#include <string.h>
#include <ArduinoJson.h>

// Serializes a large JSON answer in small parts. Only one part at a time is
// held in a small JSON document, which is reused for the next part.

struct JsonPart {
    const char *pre  = "";   // text in front of the object
    const char *post = "";   // text behind the object
    bool strip = false;      // omit the outer braces / brackets: the content
                             // belongs to an outer object / array
};

class JsonStream {
    public:
        // fills 'doc' and 'part' for the given index, false: no more parts.
        // 'doc' is an empty object, use doc.to<JsonArray>() for an array
        typedef std::function<bool(uint16_t idx, JsonDocument &doc, JsonPart &part)> Builder;

        JsonStream(size_t docSize, Builder builder) : mDoc(docSize), mBuilder(builder) {}

        bool isValid() const {
            return (mDoc.capacity() > 0);
        }

        // a part did not fit into the document, members of it are missing
        bool hasOverflow() const {
            return mOverflow;
        }

        // builds the first part immediately instead of on the first fill()
        void prepare() {
            if(!mLoaded && !mDone)
                load();
        }

        // writes the next max. 'len' bytes to 'buf', returns 0 at the end
        size_t fill(uint8_t *buf, size_t len) {
            size_t written = 0;

            while((written < len) && !mDone) {
                if(!mLoaded) {
                    load();
                    if(mDone)
                        break;
                }

                Window win(&buf[written], len - written, mOffset);
                win.write(mPart.pre);
                size_t jsonLen = measureJson(mDoc);
                if(mPart.strip) {
                    Window inner(win, 1, jsonLen - 2);
                    serializeJson(mDoc, inner);
                } else
                    serializeJson(mDoc, win);
                win.write(mPart.post);

                written += win.written();
                mOffset += win.written();
                if(mOffset >= win.total()) { // part is complete
                    mLoaded = false;
                    mIdx++;
                }
            }

            return written;
        }

    private:
        void load() {
            mDoc.to<JsonObject>(); // clears the document
            mPart = JsonPart();
            if(!mBuilder(mIdx, mDoc, mPart)) {
                mDone = true;
                return;
            }
            if(mDoc.overflowed())
                mOverflow = true;
            mOffset = 0;
            mLoaded = true;
        }

        // output which only stores the bytes [offset, offset + len) of all
        // bytes written to it, the others are just counted
        class Window {
            public:
                Window(uint8_t *buf, size_t len, size_t offset)
                    : mBuf(buf), mLen(len), mSkip(offset) {}

                // forwards the bytes [first, first + cnt) to 'parent'
                Window(Window &parent, size_t first, size_t cnt)
                    : mParent(&parent), mFirst(first), mCnt(cnt) {}

                size_t write(uint8_t c) {
                    if(nullptr != mParent) {
                        if((mTotal >= mFirst) && (mTotal < (mFirst + mCnt)))
                            mParent->write(c);
                    } else if((mTotal >= mSkip) && (mWritten < mLen))
                        mBuf[mWritten++] = c;
                    mTotal++;
                    return 1;
                }

                size_t write(const uint8_t *s, size_t n) {
                    for(size_t i = 0; i < n; i++) {
                        write(s[i]);
                    }
                    return n;
                }

                void write(const char *s) {
                    write(reinterpret_cast<const uint8_t*>(s), strlen(s));
                }

                size_t written() const { return mWritten; }
                size_t total() const { return mTotal; }

            private:
                uint8_t *mBuf = nullptr;
                size_t mLen = 0, mSkip = 0, mWritten = 0, mTotal = 0;
                Window *mParent = nullptr;
                size_t mFirst = 0, mCnt = 0;
        };

        DynamicJsonDocument mDoc;
        Builder mBuilder;
        JsonPart mPart;
        uint16_t mIdx = 0;
        size_t mOffset = 0;
        bool mLoaded = false;
        bool mDone = false;
        bool mOverflow = false;
};

#endif /*__JSON_STREAM_H__*/
